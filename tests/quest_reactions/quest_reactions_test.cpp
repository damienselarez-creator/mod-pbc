#include "pbc_quest_reaction_policy.h"
#include <iostream>
#include <stdexcept>

void Check(bool value, char const* message)
{
    if (!value)
        throw std::runtime_error(message);
}

void CheckSplit(std::string const& text)
{
    auto chunks = PBC_SplitQuestSpeech(text);
    std::string joined;
    for (auto const& chunk : chunks)
    {
        Check(!chunk.empty() && chunk.size() <= 220, "Invalid chat packet size");
        Check((static_cast<unsigned char>(chunk.front()) & 0xC0) != 0x80, "UTF-8 split inside a character");
        joined += chunk;
    }
    Check(joined == text, "Speech truncated or modified");
}

int main()
{
    try
    {
        using Tier = PBC_QuestReactionTier;
        Check(PBC_ClassifyQuestReaction(false, false, false, false) == Tier::Normal, "Normal misclassified");
        Check(PBC_ClassifyQuestReaction(false, false, true, false) == Tier::Elite, "Elite misclassified");
        Check(PBC_ClassifyQuestReaction(false, false, false, true) == Tier::Elite, "Dungeon misclassified");
        Check(PBC_ClassifyQuestReaction(true, false, false, false) == Tier::Raid, "Raid misclassified");
        Check(PBC_ClassifyQuestReaction(false, true, true, false) == Tier::Raid, "World boss priority wrong");
        Check(PBC_ClassifyQuestReaction(false, true, false, true) == Tier::Elite, "Dungeon boss treated as outdoor");
        Check(PBC_ClassifyQuestReaction(true, false, true, true) == Tier::Raid, "Raid priority wrong");
        Check(PBC_SelectQuestReaction(Tier::Normal, false, 25).chance == 25, "Normal accept changed");
        Check(PBC_SelectQuestReaction(Tier::Normal, true, 40).chance == 40, "Normal reward changed");
        Check(!PBC_SelectQuestReaction(Tier::Normal, false, 25).fixedChance, "Normal decay changed");
        Check(PBC_SelectQuestReaction(Tier::Elite, false, 25).chance == 50, "Elite accept wrong");
        Check(PBC_SelectQuestReaction(Tier::Elite, true, 40).chance == 60, "Elite reward wrong");
        Check(PBC_SelectQuestReaction(Tier::Raid, false, 25).chance == 75, "Raid accept wrong");
        Check(PBC_SelectQuestReaction(Tier::Raid, true, 40).chance == 80, "Raid reward wrong");
        Check(PBC_SelectQuestReaction(Tier::Elite, false, 25).fixedChance, "Elite penalty not bypassed");
        Check(PBC_SelectQuestReaction(Tier::Raid, true, 40).fixedChance, "Raid penalty not bypassed");
        PBC_QuestReactionRates custom{0, 100, 110, 99};
        Check(PBC_SelectQuestReaction(Tier::Elite, false, 25, custom).chance == 0, "Disable not supported");
        Check(PBC_SelectQuestReaction(Tier::Raid, false, 25, custom).chance == 100, "Chance clamp failed");
        Check(PBC_SelectQuestReaction(Tier::Elite, true, 40).instruction
            != PBC_SelectQuestReaction(Tier::Raid, true, 40).instruction, "Length tiers identical");
        CheckSplit("");
        CheckSplit("Courte replique.");
        CheckSplit(std::string(221, 'a'));
        CheckSplit(std::string(219, 'a') + u8"épisode à défendre");
        CheckSplit(std::string(218, 'a') + u8"☠ Revenons vivants, pour ceux que cela concerne encore.");
        std::string speech;
        for (int i = 0; i < 20; ++i)
            speech += u8"Nous connaissons désormais le danger. Je reste à tes côtés, malgré mes réserves. ";
        CheckSplit(speech);
        Check(PBC_SplitQuestSpeech(speech).size() > 1, "Long speech not split");
        std::cout << "PASS: classification, priority, rates, decay policy, stage prompts and UTF-8 speech\n";
        return 0;
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
