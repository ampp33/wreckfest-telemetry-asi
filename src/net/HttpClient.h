#pragma once

#include <string>

namespace wreckfest_telemetry {

// Outcome of a single HTTP(S) round trip. `ok` covers only the transport
// layer (URL parsing, connect, send, receive) -- a well-formed HTTP error
// response (4xx/5xx) still comes back ok == true with that status code;
// callers interpret status/body for themselves. Never throws.
struct HttpResult {
    bool ok;
    int status = 0;    // meaningful only when ok
    std::string body;  // meaningful only when ok
};

// Plain HTTP(S) GET.
HttpResult HttpGet(const std::wstring& url, double timeoutSeconds = 10.0);

// Plain HTTP(S) POST. `headers` is a raw CRLF-terminated header block (or
// empty for none), e.g. L"Content-Type: application/json\r\n"; `body` is
// sent as-is with no assumptions about its content type or shape.
HttpResult HttpPost(const std::wstring& url, const std::wstring& headers, const std::string& body,
                     double timeoutSeconds = 10.0);

}  // namespace wreckfest_telemetry
