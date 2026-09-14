#pragma once
#include <string>
inline void PBC_CleanUnknownTokens(std::string&) {}
inline std::string PBC_SanitizeForFmt(const std::string& s) { return s; }
inline std::string PBC_TruncateForDebug(const std::string& s) { return s; }
