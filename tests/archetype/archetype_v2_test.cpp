#include "pbc_archetype.h"
#include "pbc_json.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

void Check(bool condition, char const* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

int main(int argc, char** argv)
{
    try
    {
        Check(argc == 2, "Missing corpus");
        std::string status;
        Check(PBC_LoadArchetypes(argv[1], status), "Cannot load archetypes");
        std::ifstream file(argv[1]);
        pbc_json data;
        file >> data;
        pbc_json probes = pbc_json::array();
        std::set<std::string> cards;
        std::vector<std::pair<std::string, std::string>> const scenes = {
            {"Un allié blessé refuse de s'arrêter.", "blessure"},
            {"An injured ally refuses to stop.", "blessure"},
            {"Le chef donne un ordre précipité.", "autorite"},
            {"The commander gives a reckless order.", "autorite"},
            {"Un prisonnier demande grâce.", "capture"},
            {"A prisoner asks for mercy.", "capture"},
            {"Un sanctuaire risque une profanation.", "spiritualite"},
            {"A sacred shrine faces desecration.", "spiritualite"},
            {"Nos provisions manquent.", "penurie"},
            {"Our supplies are running low.", "penurie"},
            {"La défaite laisse un goût amer.", "defaite"},
            {"The defeat has shaken everyone.", "defaite"}
        };
        for (auto const& [key, combination] : data.at("combinations").items())
        {
            unsigned r = 0, c = 0, s = 0;
            Check(sscanf(key.c_str(), "%u:%u:%u", &r, &c, &s) == 3, "Bad key");
            auto card = PBC_ArchetypeCard(r, c, s, 0);
            Check(!card.empty() && card.size() < 17000, "Invalid profile budget");
            Check(cards.insert(card).second, "Duplicate full profile");
            Check(PBC_UsesCollectiveIdentity(r), "Horde not collective");
            for (auto const& [scene, expected] : scenes)
            {
                auto result = pbc_json::parse(PBC_ArchetypeSelection(r, c, s, scene));
                Check(std::find(result["concepts"].begin(), result["concepts"].end(), expected)
                    != result["concepts"].end(), "Concept not recognized");
                if (result["selected"].empty())
                    std::cerr << key << ": " << scene << '\n';
                Check(!result["selected"].empty() && result["selected"].size() <= 4,
                    "Selection empty or oversized");
                std::set<std::string> sections;
                for (auto const& item : result["selected"])
                {
                    Check(item.value("retrieval_mode", std::string{}) != "documentary_only",
                        "Legacy metaphor mistaken for an immediate situation");
                    Check(item["scope_race"] == 0 || item["scope_race"] == r, "Race leak");
                    Check(item["scope_class"] == 0 || item["scope_class"] == c, "Class leak");
                    Check(item["scope_spec"] == -1 || item["scope_spec"] == s, "Spec leak");
                    Check(sections.insert(item["section_id"].get<std::string>()).second,
                        "Duplicate section");
                }
                Check(PBC_ArchetypeKnowledge(r, c, s, scene).size() <= 6000, "Context budget exceeded");
                probes.push_back({{"combination", key}, {"scene", scene}, {"result", result}});
            }
            auto novice = PBC_ArchetypeCard(r, c, -1, 0);
            Check(novice.find("indeterminee_aucune_orientation_imposee") != std::string::npos,
                "Novice forced to specialization");
            Check(novice.find("talent_evidence") == std::string::npos, "Novice receives specialization");
            Check(PBC_ArchetypeFocus(r, c, s) == "normal", "Automatic profession focus");
        }
        Check(cards.size() == 93, "Horde coverage mismatch");
        Check(!PBC_UsesCollectiveIdentity(1), "Alliance identity changed");
        Check(PBC_ArchetypeCard(10, 1, 0, 0).empty(), "Post-Wrath race/class accepted");
        Check(PBC_ArchetypeKnowledge(5, 5, 2, "refuse").empty(), "Incidental refuse match");
        auto historical = pbc_json::parse(PBC_ArchetypeSelection(10, 4, 0, "Garithos Quel Thalas"));
        bool foundHistorical = false;
        for (auto const& item : historical["selected"])
            foundHistorical |= item.value("retrieval_mode", std::string{}) == "documentary_only";
        Check(foundHistorical, "Explicit historical entity no longer retrieves documents");
        auto shared = pbc_json::parse(PBC_ArchetypeSelection(8, 5, 1, "souffrance secours"));
        bool foundShared = false;
        for (auto const& item : shared["selected"])
            foundShared |= item["scope_race"] == 0 && item["scope_class"] == 5;
        Check(foundShared, "Shared class corpus unreachable");
        auto before = PBC_ArchetypeCard(8, 5, 1, 0);
        auto bad = data;
        bad["collective_races"] = pbc_json::array();
        auto invalidPath = std::string(argv[1]) + ".invalid-test";
        {
            std::ofstream invalid(invalidPath);
            invalid << bad;
        }
        Check(!PBC_LoadArchetypes(invalidPath, status), "Malformed policy accepted");
        Check(PBC_ArchetypeCard(8, 5, 1, 0) == before, "Failed load changed snapshot");
        std::remove(invalidPath.c_str());
        std::ofstream output(std::string(argv[1]) + ".probes.json");
        output << probes.dump(2);
        Check(PBC_LoadArchetypes("", status), "Disable rejected");
        Check(PBC_ArchetypeCard(5, 5, 2, 0).empty(), "Disable ineffective");
        std::cout << "PASS: 93 profiles, 1116 bilingual selections, scopes, diversity, budgets, "
            "novices, shared knowledge, atomic rejection and disable\n";
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
