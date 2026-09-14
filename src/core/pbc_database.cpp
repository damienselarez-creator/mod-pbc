#include "pbc_database.h"
#include "pbc_config.h"
#include "pbc_log.h"
#include "pbc_utils.h"

#include "DatabaseEnv.h"
#include "pbc_memory_parser.h"

#include <string>
#include <vector>
#include <cstdint>
#include <ctime>
#include <exception>
#include <algorithm>

bool DB_CommitCondensation(uint64_t botGuid, const std::vector<PBC_ParsedMemory>& memories,
                           const std::deque<uint64_t>& sourceIds)
{
    if (memories.empty() || sourceIds.empty())
        return false;

    auto transaction = CharacterDatabase.BeginTransaction();
    for (const auto& memory : memories)
    {
        std::string escaped = memory.text;
        CharacterDatabase.EscapeString(escaped);
        transaction->Append(
            "INSERT INTO mod_pbc_memories (bot_guid, memory_text, importance) VALUES ({}, '{}', {})",
            botGuid, escaped, static_cast<uint32_t>(memory.importance));
    }
    for (uint64_t id : sourceIds)
    {
        transaction->Append("DELETE FROM mod_pbc_history_owners WHERE guid = {} AND history_id = {}",
                            botGuid, id);
        transaction->Append(
            "DELETE FROM mod_pbc_history WHERE id = {} AND NOT EXISTS "
            "(SELECT 1 FROM mod_pbc_history_owners WHERE history_id = {})", id, id);
    }

    try
    {
        auto completion = CharacterDatabase.AsyncCommitTransaction(transaction);
        return completion.m_future.get();
    }
    catch (const std::exception&)
    {
        // Do not assume that an interrupted acknowledgement means rollback.
        return false;
    }
}

// ---------------------------------------------------------------------------
// Chat history — normalized schema (mod_pbc_history + mod_pbc_history_owners)
// ---------------------------------------------------------------------------

uint64_t DB_InsertHistoryMessage(uint64_t authorGuid, uint8_t type,
                                 const std::string& message,
                                 const std::vector<uint64_t>& ownerGuids)
{
    if (ownerGuids.empty())
        return 0;

    std::string escaped = message;
    CharacterDatabase.EscapeString(escaped);

    std::vector<uint64_t> owners = ownerGuids;
    std::sort(owners.begin(), owners.end());
    owners.erase(std::unique(owners.begin(), owners.end()), owners.end());

    auto transaction = CharacterDatabase.BeginTransaction();
    transaction->Append(
        "INSERT INTO mod_pbc_history (author_guid, type, message) VALUES ({}, {}, '{}')",
        authorGuid,
        static_cast<uint32_t>(type),
        escaped);

    // LAST_INSERT_ID is evaluated by the same connection inside the transaction.
    // The ownership table has no auto-increment column. The core captures the
    // generated message id immediately after the FIRST statement, before these
    // ownership inserts can overwrite the client library's insert-id metadata.
    for (uint64_t ownerGuid : owners)
    {
        transaction->Append(
            "INSERT INTO mod_pbc_history_owners (guid, history_id) VALUES ({}, LAST_INSERT_ID())", ownerGuid);
    }

    uint64_t historyId = CharacterDatabase.DirectCommitTransactionWithInsertId(transaction);
    if (!historyId)
        PBC_Log(PBC_LogLevel::PBC_WARNING,
            "History insert unconfirmed: no id published to the cache; no automatic retry");
    return historyId;
}

void DB_UpdateHistoryMessage(uint64_t historyId, const std::string& newMessage)
{
    std::string escaped = newMessage;
    CharacterDatabase.EscapeString(escaped);
    CharacterDatabase.Execute(
        "UPDATE mod_pbc_history SET message = '{}' WHERE id = {}",
        escaped,
        historyId
    );
}

void DB_RemoveHistoryOwnership(uint64_t guid, uint64_t historyId,
                               bool removeOrphaned)
{
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_history_owners WHERE guid = {} AND history_id = {}",
        guid,
        historyId
    );

    if (removeOrphaned)
    {
        CharacterDatabase.Execute(
            "DELETE FROM mod_pbc_history "
            "WHERE id = {} AND NOT EXISTS ("
            "  SELECT 1 FROM mod_pbc_history_owners WHERE history_id = {}"
            ")",
            historyId,
            historyId
        );
    }
}

void DB_RemoveAllHistoryOwnership(uint64_t guid)
{
    // Delete all ownership rows for this character
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_history_owners WHERE guid = {}",
        guid
    );

    // Clean orphaned messages (those with zero remaining owners)
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_history "
        "WHERE NOT EXISTS ("
        "  SELECT 1 FROM mod_pbc_history_owners WHERE history_id = mod_pbc_history.id"
        ")"
    );
}

// ---------------------------------------------------------------------------
// Character memories
// ---------------------------------------------------------------------------

void DB_InsertMemory(uint64_t botGuid, const std::string& memoryText, uint8_t importance)
{
    std::string escaped = memoryText;
    CharacterDatabase.EscapeString(escaped);
    CharacterDatabase.DirectExecute(
        "INSERT INTO mod_pbc_memories (bot_guid, memory_text, importance) VALUES ({}, '{}', {})",
        botGuid,
        escaped,
        importance
    );
}

void DB_DeleteMemoriesForCharacter(uint64_t botGuid)
{
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_memories WHERE bot_guid = {}",
        botGuid
    );
}

void DB_DeleteAllMemories()
{
    CharacterDatabase.Execute("DELETE FROM mod_pbc_memories");
}

void DB_UpdateMemoryById(uint64_t memoryId, const std::string& newText, uint8_t importance)
{
    std::string escaped = newText;
    CharacterDatabase.EscapeString(escaped);
    CharacterDatabase.Execute(
        "UPDATE mod_pbc_memories SET memory_text = '{}', importance = {} WHERE id = {}",
        escaped,
        static_cast<uint32_t>(importance),
        memoryId
    );
}

void DB_DeleteMemoryById(uint64_t memoryId)
{
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_memories WHERE id = {}",
        memoryId
    );
}

// ---------------------------------------------------------------------------
// Character data (roll chance modifier)
// ---------------------------------------------------------------------------

void DB_UpsertRollChanceModifier(uint64_t botGuid, int32_t modifier)
{
    CharacterDatabase.Execute(
        "INSERT INTO mod_pbc_data (bot_guid, roll_chance_modifier) VALUES ({}, {}) "
        "ON DUPLICATE KEY UPDATE roll_chance_modifier = {}",
        botGuid,
        modifier,
        modifier
    );
}

// ---------------------------------------------------------------------------
// Character relationships
// ---------------------------------------------------------------------------

void DB_UpsertRelationship(uint64_t botGuid, const std::string& targetName,
                            const std::string& relationshipText)
{
    std::string escapedName = targetName;
    CharacterDatabase.EscapeString(escapedName);
    std::string escapedText = relationshipText;
    CharacterDatabase.EscapeString(escapedText);
    CharacterDatabase.Execute(
        "INSERT INTO mod_pbc_relationships "
        "  (bot_guid, target_name, relationship_text) "
        "VALUES ({}, '{}', '{}') "
        "ON DUPLICATE KEY UPDATE "
        "  relationship_text = '{}', "
        "  updated_at = CURRENT_TIMESTAMP",
        botGuid,
        escapedName,
        escapedText,
        escapedText
    );
}

void DB_DeleteRelationshipsForCharacter(uint64_t botGuid)
{
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_relationships WHERE bot_guid = {}",
        botGuid
    );
}

void DB_DeleteAllRelationships()
{
    CharacterDatabase.Execute("DELETE FROM mod_pbc_relationships");
}

void DB_UpdateRelationshipText(uint64_t botGuid, const std::string& targetName,
                                const std::string& newText)
{
    std::string escapedName = targetName;
    CharacterDatabase.EscapeString(escapedName);
    std::string escapedText = newText;
    CharacterDatabase.EscapeString(escapedText);
    CharacterDatabase.Execute(
        "UPDATE mod_pbc_relationships SET relationship_text = '{}' "
        "WHERE bot_guid = {} AND target_name = '{}'",
        escapedText,
        botGuid,
        escapedName
    );
}

void DB_DeleteRelationship(uint64_t botGuid, const std::string& targetName)
{
    std::string escapedName = targetName;
    CharacterDatabase.EscapeString(escapedName);
    CharacterDatabase.Execute(
        "DELETE FROM mod_pbc_relationships WHERE bot_guid = {} AND target_name = '{}'",
        botGuid,
        escapedName
    );
}

// ---------------------------------------------------------------------------
// Migration helpers
// ---------------------------------------------------------------------------

bool DB_MemoriesTableEmpty()
{
    QueryResult result = CharacterDatabase.Query("SELECT 1 FROM mod_pbc_memories LIMIT 1");
    return !result;
}

bool DB_CardAdditionsTableNotEmpty()
{
    QueryResult tableCheck = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM information_schema.tables "
        "WHERE table_schema = DATABASE() AND table_name = 'mod_pbc_character_card_additions'"
    );
    if (!tableCheck || (*tableCheck)[0].Get<uint64_t>() == 0)
        return false;

    QueryResult result = CharacterDatabase.Query("SELECT 1 FROM mod_pbc_character_card_additions LIMIT 1");
    return !!result;
}

// ---------------------------------------------------------------------------
// DB Loader functions (moved from pbc_config.cpp)
// ---------------------------------------------------------------------------

void PBC_LoadHistoryFromDB()
{
    std::lock_guard<std::mutex> lock(g_PBC_HistoryMutex);
    g_PBC_History.clear();
    g_PBC_HistoryOwners.clear();
    g_PBC_LastHistoryTime.clear();

    // 1. Load all messages from mod_pbc_history
    QueryResult msgResult = CharacterDatabase.Query(
        "SELECT id, UNIX_TIMESTAMP(timestamp), author_guid, type, message "
        "FROM mod_pbc_history ORDER BY id ASC");

    if (msgResult)
    {
        do {
            PBC_HistoryEntry entry;
            entry.id         = (*msgResult)[0].Get<uint64_t>();
            entry.timestamp  = static_cast<time_t>((*msgResult)[1].Get<uint64_t>());
            entry.authorGuid = (*msgResult)[2].Get<uint64_t>();
            entry.type       = static_cast<uint8_t>((*msgResult)[3].Get<uint32_t>());
            entry.message    = (*msgResult)[4].Get<std::string>();
            g_PBC_History[entry.id] = std::move(entry);
        } while (msgResult->NextRow());
    }

    // 2. Load ownership (ordered by history_id = chronological)
    QueryResult ownResult = CharacterDatabase.Query(
        "SELECT guid, history_id FROM mod_pbc_history_owners ORDER BY history_id ASC");

    if (ownResult)
    {
        do {
            uint64_t guid      = (*ownResult)[0].Get<uint64_t>();
            uint64_t historyId = (*ownResult)[1].Get<uint64_t>();
            g_PBC_HistoryOwners[guid].push_back(historyId);

            // Track last timestamp per character
            auto hit = g_PBC_History.find(historyId);
            if (hit != g_PBC_History.end() && hit->second.timestamp > 0)
                g_PBC_LastHistoryTime[guid] = hit->second.timestamp;
        } while (ownResult->NextRow());
    }

    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Chat history loaded from DB ({} messages, {} characters).",
             g_PBC_History.size(), g_PBC_LastHistoryTime.size());
}

void PBC_LoadMemoriesFromDB()
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT id, bot_guid, memory_text, importance, UNIX_TIMESTAMP(created_at) FROM mod_pbc_memories ORDER BY bot_guid ASC, id ASC"
    );

    std::lock_guard<std::mutex> lock(g_PBC_MemoriesMutex);
    g_PBC_Memories.clear();

    if (!result) return;

    size_t count = 0;
    do {
        uint64_t    dbId       = (*result)[0].Get<uint64_t>();
        uint64_t    botGuid    = (*result)[1].Get<uint64_t>();
        std::string memText    = (*result)[2].Get<std::string>();
        uint8_t     importance = static_cast<uint8_t>((*result)[3].Get<uint32_t>());
        time_t      createdAt  = static_cast<time_t>((*result)[4].Get<uint64_t>());

        PBC_MemoryEntry entry;
        entry.dbId       = dbId;
        entry.text       = std::move(memText);
        entry.importance = importance;
        entry.createdAt  = PBC_FormatDate(createdAt);
        g_PBC_Memories[botGuid].push_back(std::move(entry));
        ++count;
    } while (result->NextRow());

    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Character memories loaded from DB ({} entries).", count);
}

void PBC_LoadCharacterDataFromDB()
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT bot_guid, roll_chance_modifier FROM mod_pbc_data"
    );

    std::lock_guard<std::mutex> lock(g_PBC_DataMutex);
    g_PBC_RollChanceModifiers.clear();

    if (!result)
    {
        PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Characters data loaded from DB (0 entries).");
        return;
    }

    size_t count = 0;
    do {
        uint64_t botGuid = (*result)[0].Get<uint64_t>();
        int32_t  rollMod = (*result)[1].Get<int32_t>();
        if (rollMod != 0)
            g_PBC_RollChanceModifiers[botGuid] = rollMod;
        ++count;
    } while (result->NextRow());

    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Characters data loaded from DB ({} entries, {} with roll modifier).",
             count, g_PBC_RollChanceModifiers.size());
}

void PBC_LoadRelationshipsFromDB()
{
    QueryResult result = CharacterDatabase.Query(
        "SELECT bot_guid, target_name, relationship_text, "
        "UNIX_TIMESTAMP(updated_at) FROM mod_pbc_relationships"
    );

    std::lock_guard<std::mutex> lock(g_PBC_RelationshipsMutex);
    g_PBC_Relationships.clear();

    if (!result)
    {
        PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Relationships loaded from DB (0 entries).");
        return;
    }

    size_t count = 0;
    do {
        uint64_t    botGuid    = (*result)[0].Get<uint64_t>();
        std::string targetName = (*result)[1].Get<std::string>();
        std::string relText    = (*result)[2].Get<std::string>();
        time_t      updatedAt  = static_cast<time_t>((*result)[3].Get<uint64_t>());

        auto& entry = g_PBC_Relationships[botGuid][targetName];
        entry.text      = std::move(relText);
        entry.updatedAt = PBC_FormatDateTime(updatedAt);
        ++count;
    } while (result->NextRow());

    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Relationships loaded from DB ({} entries).", count);
}
