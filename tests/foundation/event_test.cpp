#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
enum class PBC_EventType { Normal, Regen, QuestSummarization, CombatSummarization, Condensation };
enum class PBC_LogLevel { PBC_WARNING };
template<class... T> void PBC_Log(PBC_LogLevel,const char*,T&&...) {}
constexpr int CHAT_MSG_WHISPER=7;
struct Source {
 uint64_t senderGuid=0; std::string message,narratorText;
 bool IsChat() const {return senderGuid && !message.empty();}
 bool IsNarrator() const {return !narratorText.empty();}
};
struct Snapshot {uint64_t charGuidRaw=0;};
struct PBC_EventItem {
 std::chrono::steady_clock::time_point createdAt=std::chrono::steady_clock::now();
 PBC_EventType type=PBC_EventType::Normal;
 Source source; uint32_t chatType=1; uint64_t sourceHistoryId=0; bool sourceRecorded=false;
 std::vector<Snapshot> respondingChars;
 std::vector<uint64_t> silentCharGuids,replyOnlyCharGuids,playerCharGuids;
};
std::atomic<bool> g_PBC_Stopping{false};
std::mutex g_PBC_EventQueueMutex;
std::queue<PBC_EventItem> g_PBC_EventQueue;
int writes=0; bool persist=true,pending=false;
void PBC_MaybeInsertSharedTimeGap(const std::vector<uint64_t>&,bool) {}
uint64_t PBC_AppendHistoryMessage(uint64_t,uint8_t,const std::string&,const std::vector<uint64_t>&,bool* durable) {
 ++writes; *durable=persist; return persist ? 42 : 0;
}
bool DB_HistoryRecoveryPending() { return pending; }
struct PBC_APIConfig { int version=0; };
std::unordered_map<std::string,PBC_APIConfig> g_PBC_Connections;
std::mutex g_PBC_ConnectionsMutex;
#include "production_events.inc"
void Check(bool ok) {if(!ok) throw std::runtime_error("event invariant");}
int main() {
 PBC_EventItem event; event.source.senderGuid=1; event.source.message="input"; event.respondingChars={{2}};
 for(int i=0;i<65;++i) PBC_PushEvent(event);
 Check(g_PBC_EventQueue.size()==64 && writes==65);
 Check(g_PBC_EventQueue.front().sourceRecorded && g_PBC_EventQueue.front().sourceHistoryId==42);
 g_PBC_EventQueue={}; persist=false; PBC_PushEvent(event); Check(g_PBC_EventQueue.empty());
 persist=true; pending=true; PBC_PushEvent(event); Check(g_PBC_EventQueue.empty());
 pending=false; g_PBC_Stopping=true; int before=writes; PBC_PushEvent(event); Check(writes==before && g_PBC_EventQueue.empty());
 g_PBC_Stopping=false; event.createdAt-=std::chrono::seconds(61); Check(PBC_EventExpired(event));
 event.type=PBC_EventType::Condensation; Check(!PBC_EventExpired(event));
 g_PBC_Stopping=true; Check(PBC_EventExpired(event));
 Check(!PBC_GetConnection("missing")); g_PBC_Connections["default"]={1};
 auto held=PBC_GetConnection("utility"); Check(held && held->version==1);
 std::thread reload([] {for(int i=2;i<2000;++i) {std::lock_guard<std::mutex> lock(g_PBC_ConnectionsMutex); g_PBC_Connections.clear(); g_PBC_Connections["default"]={i};}});
 for(int i=0;i<2000;++i) {auto copy=PBC_GetConnection("default"); Check(copy && copy->version>=1);}
 reload.join(); Check(held->version==1);
 std::cout<<"Admission: bounded reactions, source retained, failed persistence rejected, stale/stopping events rejected; config snapshots survive concurrent reload.\n";
}
