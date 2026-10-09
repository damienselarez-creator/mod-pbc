#ifndef PBC_VOCATION_STORE_H
#define PBC_VOCATION_STORE_H
#include "pbc_json.h"
#include <array>

inline bool PBC_ValidVocations(pbc_json const& data, pbc_json const& catalog)
{
    try
    {
        if (data.at("version") != 1 || !data.at("characters").is_object() ||
            data.at("characters").size() > 4096)
            return false;
        for (auto const& item : data.at("characters"))
        {
            if (!item.at("account").is_number_unsigned() || !item.at("race").is_number_unsigned() ||
                !item.at("class").is_number_unsigned() || !item.at("tab").is_number_integer() ||
                !item.at("stage").is_string() || !item.at("professions").is_array() ||
                item.at("professions").size() != 2 ||
                (item.contains("paused") && !item.at("paused").is_boolean()) ||
                (item.contains("topic") && (!item.at("topic").is_string() ||
                    (item.at("topic") != "spec" && item.at("topic") != "professions"))))
                return false;
            auto tab = item.at("tab").get<int>();
            auto stage = item.at("stage").get<std::string>();
            auto skills = item.at("professions").get<std::array<uint32_t, 2>>();
            if (tab < -1 || tab > 2 || (stage != "spec" && stage != "professions" && stage != "done") ||
                (stage == "spec" && tab != -1) ||
                (stage != "done" && skills != std::array<uint32_t, 2>{0, 0}))
                return false;
            bool found = false;
            for (auto const& profile : catalog.at("profiles"))
                if (profile.at("race") == item.at("race") && profile.at("class") == item.at("class"))
                {
                    found = stage != "done";
                    for (auto const& pair : profile.at("pairs"))
                        found = found || pair == item.at("professions");
                    break;
                }
            if (!found)
                return false;
        }
        return true;
    }
    catch (pbc_json::exception const&)
    {
        return false;
    }
}
#endif
