#include <algorithm>
#include <atomic>
#include <cstdint>
#include <ctime>
#include <deque>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

struct PBC_HistoryEntry {
    uint64_t id = 0, authorGuid = 0;
    time_t timestamp = 0;
    uint8_t type = 0;
    std::string message;
};
std::mutex g_PBC_HistoryMutex;
std::unordered_map<uint64_t, PBC_HistoryEntry> g_PBC_History;
std::unordered_map<uint64_t, std::deque<uint64_t>> g_PBC_HistoryOwners;
std::unordered_map<uint64_t, time_t> g_PBC_LastHistoryTime;
bool failWrite = false;
uint64_t writes = 0, notifications = 0;
uint64_t adventureEvents = 0;
void Check(bool ok) { if (!ok) throw std::runtime_error("history invariant failed"); }
void PBC_AdventureHistory(uint64_t, uint8_t, std::string const&, std::vector<uint64_t> const& owners) {
    Check(std::is_sorted(owners.begin(), owners.end()));
    Check(std::adjacent_find(owners.begin(), owners.end()) == owners.end());
    ++adventureEvents;
}
uint64_t DB_InsertHistoryMessage(uint64_t, uint8_t, const std::string&, const std::vector<uint64_t>& owners, bool* durable) {
    if (durable) *durable = !failWrite;
    Check(std::adjacent_find(owners.begin(), owners.end()) == owners.end());
    return failWrite ? 0 : ++writes;
}
void PBC_WsNotifyHistory(uint64_t, const PBC_HistoryEntry&) { ++notifications; }
bool PBC_TimeGapNeeded_Locked(uint64_t, bool) { return true; }
std::string PBC_Localize(const char* value) { return value; }
uint64_t PBC_AppendHistoryMessage(uint64_t, uint8_t, const std::string&, const std::vector<uint64_t>&, bool* = nullptr, const std::deque<std::string>* = nullptr);
std::string PBC_RenderHistoryLine(const PBC_HistoryEntry& entry, uint64_t) { return entry.message; }
#include "production_history.inc"

int main() {
    failWrite = true;
    Check(PBC_AppendHistoryMessage(3, 1, "Hello", {2,1,1}) == 0);
    Check(g_PBC_History.empty() && g_PBC_HistoryOwners.empty() && notifications == 0);
    Check(adventureEvents == 1);
    Check(!PBC_MaybeInsertTimeGap(1, false));
    Check(adventureEvents == 2);
    failWrite = false;
    Check(PBC_AppendHistoryMessage(3, 1, "Hello", {2,1,1}) == 1);
    Check(g_PBC_HistoryOwners.at(1) == std::deque<uint64_t>{1});
    Check(g_PBC_HistoryOwners.at(2) == std::deque<uint64_t>{1});
    Check(notifications == 2);
    Check(adventureEvents == 3);
    std::vector<std::thread> threads;
    for (int i=0; i<8; ++i) threads.emplace_back([] {
        for (int n=0; n<50; ++n) PBC_AppendHistoryMessage(3, 1, "Concurrent", {1,2,1});
    });
    for (auto& thread : threads) thread.join();
    Check(writes == 2 && g_PBC_History.size() == 2 && notifications == 4);
    Check(adventureEvents == 4);
    Check(g_PBC_HistoryOwners.at(1) == std::deque<uint64_t>({1,2}));
    Check(g_PBC_HistoryOwners.at(2) == std::deque<uint64_t>({1,2}));
    std::deque<std::string> stale{"old"};
    Check(PBC_AppendHistoryMessage(1,1,"stale reply",{1},nullptr,&stale)==0 && writes==2);
    Check(adventureEvents == 4);
    Check(PBC_MaybeInsertTimeGap(1, false));
    Check(!PBC_MaybeInsertTimeGap(1, false));
    Check(adventureEvents == 5);
    std::cout << "Production history publication tests passed.\n";
}
