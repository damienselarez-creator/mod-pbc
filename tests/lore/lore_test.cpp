#include "pbc_lore.h"
#include "pbc_json.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

static void Check(bool ok, char const* description)
{
    if (!ok)
        throw std::runtime_error(description);
}

int main(int argc, char** argv)
{
    try
    {
        Check(argc == 2 || argc == 4, "corpus argument required");
        std::ifstream input(argv[1]);
        pbc_json corpus;
        input >> corpus;
        std::string allowed;
        for (auto const& c : corpus.at("chunks"))
            allowed += (allowed.empty() ? "" : ",") + c.at("id").get<std::string>();
        std::string status;
        Check(PBC_LoadLore(argv[1], "5", allowed, 4, 6000, status), "valid load");
        auto result = PBC_GetLoreBlock(5, "Tu détestes plus Arthas ou Keltuzad ?");
        Check(result.find("origines-fleau") != std::string::npos, "KelThuzad origins retrieved");
        Check(result.find("embuscade-arthas") != std::string::npos, "ambush retrieved");
        Check(result.size() <= 6000, "byte bound including header");
        Check(PBC_GetLoreBlock(4, "Arthas").empty(), "other characters excluded");
        Check(PBC_GetLoreBlock(5, "Recette de gâteau au chocolat").empty(), "unrelated query");
        Check(PBC_GetLoreBlock(5, "").empty(), "empty query");
        auto exact = PBC_GetLoreBlock(5, "Kel’Thuzad");
        Check(exact == PBC_GetLoreBlock(5, "KEL'THUZAD"), "case and apostrophe folding");
        auto testPath = std::filesystem::path("lore-invalid-test.json");
        auto invalid = corpus;
        invalid["chunks"].push_back(invalid["chunks"][0]);
        { std::ofstream out(testPath); out << invalid.dump(); }
        Check(!PBC_LoadLore(testPath.string(), "4", allowed, 4, 6000, status), "duplicate rejected");
        Check(PBC_GetLoreBlock(5, "Tu détestes plus Arthas ou Keltuzad ?") == result, "previous snapshot retained");
        Check(PBC_GetLoreBlock(4, "Arthas").empty(), "failed reload retains old access policy");
        invalid = corpus;
        invalid["version"] = "future";
        { std::ofstream out(testPath); out << invalid.dump(); }
        Check(!PBC_LoadLore(testPath.string(), "5", allowed, 4, 6000, status), "unknown format rejected");
        { std::ofstream out(testPath); out << "{truncated"; }
        Check(!PBC_LoadLore(testPath.string(), "5", allowed, 4, 6000, status), "partial write rejected");
        Check(!PBC_LoadLore(argv[1], "5oops", allowed, 4, 6000, status), "bad guid rejected");
        Check(!PBC_LoadLore(argv[1], "5", "missing-id", 4, 6000, status), "unknown allowlist entry rejected");
        Check(!PBC_LoadLore(argv[1], "5", "", 4, 6000, status), "empty access rejected");
        Check(!PBC_LoadLore(argv[1], "5", allowed, 100, 6000, status), "unbounded selection rejected");
        Check(PBC_LoadLore(argv[1], "5", "reprouves.histoire.putress-remede", 1, 6000, status), "restricted policy");
        Check(PBC_GetLoreBlock(5, "Mug’thol Couronne de volonté").empty(), "unapproved knowledge excluded");
        Check(PBC_GetLoreBlock(5, "Putress remède").find("putress-remede") != std::string::npos, "approved knowledge included");
        Check(PBC_LoadLore(argv[1], "5", allowed, 4, 1024, status), "small budget accepted");
        Check(PBC_GetLoreBlock(5, "Arthas").size() <= 1024, "small budget keeps whole chunks");
        std::atomic<bool> stop{false};
        std::atomic<bool> bad{false};
        std::thread reader([&]
        {
            while (!stop.load())
                if (PBC_GetLoreBlock(5, "Arthas").size() > 6000)
                    bad.store(true);
        });
        for (int i = 0; i < 20; ++i)
            if (!PBC_LoadLore(argv[1], "5", allowed, 4, 6000, status))
                bad.store(true);
        stop.store(true);
        reader.join();
        Check(!bad.load(), "concurrent publication");
        auto mixed = corpus;
        mixed["format"] = "pbc.corpus.documentaire";
        mixed["pilier"] = "documentaire";
        auto culture = corpus["chunks"][0];
        culture["id"] = "reprouves.socioculturel.robe-garde-kel";
        culture["pilier"] = "socioculturel";
        culture["nature_memoire"] = "savoir_socioculturel_non_vecu";
        culture["titre"] = "Beryl et la robe du novice";
        culture["faits_rapportes"] = "Beryl demande de soigner Kel.";
        culture["interpretation_du_manuscrit"] = "Une reconnaissance sociale, pas un souvenir personnel.";
        culture["entites"] = {"Beryl", "Kel"};
        culture["themes"] = {"robe", "soin"};
        mixed["chunks"].push_back(culture);
        std::string const cultureId = culture["id"].get<std::string>();
        { std::ofstream out(testPath); out << mixed.dump(); }
        Check(PBC_LoadLore(testPath.string(), "4", allowed + "," + cultureId, 4, 6000, status), "mixed load");
        auto culturalResult = PBC_GetLoreBlock(4, "Beryl robe novice");
        Check(culturalResult.find(cultureId) != std::string::npos, "cultural knowledge retrieved");
        Check(culturalResult.find("savoir_socioculturel_non_vecu") != std::string::npos, "scope retained");
        Check(culturalResult.size() <= 6000, "mixed budget");
        Check(PBC_GetLoreBlock(5, "Beryl").empty(), "mixed access restricted");
        Check(PBC_LoadLore(testPath.string(), "4", allowed, 4, 6000, status), "mixed allowlist");
        Check(PBC_GetLoreBlock(4, "Beryl").find(cultureId) == std::string::npos, "culture excluded by policy");
        mixed["chunks"].back()["nature_memoire"] = "souvenir_vecu";
        { std::ofstream out(testPath); out << mixed.dump(); }
        Check(!PBC_LoadLore(testPath.string(), "4", allowed, 4, 6000, status), "lived memories rejected");
        mixed["chunks"].back()["nature_memoire"] = "savoir_socioculturel_non_vecu";
        mixed["chunks"].back()["pilier"] = "personnalite";
        { std::ofstream out(testPath); out << mixed.dump(); }
        Check(!PBC_LoadLore(testPath.string(), "4", allowed, 4, 6000, status), "personality rejected");
        mixed["chunks"].back()["pilier"] = "socioculturel";
        mixed["format"] = "pbc.corpus.histoire";
        mixed["pilier"] = "histoire";
        { std::ofstream out(testPath); out << mixed.dump(); }
        Check(!PBC_LoadLore(testPath.string(), "4", allowed, 4, 6000, status), "legacy scope unchanged");
        auto restricted = corpus;
        for (auto& chunk : restricted["chunks"])
            chunk["character_guids"] = {4};
        auto elvidia = restricted["chunks"][0];
        elvidia["id"] = "elvidia.histoire.test";
        elvidia["character_guids"] = {5};
        restricted["chunks"].push_back(elvidia);
        std::string const bothAllowed = allowed + ",elvidia.histoire.test";
        { std::ofstream out(testPath); out << restricted.dump(); }
        Check(PBC_LoadLore(testPath.string(), "4,5", bothAllowed, 4, 6000, status), "character policies load");
        auto elvidiaResult = PBC_GetLoreBlock(5, "Kelthuzad");
        Check(elvidiaResult.find("elvidia.histoire.test") != std::string::npos, "Elvidia access");
        Check(elvidiaResult.find("reprouves.histoire.") == std::string::npos, "Antanagor corpus isolated");
        auto antanagorResult = PBC_GetLoreBlock(4, "Kelthuzad");
        Check(antanagorResult.find("reprouves.histoire.") != std::string::npos, "Antanagor access preserved");
        Check(antanagorResult.find("elvidia.histoire.test") == std::string::npos, "Elvidia corpus isolated");
        Check(PBC_GetLoreBlock(6, "Kelthuzad").empty(), "global access still required");
        for (auto const& policy : std::vector<pbc_json>{pbc_json::array(), {0}, {-1}, {"5"}, {5, 5}, {6}, {5.5}})
        {
            auto badPolicy = restricted;
            badPolicy["chunks"].back()["character_guids"] = policy;
            { std::ofstream out(testPath); out << badPolicy.dump(); }
            Check(!PBC_LoadLore(testPath.string(), "4,5", bothAllowed, 4, 6000, status), "invalid policy rejected");
            Check(PBC_GetLoreBlock(5, "Kelthuzad") == elvidiaResult, "invalid policy retains snapshot");
        }
        { std::ofstream out(testPath); out << restricted.dump(); }
        Check(PBC_LoadLore(testPath.string(), "4,5", allowed, 4, 6000, status), "global allowlist intersects");
        Check(PBC_GetLoreBlock(5, "Kelthuzad").empty(), "per-character access cannot bypass allowlist");
        if (argc == 4)
        {
            std::ifstream policyInput(argv[3]);
            pbc_json policy;
            policyInput >> policy;
            Check(PBC_LoadLore(argv[2], policy.at("HistoryCharacterGuids").get<std::string>(),
                policy.at("HistoryAllowedChunks").get<std::string>(), 4, 6000, status), "deployment corpus load");
            for (auto const& query : {"Puits de soleil", "Garithos", "Arthas", "Quel Thalas"})
            {
                auto selected = PBC_GetLoreBlock(5, query);
                Check(selected.find("elvidia_p1_") != std::string::npos, "deployed Elvidia knowledge selected");
                Check(selected.find("reprouves.") == std::string::npos, "deployed Elvidia isolation");
                Check(selected.size() <= 6000, "deployed byte budget");
            }
            auto antanagor = PBC_GetLoreBlock(4, "Arthas");
            Check(antanagor.find("reprouves.") != std::string::npos, "deployed Antanagor preserved");
            Check(antanagor.find("elvidia_p1_") == std::string::npos, "deployed Antanagor isolation");
            Check(PBC_GetLoreBlock(6, "Puits de soleil").empty(), "deployed other character excluded");
            std::cout << "Deployment corpus: " << status << '\n';
        }
        Check(PBC_LoadLore("", "", "", 4, 6000, status), "explicit disable");
        Check(PBC_GetLoreBlock(5, "Arthas").empty(), "disable clears corpus");
        std::filesystem::remove(testPath);
        std::cout << "Historical corpus, selection, access, bounds, reload and concurrency passed.\n";
        return 0;
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
