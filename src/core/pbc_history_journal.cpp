#include "pbc_history_journal.h"
#include "pbc_json.h"
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace
{
uint64_t JournalOrder(std::filesystem::path const& path)
{
    auto name = path.stem().string();
    auto separator = name.find('-');
    size_t consumed = 0;
    auto value = std::stoull(name.substr(0, separator), &consumed);
    if (separator == std::string::npos || consumed != separator)
        throw std::runtime_error("invalid journal filename");
    return value;
}

std::string Digest(std::string const& data)
{
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<unsigned char const*>(data.data()), data.size(), digest);
    std::string result;
    for (auto byte : digest)
    {
        result += "0123456789abcdef"[byte >> 4];
        result += "0123456789abcdef"[byte & 15];
    }
    return result;
}

void SyncDirectory(std::filesystem::path const& directory)
{
#ifndef _WIN32
    int fd = open(directory.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        throw std::runtime_error("journal directory open failed");
    int result = fsync(fd);
    close(fd);
    if (result != 0)
        throw std::runtime_error("journal directory sync failed");
#else
    (void)directory; // Publication uses MoveFileExW with WRITE_THROUGH.
#endif
}
}

PBC_HistoryJournal::PBC_HistoryJournal(std::filesystem::path directory) : directory_(std::move(directory))
{
    std::filesystem::create_directories(directory_);
#ifndef _WIN32
    auto parent = std::filesystem::absolute(directory_);
    while (!parent.empty())
    {
        SyncDirectory(parent);
        if (parent == parent.parent_path())
            break;
        parent = parent.parent_path();
    }
#endif
    auto path = directory_ / "journal.lock";
#ifdef _WIN32
    auto handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("journal directory already locked or unavailable");
    lock_ = reinterpret_cast<intptr_t>(handle);
#else
    int fd = open(path.c_str(), O_CREAT | O_RDWR, 0600);
    if (fd < 0)
        throw std::runtime_error("journal lock open failed");
    if (flock(fd, LOCK_EX | LOCK_NB) != 0)
    {
        close(fd);
        throw std::runtime_error("journal directory already locked");
    }
    lock_ = fd;
#endif
}

PBC_HistoryJournal::~PBC_HistoryJournal()
{
#ifdef _WIN32
    CloseHandle(reinterpret_cast<HANDLE>(lock_));
#else
    close(static_cast<int>(lock_));
#endif
}

PBC_PendingHistory PBC_HistoryJournal::Append(uint64_t author, uint8_t type,
    std::string const& message, std::vector<uint64_t> const& owners)
{
    if (owners.empty())
        throw std::runtime_error("journal record has no owner");
    unsigned char random[16];
    if (RAND_bytes(random, sizeof(random)) != 1)
        throw std::runtime_error("journal token generation failed");
    std::string token;
    for (auto byte : random)
    {
        token += "0123456789abcdef"[byte >> 4];
        token += "0123456789abcdef"[byte & 15];
    }
    auto now = std::chrono::system_clock::now().time_since_epoch();
    auto stamp = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(now).count());
    auto order = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
    for (auto const& item : std::filesystem::directory_iterator(directory_))
        if (item.path().extension() == ".pending")
        {
            auto previous = JournalOrder(item.path());
            if (previous == std::numeric_limits<uint64_t>::max())
                throw std::runtime_error("journal order exhausted");
            order = std::max(order, previous + 1);
        }
    auto name = std::to_string(order) + "-" + token;
    auto path = directory_ / (name + ".pending");
    auto temporary = directory_ / (name + ".tmp");
    auto payload = pbc_json{{"version", 1}, {"token", token}, {"author", author}, {"type", type},
        {"timestamp", stamp}, {"message", message}, {"owners", owners}};
    auto bytes = pbc_json{{"payload", payload}, {"sha256", Digest(payload.dump())}}.dump();
    if (bytes.size() > 16 * 1024 * 1024)
        throw std::runtime_error("journal record too large");
#ifdef _WIN32
    FILE* file = nullptr;
    _wfopen_s(&file, temporary.c_str(), L"wbx");
#else
    FILE* file = fopen(temporary.c_str(), "wbx");
#endif
    if (!file)
        throw std::runtime_error("journal write open failed");
    bool ok = fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    ok = fflush(file) == 0 && ok;
#ifdef _WIN32
    ok = _commit(_fileno(file)) == 0 && ok;
#else
    ok = fsync(fileno(file)) == 0 && ok;
#endif
    ok = fclose(file) == 0 && ok;
    if (!ok)
        throw std::runtime_error("journal durable write failed");
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("journal publish failed");
#else
    std::filesystem::rename(temporary, path);
#endif
    SyncDirectory(directory_);
    return {token, author, stamp, type, message, owners, path};
}

std::vector<PBC_PendingHistory> PBC_HistoryJournal::Pending() const
{
    std::vector<std::filesystem::path> files;
    for (auto const& item : std::filesystem::directory_iterator(directory_))
        if (item.path().extension() == ".pending")
            files.push_back(item.path());
    std::sort(files.begin(), files.end(), [](auto const& left, auto const& right)
    {
        auto l = JournalOrder(left);
        auto r = JournalOrder(right);
        return l == r ? left < right : l < r;
    });
    std::vector<PBC_PendingHistory> records;
    for (auto const& file : files)
    {
        if (std::filesystem::file_size(file) > 16 * 1024 * 1024)
            throw std::runtime_error("journal record too large");
        std::ifstream stream(file, std::ios::binary);
        auto envelope = pbc_json::parse(stream);
        auto const& value = envelope.at("payload");
        if (envelope.at("sha256").get<std::string>() != Digest(value.dump()))
            throw std::runtime_error("journal checksum mismatch");
        PBC_PendingHistory record;
        record.token = value.at("token").get<std::string>();
        if (value.at("version").get<int>() != 1 || record.token.size() != 32 ||
            record.token.find_first_not_of("0123456789abcdef") != std::string::npos ||
            file.stem().string().substr(file.stem().string().size() - 32) != record.token)
            throw std::runtime_error("invalid journal record");
        record.author = value.at("author").get<uint64_t>();
        record.timestamp = value.at("timestamp").get<uint64_t>();
        auto type = value.at("type").get<unsigned>();
        if (type > 255)
            throw std::runtime_error("invalid journal message type");
        record.type = static_cast<uint8_t>(type);
        record.message = value.at("message").get<std::string>();
        record.owners = value.at("owners").get<std::vector<uint64_t>>();
        if (record.owners.empty())
            throw std::runtime_error("invalid journal owners");
        record.file = file;
        records.push_back(std::move(record));
    }
    return records;
}

void PBC_HistoryJournal::Acknowledge(PBC_PendingHistory const& record)
{
    if (!std::filesystem::remove(record.file))
        throw std::runtime_error("journal acknowledgement failed");
    SyncDirectory(directory_);
}
