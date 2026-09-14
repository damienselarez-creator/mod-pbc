#pragma once
#include <iostream>
#include <sstream>
inline std::string testLastError;
enum class PBC_LogLevel { PBC_DEBUG, PBC_WARNING, PBC_ERROR };
template<class... T> void PBC_Log(PBC_LogLevel level, const char* message, T&&... args) {
 if(level == PBC_LogLevel::PBC_ERROR) {
  std::ostringstream output; output << message; ((output << " " << args),...);
  testLastError=output.str(); std::cerr << testLastError << std::endl;
 }
}
