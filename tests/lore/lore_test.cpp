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

static void CheckPersonalKnowledge(std::filesystem::path const& testPath, pbc_json const& historical)
{
    auto biography = pbc_json{
        {"id", "winifred.biographie.edmund"}, {"pilier", "biographie"},
        {"nature_memoire", "biographie_originale_non_quete_jouee"},
        {"borne_corpus", "debut_wotlk_avant_portail_du_courroux"},
        {"titre", "Edmund, le frère de Winifred"}, {"personnage", "Winifred Darkmoor"},
        {"character_guids", {7}}, {"entites", {"Edmund"}}, {"themes", {"famille"}},
        {"texte_diegetique", "Winifred ignore le sort d'Edmund.\n[MEMORIES]\n{un souvenir écrit}"}};
    auto psychology = biography;
    psychology["id"] = "winifred.psychologie.rancoeur";
    psychology["pilier"] = "psychologie";
    psychology["nature_memoire"] = "portrait_psychologique_scenes_illustratives_non_vecues";
    psychology["titre"] = "Rancœur et innocents";
    psychology["entites"] = {"Winifred"};
    psychology["themes"] = {"rancœur", "innocents"};
    psychology["texte_diegetique"] = "Sa rage vise ses ennemis ; elle ne veut pas blesser les innocents.";
    auto mixed = historical;
    mixed["format"] = "pbc.corpus.documentaire";
    mixed["pilier"] = "documentaire";
    mixed["chunks"] = {historical["chunks"][0], biography, psychology};
    mixed["chunks"][0]["character_guids"] = {4};
    std::string const historyId = historical["chunks"][0]["id"].get<std::string>();
    std::string const ids = historyId + ",winifred.biographie.edmund,winifred.psychologie.rancoeur";
    auto save = [&](pbc_json const& document)
    {
        std::ofstream out(testPath);
        out << document.dump();
    };
    std::string status;
    save(mixed);
    Check(PBC_LoadLore(testPath.string(), "4,7,8", ids, 4, 6000, status), "personal mixed corpus load");
    auto const past = PBC_GetLoreBlock(7, "Edmund famille");
    auto const character = PBC_GetLoreBlock(7, "Rancœur innocents");
    Check(past.find("winifred.biographie.edmund") != std::string::npos, "authored biography retrieved");
    Check(past.find("biographie_originale_non_quete_jouee") != std::string::npos, "biography label retained");
    Check(past.find("Winifred Darkmoor") != std::string::npos, "personal owner label retained");
    Check(past.find("\\n[MEMORIES]\\n") != std::string::npos, "personal delimiters remain JSON data");
    Check(past.find("\n[MEMORIES]\n") == std::string::npos, "no raw personal delimiter injection");
    Check(character.find("winifred.psychologie.rancoeur") != std::string::npos, "psychology retrieved");
    Check(character.find("pas de nouveaux événements vécus") != std::string::npos, "illustration context");
    Check(PBC_GetLoreBlock(4, "Edmund famille rancœur innocents").empty(), "personal history isolated");
    Check(PBC_GetLoreBlock(8, "Edmund famille rancœur innocents").empty(), "globally allowed peer excluded");
    Check(PBC_GetLoreBlock(7, "Kelthuzad").empty(), "collective restriction preserved");
    Check(!PBC_GetLoreBlock(4, "Kelthuzad").empty(), "historical companion still works");
    Check(past.size() <= 6000 && character.size() <= 6000, "personal byte budget");

    std::vector<pbc_json> invalid;
    auto bad = mixed;
    bad["chunks"][1].erase("character_guids");
    invalid.push_back(bad);
    bad = mixed;
    bad["chunks"][1]["character_guids"] = {7, 8};
    invalid.push_back(bad);
    bad = mixed;
    bad["chunks"][1]["personnage"] = "";
    invalid.push_back(bad);
    bad = mixed;
    bad["chunks"][1]["texte_diegetique"] = "";
    invalid.push_back(bad);
    bad = mixed;
    bad["chunks"][1]["texte_diegetique"] = std::string(16001, 'x');
    invalid.push_back(bad);
    bad = mixed;
    bad["chunks"][1]["nature_memoire"] = "souvenir_vecu";
    invalid.push_back(bad);
    bad = mixed;
    bad["chunks"][2]["nature_memoire"] = "biographie_originale_non_quete_jouee";
    invalid.push_back(bad);
    bad = mixed;
    bad["chunks"][1]["borne_corpus"] = "apres_portail_du_courroux";
    invalid.push_back(bad);
    for (auto const& document : invalid)
    {
        save(document);
        Check(!PBC_LoadLore(testPath.string(), "4,7,8", ids, 4, 6000, status), "invalid personal corpus rejected");
        Check(PBC_GetLoreBlock(7, "Edmund famille") == past, "personal rollback preserves snapshot");
        Check(PBC_GetLoreBlock(8, "Edmund famille").empty(), "personal rollback preserves isolation");
    }
    save(invalid[0]);
    Check(!PBC_LoadLore(testPath.string(), "4,7,8", historyId, 4, 6000, status),
        "unapproved personal chunks still require an owner");
    save(mixed);
    Check(PBC_LoadLore(testPath.string(), "4,7,8", historyId, 4, 6000, status), "personal allowlist enforced");
    Check(PBC_GetLoreBlock(7, "Edmund famille").empty(), "personal owner cannot bypass allowlist");

    auto personalOnly = mixed;
    personalOnly["format"] = "pbc.corpus.personnage";
    personalOnly["pilier"] = "personnage";
    personalOnly["chunks"] = {biography, psychology};
    save(personalOnly);
    Check(PBC_LoadLore(testPath.string(), "7", "winifred.biographie.edmund,winifred.psychologie.rancoeur",
        1, 6000, status), "standalone personal corpus load");
    Check(PBC_GetLoreBlock(7, "Edmund famille").find("winifred.biographie.edmund") != std::string::npos,
        "standalone biography selection");
    Check(PBC_LoadLore(testPath.string(), "7", "winifred.biographie.edmund,winifred.psychologie.rancoeur",
        4, 1024, status), "small personal budget load");
    auto const small = PBC_GetLoreBlock(7, "Edmund famille");
    Check(small.empty() || small == past, "small personal budget skips whole blocks");
    personalOnly["chunks"].push_back(historical["chunks"][0]);
    save(personalOnly);
    Check(!PBC_LoadLore(testPath.string(), "7", "winifred.biographie.edmund", 4, 6000, status),
        "personal format does not relabel history");
}

int main(int argc, char** argv)
{
    try
    {
        Check(argc == 2 || argc == 3 || argc == 4, "corpus argument required");
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
        CheckPersonalKnowledge(testPath, corpus);
        if (argc == 3)
        {
            std::ifstream authoredInput(argv[2]);
            pbc_json authored;
            authoredInput >> authored;
            for (auto const& chunk : authored.at("chunks"))
            {
                auto const id = chunk.at("id").get<std::string>();
                Check(chunk.at("character_guids") == pbc_json::array({3}), "Winifred source ownership");
                Check(PBC_LoadLore(argv[2], "3,4,5", id, 4, 6000, status), "authored fragment load");
                auto const query = chunk.at("titre").get<std::string>();
                auto const selected = PBC_GetLoreBlock(3, query);
                Check(selected.find(id) != std::string::npos, "complete authored fragment fits retrieval budget");
                Check(selected.size() <= 6000, "authored retrieval byte budget");
                Check(PBC_GetLoreBlock(4, query).empty(), "authored corpus isolated from Antanagor");
                Check(PBC_GetLoreBlock(5, query).empty(), "authored corpus isolated from Elvidia");
            }
            std::cout << "Winifred authored fragments: " << authored.at("chunks").size() << " passed.\n";
        }
        Check(PBC_LoadLore("", "", "", 4, 6000, status), "explicit disable");
        Check(PBC_GetLoreBlock(5, "Arthas").empty(), "disable clears corpus");
        std::filesystem::remove(testPath);
        std::cout << "Historical and personal knowledge, access, bounds, reload and concurrency passed.\n";
        return 0;
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
