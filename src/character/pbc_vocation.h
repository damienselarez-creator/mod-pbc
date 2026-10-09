#ifndef PBC_VOCATION_H
#define PBC_VOCATION_H
#include <string>
#include <vector>
class Player;
bool PBC_VocationChat(Player* player, Player* bot, std::string const& message);
bool PBC_VocationConversation(Player* player, std::vector<Player*> const& bots, std::string const& message);
std::string PBC_VocationContext(Player* bot);
void AddPBCVocationScripts();
#endif
