#pragma once
#include <cstdint>

inline bool adventureManaged = false;

inline bool PBC_AdventureManaged(uint64_t)
{
    return adventureManaged;
}
