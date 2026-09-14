#ifdef _WIN32
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#include "pbc_http.h"
#include "pbc_log.h"
#include "pbc_utils.h"

// Rename httplib namespace to avoid ODR violations with other modules.
#define httplib pbc_httplib
#include <httplib.h>
#include <regex>
#include <vector>
#include <utility>
#include <algorithm>
#include "Config.h"

PBC_HttpClient::PBC_HttpClient() : m_timeoutSec(120) {}

void PBC_HttpClient::SetTimeoutSeconds(int seconds)
{
    m_timeoutSec = std::clamp(seconds, 1, 120);
}

std::string PBC_HttpClient::Post(const std::string& url,
                                 const std::string& jsonData,
                                 const std::vector<std::pair<std::string, std::string>>& extraHeaders)
{
    try
    {
        // Parse URL  e.g. https://api.deepseek.com/v1/chat/completions
        static const std::regex urlRe(R"(^(https?)://([^:/]+)(?::(\d+))?(/.*)?$)");
        std::smatch m;
        if (!std::regex_match(url, m, urlRe))
        {
            PBC_Log(PBC_LogLevel::PBC_ERROR, "Invalid URL: {}", url);
            return "";
        }

        std::string proto  = m[1].str();
        std::string host   = m[2].str();
        std::string path   = m[4].matched ? m[4].str() : "/";
        int         port   = proto == "https" ? 443 : 80;
        if (m[3].matched) port = std::stoi(m[3].str());
        if (port < 1 || port > 65535 || host.find_first_of("@?#\\\r\n") != std::string::npos)
            return "";

        PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP {} {}:{}{}", proto, host, port, path);

        httplib::Headers headers = {
            {"Accept",       "application/json"},
            {"User-Agent",   "AzerothCore-PBC/1.0"}
        };
        for (const auto& hdr : extraHeaders)
            headers.emplace(hdr.first, hdr.second);

        httplib::Result res;

        if (proto == "https")
        {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
            httplib::SSLClient cli(host, port);
            cli.enable_server_certificate_verification(true);
            std::string caFile = sConfigMgr->GetOption<std::string>("PBC.HttpCaFile", "");
            if (!caFile.empty())
                cli.set_ca_cert_path(caFile);
            cli.set_follow_location(false);
            cli.set_max_timeout(std::chrono::seconds(m_timeoutSec));
            cli.set_payload_max_length(2 * 1024 * 1024);
            cli.set_connection_timeout(std::min(m_timeoutSec, 10));
            cli.set_read_timeout(m_timeoutSec);
            cli.set_write_timeout(m_timeoutSec);
            res = cli.Post(path, headers, jsonData, "application/json");
            if (!res)
                PBC_Log(PBC_LogLevel::PBC_ERROR, "TLS certificate verification result: {}", res.ssl_backend_error());
#else
            PBC_Log(PBC_LogLevel::PBC_ERROR, "HTTPS requested but OpenSSL not compiled in.");
            return "";
#endif
        }
        else
        {
            httplib::Client cli(host, port);
            cli.set_follow_location(false);
            cli.set_max_timeout(std::chrono::seconds(m_timeoutSec));
            cli.set_payload_max_length(2 * 1024 * 1024);
            cli.set_connection_timeout(std::min(m_timeoutSec, 10));
            cli.set_read_timeout(m_timeoutSec);
            cli.set_write_timeout(m_timeoutSec);
            res = cli.Post(path, headers, jsonData, "application/json");
        }

        if (!res)
        {
            PBC_Log(PBC_LogLevel::PBC_ERROR, "HTTP request failed for {}:{}{}: {}", host, port, path,
                httplib::to_string(res.error()));
            return "";
        }
        if (res->status != 200)
        {
            PBC_Log(PBC_LogLevel::PBC_ERROR, "HTTP {} from {}:{}{}", res->status, host, port, path);
            PBC_Log(PBC_LogLevel::PBC_DEBUG, "Response body: {}", PBC_SanitizeForFmt(res->body));
            return "";
        }

        PBC_Log(PBC_LogLevel::PBC_DEBUG, "HTTP OK, body length={}", res->body.size());

        return res->body;
    }
    catch (const std::exception& ex)
    {
        PBC_Log(PBC_LogLevel::PBC_ERROR, "HTTP exception: {}", ex.what());
        return "";
    }
}
