#ifndef PBC_QUEST_REACTIONS_H
#define PBC_QUEST_REACTIONS_H

#include "pbc_quest_reaction_policy.h"
class Quest;

void PBC_LoadQuestReactionConfig();
PBC_QuestReactionSpec PBC_GetQuestReaction(Quest const* quest, bool rewarded, uint32_t normalChance);

#endif
