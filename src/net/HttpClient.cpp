#include "net/HttpClient.h"

#include <windows.h>
#include <winhttp.h>

#include <utility>
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

HttpResult Failed() { return {false, 0, ""}; }

// Shared WinHTTP round trip: crack `url`, open a session/connection/
// request, send it (optional headers/body), and read back the status code
// + full body. Both HttpGet and HttpPost are thin wrappers around this.
HttpResult SendRequest(const std::wstring& url, const wchar_t* method, const std::wstring& headers,
                       const std::string& body, double timeoutSeconds) {
    wchar_t hostBuf[256]{};
    wchar_t pathBuf[2048]{};
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.lpszHostName = hostBuf;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = pathBuf;
    uc.dwUrlPathLength = 2048;
    uc.dwSchemeLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
        return Failed();
    }
    bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;

    HInternetHandle hSession(WinHttpOpen(L"wreckfest-telemetry-asi/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!hSession) return Failed();

    DWORD ms = static_cast<DWORD>(timeoutSeconds * 1000.0);
    WinHttpSetTimeouts(hSession, static_cast<int>(ms), static_cast<int>(ms), static_cast<int>(ms),
                        static_cast<int>(ms));

    HInternetHandle hConnect(WinHttpConnect(hSession, uc.lpszHostName, uc.nPort, 0));
    if (!hConnect) return Failed();

    HInternetHandle hRequest(WinHttpOpenRequest(hConnect, method, uc.lpszUrlPath, nullptr, WINHTTP_NO_REFERER,
                                                 WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0));
    if (!hRequest) return Failed();

    LPCWSTR headersPtr = headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str();
    DWORD headersLen = headers.empty() ? 0 : static_cast<DWORD>(-1);
    LPVOID bodyPtr = body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data());
    DWORD bodyLen = static_cast<DWORD>(body.size());

    BOOL sent = WinHttpSendRequest(hRequest, headersPtr, headersLen, bodyPtr, bodyLen, bodyLen, 0);
    if (!sent || !WinHttpReceiveResponse(hRequest, nullptr)) {
        return Failed();
    }

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
    return {true, static_cast<int>(statusCode), std::move(raw)};
}

}  // namespace

HttpResult HttpGet(const std::wstring& url, double timeoutSeconds) try {
    return SendRequest(url, L"GET", L"", "", timeoutSeconds);
} catch (...) {
    return Failed();
}

HttpResult HttpPost(const std::wstring& url, const std::wstring& headers, const std::string& body,
                     double timeoutSeconds) try {
    return SendRequest(url, L"POST", headers, body, timeoutSeconds);
} catch (...) {
    return Failed();
}

}  // namespace wreckfest_telemetry
