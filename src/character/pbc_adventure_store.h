#ifndef PBC_ADVENTURE_STORE_H
#define PBC_ADVENTURE_STORE_H

#include "pbc_history_journal.h"
#include "pbc_json.h"
#include <map>
#include <set>

// Caller serializes access. The journal is authoritative; exports are projections.
class PBC_AdventureStore
{
public:
    explicit PBC_AdventureStore(std::filesystem::path const& path);
    std::string Begin(uint64_t player, uint64_t companion, std::string const& playerName,
        std::string const& companionName, std::string const& card);
    bool BeginPersonal(uint64_t character, std::string const& name, std::string const& card);
    bool CloseIfActive(uint64_t character);
    void CloseAllActive();
    std::vector<uint64_t> Owners(uint64_t companion) const;
    bool Record(uint64_t player, pbc_json event);
    bool RecordMilestone(uint64_t player, pbc_json event, std::string const& key, uint64_t interval);
    std::string Close(uint64_t player);
    std::string Focus(uint64_t player) const;
    pbc_json Batch(uint64_t player) const;
    bool Commit(pbc_json const& batch, pbc_json const& response);
    pbc_json Status(uint64_t player) const;
    pbc_json Export(uint64_t player) const;
    std::string Context(uint64_t player, std::string const& query) const;
    uint64_t Companion(uint64_t player) const;
    uint64_t Owner(uint64_t companion) const;
    bool Active(uint64_t player) const;
    bool Managed(uint64_t guid) const;
    std::vector<uint64_t> PendingPlayers() const;

private:
    PBC_HistoryJournal journal_;
    std::map<std::string, pbc_json> sessions_;
    std::map<uint64_t, std::string> active_;
    std::map<uint64_t, uint64_t> companions_;
    std::map<std::string, pbc_json> chunks_;
    std::set<std::string> processed_;
    std::set<std::string> commits_;
    std::map<uint64_t, std::map<std::string, pbc_json>> relationships_;
    std::map<uint64_t, std::map<std::string, uint64_t>> milestones_;
    std::vector<std::string> order_;
    bool faulted_ = false;
    std::string Persist(pbc_json const& operation, uint64_t player);
    void Apply(std::string const& token, pbc_json const& operation, uint64_t timestamp);
};

#endif
