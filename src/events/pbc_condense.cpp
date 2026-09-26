#include "pbc_condense.h"
#include "pbc_adventure.h"
#include "pbc_config.h"
#include "pbc_character.h"
#include "pbc_database.h"
#include "pbc_llm.h"
#include "pbc_http.h"
#include "pbc_utils.h"
#include "pbc_log.h"

#include "pbc_memory_parser.h"

#include <ctime>
#include <algorithm>
#include <mutex>
#include <utility>
#include <vector>
#include <unordered_set>

// ---------------------------------------------------------------------------
// Internal helpers for validated persistence and snapshot checks.
// ---------------------------------------------------------------------------
namespace
{
// History mutex protects this process-lifetime circuit breaker. Restart reloads
// the authoritative database state after an uncertain transaction outcome.
std::unordered_set<uint64_t> suspendedPersistence;

// Caller holds the history mutex. Fail closed on stale snapshots, missing rows,
// edits or replacement histories; never discard data absent from the prompt.
bool HistoryMatchesSnapshot(const PBC_CharacterSnapshot& snap)
{
    auto owner = g_PBC_HistoryOwners.find(snap.charGuidRaw);
    if (snap.history.empty() || owner == g_PBC_HistoryOwners.end()
        || owner->second.size() != snap.history.size())
        return false;
    for (size_t i = 0; i < owner->second.size(); ++i)
    {
        auto entry = g_PBC_History.find(owner->second[i]);
        if (entry == g_PBC_History.end()
            || PBC_RenderHistoryLine(entry->second, snap.charGuidRaw) != snap.history[i])
            return false;
    }
    return true;
}
} // namespace

// ---------------------------------------------------------------------------
// PBC_CondenseInline
//
// Runs condensation synchronously inside the event thread.
// ---------------------------------------------------------------------------
bool PBC_CondenseInline(PBC_CharacterSnapshot& snap,
                        const std::string& sysPrompt,
                        const std::string& userPromptTmpl, int timeoutCapSec)
{
    if (PBC_AdventureManaged(snap.charGuidRaw))
        return false;
    if (sysPrompt.empty() || userPromptTmpl.empty())
    {
        PBC_Log(PBC_LogLevel::PBC_DEBUG, "CondenseInline: prompts not configured, skipping for character={}", snap.charName);
        return false;
    }

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "CondenseInline: character={} history_lines={}", snap.charName, snap.history.size());

    std::deque<uint64_t> sourceIds;
    {
        std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
        if (suspendedPersistence.count(snap.charGuidRaw))
            return false;
        if (!HistoryMatchesSnapshot(snap))
        {
            PBC_Log(PBC_LogLevel::PBC_WARNING, "CondenseInline: empty or stale snapshot for character={} — history retained", snap.charName);
            return false;
        }
        sourceIds = g_PBC_HistoryOwners.at(snap.charGuidRaw);
    }

    std::string userPrompt = PBC_BuildCondensationPromptFromSnapshot(snap, userPromptTmpl);
    auto cfg = PBC_GetConnection("condensation");
    if (!cfg)
        return false;
    // Own a copy while the network request is running.
    auto connection = *cfg;
    if (timeoutCapSec > 0) connection.requestTimeoutSec = std::min(connection.requestTimeoutSec, timeoutCapSec);
    PBC_LLMResult res = PBC_CallLLMWithConfig(connection, sysPrompt, userPrompt, /*preserveNewlines=*/true);

    if (!res.success || res.text.empty())
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING, "CondenseInline: LLM failed for character={} — history left untouched, will retry on next event", snap.charName);
        return false;
    }

    const auto memories = PBC_ValidateMemoryLines(res.text);
    if (memories.empty())
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING, "CondenseInline: invalid or empty memory batch for character={} — history retained", snap.charName);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
        if (!HistoryMatchesSnapshot(snap)
            || g_PBC_HistoryOwners.at(snap.charGuidRaw) != sourceIds)
        {
            PBC_Log(PBC_LogLevel::PBC_WARNING, "CondenseInline: history changed during extraction for character={} — result discarded", snap.charName);
            return false;
        }
        if (suspendedPersistence.count(snap.charGuidRaw))
            return false;

        // Reserve the cache before committing so allocation failure cannot
        // leave a successfully committed batch only partially cached.
        std::lock_guard<std::mutex> memoryLock(g_PBC_MemoriesMutex);
        std::vector<PBC_MemoryEntry> updated = g_PBC_Memories[snap.charGuidRaw];
        for (const auto& memory : memories)
        {
            PBC_MemoryEntry entry;
            entry.dbId = 0;
            entry.text = memory.text;
            entry.importance = memory.importance;
            entry.createdAt = PBC_FormatDate(std::time(nullptr));
            updated.push_back(std::move(entry));
        }
        // Insert the suspension before starting any database writes. In
        // particular, never automatically retry an uncertain COMMIT.
        suspendedPersistence.insert(snap.charGuidRaw);
        if (!DB_CommitCondensation(snap.charGuidRaw, memories, sourceIds))
        {
            PBC_Log(PBC_LogLevel::PBC_WARNING,
                "CondenseInline: persistence unconfirmed for character={}; history retained, "
                "condensation suspended until server restart", snap.charName);
            return false;
        }
        g_PBC_Memories[snap.charGuidRaw].swap(updated);
        suspendedPersistence.erase(snap.charGuidRaw);
        g_PBC_HistoryOwners.erase(snap.charGuidRaw);
        g_PBC_LastHistoryTime.erase(snap.charGuidRaw);
    }
    PBC_WsNotify(snap.charGuidRaw, "memory");
    snap.history.clear();

    PBC_Log(PBC_LogLevel::PBC_DEBUG, "CondenseInline: condensed character={} memories_extracted={}",
             snap.charName, memories.size());

    return true;
}

// ---------------------------------------------------------------------------
// QueueRelationshipUpdatesAfterCondensation
//
// After condensation succeeds, queue RelationshipUpdate events for all party
// members of the given character, using the pre-condensation history so the
// LLM has full context.
// ---------------------------------------------------------------------------
void QueueRelationshipUpdatesAfterCondensation(
    const PBC_CharacterSnapshot& snap,
    const std::deque<std::string>& preCondensationHistory)
{
    if (g_PBC_RelationshipUpdateSystemPrompt.empty() ||
        g_PBC_RelationshipUpdateUserPrompt.empty() ||
        snap.partyMemberNames.empty())
        return;

    for (const auto& memberName : snap.partyMemberNames)
    {
        std::string currentRel;
        {
            std::lock_guard<std::mutex> lk(g_PBC_RelationshipsMutex);
            auto botIt = g_PBC_Relationships.find(snap.charGuidRaw);
            if (botIt != g_PBC_Relationships.end())
            {
                auto tgtIt = botIt->second.find(memberName);
                if (tgtIt != botIt->second.end())
                    currentRel = tgtIt->second.text;
            }
        }
        if (currentRel.empty())
            currentRel = PBC_DefaultRelationshipText(memberName);

        // Use the pre-condensation history so the LLM has full context.
        PBC_CharacterSnapshot relSnap = snap;
        relSnap.history = preCondensationHistory;

        PBC_EventItem relEv;
        relEv.type                       = PBC_EventType::RelationshipUpdate;
        relEv.relationshipChar            = std::move(relSnap);
        relEv.relationshipTargetName     = memberName;
        relEv.relationshipTargetInfo     = PBC_BuildTargetInfo(memberName);
        relEv.relationshipCurrentText    = currentRel;
        relEv.relationshipSystemPrompt   = g_PBC_RelationshipUpdateSystemPrompt;
        relEv.relationshipUserPromptTmpl = g_PBC_RelationshipUpdateUserPrompt;

        PBC_PushEvent(std::move(relEv));

        PBC_Log(PBC_LogLevel::PBC_DEBUG,
                 "Condensation: queuing relationship update for character={} target={}",
                 snap.charName, memberName);
    }
}
