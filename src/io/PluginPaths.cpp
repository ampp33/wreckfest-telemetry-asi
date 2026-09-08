#include "io/PluginPaths.h"

namespace wreckfest_telemetry {

std::wstring GetPluginDirectory(HMODULE hModule) {
    wchar_t path[MAX_PATH];
    DWORD len = GetModuleFileNameW(hModule, path, MAX_PATH);
    if (len == 0 || len == MAX_PATH) {
        return L"";
    }
    std::wstring full(path, len);
    size_t slash = full.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return L"";
    }
    return full.substr(0, slash);
}

std::wstring PluginFilePath(HMODULE hModule, const wchar_t* fileName) {
    std::wstring dir = GetPluginDirectory(hModule);
    if (dir.empty()) {
        return L"";
    }
    return dir + L"\\" + fileName;
}

}  // namespace wreckfest_telemetry
