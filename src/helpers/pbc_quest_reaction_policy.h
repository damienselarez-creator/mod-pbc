#ifndef PBC_QUEST_REACTION_POLICY_H
#define PBC_QUEST_REACTION_POLICY_H

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

enum class PBC_QuestReactionTier { Normal, Elite, Raid };

struct PBC_QuestReactionRates
{
    uint32_t eliteAccepted = 50;
    uint32_t eliteRewarded = 60;
    uint32_t raidAccepted = 75;
    uint32_t raidRewarded = 80;
};

struct PBC_QuestReactionSpec
{
    uint32_t chance;
    bool fixedChance;
    std::string instruction;
};

inline PBC_QuestReactionTier PBC_ClassifyQuestReaction(bool raid, bool worldBoss, bool elite, bool dungeon)
{
    if (raid || (worldBoss && !dungeon))
        return PBC_QuestReactionTier::Raid;
    if (elite || dungeon)
        return PBC_QuestReactionTier::Elite;
    return PBC_QuestReactionTier::Normal;
}

inline PBC_QuestReactionSpec PBC_SelectQuestReaction(PBC_QuestReactionTier tier, bool rewarded,
    uint32_t normalChance, PBC_QuestReactionRates const& rates = {})
{
    uint32_t chance = normalChance;
    std::string length = "1 a 2 phrases, environ 15 a 35 mots";
    if (tier == PBC_QuestReactionTier::Elite)
    {
        chance = rewarded ? rates.eliteRewarded : rates.eliteAccepted;
        length = "2 a 3 phrases developpees, environ 35 a 65 mots";
    }
    else if (tier == PBC_QuestReactionTier::Raid)
    {
        chance = rewarded ? rates.raidRewarded : rates.raidAccepted;
        length = "4 a 5 phrases, environ 65 a 100 mots";
    }
    std::string instruction = "\n[REACTION A CETTE QUETE]\nPour cette replique seulement, la longueur "
        "suivante remplace les consignes generales de brievete, y compris celles de la fiche : " + length +
        ". Parle en francais, naturellement, en restant fidele a ta personnalite. "
        "Appuie-toi sur des details connus de cette mission ; ne remplis pas avec des banalites. "
        "Ne recite pas la fiche de quete, ne dis pas 'quete elite', 'quete de raid' ou un pourcentage. "
        "Ne transforme pas un objectif ou les affirmations d'un PNJ en action deja accomplie. "
        "N'invente ni rencontre, soin, sauvetage, dialogue passe, ni information future. ";
    instruction += rewarded
        ? "La mission vient d'etre rendue : reagis au resultat connu et a ses consequences.\n"
        : "La mission vient d'etre acceptee : reagis a ce qui nous attend, sans anticiper son resultat.\n";
    return {std::min(chance, 100u), tier != PBC_QuestReactionTier::Normal, instruction};
}

// Preserve every byte while keeping packets short; never split a UTF-8 code point.
inline std::vector<std::string> PBC_SplitQuestSpeech(std::string const& text)
{
    std::vector<std::string> result;
    size_t start = 0;
    while (start < text.size())
    {
        size_t end = std::min(start + 220, text.size());
        if (end < text.size())
        {
            while (end > start && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
                --end;
            if (end == start)
                end = std::min(start + 220, text.size());
            auto space = text.rfind(' ', end - 1);
            if (space != std::string::npos && space > start)
                end = space + 1;
        }
        result.push_back(text.substr(start, end - start));
        start = end;
    }
    return result;
}

#endif
