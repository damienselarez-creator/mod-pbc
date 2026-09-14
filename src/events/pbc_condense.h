#ifndef MOD_PBC_CONDENSE_H
#define MOD_PBC_CONDENSE_H

#include <cstdint>
#include <string>
#include <deque>

struct PBC_CharacterSnapshot;

// ---------------------------------------------------------------------------
// Runs condensation synchronously inside the event thread.
// Returns true if condensation succeeded, false otherwise.
// ---------------------------------------------------------------------------
bool PBC_CondenseInline(PBC_CharacterSnapshot& snap,
                        const std::string& sysPrompt,
                        const std::string& userPromptTmpl);

// ---------------------------------------------------------------------------
// Validate the entire [N] text batch before inserting any memories.
// Returns zero without writes for empty or malformed responses.
// ---------------------------------------------------------------------------
int PBC_ParseMemoryLines(const std::string& text, uint64_t botGuid);

// ---------------------------------------------------------------------------
// After condensation succeeds, queue RelationshipUpdate events for all party
// members of the given character, using the pre-condensation history so the
// LLM has full context.
// ---------------------------------------------------------------------------
void QueueRelationshipUpdatesAfterCondensation(
    const PBC_CharacterSnapshot& snap,
    const std::deque<std::string>& preCondensationHistory);

#endif // MOD_PBC_CONDENSE_H
