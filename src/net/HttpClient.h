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

// POSTs one payload. api_key is injected here, at send time -- callers must
// never persist a payload that already has it baked in (the queue file
// gets the payload before this call, not after).
//
// Response handling: the backend answers HTTP 200 even for validation
// failures -- the real success signal is the in-body JSON "success" field.
// A non-2xx status is retryable only if >=500 or in {408, 429}; a
// non-JSON/non-object body is treated as retryable (proxy/captive-portal
// misbehavior, not a real rejection). Never throws.
PostResult PostPayload(const ApiConfig& config, nlohmann::json payload, double timeoutSeconds = 10.0);

}  // namespace wreckfest_telemetry
