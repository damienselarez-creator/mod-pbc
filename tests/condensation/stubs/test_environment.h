#pragma once
#include <cstdint>
#include <ctime>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct PBC_CharacterSnapshot {
    uint64_t charGuidRaw = 1;
    std::string charName = "Antanagor";
    std::deque<std::string> history;
    std::vector<std::string> partyMemberNames;
};
struct PBC_HistoryEntry { std::string message; };
struct PBC_MemoryEntry {
    uint64_t dbId = 0;
    std::string text;
    uint8_t importance = 0;
    std::string createdAt;
};
struct PBC_RelationshipEntry { std::string text; };
struct PBC_APIConfig {};
struct PBC_LLMResult { bool success; std::string text; int tokensUsed = 0; };
enum class PBC_LogLevel { PBC_DEBUG, PBC_WARNING };
enum class PBC_EventType { RelationshipUpdate };
struct PBC_EventItem {
    PBC_EventType type{};
    PBC_CharacterSnapshot relationshipChar;
    std::string relationshipTargetName, relationshipTargetInfo, relationshipCurrentText;
    std::string relationshipSystemPrompt, relationshipUserPromptTmpl;
};

inline std::mutex g_PBC_HistoryMutex, g_PBC_MemoriesMutex, g_PBC_RelationshipsMutex;
inline std::unordered_map<uint64_t, PBC_HistoryEntry> g_PBC_History;
inline std::unordered_map<uint64_t, std::deque<uint64_t>> g_PBC_HistoryOwners;
inline std::unordered_map<uint64_t, time_t> g_PBC_LastHistoryTime;
inline std::unordered_map<uint64_t, std::vector<PBC_MemoryEntry>> g_PBC_Memories;
inline std::unordered_map<uint64_t, std::unordered_map<std::string, PBC_RelationshipEntry>> g_PBC_Relationships;
inline std::string g_PBC_RelationshipUpdateSystemPrompt, g_PBC_RelationshipUpdateUserPrompt;
inline PBC_APIConfig connection;
inline bool connectionAvailable = true;
inline PBC_LLMResult response{true, "[7] You helped Norka."};
inline std::function<void()> duringRequest, duringDelete;
inline int calls = 0, writes = 0;
inline std::vector<uint64_t> deletedIds;
inline std::vector<PBC_EventItem> queued;

template<class... Args> void PBC_Log(PBC_LogLevel, const char*, Args&&...) {}
inline std::string PBC_RenderHistoryLine(const PBC_HistoryEntry& e, uint64_t) { return e.message; }
inline std::string PBC_FormatDate(time_t) { return "2026-09-14"; }
inline void DB_InsertMemory(uint64_t, const std::string&, uint8_t) { ++writes; }
inline void DB_RemoveHistoryOwnership(uint64_t, uint64_t id, bool) {
    deletedIds.push_back(id);
    if (duringDelete) { auto callback = std::move(duringDelete); duringDelete = {}; callback(); }
}
inline const PBC_APIConfig* PBC_GetConnection(const std::string&) { return connectionAvailable ? &connection : nullptr; }
inline std::string PBC_BuildCondensationPromptFromSnapshot(const PBC_CharacterSnapshot&, const std::string&) { return "prompt"; }
inline PBC_LLMResult PBC_CallLLMWithConfig(const PBC_APIConfig&, const std::string&, const std::string&, bool) {
    ++calls;
    if (duringRequest) duringRequest();
    return response;
}
inline void PBC_WsNotify(uint64_t, const std::string&) {}
inline std::string PBC_DefaultRelationshipText(const std::string& name) { return name; }
inline std::string PBC_BuildTargetInfo(const std::string& name) { return name; }
inline void PBC_PushEvent(PBC_EventItem ev) { queued.push_back(std::move(ev)); }
