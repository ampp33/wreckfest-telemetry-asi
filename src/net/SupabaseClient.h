#pragma once

#include <string>

#include "io/Config.h"
#include "json.hpp"

namespace wreckfest_telemetry {

struct PostResult {
    bool ok;
    bool retryable;  // meaningful only when ok == false
    std::string message;
};

// POSTs one race payload to the Supabase RPC endpoint configured in
// `config`, via HttpClient's generic HttpPost. api_key is injected here,
// at send time -- callers must never persist a payload that already has
// it baked in (the queue file gets the payload before this call, not
// after).
//
// Response handling: the backend answers HTTP 200 even for validation
// failures -- the real success signal is the in-body JSON "success" field.
// A non-2xx status is retryable only if >=500 or in {408, 429}; a
// non-JSON/non-object body is treated as retryable (proxy/captive-portal
// misbehavior, not a real rejection). A malformed supabase_url is treated
// as not retryable -- retrying won't fix it. Never throws.
PostResult PostPayload(const ApiConfig& config, nlohmann::json payload, double timeoutSeconds = 10.0);

// Tells the backend the game is running (shown as "online now" on the
// site). Sends nothing but the api_key -- no server or game state -- to
// config.heartbeat_url (the heartbeat_wf1 RPC).
PostResult PostHeartbeat(const ApiConfig& config);

}  // namespace wreckfest_telemetry
