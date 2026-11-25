#include "io/Config.h"

#include <algorithm>
#include <cctype>
#include <fstream>

#include "json.hpp"
#include "net/HttpClient.h"

namespace wreckfest_telemetry {

namespace {

std::string Trim(const std::string& s) {
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    auto begin = std::find_if(s.begin(), s.end(), notSpace);
    auto end = std::find_if(s.rbegin(), s.rend(), notSpace).base();
    if (begin >= end) return "";
    return std::string(begin, end);
}

}  // namespace

std::string LoadApiKey(const std::wstring& path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return "";

    std::string line;
    while (std::getline(in, line)) {
        std::string trimmed = Trim(line);
        if (!trimmed.empty()) return trimmed;
    }
    return "";
}

RemoteApiDefaults FetchRemoteApiDefaults(const std::wstring& url, double timeoutSeconds) {
    RemoteApiDefaults out;

    HttpResult res = HttpGet(url, timeoutSeconds);
    if (!res.ok) return out;

    // value() throws on a non-string field too, so it's inside the try.
    try {
        nlohmann::json j = nlohmann::json::parse(res.body);
        if (!j.is_object()) return out;
        out.supabase_url = j.value("supabase_url", "");
        out.supabase_anon_key = j.value("supabase_anon_key", "");
        out.heartbeat_url = j.value("heartbeat_url", "");
    } catch (const nlohmann::json::exception&) {
        return RemoteApiDefaults{};
    }
    return out;
}

bool ApiConfigComplete(const ApiConfig& config) {
    return !config.api_key.empty() && !config.supabase_url.empty() && !config.supabase_anon_key.empty();
}

}  // namespace wreckfest_telemetry
