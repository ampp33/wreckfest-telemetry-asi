#pragma once

#include <string>

namespace wreckfest_telemetry {

struct ApiConfig {
    std::string api_key;
    std::string supabase_url;
    std::string supabase_anon_key;
};

// Reads the API key from a local api-key.txt: the first non-blank line,
// trimmed of surrounding whitespace (so blank lines, CRLF endings, and
// stray spaces are all tolerated). A missing/unreadable file or a
// blank/whitespace-only one both yield "".
std::string LoadApiKey(const std::wstring& path);

// {supabase_url, supabase_anon_key} as fetched from a remote
// config.default.json.
struct RemoteApiDefaults {
    std::string supabase_url;
    std::string supabase_anon_key;
};

// Fetches RemoteApiDefaults from `url` (e.g.
// https://wfracelog.com/plugin/config.default.json). On ANY failure --
// site unreachable, non-2xx response, unparsable body, or the fields
// simply missing from it -- both fields come back empty. Combined with
// ApiConfigComplete() below, that disables *posting* for the rest of this
// run, but races are still queued to pending_races.jsonl rather than
// dropped (see WorkerThread's EmitRace) -- the backlog gets drained on a
// later run once config loads successfully. Never throws.
RemoteApiDefaults FetchRemoteApiDefaults(const std::wstring& url, double timeoutSeconds = 10.0);

bool ApiConfigComplete(const ApiConfig& config);

}  // namespace wreckfest_telemetry
