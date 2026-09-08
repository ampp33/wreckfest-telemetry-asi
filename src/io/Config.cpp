#include "io/Config.h"

#include <fstream>

#include "json.hpp"

namespace wreckfest_telemetry {

ApiConfig LoadConfig(const std::wstring& path) {
    ApiConfig config;
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return config;

    nlohmann::json j;
    try {
        in >> j;
    } catch (const nlohmann::json::exception&) {
        return config;
    }
    if (!j.is_object()) return config;

    config.api_key = j.value("api_key", "");
    config.supabase_url = j.value("supabase_url", "");
    config.supabase_anon_key = j.value("supabase_anon_key", "");
    config.debug_console = j.value("debug_console", false);
    return config;
}

bool ApiConfigComplete(const ApiConfig& config) {
    return !config.api_key.empty() && !config.supabase_url.empty() && !config.supabase_anon_key.empty();
}

}  // namespace wreckfest_telemetry
