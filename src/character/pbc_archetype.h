#ifndef PBC_ARCHETYPE_H
#define PBC_ARCHETYPE_H

#include <cstdint>
#include <string>

bool PBC_LoadArchetypes(std::string const& path, std::string& status);
std::string PBC_ArchetypeCard(uint8_t race, uint8_t characterClass, int specialization, uint64_t identity);
std::string PBC_ArchetypeFocus(uint8_t race, uint8_t characterClass, int specialization);
std::string PBC_ArchetypeKnowledge(uint8_t race, uint8_t characterClass, int specialization,
    std::string const& event, std::string const& alreadySelected = "");

bool PBC_UsesCollectiveIdentity(uint8_t race);
std::string PBC_ArchetypeSelection(uint8_t race, uint8_t cls, int spec, std::string const& event);

#endif
