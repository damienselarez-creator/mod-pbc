#ifndef PBC_NARRATIVE_POLICY_H
#define PBC_NARRATIVE_POLICY_H

#include "pbc_json.h"
#include <set>
#include <stdexcept>

// Pure validation: missing coverage may be repaired, invented references may not.
inline pbc_json PBC_MissingNarrativeSources(pbc_json const& batch, pbc_json const& response)
{
    std::set<std::string> expected;
    std::set<std::string> covered;
    for (auto const& event : batch.at("events"))
        expected.insert(event.at("id").get<std::string>());
    auto collect = [&](pbc_json const& refs)
    {
        if (!refs.is_array() || refs.size() > 48)
            throw std::runtime_error("Invalid narrative references");
        for (auto const& ref : refs)
        {
            auto id = ref.get<std::string>();
            if (!expected.count(id))
                throw std::runtime_error("Invented narrative reference");
            covered.insert(id);
        }
    };
    if (!response.at("chunks").is_array() || response.at("chunks").size() > 12)
        throw std::runtime_error("Invalid narrative chunks");
    for (auto const& chunk : response.at("chunks"))
        collect(chunk.at("source_event_ids"));
    collect(response.at("routine_event_ids"));
    pbc_json missing = pbc_json::array();
    for (auto const& id : expected)
        if (!covered.count(id))
            missing.push_back(id);
    return missing;
}

#endif
