#include "pbc_archetype.h"
#include <iostream>
#include <stdexcept>
#include "pbc_json.h"
#include <fstream>

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
        for (auto const& [raceKey, classes] : data.at("allowed_classes_by_race").items())
            for (auto characterClass : classes)
                for (int spec : {-1, 0, 1, 2})
                {
                    auto card = PBC_ArchetypeCard(static_cast<uint8_t>(std::stoi(raceKey)),
                        characterClass.get<uint8_t>(), spec, 17);
                    Check(!card.empty() && card.size() < 12000, "Invalid composed archetype");
                    Check(card.find("biographie") != std::string::npos, "Knowledge boundary missing");
                }
        auto assassin = PBC_ArchetypeCard(10, 4, 0, 4);
        Check(assassin.find("Assassinat") != std::string::npos, "Assassin missing");
        Check(PBC_ArchetypeFocus(10, 4, 0) == "craft", "Assassin behavioral focus missing");
        auto novice = PBC_ArchetypeCard(5, 9, -1, 3);
        Check(novice.find("voie_envisagee") != std::string::npos, "Novice intention absent");
        Check(novice.find("arbre_de_talents_actuel") == std::string::npos, "Novice invents talents");
        Check(PBC_ArchetypeCard(5, 9, 0, 3).find("voie_envisagee") == std::string::npos,
            "Real specialization mixed with intention");
        Check(PBC_ArchetypeCard(5, 9, 0, 3) != PBC_ArchetypeCard(5, 9, 0, 4), "Cloned voices");
        Check(PBC_ArchetypeCard(99, 9, 0, 3).empty(), "Unknown race accepted");
        Check(PBC_ArchetypeCard(10, 1, 0, 3).empty(), "Post-WotLK combination accepted");
        auto bloodElf = PBC_ArchetypeKnowledge(10, 4, 0, "Garithos Quel Thalas Arthas");
        Check(bloodElf.find("elvidia_p1_") != std::string::npos, "Blood elf historical pillar missing");
        Check(bloodElf.find("reprouves.") == std::string::npos, "Forsaken pillar leaked to blood elf");
        Check(bloodElf.size() <= 6000, "Collective context budget exceeded");
        Check(PBC_ArchetypeKnowledge(8, 5, 1, "Garithos Quel Thalas Arthas").empty(),
            "Troll holy priest borrowed blood elf memories");
        auto forsaken = PBC_ArchetypeKnowledge(5, 9, 0, "volonte maledictions affliction grimoires");
        Check(forsaken.find("reprouves.") != std::string::npos, "Forsaken collective pillar missing");
        Check(forsaken.find("elvidia_") == std::string::npos, "Blood elf pillar leaked to Forsaken");
        Check(PBC_ArchetypeCard(8, 5, 1, 17).find("Sombrelance") != std::string::npos,
            "Troll holy priest context not distinct");
        Check(!PBC_LoadArchetypes("/nonexistent/archetypes.json", status), "Bad reload accepted");
        Check(!PBC_ArchetypeCard(5, 9, 0, 3).empty(), "Reload lost valid profile");
        Check(PBC_LoadArchetypes("", status), "Explicit disable failed");
        Check(PBC_ArchetypeCard(5, 9, 0, 3).empty(), "Disable failed");
        std::cout << "PASS: composition, novice/actual spec, voice variation, focus, isolation and atomic reload\n";
        return 0;
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
