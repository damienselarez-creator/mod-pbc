#ifndef MOD_PBC_MEMORY_PARSER_H
#define MOD_PBC_MEMORY_PARSER_H

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

struct PBC_ParsedMemory
{
    uint8_t importance;
    std::string text;
};

// Validate the entire response before allowing any persistence. Empty output,
// prose, fences, invalid scores and partially malformed batches fail closed.
inline std::vector<PBC_ParsedMemory> PBC_ValidateMemoryLines(const std::string& text)
{
    std::vector<PBC_ParsedMemory> memories;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line))
    {
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos)
            continue;
        line = line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
        if (line.front() != '[')
            return {};
        const auto close = line.find(']');
        if (close == std::string::npos || close + 1 >= line.size()
            || (line[close + 1] != ' ' && line[close + 1] != '\t'))
            return {};
        const std::string score = line.substr(1, close - 1);
        if (score != "10" && (score.size() != 1 || score[0] < '1' || score[0] > '9'))
            return {};
        const auto start = line.find_first_not_of(" \t", close + 1);
        if (start == std::string::npos)
            return {};
        std::string memory = line.substr(start);
        // Match the prompt's maximum count and the SQL TEXT byte capacity.
        if (memories.size() == 30 || memory.size() > 65535)
            return {};
        memories.push_back({static_cast<uint8_t>(std::stoi(score)), memory});
    }
    return memories;
}

#endif
