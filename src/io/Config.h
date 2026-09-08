#pragma once

#include <string>

namespace wreckfest_telemetry {

struct ApiConfig {
    std::string api_key;
    std::string supabase_url;
    std::string supabase_anon_key;
};

// Loads {api_key, supabase_url, supabase_anon_key} from a JSON file.
// Missing/invalid config returns a default (all-empty) ApiConfig rather
// than failing -- API posting is then silently disabled, matching the
// Python tool's fail-quiet contract.
ApiConfig LoadConfig(const std::wstring& path);

bool ApiConfigComplete(const ApiConfig& config);

}  // namespace wreckfest_telemetry
