#include "net/SupabaseClient.h"

#include "net/HttpClient.h"

namespace wreckfest_telemetry {

namespace {

std::string SafeStringField(const nlohmann::json& data, const std::string& key, const std::string& fallback) {
    if (!data.contains(key)) return fallback;
    const auto& v = data[key];
    return v.is_string() ? v.get<std::string>() : v.dump();
}

PostResult ParseResponse(int status, const std::string& raw) {
    nlohmann::json data;
    try {
        data = nlohmann::json::parse(raw);
    } catch (const nlohmann::json::exception&) {
        return {false, true, "HTTP " + std::to_string(status) + ", non-JSON response"};
    }
    if (!data.is_object()) {
        return {false, true, "HTTP " + std::to_string(status) + ", unexpected response"};
    }
    if (!(status >= 200 && status < 300)) {
        bool retryable = status >= 500 || status == 408 || status == 429;
        return {false, retryable, "HTTP " + std::to_string(status) + ": " + SafeStringField(data, "error", raw)};
    }
    if (!data.value("success", false)) {
        return {false, false, "rejected: " + SafeStringField(data, "error", "unknown error")};
    }
    return {true, false, "ok"};
}

std::wstring Widen(const std::string& s) {
    return std::wstring(s.begin(), s.end());  // ASCII in practice (Supabase URLs/keys)
}

bool LooksLikeUrl(const std::string& s) {
    return s.rfind("http://", 0) == 0 || s.rfind("https://", 0) == 0;
}

}  // namespace

PostResult PostPayload(const ApiConfig& config, nlohmann::json payload, double timeoutSeconds) try {
    // HttpClient is transport-only and can't tell a malformed URL apart
    // from any other network failure -- check it here so a bad
    // supabase_url (e.g. a broken remote config) fails fast as
    // non-retryable instead of churning the retry queue forever.
    if (!LooksLikeUrl(config.supabase_url)) {
        return {false, false, "malformed supabase_url"};
    }

    payload["api_key"] = config.api_key;
    std::string body = payload.dump();
    std::wstring headers = L"Content-Type: application/json\r\napikey: " + Widen(config.supabase_anon_key) + L"\r\n";

    HttpResult res = HttpPost(Widen(config.supabase_url), headers, body, timeoutSeconds);
    if (!res.ok) return {false, true, "network error: request failed"};
    return ParseResponse(res.status, res.body);
} catch (...) {
    return {false, true, "network error: unexpected exception"};
}

}  // namespace wreckfest_telemetry
