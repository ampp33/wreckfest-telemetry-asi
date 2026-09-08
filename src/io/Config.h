#pragma once

#include <string>

namespace wreckfest_telemetry {

struct ApiConfig {
    std::string api_key;
    std::string supabase_url;
    std::string supabase_anon_key;
    // Optional "debug_console": true -- opens an AllocConsole window for
    // troubleshooting. Off by default; normal operation is silent.
    bool debug_console = false;
};

// Loads {api_key, supabase_url, supabase_anon_key, debug_console} from a
// JSON file. Missing/invalid config returns a default (all-empty,
// debug_console off) ApiConfig rather than failing -- API posting is then
// silently disabled, matching the Python tool's fail-quiet contract.
ApiConfig LoadConfig(const std::wstring& path);

bool ApiConfigComplete(const ApiConfig& config);

}  // namespace wreckfest_telemetry
