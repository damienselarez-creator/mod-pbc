#ifndef PBC_ADVENTURE_H
#define PBC_ADVENTURE_H
#include <cstdint>
#include <string>
#include <vector>
class Player;

void AddPBCAdventureScripts();
void PBC_AdventureEncounter(Player* player, uint32_t questId, std::string const& name, std::string const& type);
bool PBC_AdventureManaged(uint64_t guid);
void PBC_AdventureHistory(uint64_t author, uint8_t type, std::string const& message,
    std::vector<uint64_t> const& owners);
std::string PBC_AdventureContext(uint64_t bot, uint64_t whisperTarget,
    std::vector<uint64_t> const& groupPlayers, std::string const& query);
#endif
