#ifndef PBC_HISTORY_JOURNAL_H
#define PBC_HISTORY_JOURNAL_H
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct PBC_PendingHistory
{
    std::string token;
    uint64_t author = 0;
    uint64_t timestamp = 0;
    uint8_t type = 0;
    std::string message;
    std::vector<uint64_t> owners;
    std::filesystem::path file;
};

// Caller serializes access. The directory is exclusively locked for this process.
class PBC_HistoryJournal
{
public:
    explicit PBC_HistoryJournal(std::filesystem::path directory);
    ~PBC_HistoryJournal();
    PBC_HistoryJournal(PBC_HistoryJournal const&) = delete;
    PBC_HistoryJournal& operator=(PBC_HistoryJournal const&) = delete;
    PBC_PendingHistory Append(uint64_t author, uint8_t type, std::string const& message,
        std::vector<uint64_t> const& owners);
    std::vector<PBC_PendingHistory> Pending() const;
    void Acknowledge(PBC_PendingHistory const& record);
private:
    std::filesystem::path directory_;
    intptr_t lock_ = -1;
};
#endif
