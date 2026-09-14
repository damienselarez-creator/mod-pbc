#include <algorithm>
#include <atomic>
#include <cstdint>
#include <ctime>
#include <deque>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
struct History { std::string message; };
struct Memory { uint64_t dbId; std::string text; uint8_t importance; };
struct Relationship { std::string text, updatedAt; };
std::unordered_map<uint64_t, History> g_PBC_History;
std::unordered_map<uint64_t, std::deque<uint64_t>> g_PBC_HistoryOwners;
std::unordered_map<uint64_t, time_t> g_PBC_LastHistoryTime;
std::unordered_map<uint64_t, std::vector<Memory>> g_PBC_Memories;
std::unordered_map<uint64_t, std::unordered_map<std::string, Relationship>> g_PBC_Relationships;
std::mutex g_PBC_HistoryMutex, g_PBC_MemoriesMutex, g_PBC_RelationshipsMutex;
std::atomic<uint64_t> relationshipGeneration{0};
enum class PBC_HistoryResult { Ok, NotFound, Desync, PersistenceFailed, Forbidden };
bool failWrite = false;
int writes = 0;
bool Write() { ++writes; return !failWrite; }
bool DB_UpdateHistoryMessage(uint64_t, const std::string&) { return Write(); }
bool DB_DeleteHistoryMessage(uint64_t) { return Write(); }
bool DB_RemoveHistoryOwnership(uint64_t, uint64_t, bool) { return Write(); }
bool DB_ResetCharacterMemory(uint64_t, bool) { return Write(); }
bool DB_UpsertRelationship(uint64_t, const std::string&, const std::string&) { return Write(); }
bool DB_UpdateRelationshipText(uint64_t, const std::string&, const std::string&) { return Write(); }
bool DB_DeleteRelationship(uint64_t, const std::string&) { return Write(); }
bool DB_UpdateMemoryById(uint64_t, const std::string&, uint8_t) { return Write(); }
bool DB_DeleteMemoryById(uint64_t) { return Write(); }
std::string PBC_FormatDateTime(time_t) { return "now"; }
using ObjectGuid = uint64_t;
struct CharacterCache {
    uint32_t GetCharacterAccountIdByGuid(uint64_t guid) { return guid == 10 ? 1 : 2; }
} characterCache;
auto* sCharacterCache = &characterCache;
PBC_HistoryResult PBC_UpdateHistoryMessage(uint64_t, const std::string&, uint64_t = 0, uint32_t = 0);
PBC_HistoryResult PBC_DeleteHistoryMessage(uint64_t, uint64_t = 0, uint32_t = 0);
#include "production_mutations.inc"
namespace httplib {
struct Response {
    int status=200;
    std::string content;
    void set_content(const std::string& value, const char*) { content=value; }
};
}
#include "production_response.inc"
void Check(bool ok) { if (!ok) throw std::runtime_error("mutation invariant failed"); }
void Seed() {
    g_PBC_History = {{1,{"shared"}},{2,{"private"}}};
    g_PBC_HistoryOwners = {{10,{1,2}},{20,{1}}};
    g_PBC_LastHistoryTime = {{10,100},{20,100}};
    g_PBC_Memories = {{10,{{3,"memory",5}}},{20,{{4,"other",5}}}};
    g_PBC_Relationships = {{10,{{"friend",{"old","before"}}}},{20,{{"friend",{"other","before"}}}}};
}
int main() {
    using R=PBC_HistoryResult;
    httplib::Response response;
    Check(!RespondMutationResult(response,R::PersistenceFailed,"memory") && response.status==503);
    Check(response.content=="{\"error\":\"persistence_unconfirmed\"}");
    Check(!RespondMutationResult(response,R::Desync,"memory") && response.status==409);
    Check(!RespondMutationResult(response,R::NotFound,"memory") && response.status==404);
    Check(RespondMutationResult(response,R::Ok,"memory"));
    Seed();
    Check(!RespondMutationResult(response,R::Forbidden,"history") && response.status==403);
    Check(PBC_UpdateHistoryMessage(2,"foreign",20,2)==R::Forbidden);
    Check(PBC_DeleteHistoryMessage(1,10,1)==R::Forbidden);
    Check(PBC_UpdateHistoryMessage(1,"shared",10,1)==R::Forbidden);
    Check(writes==0);
    Check(PBC_UpdateHistoryMessage(2,"allowed",10,1)==R::Ok);
    Seed(); failWrite=true;
    Check(PBC_UpdateHistoryMessage(1,"new")==R::PersistenceFailed);
    Check(PBC_DeleteHistoryMessage(1)==R::PersistenceFailed);
    Check(PBC_RemoveHistoryOwnership(10,1)==R::PersistenceFailed);
    Check(PBC_UpdateMemory(10,3,"new",8,"memory")==R::PersistenceFailed);
    Check(PBC_DeleteMemory(10,3,"memory")==R::PersistenceFailed);
    Check(PBC_UpdateRelationship(10,"friend","new","old")==R::PersistenceFailed);
    Check(PBC_DeleteRelationship(10,"friend","old")==R::PersistenceFailed);
    Check(PBC_ResetCharacterMemory(10,false)==R::PersistenceFailed);
    Check(PBC_ResetCharacterMemory(0,true)==R::PersistenceFailed);
    Check(g_PBC_History.at(1).message=="shared" && g_PBC_HistoryOwners.at(10).size()==2);
    Check(g_PBC_Memories.at(10).at(0).text=="memory");
    Check(g_PBC_Relationships.at(10).at("friend").text=="old");
    Check(g_PBC_LastHistoryTime.size()==2);
    Check(PBC_StoreGeneratedRelationship(10,"friend","new","old",relationshipGeneration.load())==R::PersistenceFailed);
    Check(g_PBC_Relationships.at(10).at("friend").text=="old");
    failWrite=false;
    Check(PBC_UpdateHistoryMessage(1,"new")==R::Ok);
    Check(PBC_RemoveHistoryOwnership(10,1)==R::Ok && g_PBC_History.count(1)==1);
    Check(PBC_RemoveHistoryOwnership(20,1)==R::Ok && g_PBC_History.count(1)==0);
    Check(PBC_UpdateMemory(10,3,"new",8,"wrong")==R::Desync);
    Check(PBC_UpdateMemory(10,3,"new",8,"memory")==R::Ok);
    Check(g_PBC_Memories.at(10).at(0).importance==8);
    Check(PBC_DeleteMemory(10,3,"new")==R::Ok);
    Check(PBC_UpdateRelationship(10,"friend","new","old")==R::Ok);
    Check(PBC_DeleteRelationship(10,"friend","new")==R::Ok);
    Seed(); const auto beforeReset=relationshipGeneration.load();
    Check(PBC_ResetCharacterMemory(10,false)==R::Ok);
    Check(g_PBC_History.count(1)==1 && g_PBC_History.count(2)==0);
    Check(g_PBC_HistoryOwners.count(10)==0 && g_PBC_HistoryOwners.count(20)==1);
    Check(g_PBC_Memories.count(10)==0 && g_PBC_Memories.count(20)==1);
    Check(g_PBC_Relationships.count(10)==0 && g_PBC_Relationships.count(20)==1);
    const int beforeWrites=writes;
    Check(PBC_StoreGeneratedRelationship(10,"friend","resurrected","",beforeReset)==R::Desync);
    Check(writes==beforeWrites && g_PBC_Relationships.count(10)==0);
    Check(PBC_StoreGeneratedRelationship(10,"friend","fresh","",relationshipGeneration.load())==R::Ok);
    Check(PBC_ResetCharacterMemory(0,true)==R::Ok);
    Check(g_PBC_History.empty() && g_PBC_HistoryOwners.empty() && g_PBC_Memories.empty() && g_PBC_Relationships.empty());
    Seed(); Check(PBC_DeleteHistoryMessage(1)==R::Ok);
    Check(g_PBC_HistoryOwners.at(10)==std::deque<uint64_t>{2} && g_PBC_HistoryOwners.at(20).empty());
    Check(PBC_DeleteHistoryMessage(99)==R::NotFound);
    std::cout << "Production mutations: failures retain caches, confirmed reset preserves other owners, stale relationships rejected.\n";
}
