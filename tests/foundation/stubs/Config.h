#pragma once
#include <string>
inline std::string testCaFile;
struct Config {
 template<class T> T GetOption(const char*, const char*) { return testCaFile; }
};
inline Config testConfig;
inline Config* sConfigMgr = &testConfig;
