#pragma once
#include "test_environment.h"
#include "pbc_memory_parser.h"

inline bool commitSucceeds = true;
inline int commitCalls = 0;
inline bool DB_CommitCondensation(uint64_t, const std::vector<PBC_ParsedMemory>& memories,
                                  const std::deque<uint64_t>& ids)
{
    ++commitCalls;
    if (!commitSucceeds)
        return false;
    writes += static_cast<int>(memories.size());
    deletedIds.assign(ids.begin(), ids.end());
    return true;
}
