#include "net/HttpClient.h"

#include <windows.h>
#include <winhttp.h>

#include <vector>

namespace wreckfest_telemetry {

namespace {

// RAII so an early return can never leak a handle.
struct HInternetHandle {
    HINTERNET h = nullptr;
    HInternetHandle() = default;
    explicit HInternetHandle(HINTERNET handle) : h(handle) {}
    ~HInternetHandle() {
        if (h) WinHttpCloseHandle(h);
    }
    HInternetHandle(const HInternetHandle&) = delete;
    HInternetHandle& operator=(const HInternetHandle&) = delete;
    operator HINTERNET() const { return h; }
    explicit operator bool() const { return h != nullptr; }
};

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

}  // namespace

PostResult PostPayload(const ApiConfig& config, nlohmann::json payload, double timeoutSeconds) try {
    payload["api_key"] = config.api_key;
    std::string body = payload.dump();

    std::wstring wUrl = Widen(config.supabase_url);
    wchar_t hostBuf[256]{};
    wchar_t pathBuf[2048]{};
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = hostBuf;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = pathBuf;
    uc.dwUrlPathLength = 2048;
    uc.dwSchemeLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wUrl.c_str(), 0, 0, &uc)) {
        return {false, false, "malformed supabase_url"};
    }
    bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;

    HInternetHandle hSession(WinHttpOpen(L"wreckfest-telemetry-asi/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!hSession) return {false, true, "network error: WinHttpOpen failed"};

    DWORD ms = static_cast<DWORD>(timeoutSeconds * 1000.0);
    WinHttpSetTimeouts(hSession, static_cast<int>(ms), static_cast<int>(ms), static_cast<int>(ms),
                        static_cast<int>(ms));

    HInternetHandle hConnect(WinHttpConnect(hSession, uc.lpszHostName, uc.nPort, 0));
    if (!hConnect) return {false, true, "network error: WinHttpConnect failed"};

    HInternetHandle hRequest(WinHttpOpenRequest(hConnect, L"POST", uc.lpszUrlPath, nullptr, WINHTTP_NO_REFERER,
                                                 WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0));
    if (!hRequest) return {false, true, "network error: WinHttpOpenRequest failed"};

    std::wstring headers = L"Content-Type: application/json\r\napikey: " + Widen(config.supabase_anon_key) + L"\r\n";

    PostResult result;
    BOOL sent = WinHttpSendRequest(hRequest, headers.c_str(), static_cast<DWORD>(-1),
                                    const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
                                    static_cast<DWORD>(body.size()), 0);
    if (!sent) {
        result = {false, true, "network error: send failed"};
    } else if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        result = {false, true, "network error: no response"};
    } else {
        DWORD statusCode = 0;
        DWORD size = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_FLAG_NUMBER | WINHTTP_QUERY_STATUS_CODE,
                             WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &size, WINHTTP_NO_HEADER_INDEX);

        std::string raw;
        DWORD avail = 0;
        while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0) {
            std::vector<char> buf(avail);
            DWORD read = 0;
            if (!WinHttpReadData(hRequest, buf.data(), avail, &read)) break;
            raw.append(buf.data(), read);
        }
        result = ParseResponse(static_cast<int>(statusCode), raw);
    }
    return result;
} catch (...) {
    return {false, true, "network error: unexpected exception"};
}

}  // namespace wreckfest_telemetry
