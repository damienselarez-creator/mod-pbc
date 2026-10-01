#include "pbc_group_helpers.h"
#include "pbc_character.h"
#include "pbc_lore.h"
#include "pbc_adventure.h"
#include "pbc_config.h"
#include "pbc_database.h"
#include "pbc_http.h"
#include "pbc_llm.h"
#include "pbc_utils.h"
#include "pbc_locales.h"
#include "pbc_scene_helpers.h"
#include "pbc_equipment_helpers.h"
#include "pbc_log.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "ObjectAccessor.h"
#include "Map.h"
#include "CharacterCache.h"

#include <fmt/core.h>
#include <sstream>
#include <mutex>
#include <unordered_set>
#include <ctime>
#include <algorithm>
#include <atomic>

namespace
{
// Conservatively invalidate queued relationship snapshots after a reset or edit.
std::atomic<uint64_t> relationshipGeneration{0};
}


// ---------------------------------------------------------------------------
// Variable substitution — var map
// ---------------------------------------------------------------------------

void PBC_SubstituteFromMap(std::string& tmpl, const PBC_VarMap& vars, bool annotate)
{
    PBC_ExpandNewlineEscapes(tmpl);
    for (const auto& [key, value] : vars)
    {
        PBC_ReplaceToken(tmpl, key,
            annotate ? ("[" + key + "]" + value) : value);
    }
}

PBC_VarMap PBC_BuildVarMap(Player* bot, const std::string& event)
{
    PBC_VarMap vars;
    if (!bot) return vars;

    vars["char_name"]   = bot->GetName();
    vars["char_gender"] = PBC_GenderStr(bot->getGender());
    vars["char_race"]   = PBC_RaceStr(bot->getRace());
    vars["char_class"]  = PBC_ClassStr(bot->getClass());
    vars["char_role"]   = PBC_RoleStr(bot);
    vars["char_level"]  = std::to_string(bot->GetLevel());
    { uint32 m = bot->GetMoney(); vars["char_gold"] = std::to_string(m / 10000) + "g " + std::to_string((m % 10000) / 100) + "s"; }
    vars["scene"]        = PBC_BuildSceneStr(bot);
    vars["pet_info"]     = PBC_BuildPetInfoStr(bot);
    vars["combat_status"] = PBC_BuildCombatStatusStr(bot);
    vars["equipment"]    = PBC_BuildEquipmentStr(bot);
    vars["char_group"]   = PBC_BuildGroupStatusStr(bot);
    vars["char_los"]     = PBC_BuildLosStr(bot);

    if (!event.empty())
        vars["event"] = event;

    return vars;
}

PBC_VarMap PBC_BuildVarMapFromSnapshot(const PBC_CharacterSnapshot& snap, const std::string& event)
{
    PBC_VarMap vars;
    vars["char_name"]   = snap.charName;
    vars["char_gender"] = snap.charGender;
    vars["char_race"]   = snap.charRace;
    vars["char_class"]  = snap.charClass;
    vars["char_role"]   = snap.charRole;
    vars["char_level"]  = snap.charLevel;
    vars["char_gold"]   = snap.charGold;
    vars["scene"]        = snap.scene;
    vars["pet_info"]     = snap.petInfo;
    vars["combat_status"] = snap.combatStatus;
    vars["equipment"]    = snap.equipment;
    vars["char_group"]   = snap.charGroup;
    vars["char_los"]     = snap.charLos;

    if (!event.empty())
        vars["event"] = event;

    return vars;
}

void PBC_TriggerCondensation(Player* bot)
{
    if (!bot) return;
    if (PBC_AdventureManaged(bot->GetGUID().GetCounter()))
        return;

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "TriggerCondensation: queuing condensation for character={}", bot->GetName());

    PBC_EventItem ev;
    ev.type                      = PBC_EventType::Condensation;
    ev.condensationChar          = PBC_SnapshotCharacter(bot);
    ev.condensationSystemPrompt  = g_PBC_CondensationSystemPrompt;
    ev.condensationUserPrompt    = g_PBC_CondensationUserPrompt;

    PBC_PushEvent(std::move(ev));
}


std::string PBC_SubstituteVars(const std::string& tmpl, Player* bot, const std::string& event,
                                bool expandComposites, bool annotate)
{
    std::string out = tmpl;
    PBC_VarMap vars = PBC_BuildVarMap(bot, event);
    PBC_SubstituteFromMap(out, vars, annotate);

    if (expandComposites && bot)
    {
        PBC_ReplaceToken(out, "character_card",
            annotate ? ("[character_card]" + PBC_GetCharacterCard(bot)) : PBC_GetCharacterCard(bot));
        PBC_ReplaceToken(out, "chat_history",
            annotate ? ("[chat_history]" + PBC_GetChatHistory(bot->GetGUID().GetCounter())) : PBC_GetChatHistory(bot->GetGUID().GetCounter()));
        PBC_ReplaceToken(out, "context",
            annotate ? ("[context]" + PBC_GetCharacterContext(bot)) : PBC_GetCharacterContext(bot));
    }

    PBC_CleanUnknownTokens(out);
    return out;
}


std::string PBC_GetCharacterCard(Player* bot)
{
    const std::string& name = bot->GetName();

    auto it = g_PBC_CharacterCards.find(name);
    if (it != g_PBC_CharacterCards.end())
        return PBC_SubstituteVars(it->second, bot, "", false);
    return PBC_SubstituteVars(g_PBC_DefaultCharacterDescription, bot, "", false);
}

// Builds the [MEMORIES] block for a character's prompt.
// Selection: most important memories within token budget, output chronologically.
std::string PBC_GetMemoriesBlock(uint64_t botGuid)
{
    // We need both the entries and their original positions for chronological
    // ordering. The vector in g_PBC_Memories is always maintained in
    // chronological order (loaded by id ASC from DB, new entries appended at
    // end during condensation), so the vector index is a reliable proxy.
    struct IndexedEntry
    {
        size_t           origIndex;
        PBC_MemoryEntry  entry;
    };

    std::vector<IndexedEntry> entries;
    {
        std::lock_guard<std::mutex> lock(g_PBC_MemoriesMutex);
        auto it = g_PBC_Memories.find(botGuid);
        if (it == g_PBC_Memories.end() || it->second.empty())
            return "";
        entries.reserve(it->second.size());
        for (size_t i = 0; i < it->second.size(); ++i)
            entries.push_back({i, it->second[i]});
    }

    // Sort by importance DESC (most important first) for selection
    std::sort(entries.begin(), entries.end(),
        [](const IndexedEntry& a, const IndexedEntry& b)
        {
            if (a.entry.importance != b.entry.importance)
                return a.entry.importance > b.entry.importance;
            return a.origIndex < b.origIndex; // tie-break: earlier first
        });

    // Select memories that fit within the token budget
    // Token estimation: ~4 chars per token
    const uint32_t budget = g_PBC_MaxMemoriesCtx;
    uint32_t usedTokens = 0;
    std::vector<IndexedEntry> selected;

    for (const auto& ie : entries)
    {
        uint32_t entryTokens = static_cast<uint32_t>(PBC_EstimateTokens(ie.entry.text));
        if (usedTokens + entryTokens > budget)
            break;
        usedTokens += entryTokens;
        selected.push_back(ie);
    }

    if (selected.empty())
        return "";

    // Re-sort selected chronologically (by original index ASC) for output
    std::sort(selected.begin(), selected.end(),
        [](const IndexedEntry& a, const IndexedEntry& b)
        {
            return a.origIndex < b.origIndex;
        });

    std::ostringstream oss;
    for (const auto& ie : selected)
        oss << ie.entry.text << "\n";

    std::string result = oss.str();
    // Trim trailing newline
    if (!result.empty() && result.back() == '\n')
        result.pop_back();
    return result;
}


std::string PBC_GetCharacterContext(Player* bot)
{
    return PBC_SubstituteVars(g_PBC_CharacterContext, bot, "", false);
}


// ---------------------------------------------------------------------------
// Thread-safe character name lookup (fallback to DB if not in cache)
// ---------------------------------------------------------------------------
std::string PBC_GetCharacterName(uint64_t guid)
{
    if (guid == 0) return "Unknown";

    // CharacterCache is thread-safe and covers both online and offline characters
    CharacterCacheEntry const* entry = sCharacterCache->GetCharacterCacheByGuid(ObjectGuid(guid));
    if (entry && !entry->Name.empty())
        return entry->Name;

    // Fallback: DB query (rare — character may have been deleted)
    QueryResult result = CharacterDatabase.Query(
        "SELECT name FROM characters WHERE guid = {}", guid);
    if (result)
        return (*result)[0].Get<std::string>();

    return "Unknown";
}

// ---------------------------------------------------------------------------
// Derive whisper recipient from ownership (must hold g_PBC_HistoryMutex)
// ---------------------------------------------------------------------------
static uint64_t PBC_GetWhisperTarget(uint64_t historyId, uint64_t excludeGuid)
{
    for (const auto& [guid, idList] : g_PBC_HistoryOwners)
    {
        if (guid == excludeGuid)
            continue;
        if (std::find(idList.begin(), idList.end(), historyId) != idList.end())
            return guid;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Render ONE history entry from a specific character's perspective
// ---------------------------------------------------------------------------
std::string PBC_RenderHistoryLine(const PBC_HistoryEntry& entry, uint64_t forGuid)
{
    // Narrator (type=0)
    if (entry.type == 0)
        return PBC_Localize("Narrator: *{0}*", entry.message);

    std::string authorName = PBC_GetCharacterName(entry.authorGuid);

    // ----- Character's own message -----
    if (entry.authorGuid == forGuid)
    {
        if (entry.type == CHAT_MSG_WHISPER)
        {
            uint64_t targetGuid = PBC_GetWhisperTarget(entry.id, forGuid);
            std::string targetName = PBC_GetCharacterName(targetGuid);
            if (!targetName.empty() && targetName != "Unknown")
                return PBC_Localize("You (privately to {0}): {1}", targetName, entry.message);
            return PBC_Localize("You (privately): {0}", entry.message);
        }
        return PBC_Localize("You: {0}", entry.message);
    }

    // ----- Someone else's message -----
    if (entry.type == CHAT_MSG_WHISPER)
    {
        uint64_t targetGuid = PBC_GetWhisperTarget(entry.id, entry.authorGuid);
        if (targetGuid == forGuid)
            return PBC_Localize("{0} (privately to you): {1}", authorName, entry.message);

        std::string targetName = PBC_GetCharacterName(targetGuid);
        if (!targetName.empty() && targetName != "Unknown")
            return PBC_Localize("{0} (privately to {1}): {2}", authorName, targetName, entry.message);
        return PBC_Localize("{0} (privately): {1}", authorName, entry.message);
    }

    // Regular chat
    return PBC_Localize("{0}: {1}", authorName, entry.message);
}

// ---------------------------------------------------------------------------
// Build the full chat history string for prompt inclusion (thread-safe)
// ---------------------------------------------------------------------------
std::string PBC_GetChatHistory(uint64_t botGuid)
{
    std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);

    auto ownersIt = g_PBC_HistoryOwners.find(botGuid);
    if (ownersIt == g_PBC_HistoryOwners.end() || ownersIt->second.empty())
        return "";

    std::ostringstream oss;
    for (uint64_t historyId : ownersIt->second)
    {
        auto entryIt = g_PBC_History.find(historyId);
        if (entryIt == g_PBC_History.end())
            continue;
        oss << PBC_RenderHistoryLine(entryIt->second, botGuid) << "\n";
    }
    return oss.str();
}

// ---------------------------------------------------------------------------
// Pre-render all history lines for a character (used by snapshot capture)
// ---------------------------------------------------------------------------
std::deque<std::string> PBC_GetChatHistoryPreRendered(uint64_t botGuid)
{
    std::deque<std::string> result;
    std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);

    auto ownersIt = g_PBC_HistoryOwners.find(botGuid);
    if (ownersIt == g_PBC_HistoryOwners.end())
        return result;

    for (uint64_t historyId : ownersIt->second)
    {
        auto entryIt = g_PBC_History.find(historyId);
        if (entryIt == g_PBC_History.end())
            continue;
        result.push_back(PBC_RenderHistoryLine(entryIt->second, botGuid));
    }
    return result;
}

// ---------------------------------------------------------------------------
// Internal helper: checks whether botGuid needs a time-gap line.
// Must be called under g_PBC_HistoryMutex lock.
// Returns true if a time-gap should be inserted for this character.
// ---------------------------------------------------------------------------
static bool PBC_TimeGapNeeded_Locked(uint64_t botGuid, bool incomingIsWhisper)
{
    static constexpr time_t kTimeGapThresholdSec = 300; // 5 minutes

    auto timeIt = g_PBC_LastHistoryTime.find(botGuid);
    if (timeIt == g_PBC_LastHistoryTime.end())
        return false;

    time_t now = time(nullptr);
    if (now - timeIt->second < kTimeGapThresholdSec)
        return false;

    auto ownersIt = g_PBC_HistoryOwners.find(botGuid);
    if (ownersIt == g_PBC_HistoryOwners.end() || ownersIt->second.empty())
        return false;

    uint64_t lastId = ownersIt->second.back();
    auto entryIt = g_PBC_History.find(lastId);
    if (entryIt == g_PBC_History.end())
        return false;

    // Don't insert if last line is already a narrator/time-passes line
    if (entryIt->second.type == 0 && entryIt->second.message == PBC_Localize("some time passes"))
        return false;

    // Skip time-gap only when BOTH last message is a whisper AND incoming is whisper
    if (incomingIsWhisper && entryIt->second.type == CHAT_MSG_WHISPER)
        return false;

    return true;
}

// ---------------------------------------------------------------------------
// Insert "Narrator: *some time passes*" if 5+ minutes since last entry
// ---------------------------------------------------------------------------
bool PBC_MaybeInsertTimeGap(uint64_t botGuid, bool incomingIsWhisper)
{
    bool shouldInsert = false;
    {
        std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
        shouldInsert = PBC_TimeGapNeeded_Locked(botGuid, incomingIsWhisper);
    }

    if (!shouldInsert)
        return false;
    return PBC_AppendHistoryMessage(0, 0, PBC_Localize("some time passes"), {botGuid}) != 0;
}

// ---------------------------------------------------------------------------
// Batch version: creates ONE shared "some time passes" entry for all
// characters that need it, instead of one duplicate per character.
// Returns the set of GUIDs for which a time-gap was actually inserted,
// so the caller can push the rendered line into each snapshot's history.
// Thread-safe.
// ---------------------------------------------------------------------------
std::unordered_set<uint64_t> PBC_MaybeInsertSharedTimeGap(
    const std::vector<uint64_t>& botGuids, bool incomingIsWhisper)
{
    std::unordered_set<uint64_t> needGap;

    {
        std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
        for (uint64_t guid : botGuids)
        {
            if (PBC_TimeGapNeeded_Locked(guid, incomingIsWhisper))
                needGap.insert(guid);
        }
    }

    if (needGap.empty())
        return needGap;

    // Create ONE shared entry owned by all characters that need it.
    // PBC_AppendHistoryMessage handles DB write, in-memory update,
    // g_PBC_LastHistoryTime refresh, and WS notifications for all owners.
    std::vector<uint64_t> owners(needGap.begin(), needGap.end());
    uint64_t newId = PBC_AppendHistoryMessage(0, 0, PBC_Localize("some time passes"), owners);

    // If AppendHistoryMessage deduped (extremely rare race), no new entry
    // was created — return empty so the caller doesn't push to snapshots.
    if (newId == 0)
        return {};

    return needGap;
}

// ---------------------------------------------------------------------------
// Create one history message and assign it to the given owners
// ---------------------------------------------------------------------------
uint64_t PBC_AppendHistoryMessage(uint64_t authorGuid, uint8_t type,
                                  const std::string& message,
                                  const std::vector<uint64_t>& ownerGuids, bool* durable,
                                  const std::deque<std::string>* expectedHistory)
{
    if (durable) *durable = false;
    if (ownerGuids.empty())
        return 0;

    std::vector<uint64_t> owners = ownerGuids;
    std::sort(owners.begin(), owners.end());
    owners.erase(std::unique(owners.begin(), owners.end()), owners.end());

    // Serialize deduplication, persistence and cache publication. Otherwise two
    // appenders may both pass deduplication or publish messages in reverse order.
    std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
    if (expectedHistory)
    {
        std::deque<std::string> current;
        auto owner = g_PBC_HistoryOwners.find(authorGuid);
        if (owner != g_PBC_HistoryOwners.end())
            for (uint64_t id : owner->second)
            {
                auto entry = g_PBC_History.find(id);
                if (entry == g_PBC_History.end()) return 0;
                current.push_back(PBC_RenderHistoryLine(entry->second, authorGuid));
            }
        if (current != *expectedHistory) return 0;
    }
    {
        bool allDedup = true;
        for (uint64_t ownerGuid : owners)
        {
            auto ownersIt = g_PBC_HistoryOwners.find(ownerGuid);
            if (ownersIt == g_PBC_HistoryOwners.end() || ownersIt->second.empty())
            {
                allDedup = false;
                break;
            }
            uint64_t lastId = ownersIt->second.back();
            auto entryIt = g_PBC_History.find(lastId);
            if (entryIt == g_PBC_History.end()
                || entryIt->second.authorGuid != authorGuid
                || entryIt->second.type != type
                || entryIt->second.message != message)
            {
                allDedup = false;
                break;
            }
        }
        if (allDedup)
        {
            if (durable) *durable = true;
            return 0;
        }
    }

    // The adventure journal is independent of dialogue SQL/LLM availability.
    PBC_AdventureHistory(authorGuid, type, message, owners);

    // DB write
    uint64_t newId = DB_InsertHistoryMessage(authorGuid, type, message, owners, durable);
    if (newId == 0)
        return 0;

    // In-memory update
    PBC_HistoryEntry entry;
    entry.id         = newId;
    entry.timestamp  = time(nullptr);
    entry.authorGuid = authorGuid;
    entry.type       = type;
    entry.message    = message;

    g_PBC_History[newId] = entry;
    for (uint64_t ownerGuid : owners)
    {
        g_PBC_HistoryOwners[ownerGuid].push_back(newId);
        g_PBC_LastHistoryTime[ownerGuid] = entry.timestamp;

        // WS notification — renders internally
        PBC_WsNotifyHistory(ownerGuid, entry);
    }

    return newId;
}

// ---------------------------------------------------------------------------
// Estimate total token count across all owned history lines
// ---------------------------------------------------------------------------
int PBC_EstimateHistoryTokens(uint64_t botGuid)
{
    std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
    auto ownersIt = g_PBC_HistoryOwners.find(botGuid);
    if (ownersIt == g_PBC_HistoryOwners.end())
        return 0;

    int total = 0;
    for (uint64_t historyId : ownersIt->second)
    {
        auto entryIt = g_PBC_History.find(historyId);
        if (entryIt == g_PBC_History.end())
            continue;
        std::string rendered = PBC_RenderHistoryLine(entryIt->second, botGuid);
        total += PBC_EstimateTokens(rendered);
    }
    return total;
}

// ---------------------------------------------------------------------------
// Edit a message by its real mod_pbc_history.id (affects ALL owners)
// ---------------------------------------------------------------------------
static bool HistoryEditableLocked(uint64_t historyId, uint64_t ownerGuid, uint32_t accountId)
{
    if (!ownerGuid)
        return true; // Internal server operation, not an account-scoped API call.
    auto owner = g_PBC_HistoryOwners.find(ownerGuid);
    if (!accountId || owner == g_PBC_HistoryOwners.end() ||
        std::find(owner->second.begin(), owner->second.end(), historyId) == owner->second.end())
        return false;
    for (auto const& [guid, ids] : g_PBC_HistoryOwners)
        if (std::find(ids.begin(), ids.end(), historyId) != ids.end() &&
            sCharacterCache->GetCharacterAccountIdByGuid(ObjectGuid(guid)) != accountId)
            return false;
    return true;
}

PBC_HistoryResult PBC_UpdateHistoryMessage(uint64_t historyId,
                                           const std::string& newMessage, uint64_t ownerGuid, uint32_t accountId)
{
    std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
    auto it = g_PBC_History.find(historyId);
    if (it == g_PBC_History.end())
        return PBC_HistoryResult::NotFound;

    if (!HistoryEditableLocked(historyId, ownerGuid, accountId))
        return PBC_HistoryResult::Forbidden;
    if (!DB_UpdateHistoryMessage(historyId, newMessage))
        return PBC_HistoryResult::PersistenceFailed;
    it->second.message = newMessage;
    return PBC_HistoryResult::Ok;
}

// ---------------------------------------------------------------------------
// Hard delete: remove message from mod_pbc_history AND all ownership rows
// ---------------------------------------------------------------------------
PBC_HistoryResult PBC_DeleteHistoryMessage(uint64_t historyId, uint64_t ownerGuid, uint32_t accountId)
{
    std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
    if (g_PBC_History.find(historyId) == g_PBC_History.end())
        return PBC_HistoryResult::NotFound;
    if (!HistoryEditableLocked(historyId, ownerGuid, accountId))
        return PBC_HistoryResult::Forbidden;
    if (!DB_DeleteHistoryMessage(historyId))
        return PBC_HistoryResult::PersistenceFailed;
    for (auto& [guid, ids] : g_PBC_HistoryOwners)
        ids.erase(std::remove(ids.begin(), ids.end(), historyId), ids.end());
    g_PBC_History.erase(historyId);
    return PBC_HistoryResult::Ok;
}

PBC_HistoryResult PBC_RemoveHistoryOwnership(uint64_t guid, uint64_t historyId)
{
    std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
    auto owner = g_PBC_HistoryOwners.find(guid);
    if (owner == g_PBC_HistoryOwners.end())
        return PBC_HistoryResult::NotFound;
    auto pos = std::find(owner->second.begin(), owner->second.end(), historyId);
    if (pos == owner->second.end())
        return PBC_HistoryResult::NotFound;
    if (!DB_RemoveHistoryOwnership(guid, historyId, true))
        return PBC_HistoryResult::PersistenceFailed;
    owner->second.erase(pos);
    for (auto const& [otherGuid, ids] : g_PBC_HistoryOwners)
        if (std::find(ids.begin(), ids.end(), historyId) != ids.end())
            return PBC_HistoryResult::Ok;
    g_PBC_History.erase(historyId);
    return PBC_HistoryResult::Ok;
}

PBC_HistoryResult PBC_ResetCharacterMemory(uint64_t botGuid, bool allCharacters)
{
    std::scoped_lock lock(g_PBC_HistoryMutex, g_PBC_MemoriesMutex, g_PBC_RelationshipsMutex);
    ++relationshipGeneration;
    if (!DB_ResetCharacterMemory(botGuid, allCharacters))
        return PBC_HistoryResult::PersistenceFailed;
    if (allCharacters)
    {
        g_PBC_History.clear();
        g_PBC_HistoryOwners.clear();
        g_PBC_LastHistoryTime.clear();
        g_PBC_Memories.clear();
        g_PBC_Relationships.clear();
    }
    else
    {
        g_PBC_HistoryOwners.erase(botGuid);
        g_PBC_LastHistoryTime.erase(botGuid);
        g_PBC_Memories.erase(botGuid);
        g_PBC_Relationships.erase(botGuid);
        for (auto it = g_PBC_History.begin(); it != g_PBC_History.end();)
        {
            bool owned = false;
            for (auto const& [guid, ids] : g_PBC_HistoryOwners)
                if (std::find(ids.begin(), ids.end(), it->first) != ids.end())
                {
                    owned = true;
                    break;
                }
            if (owned)
                ++it;
            else
                it = g_PBC_History.erase(it);
        }
    }
    return PBC_HistoryResult::Ok;
}


PBC_HistoryResult PBC_StoreGeneratedRelationship(uint64_t botGuid, std::string const& targetName,
    std::string const& newText, std::string const& originalText, uint64_t generation)
{
    std::lock_guard<std::mutex> lock(g_PBC_RelationshipsMutex);
    if (generation != relationshipGeneration.load())
        return PBC_HistoryResult::Desync;
    auto bot = g_PBC_Relationships.find(botGuid);
    std::string current;
    if (bot != g_PBC_Relationships.end())
    {
        auto relation = bot->second.find(targetName);
        if (relation != bot->second.end())
            current = relation->second.text;
    }
    if (current != originalText)
        return PBC_HistoryResult::Desync;
    if (!DB_UpsertRelationship(botGuid, targetName, newText))
        return PBC_HistoryResult::PersistenceFailed;
    auto& entry = g_PBC_Relationships[botGuid][targetName];
    entry.text = newText;
    entry.updatedAt = PBC_FormatDateTime(std::time(nullptr));
    return PBC_HistoryResult::Ok;
}

PBC_HistoryResult PBC_UpdateRelationship(uint64_t botGuid, const std::string& targetName,
                                          const std::string& newText,
                                          const std::string& originalText)
{
    std::lock_guard<std::mutex> lock(g_PBC_RelationshipsMutex);
    auto charIt = g_PBC_Relationships.find(botGuid);
    if (charIt == g_PBC_Relationships.end())
        return PBC_HistoryResult::NotFound;

    auto relIt = charIt->second.find(targetName);
    if (relIt == charIt->second.end())
        return PBC_HistoryResult::NotFound;

    if (relIt->second.text != originalText)
        return PBC_HistoryResult::Desync;

    ++relationshipGeneration;
    if (!DB_UpdateRelationshipText(botGuid, targetName, newText))
        return PBC_HistoryResult::PersistenceFailed;
    relIt->second.text = newText;
    relIt->second.updatedAt = PBC_FormatDateTime(std::time(nullptr));
    return PBC_HistoryResult::Ok;
}

PBC_HistoryResult PBC_DeleteRelationship(uint64_t botGuid, const std::string& targetName,
                                          const std::string& originalText)
{
    std::lock_guard<std::mutex> lock(g_PBC_RelationshipsMutex);
    auto charIt = g_PBC_Relationships.find(botGuid);
    if (charIt == g_PBC_Relationships.end())
        return PBC_HistoryResult::NotFound;

    auto relIt = charIt->second.find(targetName);
    if (relIt == charIt->second.end())
        return PBC_HistoryResult::NotFound;

    if (relIt->second.text != originalText)
        return PBC_HistoryResult::Desync;

    ++relationshipGeneration;
    if (!DB_DeleteRelationship(botGuid, targetName))
        return PBC_HistoryResult::PersistenceFailed;
    charIt->second.erase(relIt);
    return PBC_HistoryResult::Ok;
}

PBC_HistoryResult PBC_UpdateMemory(uint64_t botGuid, uint64_t memoryId,
                                    const std::string& newText,
                                    uint8_t newImportance,
                                    const std::string& originalText)
{
    std::lock_guard<std::mutex> lock(g_PBC_MemoriesMutex);
    auto it = g_PBC_Memories.find(botGuid);
    if (it == g_PBC_Memories.end())
        return PBC_HistoryResult::NotFound;

    for (auto& entry : it->second)
    {
        if (entry.dbId != memoryId)
            continue;

        if (entry.text != originalText)
            return PBC_HistoryResult::Desync;

        if (!DB_UpdateMemoryById(memoryId, newText, newImportance))
            return PBC_HistoryResult::PersistenceFailed;
        entry.text       = newText;
        entry.importance = newImportance;
        return PBC_HistoryResult::Ok;
    }

    return PBC_HistoryResult::NotFound;
}

PBC_HistoryResult PBC_DeleteMemory(uint64_t botGuid, uint64_t memoryId,
                                    const std::string& originalText)
{
    std::lock_guard<std::mutex> lock(g_PBC_MemoriesMutex);
    auto it = g_PBC_Memories.find(botGuid);
    if (it == g_PBC_Memories.end())
        return PBC_HistoryResult::NotFound;

    for (auto vit = it->second.begin(); vit != it->second.end(); ++vit)
    {
        if (vit->dbId != memoryId)
            continue;

        if (vit->text != originalText)
            return PBC_HistoryResult::Desync;

        if (!DB_DeleteMemoryById(memoryId))
            return PBC_HistoryResult::PersistenceFailed;
        it->second.erase(vit);
        return PBC_HistoryResult::Ok;
    }

    return PBC_HistoryResult::NotFound;
}

// Snapshot var substitution helper (thread-safe, uses snapshot only)
// Substitutes all vars that can be derived from the snapshot + DB.
// Does NOT include relationships — that's handled per-caller since
// some callers (condensation) don't need it.  Callers should invoke
// PBC_CleanUnknownTokens after all their substitutions are done.
static void ReplaceSnapshotVars(std::string& out, const PBC_CharacterSnapshot& snap,
                                const std::string& eventLine)
{
    PBC_VarMap vars = PBC_BuildVarMapFromSnapshot(snap, eventLine);
    PBC_SubstituteFromMap(out, vars);

    // Composite vars from snapshot (need snapshot-specific data, not in the map)
    PBC_ReplaceToken(out, "character_card", snap.characterCard);
    PBC_ReplaceToken(out, "context",        snap.context);

    // Chat history from the snapshot's local (thread-local) copy
    std::ostringstream history;
    bool adventure = PBC_AdventureManaged(snap.charGuidRaw);
    size_t start = adventure && snap.history.size() > 40 ? snap.history.size() - 40 : 0;
    size_t bytes = 0;
    for (size_t i = snap.history.size(); i > start; --i)
    {
        bytes += snap.history[i - 1].size();
        if (adventure && bytes > 12000)
        {
            start = i;
            break;
        }
    }
    for (size_t i = start; i < snap.history.size(); ++i)
        history << snap.history[i] << "\n";
    PBC_ReplaceToken(out, "chat_history", history.str());

    // Memories from DB (thread-safe)
    // Keep previously condensed memories available when automatic adventure memory takes over.
    PBC_ReplaceToken(out, "memories", PBC_GetMemoriesBlock(snap.charGuidRaw));
}


PBC_CharacterSnapshot PBC_SnapshotCharacter(Player* bot)
{
    PBC_CharacterSnapshot snap;
    snap.requiresActiveSelfbot = PBC_IsActiveSelfbot(bot);
    snap.relationshipGeneration = relationshipGeneration.load();
    snap.charObjGuid  = bot->GetGUID();
    snap.charGuidRaw  = bot->GetGUID().GetCounter();
    snap.charName     = bot->GetName();

    // Pre-render the character card and context once here so the event thread
    // never needs to call into game data.
    snap.characterCard = PBC_GetCharacterCard(bot);
    snap.context       = PBC_GetCharacterContext(bot);

    // Capture raw template variables
    snap.charGender   = PBC_GenderStr(bot->getGender());
    snap.charRace     = PBC_RaceStr(bot->getRace());
    snap.charClass    = PBC_ClassStr(bot->getClass());
    snap.charRole     = PBC_RoleStr(bot);
    snap.charLevel    = std::to_string(bot->GetLevel());
    { uint32 m = bot->GetMoney(); snap.charGold = std::to_string(m / 10000) + "g " + std::to_string((m % 10000) / 100) + "s"; }

    // Scene (location + travel state + time of day + optional weather)
    snap.scene = PBC_BuildSceneStr(bot);

    // Combat status
    snap.combatStatus = PBC_BuildCombatStatusStr(bot);

    // Equipment
    snap.equipment = PBC_BuildEquipmentStr(bot);

    // Pet info
    snap.petInfo = PBC_BuildPetInfoStr(bot);

    // Group status
    snap.charGroup = PBC_BuildGroupStatusStr(bot);

    // Line-of-sight
    snap.charLos = PBC_BuildLosStr(bot);

    // Capture the current global history into the snapshot's local copy
    // using pre-rendered strings so the event thread never needs name lookups.
    snap.history = PBC_GetChatHistoryPreRendered(snap.charGuidRaw);

    // Capture party member names and whether a real player is in the group.
    {
        snap.partyMemberNames.clear();
        snap.hasRealPlayerInGroup = false;

        if (Group* grp = bot->GetGroup())
        {
            for (GroupReference* ref = grp->GetFirstMember(); ref; ref = ref->next())
            {
                Player* member = ref->GetSource();
                if (!member || !member->IsInWorld() || member == bot) continue;

                WorldSession* ms = member->GetSession();
                if (!ms) continue;

                snap.partyMemberNames.push_back(member->GetName());
                if (!ms->IsHeadless())
                {
                    snap.hasRealPlayerInGroup = true;
                    snap.adventureGroupPlayers.push_back(member->GetGUID().GetCounter());
                }
            }
        }
    }

    return snap;
}


std::string PBC_BuildTargetInfo(const std::string& name)
{
    // Upper-case the name
    std::string upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

    Player* p = ObjectAccessor::FindPlayerByName(name);
    if (!p)
        return upper;

    std::string gender = p->getGender() == GENDER_FEMALE ? "FEMALE" : "MALE";
    std::string race   = PBC_RaceStr(p->getRace());
    std::transform(race.begin(), race.end(), race.begin(), ::toupper);
    std::string cls    = PBC_ClassStr(p->getClass());
    std::transform(cls.begin(), cls.end(), cls.begin(), ::toupper);

    return upper + ", " + gender + " " + race + " " + cls;
}

// Builds the [RELATIONSHIPS] block for a character's prompt.

std::string PBC_GetRelationshipsBlock(const PBC_CharacterSnapshot& snap)
{
    // Read all relationship entries for this character under a single lock.
    std::unordered_map<std::string, std::string> relTexts;
    {
        std::lock_guard<std::mutex> lk(g_PBC_RelationshipsMutex);
        auto charIt = g_PBC_Relationships.find(snap.charGuidRaw);
        if (charIt != g_PBC_Relationships.end())
        {
            for (const auto& kv : charIt->second)
                relTexts[kv.first] = kv.second.text;
        }
    }

    std::unordered_set<std::string> emitted;

    // Emit a relationship line for a name ALWAYS (party / whisper target).
    // Falls back to default text if no stored relationship exists.
    auto emitRelationship = [&](std::ostringstream& oss, const std::string& name)
    {
        if (emitted.count(name))
            return;
        auto it = relTexts.find(name);
        if (it != relTexts.end() && !it->second.empty())
            oss << PBC_Localize("Your relationship with {0}: {1}", name, it->second) << "\n";
        else
            oss << PBC_Localize("Your relationship with {0}: {1}", name, PBC_DefaultRelationshipText(name)) << "\n";
        emitted.insert(name);
    };

    std::ostringstream oss;

    // Always emit current party members (even if default) — the bot should
    // know who it's travelling with.
    for (const auto& memberName : snap.partyMemberNames)
        emitRelationship(oss, memberName);

    // Whisper target (if not already covered by party list).
    if (!snap.whisperTargetName.empty())
        emitRelationship(oss, snap.whisperTargetName);

    // Emit ALL remaining stored relationships with non-default text.
    // This ensures the character remembers everyone they have a meaningful
    // relationship with, not just whoever is currently in the party.
    for (const auto& [name, text] : relTexts)
    {
        if (emitted.count(name))
            continue;
        if (text.empty())
            continue;   // empty stored text = never set / default, skip
        oss << PBC_Localize("Your relationship with {0}: {1}", name, text) << "\n";
    }

    std::string result = oss.str();
    // Trim trailing newline
    if (!result.empty() && result.back() == '\n')
        result.pop_back();
    return result;
}


std::string PBC_BuildUserPromptFromSnapshot(const PBC_CharacterSnapshot& snap,
                                             const std::string& eventLine)
{
    std::string out = g_PBC_UserPrompt;
    PBC_ExpandNewlineEscapes(out);

    // Substitute all snapshot vars (composites, basic vars, event)
    ReplaceSnapshotVars(out, snap, eventLine);

    // Relationships block (only in the main user prompt, not condensation)
    PBC_ReplaceToken(out, "relationships", PBC_GetRelationshipsBlock(snap));

    PBC_CleanUnknownTokens(out);
    // Append after template expansion: documentary braces must remain literal data.
    // Never insert the corpus into condensation, memories or relationship storage.
    out += PBC_GetLoreBlock(snap.charGuidRaw, eventLine);
    out += PBC_AdventureContext(snap.charGuidRaw, snap.whisperTargetGuid.GetCounter(),
        snap.adventureGroupPlayers, eventLine);
    return out;
}


std::string PBC_BuildCondensationPromptFromSnapshot(const PBC_CharacterSnapshot& snap,
                                                     const std::string& tmpl)
{
    std::string out = tmpl;
    PBC_ExpandNewlineEscapes(out);

    // Substitute all snapshot vars with empty event
    ReplaceSnapshotVars(out, snap, "");

    PBC_CleanUnknownTokens(out);
    return out;
}
