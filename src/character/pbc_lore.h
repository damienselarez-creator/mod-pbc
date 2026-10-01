#ifndef MOD_PBC_LORE_H
#define MOD_PBC_LORE_H

#include <cstdint>
#include <string>

// An immutable, fully validated corpus is published atomically. Empty path disables it.
// Failure retains the previous corpus AND its access policy. No filesystem I/O in search.
// Personal biography/psychology chunks require exactly one explicit character GUID.
bool PBC_LoadLore(std::string const& path, std::string const& guids, std::string const& allowedIds,
    uint32_t maxChunks, uint32_t maxBytes, std::string& status);
std::string PBC_GetLoreBlock(uint64_t guid, std::string const& event);
std::string PBC_LoreStatus();

#endif
