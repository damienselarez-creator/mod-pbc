#pragma once
#include "pbc_llm.h"
#include <memory>
inline bool g_PBC_DebugShowFullRequest = false;
inline std::shared_ptr<const PBC_APIConfig> testConnection;
inline auto PBC_GetConnection(const std::string&) { return testConnection; }
