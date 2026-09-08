// FindCars5Path()'s implementation -- split out from SaveFileTuning.cpp so
// the pure decompression/extraction logic there can build and unit-test
// natively on Linux, without windows.h.
#include "tuning/SaveFileTuning.h"

#include <windows.h>

#include <algorithm>

namespace wreckfest_telemetry {

namespace {

void ExpandGlob(const std::wstring& base, const std::vector<std::wstring>& segments, size_t idx,
                 std::vector<std::wstring>& results) {
    if (idx == segments.size()) {
        results.push_back(base);
        return;
    }
    const std::wstring& seg = segments[idx];
    if (seg.find(L'*') == std::wstring::npos) {
        ExpandGlob(base + L"\\" + seg, segments, idx + 1, results);
        return;
    }
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((base + L"\\" + seg).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        ExpandGlob(base + L"\\" + name, segments, idx + 1, results);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// Windows has no built-in multi-segment glob -- expands one `*` per
// directory level via FindFirstFileW/FindNextFileW.
std::vector<std::wstring> Glob(const std::wstring& rootPrefix, const std::wstring& patternSuffix) {
    std::vector<std::wstring> segments;
    size_t start = 0;
    while (true) {
        size_t pos = patternSuffix.find(L'\\', start);
        if (pos == std::wstring::npos) {
            segments.push_back(patternSuffix.substr(start));
            break;
        }
        segments.push_back(patternSuffix.substr(start, pos - start));
        start = pos + 1;
    }
    std::vector<std::wstring> results;
    ExpandGlob(rootPrefix, segments, 0, results);
    return results;
}

}  // namespace

std::optional<std::wstring> FindCars5Path() {
    static const std::wstring kSuffix = L"userdata\\*\\228380\\local\\wreckfest\\cars5.ccrs";
    std::vector<std::wstring> candidates;

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t buf[MAX_PATH];
        DWORD size = sizeof(buf);
        DWORD type;
        if (RegQueryValueExW(hKey, L"SteamPath", nullptr, &type, reinterpret_cast<BYTE*>(buf), &size) ==
                ERROR_SUCCESS &&
            type == REG_SZ) {
            std::wstring steamPath(buf);
            std::replace(steamPath.begin(), steamPath.end(), L'/', L'\\');
            auto matches = Glob(steamPath, kSuffix);
            candidates.insert(candidates.end(), matches.begin(), matches.end());
        }
        RegCloseKey(hKey);
    }

    // Proton-only: S: is mapped to the real, Linux-native Steam client
    // install root -- the registry SteamPath above points at a minimal
    // shim inside the prefix instead, which never has real userdata.
    {
        auto matches = Glob(L"S:", kSuffix);
        candidates.insert(candidates.end(), matches.begin(), matches.end());
    }

    // Proton-only fallback: Z: is Wine's default host-root mapping.
    {
        auto matches = Glob(L"Z:\\home", L"*\\.local\\share\\Steam\\" + kSuffix);
        candidates.insert(candidates.end(), matches.begin(), matches.end());
    }

    if (candidates.empty()) return std::nullopt;

    std::wstring best;
    FILETIME bestTime{};
    for (const auto& c : candidates) {
        WIN32_FILE_ATTRIBUTE_DATA data;
        if (!GetFileAttributesExW(c.c_str(), GetFileExInfoStandard, &data)) continue;
        if (best.empty() || CompareFileTime(&data.ftLastWriteTime, &bestTime) > 0) {
            best = c;
            bestTime = data.ftLastWriteTime;
        }
    }
    return best.empty() ? std::nullopt : std::optional<std::wstring>(best);
}

}  // namespace wreckfest_telemetry
