#include "pbc_vocation_policy.h"
#include "pbc_vocation_store.h"
#include "pbc_vocation.h"
#include "pbc_vocation_catalog.h"
#include "pbc_character.h"
#include "pbc_config.h"
#include "pbc_json.h"
#include "pbc_quest_reaction_policy.h"
#include "CompanionVocation.h"
#include "CompanionErrands.h"
#include "Playerbots.h"
#include "PlayerbotFactory.h"
#include "AiFactory.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "Config.h"
#include "EventMap.h"
#include "Group.h"
#include "ObjectAccessor.h"
#include "WorldSession.h"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <set>
#include <chrono>
#include <map>
#include <algorithm>

namespace
{
using Json = pbc_json;
std::mutex stateMutex;
Json records = Json::object();
Json const catalog = Json::parse(PBC_VocationCatalog);
std::filesystem::path stateFile;
bool ready = false;
using ConversationClock = std::chrono::steady_clock;
std::map<std::string, ConversationClock::time_point> proposals;
std::set<std::string> introduced;

bool RecentProposal(std::string const& key)
{
    auto it = proposals.find(key);
    return it != proposals.end() && ConversationClock::now() - it->second < std::chrono::minutes(5);
}

bool Awaiting(Player* bot)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    return RecentProposal(bot->GetGUID().ToString());
}

std::string Key(Player* bot) { return bot->GetGUID().ToString(); }

Json Profile(Player* bot)
{
    for (auto const& item : catalog.at("profiles"))
        if (item.at("race") == bot->getRace() && item.at("class") == bot->getClass())
            return item;
    return nullptr;
}

bool Authorized(Player* player, Player* bot)
{
    auto* ai = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
    return g_PBC_Enable && player && bot && player != bot && player->IsInWorld() && bot->IsInWorld() &&
        player->GetSession() && bot->GetSession() && !player->GetSession()->IsHeadless() &&
        bot->GetSession()->IsHeadless() && !player->GetSession()->IsLoggingOut() &&
        player->GetSession()->GetAccountId() == bot->GetSession()->GetAccountId() &&
        ai && ai->GetMaster() == player && player->GetGroup() && player->GetGroup() == bot->GetGroup() &&
        player->GetMap() == bot->GetMap() && bot->IsWithinDistInMap(player, 30.0f) &&
        !player->IsInCombat() && !bot->IsInCombat() && player->IsAlive() && bot->IsAlive() &&
        !player->IsInFlight() && !bot->IsInFlight() && !Profile(bot).is_null();
}

bool Matches(Json const& item, Player* bot)
{
    return bot->GetSession() && item.value("account", 0u) == bot->GetSession()->GetAccountId() &&
        item.value("race", 0u) == bot->getRace() && item.value("class", 0u) == bot->getClass();
}

Json Read(Player* bot)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    auto it = records.find(Key(bot));
    return ready && it != records.end() && Matches(*it, bot) ? *it : Json();
}

bool Save(Player* bot, Json const& item)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    if (!ready || (records.size() >= 4096 && !records.contains(Key(bot))))
        return false;
    try
    {
        auto next = records;
        next[Key(bot)] = item;
        auto temporary = stateFile;
        temporary += ".tmp";
        std::ofstream output(temporary, std::ios::trunc);
        output << Json({{"version", 1}, {"characters", next}}).dump(2) << '\n';
        output.flush();
        if (!output.good())
            return false;
        output.close();
        if (output.fail())
            return false;
        std::filesystem::rename(temporary, stateFile);
        records.swap(next);
        return true;
    }
    catch (std::exception const& error)
    {
        LOG_ERROR("module.pbc", "Vocation persistence failed: {}", error.what());
        return false;
    }
}

std::string Spec(Player* bot, int tab)
{
    return catalog.at("specs").at(std::to_string(bot->getClass())).at(tab).get<std::string>();
}

std::string Pair(Json const& pair)
{
    auto const& names = catalog.at("skills");
    return names.at(std::to_string(pair.at(0).get<uint32>())).get<std::string>() + " et " +
        names.at(std::to_string(pair.at(1).get<uint32>())).get<std::string>();
}

void Say(Player* player, Player* bot, std::string const& text)
{
    std::vector<uint64_t> owners = {player->GetGUID().GetCounter(), bot->GetGUID().GetCounter()};
    PBC_AppendHistoryMessage(bot->GetGUID().GetCounter(), CHAT_MSG_WHISPER, text, owners);
    for (auto const& part : PBC_SplitQuestSpeech(text))
        bot->Whisper(part, LANG_UNIVERSAL, player);
}

using PBCVocationPolicy::Normalize;

std::string DiscussionTopic(Json const& record)
{
    return record.value("topic", record.value("stage", "spec"));
}

bool OtherProposal(uint32 account, std::string const& self)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    for (auto const& [key, item] : records.items())
        if (key != self && item.value("account", 0u) == account && DiscussionTopic(item) != "done" &&
            !item.value("paused", false) && RecentProposal(key))
            return true;
    return false;
}

void Propose(Player* player, Player* bot, Json const& record)
{
    auto profile = Profile(bot);
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        introduced.insert(Key(bot));
        if (DiscussionTopic(record) != "done")
            proposals[Key(bot)] = ConversationClock::now();
        else
            proposals.erase(Key(bot));
    }
    if (DiscussionTopic(record) == "spec")
    {
        Say(player, bot, "Je penche vers " + Spec(bot, record.value("tab", -1) >= 0 ? record.at("tab") : profile.at("preferred")) +
            ". Nous pouvons aussi envisager " + Spec(bot, 0) + ", " + Spec(bot, 1) +
            " ou " + Spec(bot, 2) + ". Qu'en penses-tu ?");
        return;
    }
    if (DiscussionTopic(record) == "professions")
    {
        std::string text = "J'aimerais apprendre " + Pair(profile.at("pairs").at(0)) + ". ";
        for (size_t i = 1; i < profile.at("pairs").size(); ++i)
            text += "Nous pourrions aussi envisager " + Pair(profile.at("pairs").at(i)) + ". ";
        Say(player, bot, text + "Cela te paraît utile pour nos voyages ?");
        return;
    }
    Say(player, bot, "J'ai choisi " + Pair(record.at("professions")) +
        ". Je chercherai leurs maîtres en ville pour les leçons accessibles, avec de quoi les payer.");
}

bool Begin(Player* player, Player* bot, bool professions = false, bool speak = true, bool explicitTopic = false)
{
    if (!Authorized(player, bot))
        return false;
    auto record = Read(bot);
    if (record.is_null())
    {
        record = {{"account", player->GetSession()->GetAccountId()}, {"race", bot->getRace()},
            {"class", bot->getClass()}, {"stage", "spec"}, {"tab", -1}, {"professions", {0, 0}}};
        auto tabs = AiFactory::GetPlayerSpecTabs(bot);
        if (tabs[0] + tabs[1] + tabs[2])
        {
            record["tab"] = int(AiFactory::GetPlayerSpecTab(bot));
            record["stage"] = "professions";
        }
        else if (professions)
            record["stage"] = "professions";
        if (!Save(bot, record))
            return false;
    }
    if (professions && record.value("stage", "") == "spec")
    {
        record["stage"] = "professions";
        if (!Save(bot, record))
            return false;
    }
    if (explicitTopic)
    {
        record["topic"] = professions ? "professions" : "spec";
        if (!Save(bot, record))
            return false;
    }
    if (record.value("paused", false))
    {
        record["paused"] = false;
        if (!Save(bot, record))
            return false;
    }
    if (speak)
        Propose(player, bot, record);
    return true;
}

CompanionVocation Provide(Player* bot)
{
    auto* ai = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
    auto* master = ai ? ai->GetMaster() : nullptr;
    if (!g_PBC_Enable || !bot || !master || master == bot || !bot->GetSession() || !master->GetSession() ||
        !bot->GetSession()->IsHeadless() || master->GetSession()->IsHeadless() ||
        master->GetSession()->GetAccountId() != bot->GetSession()->GetAccountId())
        return {};
    auto record = Read(bot);
    if (record.is_null())
        return {};
    return {true, record.at("tab").get<int>(), record.at("professions").get<std::array<uint32_t, 2>>()};
}

bool Command(ChatHandler* handler, Acore::ChatCommands::Tail)
{
    auto* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
    if (!Begin(player, handler->getSelectedPlayer()))
        handler->SendSysMessage("Sélectionne un compagnon de ton compte, présent dans ton groupe et au calme. Vocations activées requises.");
    return true;
}

class VocationCommands : public CommandScript
{
public:
    VocationCommands() : CommandScript("PBC_VocationCommands") { }
    Acore::ChatCommands::ChatCommandTable GetCommands() const override
    {
        using namespace Acore::ChatCommands;
        static ChatCommandTable table = {{"vocation", Command, SEC_PLAYER, Console::No}};
        return table;
    }
};

class VocationWorld : public WorldScript
{
public:
    VocationWorld() : WorldScript("PBC_VocationWorld") { }
    EventMap events;
    void OnStartup() override
    {
        auto path = sConfigMgr->GetOption<std::string>("PBC.VocationsFile", "");
        if (path.empty())
            return;
        try
        {
            stateFile = path;
            if (!stateFile.is_absolute())
                throw std::runtime_error("VocationsFile must be absolute");
            std::filesystem::create_directories(stateFile.parent_path());
            if (std::filesystem::exists(stateFile))
            {
                if (std::filesystem::file_size(stateFile) > 4 * 1024 * 1024)
                    throw std::runtime_error("Vocation state exceeds size limit");
                std::ifstream input(stateFile);
                auto data = Json::parse(input);
                if (!PBC_ValidVocations(data, catalog))
                    throw std::runtime_error("Invalid vocation state");
                records = data.at("characters");
            }
            ready = true;
            CompanionVocation::provider.store(Provide);
            events.ScheduleEvent(1, std::chrono::seconds(30));
            LOG_INFO("module.pbc", "Vocations active: 31 Horde profiles, {} saved choices", records.size());
        }
        catch (std::exception const& error)
        {
            LOG_ERROR("module.pbc", "Vocations disabled: {}", error.what());
        }
    }
    void OnUpdate(uint32 diff) override
    {
        events.Update(diff);
        if (!events.ExecuteEvent())
            return;
        events.ScheduleEvent(1, std::chrono::seconds(30));
        std::vector<ObjectGuid> candidates;
        {
            std::shared_lock<std::shared_mutex> lock(*HashMapHolder<Player>::GetLock());
            for (auto const& entry : ObjectAccessor::GetPlayers())
                candidates.push_back(entry.first);
        }
        {
            std::set<std::string> present;
            for (auto guid : candidates)
                if (auto* bot = ObjectAccessor::FindPlayer(guid))
                    if (bot->GetSession() && bot->GetSession()->IsHeadless())
                        present.insert(Key(bot));
            std::lock_guard<std::mutex> lock(stateMutex);
            for (auto it = introduced.begin(); it != introduced.end();)
                if (!present.count(*it))
                    it = introduced.erase(it);
                else
                    ++it;
        }
        for (auto guid : candidates)
        {
            auto* bot = ObjectAccessor::FindPlayer(guid);
            auto* ai = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
            auto* master = ai ? ai->GetMaster() : nullptr;
            if (!Authorized(master, bot) || bot->isMoving() || master->isMoving())
                continue;
            auto saved = Read(bot);
            if (saved.is_null())
            {
                auto tabs = AiFactory::GetPlayerSpecTabs(bot);
                if (tabs[0] + tabs[1] + tabs[2] && Begin(master, bot, false, false))
                    saved = Read(bot);
            }
            if (!saved.is_null())
            {
                // A player may have assigned talents after starting the profession discussion.
                auto tabs = AiFactory::GetPlayerSpecTabs(bot);
                if (saved.at("tab").get<int>() < 0 && tabs[0] + tabs[1] + tabs[2])
                {
                    saved["tab"] = int(AiFactory::GetPlayerSpecTab(bot));
                    if (saved.value("stage", "") == "spec")
                        saved["stage"] = "professions";
                    if (saved.value("topic", "") == "spec")
                        saved.erase("topic");
                    if (!Save(bot, saved))
                        continue;
                    ai->ResetStrategies();
                }
                if (saved.at("tab").get<int>() >= 0 && bot->GetFreeTalentPoints())
                {
                    auto previous = bot->GetFreeTalentPoints();
                    PlayerbotFactory(bot, bot->GetLevel()).InitTalentsTree(true, true, false);
                    if (bot->GetFreeTalentPoints() != previous)
                        ai->ResetStrategies();
                }
                bool resume = false;
                {
                    std::lock_guard<std::mutex> lock(stateMutex);
                    resume = !saved.value("paused", false) && DiscussionTopic(saved) != "done" &&
                        !introduced.count(Key(bot));
                }
                if (resume && !OtherProposal(master->GetSession()->GetAccountId(), Key(bot)))
                    Propose(master, bot, saved);
                continue;
            }
            bool occupied = false;
            for (auto const& skill : catalog.at("skills").items())
                occupied = occupied || bot->HasSkill(std::stoul(skill.key()));
            occupied = occupied || OtherProposal(master->GetSession()->GetAccountId(), Key(bot));
            if (!occupied)
                Begin(master, bot);
        }
    }
};
}

bool PBC_VocationChat(Player* player, Player* bot, std::string const& message)
{
    if (!Authorized(player, bot))
        return false;
    std::string utterance = Normalize(message);
    auto name = Normalize(bot->GetName());
    auto offset = utterance.find(name);
    if (offset != std::string::npos &&
        (offset == 0 || utterance[offset - 1] == ' ' || utterance[offset - 1] == ',') &&
        (offset + name.size() == utterance.size() || utterance[offset + name.size()] == ' ' ||
            utterance[offset + name.size()] == ',' || utterance[offset + name.size()] == ':'))
        utterance.erase(offset, name.size());
    std::string input = Normalize(utterance);
    while (!input.empty() && (input.front() == ',' || input.front() == ':'))
        input = Normalize(input.substr(1));
    auto const words = PBCVocationPolicy::Words(input);
    bool trainingStatus = false;
    for (auto const* phrase : {"qu as tu appris", "tu as appris quoi", "as tu appris quelque chose",
        "ou en est ta formation", "pourquoi tu n apprends rien", "as tu termine ta formation"})
        trainingStatus = trainingStatus || words == PBCVocationPolicy::Words(phrase);
    if (trainingStatus)
    {
        auto const status = GetCompanionTrainingSnapshot(bot->GetGUID());
        std::string answer;
        if (status.status == "budget_insufficient")
            answer = "Les leçons accessibles dépassent ce que je peux dépenser en gardant ma réserve.";
        else if (status.status == "no_eligible_lesson")
            answer = "Je n'ai trouvé aucune leçon accessible dans cette halte pour la formation demandée.";
        else if (status.status == "learning_failed")
            answer = "J'ai tenté un apprentissage, mais la compétence n'est pas acquise. "
                "Je ne le compte pas comme réussi.";
        else if (status.status == "verifying")
            answer = "L'apprentissage est en cours de vérification ; je ne peux pas encore annoncer une réussite.";
        else if (status.status == "travelling" || status.status == "requested")
            answer = "Ma formation est prévue ou en cours de déplacement ; "
                "je n'ai pas encore d'acquis confirmé pour cette visite.";
        else if (status.status == "interrupted" || status.status == "interrupted_or_unreachable")
            answer = "J'ai interrompu ma visite ou n'ai pas pu atteindre le maître.";
        else if (status.learned)
            answer = "J'ai confirmé " + std::to_string(status.learned) +
                " apprentissage(s) pendant ma dernière visite.";
        else if (status.status == "visit_finished")
            answer = "La visite est terminée, mais elle n'a apporté aucune nouvelle compétence confirmée.";
        else
            answer = "Je n'ai pas de compte rendu récent d'apprentissage à te donner.";
        Say(player, bot, answer);
        return true;
    }
    auto activityIntent = PBCVocationPolicy::ActivityIntent(
        message.find('?') == std::string::npos ? input : input + "?");
    if (!activityIntent.empty())
    {
        if (activityIntent == "training_professions")
        {
            auto const vocation = Read(bot);
            bool chosen = !vocation.is_null() && vocation.at("professions")[0].get<uint32>() != 0;
            for (auto const& skill : catalog.at("skills").items())
                chosen = chosen || bot->HasSkill(std::stoul(skill.key()));
            if (!chosen)
                return Begin(player, bot, true, true, true);
        }
        SetCompanionNarrativeFocus(bot->GetGUID(), activityIntent);
        if (activityIntent == "follow")
            CancelCompanionErrands(GET_PLAYERBOT_AI(bot));
        else if (activityIntent.starts_with("training"))
            RequestCompanionTraining(GET_PLAYERBOT_AI(bot));
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            proposals.erase(Key(bot));
        }
        Say(player, bot, activityIntent == "follow" ? "Je laisse mes apprentissages de côté pour le moment." :
            activityIntent == "training_professions" ?
                "Je m'occupe des apprentissages de mes métiers choisis, si une leçon m'est accessible en ville." :
            activityIntent == "training_class" ?
                "Je m'occupe de ma formation de classe, si une leçon m'est accessible en ville." :
            activityIntent == "training" ?
                "Je donne la priorité à mes apprentissages en ville, si une leçon m'est accessible." :
                "Je reprends mes habitudes et les apprentissages possibles pendant nos haltes.");
        return true;
    }
    auto profile = Profile(bot);
    std::vector<std::vector<std::string>> specAliases;
    for (int index = 0; index < 3; ++index)
        specAliases.push_back({Spec(bot, index)});
    bool namesSpec = std::any_of(specAliases.begin(), specAliases.end(), [&](auto const& aliases)
    {
        return PBCVocationPolicy::IncludesAlias(PBCVocationPolicy::Words(input), aliases.front());
    });
    bool explicitSpec = namesSpec && !PBCVocationPolicy::Professions(input) &&
        !PBCVocationPolicy::Confirmation(input) &&
        !(input.size() == 1 && input[0] >= '0' && input[0] <= '9') &&
        PBCVocationPolicy::Select(message.find('?') == std::string::npos ? input : input + "?",
            specAliases, profile.at("preferred").get<int>()) >= 0;
    bool const topic = PBCVocationPolicy::Topic(input) || explicitSpec;
    auto record = Read(bot);
    if (topic)
    {
        if (!Begin(player, bot, PBCVocationPolicy::Professions(input), false, true))
            return false;
        record = Read(bot);
    }
    if (record.is_null() || record.value("paused", false) || DiscussionTopic(record) == "done")
        return false;
    if (input == "plus tard" || input == "laisse tomber" || input == "on verra plus tard" ||
        input == "pas maintenant" || input == "nous en reparlerons")
    {
        record["paused"] = true;
        if (Save(bot, record))
            Say(player, bot, "Nous en reparlerons quand tu le souhaiteras.");
        return true;
    }
    bool spec = DiscussionTopic(record) == "spec";
    std::vector<std::vector<std::string>> aliases;
    int count = spec ? 3 : int(profile.at("pairs").size());
    for (int index = 0; index < count; ++index)
    {
        aliases.push_back({spec ? Spec(bot, index) : Pair(profile.at("pairs").at(index))});
        if (!spec)
        {
            for (auto skill : profile.at("pairs").at(index))
                aliases.back().push_back(catalog.at("skills").at(std::to_string(skill.get<uint32>())));
            if (profile.at("pairs").at(index).at(0) == 165)
                aliases.back().push_back("travaille les peaux");
        }
    }
    int preferred = spec ? (record.value("tab", -1) >= 0 ? record.at("tab").get<int>() :
        profile.at("preferred").get<int>()) : 0;
    int selected = PBCVocationPolicy::Select(
        message.find('?') == std::string::npos ? input : input + "?", aliases, preferred);
    if (message.find('?') == std::string::npos && PBCVocationPolicy::Confirmation(input))
    {
        if (!Awaiting(bot))
        {
            Propose(player, bot, record);
            return true;
        }
        selected = preferred;
    }
    if (selected == -2)
    {
        Say(player, bot, "Ce métier peut accompagner plusieurs autres savoir-faire. Avec lequel l'associerais-tu ?");
        Propose(player, bot, record);
        return true;
    }
    if (selected < 0)
    {
        if (topic || (input.size() == 1 && input[0] >= '0' && input[0] <= '9'))
        {
            Propose(player, bot, record);
            return true;
        }
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            proposals.erase(Key(bot));
        }
        return false;
    }
    if (spec)
    {
        auto tabs = AiFactory::GetPlayerSpecTabs(bot);
        if (tabs[0] + tabs[1] + tabs[2] && int(AiFactory::GetPlayerSpecTab(bot)) != selected)
        {
            Say(player, bot, "J'ai déjà développé une autre voie. Je ne vais pas effacer cet apprentissage : il faut d'abord organiser ma respécialisation.");
            return true;
        }
        record["tab"] = selected;
        record["stage"] = record.at("professions").at(0) == 0 ? "professions" : "done";
        record.erase("topic");
    }
    else
    {
        auto pair = profile.at("pairs").at(selected);
        for (auto const& skill : catalog.at("skills").items())
        {
            uint32 id = std::stoul(skill.key());
            if (bot->HasSkill(id) && pair.at(0) != id && pair.at(1) != id)
            {
                Say(player, bot, "Ce choix remplacerait un métier que je connais déjà. Je conserve mes acquis ; nous devons régler ce changement séparément.");
                return true;
            }
        }
        record["professions"] = pair;
        record["stage"] = "done";
        if (record.value("tab", -1) < 0)
            record["topic"] = "spec";
        else
            record.erase("topic");
    }
    if (!Save(bot, record))
    {
        ChatHandler(player->GetSession()).SendSysMessage("Choix non enregistré : erreur de sauvegarde des vocations.");
        return true;
    }
    std::vector<uint64_t> owners = {player->GetGUID().GetCounter(), bot->GetGUID().GetCounter()};
    PBC_AppendHistoryMessage(player->GetGUID().GetCounter(), CHAT_MSG_WHISPER, message, owners);
    if (spec)
    {
        Say(player, bot, "Entendu. Je vais développer la voie " + Spec(bot, selected) + ".");
        PlayerbotFactory(bot, bot->GetLevel()).InitTalentsTree(true, true, false);
        GET_PLAYERBOT_AI(bot)->ResetStrategies();
    }
    Propose(player, bot, record);
    return true;
}

bool PBC_VocationConversation(Player* player, std::vector<Player*> const& bots, std::string const& message)
{
    std::vector<Player*> named;
    std::vector<Player*> eligible;
    std::vector<Player*> pending;
    auto words = PBCVocationPolicy::Words(message);
    for (auto* bot : bots)
    {
        if (!bot)
            continue;
        if (PBCVocationPolicy::Has(words, bot->GetName()))
            named.push_back(bot);
        if (!Authorized(player, bot))
            continue;
        eligible.push_back(bot);
        if (Awaiting(bot))
            pending.push_back(bot);
    }
    // Never apply one player's sentence to several companions.
    if (!named.empty())
        return named.size() == 1 && PBC_VocationChat(player, named.front(), message);
    if (pending.size() == 1)
        return PBC_VocationChat(player, pending.front(), message);
    return pending.empty() && eligible.size() == 1 && PBC_VocationChat(player, eligible.front(), message);
}

std::string PBC_VocationContext(Player* bot)
{
    auto record = Read(bot);
    auto context = record.is_null() ? Json::object() : record;
    context.erase("account");
    if (!record.is_null() && record.value("tab", -1) >= 0)
        context["voie_souhaitee"] = Spec(bot, record.at("tab"));
    if (!record.is_null() && record.at("professions").at(0) != 0)
        context["metiers_souhaites"] = Pair(record.at("professions"));
    auto tabs = AiFactory::GetPlayerSpecTabs(bot);
    context["faits_observes"] = {{"niveau", bot->GetLevel()}, {"argent_cuivre", bot->GetMoney()},
        {"points_talents_disponibles", bot->GetFreeTalentPoints()},
        {"points_par_arbre", {tabs[0], tabs[1], tabs[2]}}};
    auto& facts = context["faits_observes"];
    facts["priorite_actuelle"] = GetCompanionNarrativeFocus(bot->GetGUID());
    facts["metiers_appris"] = Json::array();
    for (auto const& skill : catalog.at("skills").items())
    {
        uint32 id = std::stoul(skill.key());
        if (bot->HasSkill(id))
            facts["metiers_appris"].push_back({{"nom", skill.value()},
                {"niveau", bot->GetBaseSkillValue(id)}, {"maximum", bot->GetPureMaxSkillValue(id)}});
    }
    auto activity = GetCompanionTrainingSnapshot(bot->GetGUID());
    if (!activity.status.empty())
        facts["formation_recente"] = {{"etat", activity.status}, {"maitre", activity.trainer},
            {"sort", activity.spell}, {"lecons_apprises_pendant_visite", activity.learned},
            {"anciennete_secondes", activity.ageSeconds}};
    return "\n[VOCATION ET FAITS DE JEU VERIFIES]\n" +
        context.dump() + "\nLes choix sont enregistres uniquement par le dialogue de vocation. "
        "Les voies et metiers souhaites sont des intentions. Seuls faits_observes attestent les acquis. "
        "travelling/verifying ne prouvent pas un apprentissage ; visit_finished peut contenir zero lecon. "
        "budget_insufficient signifie que le budget avec reserve ne suffit pas. "
        "learning_failed/interrupted indiquent un echec ou une interruption, jamais un succes. "
        "Explique naturellement ces faits si le joueur te questionne, sans inventer de noms a partir des identifiants. "
        "Ne pretends pas avoir valide ou appris un metier dans une reponse libre. "
        "Les questions naturelles sur les metiers ou la specialisation ouvrent ce dialogue. "
        "Le compagnon propose, le joueur accepte ou discute ; aucune commande technique a demander.\n";
}

void AddPBCVocationScripts()
{
    new VocationWorld();
    new VocationCommands();
}
