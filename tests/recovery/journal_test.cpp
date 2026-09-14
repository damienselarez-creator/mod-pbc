#include "pbc_history_journal.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
void Check(bool ok) { if (!ok) throw std::runtime_error("journal invariant failed"); }
int main(int argc, char** argv)
{
    Check(argc == 3);
    PBC_HistoryJournal journal(argv[2]);
    if (std::string(argv[1]) == "write")
    {
        Check(journal.Pending().empty());
        auto record = journal.Append(7, 2, "L'ami fidèle", {1,2});
        Check(std::filesystem::exists(record.file));
        // Simulate abrupt process exit: no destructors and no acknowledgement.
        std::_Exit(0);
    }
    auto records = journal.Pending();
    Check(records.size() == 1 && records[0].message == "L'ami fidèle");
    Check(records[0].owners == std::vector<uint64_t>({1,2}));
    bool locked = false;
    try { PBC_HistoryJournal second(argv[2]); }
    catch (std::exception const&) { locked = true; }
    Check(locked);
    std::ifstream input(records[0].file, std::ios::binary);
    std::string original((std::istreambuf_iterator<char>(input)), {});
    input.close();
    auto damaged = original;
    damaged[damaged.find("fidèle")] = 'X';
    { std::ofstream output(records[0].file, std::ios::binary); output << damaged; }
    bool rejected = false;
    try { journal.Pending(); } catch (std::exception const&) { rejected = true; }
    Check(rejected && std::filesystem::exists(records[0].file));
    { std::ofstream output(records[0].file, std::ios::binary); output << original; }
    Check(journal.Pending().size() == 1);
    journal.Acknowledge(records[0]);
    Check(journal.Pending().empty());
    auto future = journal.Append(7,2,"first",{1});
    auto futurePath = future.file.parent_path() / ("9000000000000000000-" + future.token + ".pending");
    std::filesystem::rename(future.file, futurePath);
    journal.Append(7,2,"second",{1});
    auto ordered = journal.Pending();
    Check(ordered.size()==2 && ordered[0].message=="first" && ordered[1].message=="second");
    for (auto const& item : ordered)
        journal.Acknowledge(item);
    std::cout << "Journal restart, monotonic ordering, exclusive lock, checksum and acknowledgement passed.\n";
}
