#include <cstdint>
#include <deque>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
struct PBC_HistoryEntry { std::string message; };
std::unordered_map<uint64_t,PBC_HistoryEntry> g_PBC_History;
std::unordered_map<uint64_t,std::deque<uint64_t>> g_PBC_HistoryOwners;
std::mutex g_PBC_HistoryMutex;
int writes=0; bool confirmed=false;
bool DB_ReplaceHistoryBatch(const std::vector<std::pair<uint64_t,std::string>>&) {++writes;return confirmed;}
#include "production_regeneration.inc"
void Check(bool ok) {if(!ok) throw std::runtime_error("regeneration invariant");}
int main() {
 std::vector<PBC_HistoryEntry> entries={{"new one"},{"new two"}};
 std::vector<uint64_t> ids={1,2};
 std::unordered_map<uint64_t,std::string> original={{1,"old one"},{2,"old two"}};
 std::unordered_map<uint64_t,std::deque<uint64_t>> owners={{10,{1,2}}};
 auto seed=[&] {g_PBC_History={{1,{"old one"}},{2,{"old two"}}};g_PBC_HistoryOwners=owners;};
 auto apply=[&] {return CommitRegeneratedHistory(entries,ids,original,owners);};
 seed(); Check(!apply() && writes==1 && g_PBC_History.at(1).message=="old one" && g_PBC_History.at(2).message=="old two");
 confirmed=true; Check(apply() && writes==2 && g_PBC_History.at(1).message=="new one" && g_PBC_History.at(2).message=="new two");
 seed(); g_PBC_History.at(1).message="edited"; Check(!apply() && writes==2);
 seed(); g_PBC_HistoryOwners.at(10).push_back(3); Check(!apply() && writes==2);
 seed(); g_PBC_History.erase(2); Check(!apply() && writes==2);
 seed(); entries.pop_back(); Check(!apply() && writes==2);
 std::cout<<"Regeneration: complete confirmed batch only; failures, edits, new messages and deleted history retain old state.\n";
}
