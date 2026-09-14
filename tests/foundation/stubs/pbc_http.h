#pragma once
#include <string>
#include <vector>
#include <utility>
class PBC_HttpClient {
public:
 PBC_HttpClient();
 void SetTimeoutSeconds(int seconds);
 std::string Post(const std::string&, const std::string&, const std::vector<std::pair<std::string,std::string>>& = {});
private:
 int m_timeoutSec;
};
