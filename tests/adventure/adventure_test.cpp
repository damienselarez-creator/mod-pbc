#include "pbc_adventure_store.h"
#include "pbc_narrative_policy.h"
#include <chrono>
#include <iostream>
#include <stdexcept>

void Check(bool condition, char const* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

template<class F> void Reject(F function)
{
    bool rejected = false;
    try
    {
        function();
    }
    catch (std::exception const&)
    {
        rejected = true;
    }
    Check(rejected, "Invalid operation accepted");
}

pbc_json Response(pbc_json const& batch, std::string const& id = "")
{
    pbc_json refs = pbc_json::array();
    for (auto const& event : batch.at("events"))
        refs.push_back(event.at("id"));
    return {{"chunks", pbc_json::array({{{"id", id}, {"type", "episode"},
        {"titre", "Une enquete inachevee"}, {"resume", "Le joueur a accepte une mission."},
        {"role_joueur", "Accepte la mission"}, {"role_compagnon", "Presence constatee"},
        {"regard", "Interpretation prudente"}, {"relation", ""}, {"etat", "ouvert"},
        {"themes", pbc_json::array({"enquete"})}, {"source_event_ids", refs}}})},
        {"routine_event_ids", pbc_json::array()}};
}

int main()
{
    auto root = std::filesystem::temp_directory_path() / ("pbc-adventure-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try
    {
        {
            PBC_AdventureStore narrative(root / "narrative");
            narrative.BeginPersonal(3, "Winifred", "Fiche");
            pbc_json event = {{"kind", "training_completed"}, {"actor_guid", 2},
                {"companion_present", true}, {"spell_id", 42}};
            Check(narrative.RecordMilestone(3, event, "training:42", 3600), "First milestone missing");
            Check(!narrative.RecordMilestone(3, event, "training:42", 3600), "Routine milestone flooded journal");
            narrative.Record(3, {{"kind", "focus_chosen"}, {"focus", "craft"}});
            Check(narrative.Focus(3) == "craft", "Focus not retained");
            narrative.Close(3);
            auto batch = narrative.Batch(3);
            auto reply = Response(batch);
            reply["chunks"][0]["role_joueur"] = "";
            auto omitted = reply;
            omitted["chunks"][0]["source_event_ids"].erase(0);
            Check(PBC_MissingNarrativeSources(batch, omitted).size() == 1, "Repair lost missing reference");
            auto invented = reply;
            invented["routine_event_ids"].push_back("invented");
            Reject([&] { PBC_MissingNarrativeSources(batch, invented); });
            reply["relationship_updates"] = pbc_json::array({{{"target_guid", 2},
                {"attitude", "A observe un apprentissage ; aucune intimite presumee."},
                {"source_event_ids", pbc_json::array({batch.at("events")[0].at("id")})}}});
            auto inaccessible = reply;
            inaccessible["relationship_updates"][0]["target_guid"] = 999;
            Reject([&] { narrative.Commit(batch, inaccessible); });
            auto self = reply;
            self["relationship_updates"][0]["target_guid"] = 3;
            Reject([&] { narrative.Commit(batch, self); });
            Check(narrative.Commit(batch, reply), "Narrative commit failed");
            Check(narrative.Export(3).at("relationships").size() == 1, "Relation absent");
            Check(narrative.Context(3, "apprentissage").find("RELATIONS EVOLUTIVES") != std::string::npos,
                "Relation unavailable to dialogue");
        }
        {
            PBC_AdventureStore narrative(root / "narrative");
            Check(narrative.Focus(3) == "craft", "Focus lost on restart");
            Check(narrative.Export(3).at("relationships").size() == 1, "Relations lost on replay");
            Check(narrative.Export(999).at("relationships").empty(), "Other identity sees relations");
            narrative.BeginPersonal(3, "Winifred", "Fiche");
            Check(!narrative.RecordMilestone(3, {{"kind", "training_completed"}}, "training:42", 3600),
                "Milestone dedup lost on replay");
        }
        {
            PBC_AdventureStore all(root / "all-characters");
            for (uint64_t guid : {3, 17, 999})
            {
                Check(all.BeginPersonal(guid, "Compagnon", "Fiche"), "Personal session did not open");
                all.Record(guid, {{"kind", "quest_accepted"}, {"quest_id", guid}});
            }
            // Simulate interruption without a logout hook for any of the characters.
        }
        {
            PBC_AdventureStore all(root / "all-characters");
            all.CloseAllActive();
            all.CloseAllActive();
            Check(all.PendingPlayers().size() == 3, "Recovery did not retain every character's pending memory");
            for (uint64_t guid : {3, 17, 999})
            {
                Check(!all.Active(guid), "Recovery left an interrupted session open");
                auto batch = all.Batch(guid);
                Check(batch.at("events").size() == 1, "Recovery mixed character histories");
                Check(batch.at("events")[0].at("quest_id") == guid, "Another character's event leaked");
                Check(all.BeginPersonal(guid, "Compagnon", "Fiche"), "Reconnect failed after recovery");
                Check(all.Batch(guid) == batch, "Reconnect changed the pending memory batch");
            }
            all.CloseAllActive();
        }
        {
            PBC_AdventureStore personal(root / "personal");
            personal.Begin(5, 5, "Elvidia", "Elvidia", std::string(52490, 'x'));
            Reject([&] { personal.Begin(7, 5, "Other", "Elvidia", ""); });
            personal.Record(5, {{"kind", "quest_accepted"}, {"quest_id", 42}, {"control", "selfbot"}});
            personal.Record(5, {{"kind", "quest_rewarded"}, {"quest_id", 42}, {"control", "manual"}});
            Check(personal.Context(5, "quest").find("AVENTURE PERSONNELLE") != std::string::npos,
                "Personal context missing");
            personal.Close(5);
            auto batch = personal.Batch(5);
            Check(batch.at("mode") == "personal", "Personal mode missing");
            auto response = Response(batch);
            Reject([&] { personal.Commit(batch, response); });
            response["chunks"][0]["role_joueur"] = "";
            response["chunks"][0]["role_compagnon"] = "Elvidia accepte puis rend la mission";
            Check(personal.Commit(batch, response), "Personal commit failed");
        }
        {
            PBC_AdventureStore personal(root / "personal");
            Check(personal.Owner(5) == 5 && personal.Companion(5) == 5, "Personal association lost");
            Check(personal.Export(5).at("chunks").size() == 1, "Personal memory lost");
            Check(personal.Context(7, "quest").empty(), "Personal memory crossed owner boundary");
        }
        {
            PBC_AdventureStore automatic(root / "automatic");
            automatic.Begin(2, 4, "Joueur", "Antanagor", "Ancienne fiche");
            automatic.Record(2, {{"kind", "quest_accepted"}, {"quest_id", 10}});
            Check(automatic.CloseIfActive(2), "Legacy session was not sealed");
            Check(!automatic.CloseIfActive(2), "Duplicate close was not idempotent");
            auto legacy = automatic.Batch(2);
            Check(automatic.BeginPersonal(2, "Joueur", "Fiche"), "Legacy owner could not start personal memory");
            Check(automatic.Batch(2) == legacy, "Personal transition lost legacy pending memory");
            automatic.CloseIfActive(2);
            Check(automatic.BeginPersonal(4, "Antanagor", "Fiche"), "Automatic login failed");
            Check(!automatic.BeginPersonal(4, "Antanagor", "Fiche"), "Duplicate login opened another session");
            Check(automatic.Owner(4) == 4 && automatic.Owners(4).size() == 1, "Personal ownership incorrect");
            automatic.Record(4, {{"kind", "quest_accepted"}, {"actor_guid", 2},
                {"participation", "observer"}});
            automatic.CloseIfActive(4);
            auto pending = automatic.Batch(4);
            automatic.BeginPersonal(4, "Antanagor", "Fiche");
            automatic.Record(4, {{"kind", "quest_rewarded"}, {"actor_guid", 4}, {"participation", "actor"}});
            Check(automatic.Batch(4) == pending, "Rapid reconnect changed pending synthesis batch");
            Check(automatic.Batch(2).at("events").size() == 1, "Legacy events lost");
        }
        {
            PBC_AdventureStore automatic(root / "automatic");
            Check(automatic.Active(4), "Crash recovery lost open session");
            automatic.CloseIfActive(4);
            Check(automatic.Status(4).at("pending") == 2, "Shutdown recovery lost events");
            automatic.BeginPersonal(4, "Antanagor", "Fiche");
            Check(automatic.Status(4).at("events") == 0, "Previous events leaked into new login");
        }
        pbc_json first;
        std::string episode;
        {
            PBC_AdventureStore store(root);
            store.Begin(10, 5, "Joueur", "Antanagor", "Cynique, sarcastique et loyal.");
            Reject([&] { store.Begin(10, 5, "Joueur", "Antanagor", ""); });
            Reject([&] { store.Begin(11, 5, "Autre", "Antanagor", ""); });
            Check(store.Record(10, {{"kind", "quest_accepted"}, {"quest_id", 42},
                {"companion_present", true}}), "Event missing");
            Check(store.Batch(10).is_null(), "Open session synthesized too early");
            store.Close(10);
            first = store.Batch(10);
            Check(first.at("events").size() == 1, "Closed boundary incorrect");
            store.Begin(10, 5, "Joueur", "Antanagor", "Cynique");
            store.Record(10, {{"kind", "quest_rewarded"}, {"quest_id", 42}});
            Check(store.Batch(10) == first, "Next session leaked into pending batch");
            auto invalid = Response(first);
            invalid["chunks"][0]["source_event_ids"][0] = "invented";
            Reject([&] { store.Commit(first, invalid); });
            Check(store.Status(10).at("pending") == 1, "Failure lost source events");
            auto valid = Response(first);
            Check(store.Commit(first, valid), "Commit failed");
            Check(!store.Commit(first, valid), "Retry duplicated a commit");
            episode = store.Export(10).at("chunks")[0].at("id");
            Check(store.Export(10).at("chunks").size() == 1, "Duplicate memory");
        }
        {
            PBC_AdventureStore store(root);
            Check(store.Active(10), "Open session lost after restart");
            Check(store.Status(10).at("pending") == 0, "Committed events replayed");
            Check(store.Export(10).at("chunks")[0].at("id") == episode, "Memory lost after restart");
            store.Close(10);
            auto second = store.Batch(10);
            auto response = Response(second, episode);
            response["chunks"][0]["resume"] = "Le joueur a accepte puis rendu la mission.";
            store.Commit(second, response);
            auto chunk = store.Export(10).at("chunks")[0];
            Check(chunk.at("version") == 2, "Episode not updated");
            Check(chunk.at("sessions").size() == 2, "Cross-session provenance missing");
            Check(chunk.at("source_event_ids").size() == 2, "Old sources overwritten");
            Check(store.Context(999, "enquete").empty(), "Other player can read memories");
            Check(store.Context(10, "enquete").find(episode) != std::string::npos, "Retrieval missing");
            store.Begin(10, 5, "Joueur", "Antanagor", "Cynique");
            // A repeated quest is a new event, not deduplicated by quest ID.
            store.Record(10, {{"kind", "quest_accepted"}, {"quest_id", 42}});
            store.Record(10, {{"kind", "quest_rewarded"}, {"quest_id", 42}});
            store.Close(10);
            auto repeated = store.Batch(10);
            Check(repeated.at("events").size() == 2, "Repeatable quest lost");
            auto missing = Response(repeated);
            missing["chunks"][0]["source_event_ids"].erase(1);
            Reject([&] { store.Commit(repeated, missing); });
            pbc_json routine = {{"chunks", pbc_json::array()}, {"routine_event_ids", pbc_json::array()}};
            for (auto const& event : repeated.at("events"))
                routine["routine_event_ids"].push_back(event.at("id"));
            store.Commit(repeated, routine);
            Check(store.Export(10).at("chunks").size() == 1, "Routine fabricated memory");
            Check(store.PendingPlayers().empty(), "Completed session pending");
        }
        {
            PBC_AdventureStore store(root);
            Check(store.Export(10).at("chunks")[0].at("version") == 2, "Replay changed version");
            Check(store.PendingPlayers().empty(), "Replay lost routine accounting");
            store.Begin(10, 5, "Joueur", "Antanagor", "Cynique");
            for (int i = 0; i < 50; ++i)
                store.Record(10, {{"kind", "location"}, {"zone_id", i}});
            store.Close(10);
            auto batch = store.Batch(10);
            Check(batch.at("events").size() == 24, "Batch limit not respected");
            store.Commit(batch, Response(batch, episode));
            Check(store.Batch(10).at("events").size() == 24, "Batch overflow events lost");
            auto next = store.Batch(10);
            store.Commit(next, Response(next, episode));
            Check(store.Batch(10).at("events").size() == 2, "Last overflow events lost");
            Reject([&] { PBC_AdventureStore concurrent(root); });
        }
        Check(std::filesystem::weakly_canonical(root).parent_path()
            == std::filesystem::weakly_canonical(std::filesystem::temp_directory_path()), "Unsafe fixture cleanup");
        Check(root.filename().string().rfind("pbc-adventure-test-", 0) == 0, "Unsafe fixture name");
        std::filesystem::remove_all(root);
        std::cout << "PASS: recovery, boundaries, provenance, retry, arc updates, repeats, isolation, routine\n";
        return 0;
    }
    catch (std::exception const& error)
    {
        std::cerr << "FAIL: " << error.what() << " (fixtures retained at " << root << ")\n";
        return 1;
    }
}
