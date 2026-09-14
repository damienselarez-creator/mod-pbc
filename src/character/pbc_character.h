#ifndef MOD_PBC_CHARACTER_H
#define MOD_PBC_CHARACTER_H

#include <cstdint>
#include <string>
#include <unordered_map>
#include "Player.h"
#include "pbc_config.h"
#include "pbc_scene_helpers.h"
#include "pbc_equipment_helpers.h"

// ---------------------------------------------------------------------------
// Variable substitution types
// ---------------------------------------------------------------------------

// Key-value map for template variable substitution.
using PBC_VarMap = std::unordered_map<std::string, std::string>;

// Core substitution engine: iterates over the map and replaces {key} with
// value in tmpl (in-place).  When annotate=true, uses {key}value format so
// variable boundaries are visible (used only by the API context endpoint).
void PBC_SubstituteFromMap(std::string& tmpl, const PBC_VarMap& vars, bool annotate = false);

// Build a var map from a live Player*.  Main-thread only (accesses game data).
PBC_VarMap PBC_BuildVarMap(Player* bot, const std::string& event = "");

// Build a var map from a PBC_CharacterSnapshot.  Thread-safe.
PBC_VarMap PBC_BuildVarMapFromSnapshot(const PBC_CharacterSnapshot& snap, const std::string& event = "");

// ---------------------------------------------------------------------------
// Snapshot builder (main-thread only)
//
// Captures all data needed from a live Player* into a PBC_CharacterSnapshot.
// Must be called on the main thread.  The resulting struct is safe to pass
// to the event thread without further access to game objects.
// ---------------------------------------------------------------------------
PBC_CharacterSnapshot PBC_SnapshotCharacter(Player* bot);

// ---------------------------------------------------------------------------
// Prompt builder (thread-safe, uses snapshot only)
//
// Builds the fully-substituted user prompt string for a character using only data
// stored in its snapshot (including its local history copy).  Safe to call
// from any thread.
// ---------------------------------------------------------------------------
std::string PBC_BuildUserPromptFromSnapshot(const PBC_CharacterSnapshot& snap,
                                             const std::string& eventLine);

// ---------------------------------------------------------------------------
// Condensation prompt builder (thread-safe, uses snapshot only)
// ---------------------------------------------------------------------------
std::string PBC_BuildCondensationPromptFromSnapshot(const PBC_CharacterSnapshot& snap,
                                                     const std::string& tmpl);

// ---------------------------------------------------------------------------
// Global history access (thread-safe)
// ---------------------------------------------------------------------------

std::string PBC_GetChatHistory(uint64_t botGuid);
std::deque<std::string> PBC_GetChatHistoryPreRendered(uint64_t botGuid);

// Append a structured message to the history of all ownerGuids.
// Deduplicates against each owner's last message. Returns the new history_id (0 if deduped).
uint64_t PBC_AppendHistoryMessage(uint64_t authorGuid, uint8_t type,
                                  const std::string& message,
                                  const std::vector<uint64_t>& ownerGuids);

int PBC_EstimateHistoryTokens(uint64_t botGuid);

// ---------------------------------------------------------------------------
// Insert a "Narrator: *some time passes*" line into the character's history
// if more than 5 minutes have elapsed since the last entry (and the last
// entry is not already a time-passes line or part of a whisper chain).
// Updates g_PBC_LastHistoryTime, writes to DB, sends WS notification.
// Returns true if a line was inserted.
// Thread-safe.
// ---------------------------------------------------------------------------
bool PBC_MaybeInsertTimeGap(uint64_t botGuid, bool incomingIsWhisper = false);

// ---------------------------------------------------------------------------
// Batch version of PBC_MaybeInsertTimeGap for use inside PBC_ProcessEventItem.
// Collects all botGuids that need a time-gap line, then creates ONE shared
// "Narrator: *some time passes*" entry owned by all of them (instead of
// creating one duplicate DB row per character).  Returns the set of GUIDs
// for which a time-gap was actually inserted, so the caller can push the
// rendered line into each snapshot's history.
// Thread-safe.
// ---------------------------------------------------------------------------
std::unordered_set<uint64_t> PBC_MaybeInsertSharedTimeGap(
    const std::vector<uint64_t>& botGuids, bool incomingIsWhisper = false);

// ---------------------------------------------------------------------------
// Mutation result (thread-safe, also updates the database)
// ---------------------------------------------------------------------------
enum class PBC_HistoryResult { Ok, NotFound, Desync, PersistenceFailed };

PBC_HistoryResult PBC_StoreGeneratedRelationship(uint64_t botGuid, std::string const& targetName,
    std::string const& newText, std::string const& originalText, uint64_t generation);

PBC_HistoryResult PBC_ResetCharacterMemory(uint64_t botGuid, bool allCharacters = false);

// ---------------------------------------------------------------------------
// History mutation (thread-safe, also updates the database)
//
// Uses real mod_pbc_history.id — no index conversion, no original comparison.
// Editing affects ALL characters who own the shared message.
// ---------------------------------------------------------------------------
PBC_HistoryResult PBC_UpdateHistoryMessage(uint64_t historyId,
                                           const std::string& newMessage);

// Hard delete: removes the message from mod_pbc_history AND all ownership rows.
// Affects every character who shared this message.
PBC_HistoryResult PBC_DeleteHistoryMessage(uint64_t historyId);

// Soft unlink: removes one character's ownership only. If the message becomes
// orphaned (zero owners), it is cleaned up from mod_pbc_history.
PBC_HistoryResult PBC_RemoveHistoryOwnership(uint64_t guid, uint64_t historyId);

// Render a single history entry from a specific character's perspective.
// "You" for own messages, correctly identifies whisper targets.
std::string PBC_RenderHistoryLine(const PBC_HistoryEntry& entry, uint64_t forGuid);

// Thread-safe character name lookup. Uses CharacterCache first, falls back to DB.
std::string PBC_GetCharacterName(uint64_t guid);

// ---------------------------------------------------------------------------
// Build the memories block for a character's prompt (thread-safe).
// Returns a multi-line string (one memory per line) to be substituted
// into {memories} in the user prompt template. Memories are selected
// by importance DESC (most important first) until the token budget
// (g_PBC_MaxMemoriesCtx) is exceeded. The selected memories are then
// output in chronological order (by DB id ASC) so the narrative flows
// naturally — a lower-importance early event appears before a later
// high-importance one.
// ---------------------------------------------------------------------------
std::string PBC_GetMemoriesBlock(uint64_t botGuid);

// ---------------------------------------------------------------------------
// Relationship mutation (thread-safe, also updates the database)
//
// targetName identifies the relationship entry (the key in the map).
// originalText: the current relationship text is compared against this value
// before applying the mutation.  If they differ, Desync is returned and no
// modification is made.
// ---------------------------------------------------------------------------
PBC_HistoryResult PBC_UpdateRelationship(uint64_t botGuid, const std::string& targetName,
                                          const std::string& newText,
                                          const std::string& originalText);
PBC_HistoryResult PBC_DeleteRelationship(uint64_t botGuid, const std::string& targetName,
                                          const std::string& originalText);

// ---------------------------------------------------------------------------
// Memory mutation (thread-safe, also updates the database)
//
// memoryId is the DB row id (the "id" field returned by GET /api/char/:guid/memory).
// originalText: the current memory text is compared against this value before
// applying the mutation.  If they differ, Desync is returned and no
// modification is made.
// ---------------------------------------------------------------------------
PBC_HistoryResult PBC_UpdateMemory(uint64_t botGuid, uint64_t memoryId,
                                    const std::string& newText,
                                    uint8_t newImportance,
                                    const std::string& originalText);
PBC_HistoryResult PBC_DeleteMemory(uint64_t botGuid, uint64_t memoryId,
                                    const std::string& originalText);

// ---------------------------------------------------------------------------
// Character card / context (main-thread only)
// ---------------------------------------------------------------------------
std::string PBC_GetCharacterCard(Player* bot);
std::string PBC_GetCharacterContext(Player* bot);

// ---------------------------------------------------------------------------
// Full variable substitution (main-thread only)
//
// expandComposites=false skips {character_card}, {chat_history}, {context}
// {memories}, {relationships} to prevent infinite recursion when called from
// card/context builders.
//
// annotate=true replaces {key} with {key}value instead of just value,
// leaving the variable name visible in the output so the frontend can
// identify which variable produced each part.  Used only by the API
// context endpoint — LLM requests always use annotate=false.
// ---------------------------------------------------------------------------
std::string PBC_SubstituteVars(const std::string& tmpl, Player* bot,
                                const std::string& event = "",
                                bool expandComposites = true,
                                bool annotate = false);

// ---------------------------------------------------------------------------
// Push a Condensation event for a character onto the global event queue.
// May be called from the main thread (commands).
// ---------------------------------------------------------------------------
void PBC_TriggerCondensation(Player* bot);

// ---------------------------------------------------------------------------
// Build the relationships block for a character's prompt (thread-safe).
// Returns a multi-line string to be substituted into {relationships} in the
// user prompt template.  Always emits current party members and the whisper
// target (with default text if no stored relationship).  Additionally emits
// ALL other stored relationships that have non-default text, ensuring the
// character remembers everyone they've formed a meaningful bond with.
// ---------------------------------------------------------------------------
std::string PBC_GetRelationshipsBlock(const PBC_CharacterSnapshot& snap);

// ---------------------------------------------------------------------------
// Build a target info string for a named player, e.g. "DESEVEN, MALE TAUREN SHAMAN".
// Looks up the player live (safe for read-only access from any thread).
// Falls back to just the uppercased name if the player cannot be found.
// ---------------------------------------------------------------------------
std::string PBC_BuildTargetInfo(const std::string& name);

#endif // MOD_PBC_CHARACTER_H
