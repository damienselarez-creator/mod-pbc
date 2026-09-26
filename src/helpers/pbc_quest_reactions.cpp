#include "pbc_quest_reactions.h"
#include "pbc_log.h"
#include "Config.h"
#include "CreatureData.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "SharedDefines.h"

namespace
{
PBC_QuestReactionRates rates;
}

void PBC_LoadQuestReactionConfig()
{
    rates.eliteAccepted = std::min(100u,
        sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceQuestTakenElite", 50));
    rates.eliteRewarded = std::min(100u,
        sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceQuestCompletedElite", 60));
    rates.raidAccepted = std::min(100u,
        sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceQuestTakenRaid", 75));
    rates.raidRewarded = std::min(100u,
        sConfigMgr->GetOption<uint32_t>("PBC.ReplyChanceQuestCompletedRaid", 80));
    PBC_Log(PBC_LogLevel::PBC_DEFAULT,
        "Quest reactions: elite/dungeon accept={}% reward={}%; raid/world-boss accept={}% reward={}%; fixed rolls",
        rates.eliteAccepted, rates.eliteRewarded, rates.raidAccepted, rates.raidRewarded);
}

PBC_QuestReactionSpec PBC_GetQuestReaction(Quest const* quest, bool rewarded, uint32_t normalChance)
{
    bool worldBoss = false;
    for (size_t i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
    {
        auto entry = quest->RequiredNpcOrGo[i];
        if (entry <= 0 || !quest->RequiredNpcOrGoCount[i])
            continue;
        auto creature = sObjectMgr->GetCreatureTemplate(static_cast<uint32>(entry));
        if (creature && creature->rank == CREATURE_ELITE_WORLDBOSS
            && !(creature->flags_extra & CREATURE_FLAG_EXTRA_DUNGEON_BOSS))
            worldBoss = true;
    }
    auto type = quest->GetType();
    bool raid = type == QUEST_TYPE_RAID || type == QUEST_TYPE_RAID_10 || type == QUEST_TYPE_RAID_25;
    auto tier = PBC_ClassifyQuestReaction(raid, worldBoss, type == QUEST_TYPE_ELITE, type == QUEST_TYPE_DUNGEON);
    return PBC_SelectQuestReaction(tier, rewarded, normalChance, rates);
}
