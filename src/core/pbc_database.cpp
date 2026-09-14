#include "pbc_database.h"
#include "pbc_config.h"
#include "pbc_log.h"
#include "pbc_utils.h"

#include "DatabaseEnv.h"
#include "pbc_memory_parser.h"
#include "pbc_history_journal.h"
#include "Config.h"
#include <memory>
#include <mutex>

#include <string>
#include <vector>
#include <cstdint>
#include <ctime>
#include <exception>
#include <algorithm>

namespace
{
bool DB_ConfirmMutation(CharacterDatabaseTransaction& transaction)
{
    try
    {
        auto completion = CharacterDatabase.AsyncCommitTransaction(transaction);
        if (completion.m_future.get())
            return true;
    }
    catch (std::exception const&)
    {
        // A missing acknowledgement does not prove rollback.
    }
    PBC_Log(PBC_LogLevel::PBC_WARNING, "Memory mutation unconfirmed; cache retained");
    return false;
}
}

bool DB_DeleteHistoryMessage(uint64_t historyId)
{
    auto transaction = CharacterDatabase.BeginTransaction();
    transaction->Append("DELETE FROM mod_pbc_history_owners WHERE history_id = {}", historyId);
    transaction->Append("DELETE FROM mod_pbc_history WHERE id = {}", historyId);
    return DB_ConfirmMutation(transaction);
}

bool DB_ResetCharacterMemory(uint64_t botGuid, bool allCharacters)
{
    // Reload/recover first; never reset while older exchanges remain queued on disk.
    if (DB_HistoryRecoveryPending())
    {
        PBC_Log(PBC_LogLevel::PBC_WARNING, "Reset refused: recover the history journal with .chars reload first");
        return false;
    }
    auto transaction = CharacterDatabase.BeginTransaction();
    if (allCharacters)
    {
        transaction->Append("DELETE FROM mod_pbc_history_owners");
        transaction->Append("DELETE FROM mod_pbc_history");
        transaction->Append("DELETE FROM mod_pbc_memories");
        transaction->Append("DELETE FROM mod_pbc_relationships");
    }
    else
    {
        transaction->Append("DELETE FROM mod_pbc_history_owners WHERE guid = {}", botGuid);
        transaction->Append("DELETE FROM mod_pbc_history WHERE NOT EXISTS "
            "(SELECT 1 FROM mod_pbc_history_owners WHERE history_id = mod_pbc_history.id)");
        transaction->Append("DELETE FROM mod_pbc_memories WHERE bot_guid = {}", botGuid);
        transaction->Append("DELETE FROM mod_pbc_relationships WHERE bot_guid = {}", botGuid);
    }
    return DB_ConfirmMutation(transaction);
}

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
    catch (std::exception const&)
    {
        // Do not assume that an interrupted acknowledgement means rollback.
        return false;
    }
}

// ---------------------------------------------------------------------------
// Chat history — normalized schema (mod_pbc_history + mod_pbc_history_owners)
// ---------------------------------------------------------------------------

namespace
{
std::mutex journalMutex;
PBC_HistoryJournal& HistoryJournal()
{
    // Fixed for the process lifetime; changing the directory requires a restart.
    static PBC_HistoryJournal journal(sConfigMgr->GetOption<std::string>("PBC.HistoryJournalPath", "./pbc-journal"));
    return journal;
}

bool ReceiptSchemaAvailable()
{
    auto result = CharacterDatabase.Query("SELECT COUNT(*) FROM information_schema.tables "
        "WHERE table_schema = DATABASE() AND table_name = 'mod_pbc_history_receipts' AND engine = 'InnoDB'");
    if (result && (*result)[0].Get<uint64_t>() == 1)
        return true;
    PBC_Log(PBC_LogLevel::PBC_ERROR, "History receipt schema unavailable; apply the PBC receipt migration first");
    return false;
}

uint64_t CommitPendingHistory(PBC_PendingHistory const& record)
{
    if (!ReceiptSchemaAvailable())
        return 0;
    std::string escaped = record.message;
    CharacterDatabase.EscapeString(escaped);
    auto transaction = CharacterDatabase.BeginTransaction();
    transaction->Append("INSERT IGNORE INTO mod_pbc_history_receipts (token) VALUES ('{}')", record.token);
    transaction->Append("SET @pbc_journal_new = ROW_COUNT()");
    transaction->Append("INSERT INTO mod_pbc_history (timestamp, author_guid, type, message) "
        "SELECT FROM_UNIXTIME({}), {}, {}, '{}' WHERE @pbc_journal_new = 1",
        record.timestamp, record.author, static_cast<uint32_t>(record.type), escaped);
    transaction->Append("SET @pbc_journal_id = LAST_INSERT_ID()");
    for (auto owner : record.owners)
        transaction->Append("INSERT IGNORE INTO mod_pbc_history_owners (guid, history_id) "
            "SELECT {}, @pbc_journal_id WHERE @pbc_journal_new = 1", owner);
    transaction->Append("UPDATE mod_pbc_history_receipts SET history_id = @pbc_journal_id "
        "WHERE token = '{}' AND @pbc_journal_new = 1", record.token);
    if (!DB_ConfirmMutation(transaction))
        return 0;
    auto result = CharacterDatabase.Query("SELECT history_id FROM mod_pbc_history_receipts WHERE token = '{}'",
        record.token);
    return result ? (*result)[0].Get<uint64_t>() : 0;
}
}

bool DB_RecoverPendingHistory()
{
    std::lock_guard<std::mutex> lock(journalMutex);
    try
    {
        auto& journal = HistoryJournal();
        if (!ReceiptSchemaAvailable())
            return false;
        // Parse every record before replay: corruption does not silently skip exchanges.
        for (auto const& record : journal.Pending())
        {
            if (!CommitPendingHistory(record))
                return false;
            journal.Acknowledge(record);
        }
        return true;
    }
    catch (std::exception const& error)
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR, "History recovery stopped: {}", error.what());
        return false;
    }
}

bool DB_HistoryRecoveryPending()
{
    std::lock_guard<std::mutex> lock(journalMutex);
    try
    {
        return !HistoryJournal().Pending().empty();
    }
    catch (std::exception const&)
    {
        return true;
    }
}

uint64_t DB_InsertHistoryMessage(uint64_t authorGuid, uint8_t type,
    std::string const& message, std::vector<uint64_t> const& ownerGuids)
{
    if (ownerGuids.empty())
        return 0;
    std::lock_guard<std::mutex> lock(journalMutex);
    try
    {
        auto owners = ownerGuids;
        std::sort(owners.begin(), owners.end());
        owners.erase(std::unique(owners.begin(), owners.end()), owners.end());
        auto& journal = HistoryJournal();
        auto record = journal.Append(authorGuid, type, message, owners);
        if (journal.Pending().size() != 1)
        {
            PBC_Log(PBC_LogLevel::PBC_WARNING,
                "History exchange journaled; earlier writes await restart or .chars reload recovery");
            return 0;
        }
        auto id = CommitPendingHistory(record);
        if (id)
            journal.Acknowledge(record);
        return id;
    }
    catch (std::exception const& error)
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR, "History persistence unconfirmed: {}", error.what());
        return 0;
    }
}

bool DB_UpdateHistoryMessage(uint64_t historyId, const std::string& newMessage)
{
    auto transaction = CharacterDatabase.BeginTransaction();
    std::string escaped = newMessage;
    CharacterDatabase.EscapeString(escaped);
    transaction->Append(
        "UPDATE mod_pbc_history SET message = '{}' WHERE id = {}",
        escaped,
        historyId
    );
    return DB_ConfirmMutation(transaction);
}

bool DB_RemoveHistoryOwnership(uint64_t guid, uint64_t historyId,
                               bool removeOrphaned)
{
    auto transaction = CharacterDatabase.BeginTransaction();
    transaction->Append(
        "DELETE FROM mod_pbc_history_owners WHERE guid = {} AND history_id = {}",
        guid,
        historyId
    );

    if (removeOrphaned)
    {
        transaction->Append(
            "DELETE FROM mod_pbc_history "
            "WHERE id = {} AND NOT EXISTS ("
            "  SELECT 1 FROM mod_pbc_history_owners WHERE history_id = {}"
            ")",
            historyId,
            historyId
        );
    }
    return DB_ConfirmMutation(transaction);
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

bool DB_UpdateMemoryById(uint64_t memoryId, const std::string& newText, uint8_t importance)
{
    auto transaction = CharacterDatabase.BeginTransaction();
    std::string escaped = newText;
    CharacterDatabase.EscapeString(escaped);
    transaction->Append(
        "UPDATE mod_pbc_memories SET memory_text = '{}', importance = {} WHERE id = {}",
        escaped,
        static_cast<uint32_t>(importance),
        memoryId
    );
    return DB_ConfirmMutation(transaction);
}

bool DB_DeleteMemoryById(uint64_t memoryId)
{
    auto transaction = CharacterDatabase.BeginTransaction();
    transaction->Append(
        "DELETE FROM mod_pbc_memories WHERE id = {}",
        memoryId
    );
    return DB_ConfirmMutation(transaction);
}

// ---------------------------------------------------------------------------
// Character data (roll chance modifier)
// ---------------------------------------------------------------------------

bool DB_UpsertRollChanceModifier(uint64_t botGuid, int32_t modifier)
{
    auto transaction = CharacterDatabase.BeginTransaction();
    transaction->Append(
        "INSERT INTO mod_pbc_data (bot_guid, roll_chance_modifier) VALUES ({}, {}) "
        "ON DUPLICATE KEY UPDATE roll_chance_modifier = {}",
        botGuid,
        modifier,
        modifier
    );
    return DB_ConfirmMutation(transaction);
}

// ---------------------------------------------------------------------------
// Character relationships
// ---------------------------------------------------------------------------

bool DB_UpsertRelationship(uint64_t botGuid, const std::string& targetName,
                            const std::string& relationshipText)
{
    auto transaction = CharacterDatabase.BeginTransaction();
    std::string escapedName = targetName;
    CharacterDatabase.EscapeString(escapedName);
    std::string escapedText = relationshipText;
    CharacterDatabase.EscapeString(escapedText);
    transaction->Append(
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
    return DB_ConfirmMutation(transaction);
}

bool DB_UpdateRelationshipText(uint64_t botGuid, const std::string& targetName,
                                const std::string& newText)
{
    auto transaction = CharacterDatabase.BeginTransaction();
    std::string escapedName = targetName;
    CharacterDatabase.EscapeString(escapedName);
    std::string escapedText = newText;
    CharacterDatabase.EscapeString(escapedText);
    transaction->Append(
        "UPDATE mod_pbc_relationships SET relationship_text = '{}' "
        "WHERE bot_guid = {} AND target_name = '{}'",
        escapedText,
        botGuid,
        escapedName
    );
    return DB_ConfirmMutation(transaction);
}

bool DB_DeleteRelationship(uint64_t botGuid, const std::string& targetName)
{
    auto transaction = CharacterDatabase.BeginTransaction();
    std::string escapedName = targetName;
    CharacterDatabase.EscapeString(escapedName);
    transaction->Append(
        "DELETE FROM mod_pbc_relationships WHERE bot_guid = {} AND target_name = '{}'",
        botGuid,
        escapedName
    );
    return DB_ConfirmMutation(transaction);
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

bool PBC_ReloadMemoryCaches()
try
{
    std::scoped_lock lock(g_PBC_HistoryMutex, g_PBC_MemoriesMutex,
        g_PBC_RelationshipsMutex, g_PBC_DataMutex);
    if (!DB_RecoverPendingHistory())
        return false;
    // A sentinel guarantees a row even when every table is empty. A null result
    // therefore means failure. One InnoDB statement supplies a coherent snapshot.
    auto result = CharacterDatabase.Query(
        "SELECT 0 AS kind, CAST(0 AS UNSIGNED) AS id, CAST(0 AS UNSIGNED) AS owner, "
        "CAST(0 AS UNSIGNED) AS author, 0 AS stamp, 0 AS value, '' AS text, '' AS target "
        "UNION ALL SELECT 1,h.id,COALESCE(o.guid,0),h.author_guid,UNIX_TIMESTAMP(h.timestamp),h.type,h.message,'' "
        "FROM mod_pbc_history h LEFT JOIN mod_pbc_history_owners o ON o.history_id=h.id "
        "UNION ALL SELECT 2,id,bot_guid,0,UNIX_TIMESTAMP(created_at),importance,memory_text,'' FROM mod_pbc_memories "
        "UNION ALL SELECT 3,0,bot_guid,0,UNIX_TIMESTAMP(updated_at),0,relationship_text,target_name "
        "FROM mod_pbc_relationships "
        "UNION ALL SELECT 4,0,bot_guid,0,0,roll_chance_modifier,'','' FROM mod_pbc_data "
        "UNION ALL SELECT 5,o.history_id,o.guid,0,0,0,'','' FROM mod_pbc_history_owners o "
        "LEFT JOIN mod_pbc_history h ON h.id=o.history_id WHERE h.id IS NULL "
        "ORDER BY kind,id,owner");
    if (!result)
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR, "Memory reload failed; all caches retained");
        return false;
    }
    decltype(g_PBC_History) history;
    decltype(g_PBC_HistoryOwners) owners;
    decltype(g_PBC_LastHistoryTime) lastTime;
    decltype(g_PBC_Memories) memories;
    decltype(g_PBC_Relationships) relationships;
    decltype(g_PBC_RollChanceModifiers) modifiers;
    do
    {
        auto* row = result->Fetch();
        auto kind = row[0].Get<uint32_t>();
        auto id = row[1].Get<uint64_t>();
        auto owner = row[2].Get<uint64_t>();
        auto stamp = static_cast<time_t>(row[4].Get<uint64_t>());
        if (kind == 1)
        {
            PBC_HistoryEntry entry;
            entry.id = id;
            entry.authorGuid = row[3].Get<uint64_t>();
            entry.timestamp = stamp;
            entry.type = static_cast<uint8_t>(row[5].Get<uint32_t>());
            entry.message = row[6].Get<std::string>();
            history[id] = std::move(entry);
            if (owner)
            {
                owners[owner].push_back(id);
                lastTime[owner] = std::max(lastTime[owner], stamp);
            }
        }
        else if (kind == 2)
        {
            PBC_MemoryEntry entry;
            entry.dbId = id;
            entry.text = row[6].Get<std::string>();
            entry.importance = static_cast<uint8_t>(row[5].Get<uint32_t>());
            entry.createdAt = PBC_FormatDate(stamp);
            memories[owner].push_back(std::move(entry));
        }
        else if (kind == 3)
        {
            auto& entry = relationships[owner][row[7].Get<std::string>()];
            entry.text = row[6].Get<std::string>();
            entry.updatedAt = PBC_FormatDateTime(stamp);
        }
        else if (kind == 4)
        {
            auto modifier = row[5].Get<int32_t>();
            if (modifier)
                modifiers[owner] = modifier;
        }
        else if (kind != 0)
        {
            PBC_Log(PBC_LogLevel::PBC_ERROR, "Memory reload found orphan ownership; all caches retained");
            return false;
        }
    } while (result->NextRow());
    g_PBC_History.swap(history);
    g_PBC_HistoryOwners.swap(owners);
    g_PBC_LastHistoryTime.swap(lastTime);
    g_PBC_Memories.swap(memories);
    g_PBC_Relationships.swap(relationships);
    g_PBC_RollChanceModifiers.swap(modifiers);
    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Memory caches reloaded from a complete database snapshot");
    return true;
}

catch (std::exception const& error)
{
    PBC_Log(PBC_LogLevel::PBC_ERROR, "Memory reload failed; caches retained: {}", error.what());
    return false;
}

bool PBC_LoadHistoryFromDB() { return PBC_ReloadMemoryCaches(); }
bool PBC_LoadMemoriesFromDB() { return PBC_ReloadMemoryCaches(); }
bool PBC_LoadCharacterDataFromDB() { return PBC_ReloadMemoryCaches(); }
bool PBC_LoadRelationshipsFromDB() { return PBC_ReloadMemoryCaches(); }
