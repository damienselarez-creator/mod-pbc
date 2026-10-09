#include "pbc_archetype.h"
#include "pbc_json.h"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace
{
std::set<std::string> Terms(std::string text)
{
    for (auto const& [from, to] : std::vector<std::pair<std::string, std::string>>{
        {"é", "e"}, {"è", "e"}, {"ê", "e"}, {"ë", "e"}, {"É", "e"}, {"È", "e"},
        {"à", "a"}, {"â", "a"}, {"À", "a"}, {"ô", "o"}, {"î", "i"}, {"ï", "i"},
        {"û", "u"}, {"ù", "u"}, {"ç", "c"}, {"œ", "oe"}})
    {
        size_t pos = 0;
        while ((pos = text.find(from, pos)) != std::string::npos)
        {
            text.replace(pos, from.size(), to);
            pos += to.size();
        }
    }
    for (auto& value : text)
    {
        if (value >= 'A' && value <= 'Z')
            value += 'a' - 'A';
        else if (!((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9')))
            value = ' ';
    }
    static std::set<std::string> const stop = {"avec", "dans", "pour", "elle", "nous", "vous", "sont",
        "cette", "etre", "mais", "comme", "plus", "leur", "leurs", "tous", "sans", "faire", "les",
        "des", "une", "qui", "que", "sur", "par", "pas", "son", "ses", "est", "notre", "votre",
        "the", "and", "with", "from", "that", "this", "you", "your", "have", "has", "wants"};
    std::set<std::string> result;
    std::istringstream words(text);
    for (std::string word; words >> word;)
        if (word.size() >= 3 && !stop.count(word))
            result.insert(word);
    return result;
}

struct Entry
{
    pbc_json chunk;
    std::set<std::string> words;
    std::set<std::string> concepts;
    std::vector<std::set<std::string>> entities;
};

struct Corpus
{
    pbc_json data;
    std::vector<Entry> entries;
    std::map<std::string, std::vector<std::set<std::string>>> aliases;
};

std::atomic<std::shared_ptr<Corpus const>> corpus;

bool Includes(std::set<std::string> const& words, std::set<std::string> const& phrase)
{
    return !phrase.empty() && std::includes(words.begin(), words.end(), phrase.begin(), phrase.end());
}

unsigned Overlap(std::set<std::string> const& first, std::set<std::string> const& second)
{
    unsigned count = 0;
    for (auto const& word : first)
        if (second.count(word))
            ++count;
    return count;
}

bool Allowed(Corpus const& state, uint8_t race, uint8_t cls)
{
    auto key = std::to_string(race);
    if (!state.data.at("allowed_classes_by_race").contains(key))
        return false;
    auto const& list = state.data.at("allowed_classes_by_race").at(key);
    return std::find(list.begin(), list.end(), cls) != list.end();
}

pbc_json Profile(Corpus const& state, uint8_t race, uint8_t cls, int spec, uint64_t identity)
{
    if (!Allowed(state, race, cls))
        return nullptr;
    auto r = std::to_string(race);
    auto c = std::to_string(cls);
    auto pair = r + ":" + c;
    pbc_json profile = {{"race", state.data.at("races").at(r)}, {"classe", state.data.at("classes").at(c)},
        {"statut", "socle_collectif_sans_biographie_personnelle"}};
    if (state.data.at("race_classes").contains(pair))
        profile["culture_de_classe"] = state.data.at("race_classes").at(pair);
    if (spec >= 0 && spec < 3)
    {
        auto s = c + ":" + std::to_string(spec);
        profile["specialisation"] = state.data.at("specializations").at(s);
        profile["specialisation_statut"] = "arbre_de_talents_actuel_pas_tous_les_sorts_acquis";
        auto key = pair + ":" + std::to_string(spec);
        if (state.data.at("combinations").contains(key))
            profile["combinaison"] = state.data.at("combinations").at(key);
    }
    else
        profile["specialisation_statut"] = "indeterminee_aucune_orientation_imposee";
    if (identity && state.data.at("variants").size())
        profile["nuance_expression"] = state.data.at("variants").at(identity % state.data.at("variants").size());
    return profile;
}

std::set<std::string> Concepts(Corpus const& state, std::set<std::string> const& query)
{
    std::set<std::string> concepts;
    for (auto const& [name, aliases] : state.aliases)
        for (auto const& phrase : aliases)
            if (Includes(query, phrase))
            {
                concepts.insert(name);
                break;
            }
    return concepts;
}

pbc_json Select(Corpus const& state, uint8_t race, uint8_t cls, int spec,
    std::string const& event, std::string const& previous)
{
    pbc_json result = {{"concepts", pbc_json::array()}, {"selected", pbc_json::array()}};
    if (!Allowed(state, race, cls) || event.empty())
        return result;
    auto query = Terms(event.substr(0, 8192));
    auto concepts = Concepts(state, query);
    result["concepts"] = concepts;
    struct Match
    {
        Entry const* entry;
        unsigned score;
    };
    std::vector<Match> matches;
    for (auto const& entry : state.entries)
    {
        auto const& item = entry.chunk;
        auto r = item.at("scope_race").get<unsigned>();
        auto c = item.at("scope_class").get<unsigned>();
        auto s = item.at("scope_spec").get<int>();
        if ((r && r != race) || (c && c != cls) || (s >= 0 && s != spec))
            continue;
        if (item.value("retrieval_mode", std::string{}) == "library")
        {
            auto const& audience = item.at("audience_races");
            if (std::find(audience.begin(), audience.end(), race) == audience.end())
                continue;
        }
        auto id = item.at("id").get<std::string>();
        if (previous.find("\"id\":\"" + id + "\"") != std::string::npos)
            continue;
        unsigned entityHits = 0;
        for (auto const& entity : entry.entities)
            if (Includes(query, entity))
                ++entityHits;
        unsigned conceptHits = Overlap(concepts, entry.concepts);
        unsigned wordHits = Overlap(query, entry.words);
        // Historical prose may use bodily terms metaphorically. Require a documentary
        // question or a named entity before drawing on an unreviewed legacy passage.
        if (item.value("retrieval_mode", std::string{}) == "documentary_only" &&
            !entityHits && (!concepts.count("histoire") || wordHits < 2))
            continue;
        bool library = item.value("retrieval_mode", std::string{}) == "library";
        // Literary excerpts have an explicit topic index. Incidental words in a
        // narrator's story must not make that story relevant to the current scene.
        if (library ? (!entityHits && wordHits < 2) :
            (!conceptHits && !entityHits && (!concepts.empty() || wordHits < 2)))
            continue;
        bool primaryTopic = library && !entry.entities.empty() && Includes(query, entry.entities.front());
        unsigned score = library
            ? entityHits * 40 + std::min(wordHits, 8u) * 3 + (r ? 10u : 0u) + (primaryTopic ? 25u : 0u)
            : conceptHits * 30 + entityHits * 20 + std::min(wordHits, 6u);
        matches.push_back({&entry, score});
    }
    // One section per result; second pass prefers distinct documentary layers.
    std::set<std::string> sections;
    std::map<std::string, unsigned> layers;
    size_t bytes = 0;
    while (result["selected"].size() < 4 && !matches.empty())
    {
        auto best = matches.end();
        unsigned bestScore = 0;
        for (auto it = matches.begin(); it != matches.end(); ++it)
        {
            auto const& item = it->entry->chunk;
            auto section = item.at("section_id").get<std::string>();
            auto layer = item.at("layer").get<std::string>();
            if (sections.count(section) || bytes + item.dump().size() + 1 > 5500)
                continue;
            unsigned score = it->score / (1 + layers[layer]);
            if (best == matches.end() || score > bestScore ||
                (score == bestScore && item.at("id") < best->entry->chunk.at("id")))
            {
                best = it;
                bestScore = score;
            }
        }
        if (best == matches.end())
            break;
        auto const& item = best->entry->chunk;
        sections.insert(item.at("section_id").get<std::string>());
        ++layers[item.at("layer").get<std::string>()];
        bytes += item.dump().size() + 1;
        result["selected"].push_back(item);
        matches.erase(best);
    }
    return result;
}
}

bool PBC_LoadArchetypes(std::string const& path, std::string& status)
{
    try
    {
        if (path.empty())
        {
            corpus.store({});
            status = "disabled";
            return true;
        }
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file || file.tellg() <= 0 || file.tellg() > 8 * 1024 * 1024)
            throw std::runtime_error("Invalid corpus size");
        std::string bytes(static_cast<size_t>(file.tellg()), '\0');
        file.seekg(0);
        if (!file.read(bytes.data(), bytes.size()))
            throw std::runtime_error("Unreadable corpus");
        auto next = std::make_shared<Corpus>();
        auto& data = next->data;
        data = pbc_json::parse(bytes, [](int depth, pbc_json::parse_event_t, pbc_json&)
        {
            if (depth > 16)
                throw std::runtime_error("Nesting limit");
            return true;
        });
        if (data.at("schema") != "pbc-archetypes-2" ||
            data.at("timeline") != "debut_wotlk_avant_portail_du_courroux")
            throw std::runtime_error("Invalid schema");
        for (auto key : {"races", "classes", "specializations", "race_classes", "combinations", "sources", "collective_races", "allowed_classes_by_race"})
            if (!data.at(key).is_object() || data.at(key).size() > 4096)
                throw std::runtime_error("Invalid table");
        for (auto const& [race, enabled] : data.at("collective_races").items())
            if (!data.at("races").contains(race) || !enabled.is_boolean() || !enabled.get<bool>())
                throw std::runtime_error("Invalid collective race");
        if (!data.at("variants").is_array() || data.at("variants").size() > 16)
            throw std::runtime_error("Invalid variants");
        for (auto const& variant : data.at("variants"))
            if (!variant.is_string() || variant.get<std::string>().size() > 600)
                throw std::runtime_error("Invalid variant");
        for (auto const& [race, value] : data.at("races").items())
        {
            if (!value.at("label").is_string() || value.dump().size() > 10000)
                throw std::runtime_error("Invalid race");
            auto const& allowed = data.at("allowed_classes_by_race").at(race);
            if (!allowed.is_array() || allowed.empty() || allowed.size() > 10)
                throw std::runtime_error("Invalid class policy");
            for (auto const& cls : allowed)
                if (!cls.is_number_unsigned() || !data.at("classes").contains(std::to_string(cls.get<unsigned>())))
                    throw std::runtime_error("Invalid class");
        }
        for (auto const& [cls, value] : data.at("classes").items())
        {
            if (!value.at("label").is_string() || value.dump().size() > 8000)
                throw std::runtime_error("Invalid class profile");
            for (int spec = 0; spec < 3; ++spec)
                if (!data.at("specializations").contains(cls + ":" + std::to_string(spec)))
                    throw std::runtime_error("Missing specialization");
        }
        if (!data.at("concept_aliases").is_object() || data.at("concept_aliases").size() > 64)
            throw std::runtime_error("Invalid concepts");
        for (auto const& [name, list] : data.at("concept_aliases").items())
        {
            if (!list.is_array() || list.size() > 64)
                throw std::runtime_error("Invalid aliases");
            for (auto const& alias : list)
            {
                auto text = alias.get<std::string>();
                if (text.size() > 160 || Terms(text).empty())
                    throw std::runtime_error("Invalid alias");
                next->aliases[name].push_back(Terms(text));
            }
        }
        auto const& chunks = data.at("documentary_chunks");
        if (!chunks.is_array() || chunks.size() > 4096)
            throw std::runtime_error("Invalid chunk count");
        std::set<std::string> ids;
        for (auto const& item : chunks)
        {
            auto id = item.at("id").get<std::string>();
            auto race = item.at("scope_race").get<unsigned>();
            auto cls = item.at("scope_class").get<unsigned>();
            auto spec = item.at("scope_spec").get<int>();
            auto text = item.at("text").get<std::string>();
            auto section = item.at("section_id").get<std::string>();
            auto layer = item.at("layer").get<std::string>();
            auto state = item.at("status").get<std::string>();
            if (id.empty() || id.size() > 160 || !ids.insert(id).second || text.empty() ||
                item.dump().size() > 5200 || section.empty() || section.size() > 200 ||
                (race && !data.at("races").contains(std::to_string(race))) ||
                (cls && !data.at("classes").contains(std::to_string(cls))) ||
                spec < -1 || spec > 2 || (!cls && spec != -1) ||
                (race && cls && !Allowed(*next, race, cls)) ||
                item.contains("personnage") || item.contains("character_guids") ||
                item.contains("texte_diegetique"))
                throw std::runtime_error("Invalid scope or chunk");
            if (!std::set<std::string>{"histoire", "culture", "classe", "specialisation", "croisement"}.count(layer) ||
                !std::set<std::string>{"fait_source", "synthese_document_fourni", "interpretation", "choix_projet"}.count(state))
                throw std::runtime_error("Invalid epistemic status");
            auto const& refs = item.at("source_refs");
            if (!refs.is_array() || refs.empty() || refs.size() > 16)
                throw std::runtime_error("Missing source");
            for (auto const& ref : refs)
                if (!ref.is_string() || !data.at("sources").contains(ref.get<std::string>()))
                    throw std::runtime_error("Unknown source");
            bool library = item.value("retrieval_mode", std::string{}) == "library";
            std::string searchText = item.at("titre").get<std::string>() + " " + text;
            if (library)
            {
                searchText = item.at("search_text").get<std::string>();
                auto const& audience = item.at("audience_races");
                if (!audience.is_array() || audience.empty() || audience.size() > 10)
                    throw std::runtime_error("Invalid library audience");
                for (auto const& allowedRace : audience)
                    if (!allowedRace.is_number_unsigned() ||
                        !data.at("races").contains(std::to_string(allowedRace.get<unsigned>())))
                        throw std::runtime_error("Unknown library audience");
                if (searchText.empty() || searchText.size() > 1500 || state != "interpretation" ||
                    !item.at("lecture").is_string() || item.at("lecture").get<std::string>().empty() ||
                    !item.at("provenance").is_object() || !item.at("reperes_documentaires").is_array())
                    throw std::runtime_error("Invalid library excerpt");
            }
            Entry entry{item, Terms(searchText), {}, {}};
            if (!item.at("concepts").is_array() || item.at("concepts").size() > 64 ||
                !item.at("entites").is_array() || item.at("entites").size() > 128)
                throw std::runtime_error("Invalid index fields");
            for (auto const& conceptValue : item.at("concepts"))
            {
                auto name = conceptValue.get<std::string>();
                if (!next->aliases.count(name))
                    throw std::runtime_error("Unknown concept");
                entry.concepts.insert(name);
            }
            for (auto const& entity : item.at("entites"))
                entry.entities.push_back(Terms(entity.get<std::string>()));
            next->entries.push_back(std::move(entry));
        }
        // Validate every composition before publishing the single immutable snapshot.
        for (auto const& [race, classes] : data.at("allowed_classes_by_race").items())
            for (auto cls : classes)
                for (int spec : {-1, 0, 1, 2})
                    if (Profile(*next, static_cast<uint8_t>(std::stoi(race)), cls.get<uint8_t>(), spec, 1).dump().size() > 16000)
                        throw std::runtime_error("Oversized composition");
        status = "v2: " + std::to_string(data.at("combinations").size()) + " combinations, " +
            std::to_string(next->entries.size()) + " sourced collective passages";
        corpus.store(std::shared_ptr<Corpus const>(std::move(next)));
        return true;
    }
    catch (std::exception const&)
    {
        status = "reload rejected; previous archetypes retained";
        return false;
    }
}

bool PBC_UsesCollectiveIdentity(uint8_t race)
{
    auto state = corpus.load();
    return state && state->data.at("collective_races").contains(std::to_string(race));
}

std::string PBC_ArchetypeCard(uint8_t race, uint8_t cls, int spec, uint64_t identity)
{
    auto state = corpus.load();
    if (!state)
        return {};
    auto profile = Profile(*state, race, cls, spec, identity);
    if (profile.is_null())
        return {};
    return "\n[SOCLE COLLECTIF RACE CLASSE SPECIALISATION]\n"
        "Le lore borne les faits ; les choix de projet donnent le style ; la scene borne les capacites et les actes. "
        "Aucune biographie personnelle necessaire. Les interpretations sont des tendances, pas des faits canoniques. "
        "La bibliotheque diegetique controlee nourrit ton regard culturel, tes valeurs et ton expression. "
        "Croise ce socle avec la classe et la specialisation ; laisse la situation nuancer le stereotype. "
        "Respecte les reperes documentes et le statut editorial des illustrations, sans emprunter la vie du narrateur. "
        "Considere ce que tu remarques, souhaites et contestes avant de choisir ta formulation. "
        "La race donne le cadre historique et collectif ; la classe donne une pratique et ses tensions ; "
        "la specialisation precise la maniere d'agir. Ne remplace jamais une de ces couches par une autre. "
        "Fais apparaitre leur croisement dans ton jugement sur la scene, pas dans une liste de traits. "
        "Nuance selon l'enjeu : humour et distance peuvent ceder devant une perte ou une urgence. "
        "L'humour noir reprouve reste sec et situationnel ; aucun trait d'esprit obligatoire a chaque replique. "
        "Chez les Sombrelances, sagesse, flegme, fatalite et rapport aux esprits se nuancent selon classe et voie ; "
        "evite l'accent caricatural et les references terrestres. N'attribue pas ces tendances aux autres races. "
        "Une pratique de classe ou un arbre n'accorde aucun sort non acquis. Parle en francais. "
        "Aucun souvenir invente, affiliation personnelle imposee ou connaissance apres le Portail du Courroux.\n" +
        profile.dump() + "\n[FIN SOCLE]\n";
}

std::string PBC_ArchetypeSelection(uint8_t race, uint8_t cls, int spec, std::string const& event)
{
    auto state = corpus.load();
    return state ? Select(*state, race, cls, spec, event, "").dump() : "{}";
}

std::string PBC_ArchetypeKnowledge(uint8_t race, uint8_t cls, int spec,
    std::string const& event, std::string const& previous)
{
    auto state = corpus.load();
    if (!state)
        return {};
    auto result = Select(*state, race, cls, spec, event, previous);
    if (result["selected"].empty())
        return {};
    std::string out = "\n[PILIERS COLLECTIFS - CONNAISSANCES ET INTERPRETATIONS, PAS SOUVENIRS]\n"
        "Respecte le statut de chaque passage. Ne cite pas les identifiants techniques. "
        "Une tradition collective ne prouve pas une experience personnelle.\n";
    for (auto const& item : result["selected"])
        out += item.dump() + "\n";
    return out;
}

std::string PBC_ArchetypeFocus(uint8_t, uint8_t, int)
{
    // A narrative specialization does not imply a profession or a trainer itinerary.
    return "normal";
}
