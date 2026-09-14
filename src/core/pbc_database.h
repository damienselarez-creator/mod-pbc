#ifndef MOD_PBC_DATABASE_H
#define MOD_PBC_DATABASE_H

#include <string>
#include <vector>
#include <cstdint>
#include <deque>

struct PBC_ParsedMemory;

// Requires a core which checks transaction boundaries and never replays a
// statement after reconnecting inside a transaction. Called off the world thread.
bool DB_CommitCondensation(uint64_t botGuid, const std::vector<PBC_ParsedMemory>& memories,
                           const std::deque<uint64_t>& sourceIds);

// ---------------------------------------------------------------------------
// Chat history — normalized schema (mod_pbc_history + mod_pbc_history_owners)
// ---------------------------------------------------------------------------

// Insert one message into mod_pbc_history and link it to one or more owners.
// Returns the new history_id (auto-increment).
uint64_t DB_InsertHistoryMessage(uint64_t authorGuid, uint8_t type,
                                 const std::string& message,
                                 const std::vector<uint64_t>& ownerGuids);

// Update the raw message text in mod_pbc_history (affects all owners).
void DB_UpdateHistoryMessage(uint64_t historyId, const std::string& newMessage);

// Remove one character's ownership of a message.
// If removeOrphaned is true and no owners remain, also delete the message.
void DB_RemoveHistoryOwnership(uint64_t guid, uint64_t historyId,
                               bool removeOrphaned = true);

// Remove all ownership rows for a character, then clean orphaned messages.
// Used by condensation and .chars reset.
void DB_RemoveAllHistoryOwnership(uint64_t guid);

// ---------------------------------------------------------------------------
// Character memories
// ---------------------------------------------------------------------------

// Insert a single memory for a character.
void DB_InsertMemory(uint64_t botGuid, const std::string& memoryText, uint8_t importance);

// Delete all memories for a character (used by .chars reset).
void DB_DeleteMemoriesForCharacter(uint64_t botGuid);

// Delete all memories for every character (used by .chars reset @ALL).
void DB_DeleteAllMemories();

// Update a single memory by DB row id.
void DB_UpdateMemoryById(uint64_t memoryId, const std::string& newText, uint8_t importance);

// Delete a single memory by DB row id.
void DB_DeleteMemoryById(uint64_t memoryId);

// ---------------------------------------------------------------------------
// Character data (roll chance modifier)
// ---------------------------------------------------------------------------

// Upsert the roll chance modifier for a character.
// modifier must be in range [-100, 100].
void DB_UpsertRollChanceModifier(uint64_t botGuid, int32_t modifier);

// ---------------------------------------------------------------------------
// Character relationships
// ---------------------------------------------------------------------------

// Upsert a relationship description for a character with a named target.
void DB_UpsertRelationship(uint64_t botGuid, const std::string& targetName,
                           const std::string& relationshipText);

// Delete all relationship rows for a character (used by .chars reset).
void DB_DeleteRelationshipsForCharacter(uint64_t botGuid);

// Delete all relationship rows for every character (used by .chars reset @ALL).
void DB_DeleteAllRelationships();

// Update the relationship text for a specific (bot, target) pair.
void DB_UpdateRelationshipText(uint64_t botGuid, const std::string& targetName,
                               const std::string& newText);

// Delete a single relationship row for a specific (bot, target) pair.
void DB_DeleteRelationship(uint64_t botGuid, const std::string& targetName);

// ---------------------------------------------------------------------------
// Migration helpers
// ---------------------------------------------------------------------------

// Check whether the memories table has any rows.
// Must be called after the DB is available (i.e. on or after OnStartup).
bool DB_MemoriesTableEmpty();

// Check whether the legacy card additions table exists and has any rows.
bool DB_CardAdditionsTableNotEmpty();

// ---------------------------------------------------------------------------
// DB Loader functions (implementations in pbc_database.cpp)
// ---------------------------------------------------------------------------

// Load all chat history from DB into g_PBC_History + g_PBC_HistoryOwners.
void PBC_LoadHistoryFromDB();

// Load all memories from DB into g_PBC_Memories.
void PBC_LoadMemoriesFromDB();

// Load all character data (roll chance modifiers) from DB into g_PBC_RollChanceModifiers.
void PBC_LoadCharacterDataFromDB();

// Load all relationships from DB into g_PBC_Relationships.
void PBC_LoadRelationshipsFromDB();

#endif // MOD_PBC_DATABASE_H
