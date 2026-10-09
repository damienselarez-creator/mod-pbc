#ifndef PBC_VOCATION_POLICY_H
#define PBC_VOCATION_POLICY_H
#include <string>
#include <vector>
#include <set>
#include <utility>
namespace PBCVocationPolicy
{
inline std::string Normalize(std::string text)
{
    for (auto const& replacement : {std::pair<std::string, std::string>{"é", "e"},
        {"è", "e"}, {"ê", "e"}, {"É", "e"}, {"î", "i"}, {"Î", "i"}, {"â", "a"},
        {"à", "a"}, {"ô", "o"}, {"ç", "c"}, {"’", "'"}})
    {
        size_t offset = 0;
        while ((offset = text.find(replacement.first, offset)) != std::string::npos)
            text.replace(offset, replacement.first.size(), replacement.second);
    }
    for (char& ch : text)
        if (ch >= 'A' && ch <= 'Z')
            ch += 'a' - 'A';
    while (!text.empty() && (text.back() == ' ' || text.back() == '.' || text.back() == '!' || text.back() == '?'))
        text.pop_back();
    while (!text.empty() && text.front() == ' ')
        text.erase(0, 1);
    return text;
}

inline std::string Words(std::string text)
{
    text = Normalize(text);
    for (char& c : text)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')))
            c = ' ';
    std::string result = " ";
    for (char c : text)
        if (c != ' ' || result.back() != ' ')
            result += c;
    if (result.back() != ' ')
        result += ' ';
    return result;
}

inline bool Has(std::string const& words, std::string const& phrase)
{
    return words.find(Words(phrase)) != std::string::npos;
}

inline bool Professions(std::string const& input)
{
    auto words = Words(input);
    for (auto const* term : {"metier", "metiers", "artisanat", "forge", "forgeron", "minage",
        "alchimie", "herboristerie", "couture", "enchantement", "depecage", "travail du cuir",
        "ingenierie", "joaillerie", "calligraphie"})
        if (Has(words, term))
            return true;
    return false;
}

inline bool Topic(std::string const& input)
{
    auto words = Words(input);
    bool directed = false;
    for (auto const* term : {"tu", "tes", "ton", "ta", "toi", "te", "je veux", "je voudrais",
        "je prefere", "je te conseille", "apprends", "vocation"})
        directed = directed || Has(words, term);
    return directed && (Professions(input) || Has(words, "vocation") || Has(words, "specialite") ||
        Has(words, "specialisation") || Has(words, "voie de combat"));
}

inline bool Hesitation(std::string const& input)
{
    auto words = Words(input);
    for (auto const* term : {"pas", "non", "jamais", "sans", "peut etre", "si", "pourquoi",
        "comment", "exemple", "quel", "quelle", "quels", "quelles", "est ce", "sais tu"})
        if (Has(words, term))
            return true;
    return input.find('?') != std::string::npos;
}

inline std::string ActivityIntent(std::string const& input)
{
    if (input.find('"') != std::string::npos || input.find("«") != std::string::npos)
        return "";
    auto words = Words(input);
    bool politeRequest = false;
    // Polite requests are accepted only when their entire remaining clause is an action.
    for (auto const* prefix : {"s il te plait", "je voudrais que tu", "j aimerais que tu",
        "peux tu", "pourrais tu", "tu peux"})
    {
        auto part = Words(prefix);
        if (words.rfind(part, 0) == 0)
        {
            words = " " + words.substr(part.size());
            politeRequest = true;
            break;
        }
    }
    for (auto const* suffix : {"s il te plait", "merci"})
    {
        auto part = Words(suffix);
        if (words.size() >= part.size() && words.ends_with(part))
            words.erase(words.size() - part.size() + 1);
    }
    if (Hesitation(words) || (!politeRequest && input.find('?') != std::string::npos))
        return "";
    for (auto const* phrase : {"reste avec moi", "rester avec moi", "reste pres de moi", "suis moi",
        "me suivre", "reviens pres de moi", "arrete tes apprentissages", "on repart ensemble"})
        if (words == Words(phrase))
            return "follow";
    for (auto const* phrase : {"va voir ton maitre de metier", "va voir tes maitres de metier",
        "va voir tes maitres de metiers",
        "va voir ton maitre de profession", "va voir tes maitres de profession",
        "aller voir tes maitres de metier", "occupe toi de ta formation professionnelle"})
        if (words == Words(phrase))
            return "training_professions";
    for (auto const* phrase : {"va voir ton maitre de classe", "aller voir ton maitre de classe"})
        if (words == Words(phrase))
            return "training_class";
    for (auto const* phrase : {"va voir tes maitres", "va voir ton maitre", "va apprendre tes competences",
        "occupe toi de ta formation", "occupe toi de tes apprentissages", "va te former",
        "aller te former", "aller voir tes maitres", "apprendre tes competences"})
        if (words == Words(phrase))
            return "training";
    for (auto const* phrase : {"reprends tes activites", "reprendre tes activites", "reprends tes habitudes"})
        if (words == Words(phrase))
            return "normal";
    return "";
}

inline bool Confirmation(std::string const& input)
{
    if (Hesitation(input))
        return false;
    auto words = Words(input);
    for (auto const* phrase : {"oui", "oui ca me va", "ca me va", "d accord", "oui d accord",
        "bonne idee", "oui bonne idee", "vas y", "oui vas y", "ca me semble bien",
        "ca me parait bien", "ca me parait utile", "oui ca me parait utile",
        "fais comme tu le sens", "je te laisse choisir", "tu peux choisir",
        "choisis toi meme", "comme tu veux"})
        if (words == Words(phrase))
            return true;
    return false;
}

inline bool IncludesAlias(std::string const& words, std::string const& alias)
{
    auto normalized = Words(alias);
    size_t at = 0;
    bool meaningful = false;
    while (at < normalized.size())
    {
        size_t end = normalized.find(' ', at);
        if (end == std::string::npos)
            break;
        auto term = normalized.substr(at, end - at);
        at = end + 1;
        if (term.empty() || term == "et" || term == "de" || term == "du" || term == "le" || term == "la")
            continue;
        meaningful = true;
        if (!Has(words, term))
            return false;
    }
    return meaningful;
}

inline int Select(std::string input, std::vector<std::vector<std::string>> const& aliases, int preferred)
{
    if (Hesitation(input))
        return -1;
    input = Normalize(input);
    std::string const naturalInput = input;
    if (Confirmation(input) && input != "oui" && input != "d'accord" &&
        (Has(Words(input), "choisir") || Has(Words(input), "sens")))
        return preferred;
    if (input == "choisis" || input == "choisis toi-meme" || input == "comme tu veux")
        return preferred;
    for (std::string const prefix : {"je prefere ", "choisis ", "je choisis "})
        if (input.rfind(prefix, 0) == 0)
        {
            input.erase(0, prefix.size());
            break;
        }
    std::set<int> matches;
    for (size_t i = 0; i < aliases.size(); ++i)
    {
        if (input == std::to_string(i + 1))
            matches.insert(int(i));
        for (auto const& alias : aliases[i])
            if (input == Normalize(alias))
                matches.insert(int(i));
    }
    if (!matches.empty())
        return matches.size() > 1 ? -2 : *matches.begin();
    auto words = Words(naturalInput);
    bool intent = false;
    for (auto const* phrase : {"je veux", "je voudrais", "j aimerais", "je prefere", "je choisis", "choisis",
        "je te propose", "je te conseille", "tu devrais",
        "tu peux apprendre", "apprends", "deviens", "partons sur", "prenons", "allons sur",
        "me semblent bien", "me semble bien", "oui pour"})
        intent = intent || Has(words, phrase);
    if (!intent)
        return -1;
    std::set<int> fullMatches;
    for (size_t i = 0; i < aliases.size(); ++i)
    {
        if (!aliases[i].empty() && IncludesAlias(words, aliases[i].front()))
            fullMatches.insert(int(i));
        for (auto const& alias : aliases[i])
            if (IncludesAlias(words, alias))
                matches.insert(int(i));
    }
    auto const& selected = fullMatches.empty() ? matches : fullMatches;
    return selected.size() > 1 ? -2 : selected.empty() ? -1 : *selected.begin();
}
}
#endif
