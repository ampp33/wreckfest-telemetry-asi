// FindCars5Path()'s implementation -- split out from SaveFileTuning.cpp so
// the pure decompression/extraction logic there can build and unit-test
// natively on Linux, without windows.h.
#include "tuning/SaveFileTuning.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>

#include "debug/DebugLog.h"
#include "memory/ProcessMemory.h"
#include "memory/SehGuard.h"

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

// Last-resort discovery for storefronts that don't use Steam's userdata
// convention (Epic, GOG, ...): the running game must itself know where
// cars5.ccrs lives, since it reads/writes it -- so scan live memory for the
// literal filename (both narrow and wide-char encodings, since we don't
// know which the game uses) and recover whatever printable path sits in
// front of each hit. Slow (a full pass over every writable region), so only
// tried once, and only when the Steam/Proton candidates below come up
// empty. Candidates are validated the same way as any other (GetFileAttributesExW
// below), so an imperfectly-trimmed hit just fails validation rather than
// being trusted blindly.
std::vector<std::wstring> ScanMemoryForCars5Paths() {
    std::vector<std::wstring> hits;
    auto regions = EnumerateWritableRegions();

    static const std::string kNeedleNarrow = "cars5.ccrs";
    std::string kNeedleWide;
    for (char c : kNeedleNarrow) {
        kNeedleWide.push_back(c);
        kNeedleWide.push_back('\0');
    }

    auto scanRegion = [&](const MemoryRegion& region, const std::string& needle, int charWidth) {
        if (hits.size() >= 20 || region.size < needle.size()) return;
        SehGuarded([&]() -> int {
            const char* base = reinterpret_cast<const char*>(region.base);
            size_t len = region.size;
            char first = needle[0];
            for (size_t i = 0; i + needle.size() <= len && hits.size() < 20; ++i) {
                if (base[i] != first) continue;
                if (std::memcmp(base + i, needle.data(), needle.size()) != 0) continue;

                // Walk backward from the match, trimming to the last run of
                // printable characters at this char width -- the likely
                // start of the full path (e.g. "C:\Users\...\cars5.ccrs").
                size_t start = i;
                while (start >= static_cast<size_t>(charWidth)) {
                    size_t prev = start - static_cast<size_t>(charWidth);
                    unsigned char lo = static_cast<unsigned char>(base[prev]);
                    unsigned char hi = charWidth == 2 ? static_cast<unsigned char>(base[prev + 1]) : 0;
                    if (lo < 0x20 || lo > 0x7e || hi != 0) break;
                    start = prev;
                }
                std::wstring path;
                for (size_t p = start; p < i + needle.size(); p += static_cast<size_t>(charWidth)) {
                    path.push_back(static_cast<wchar_t>(static_cast<unsigned char>(base[p])));
                }
                hits.push_back(std::move(path));
            }
            return 0;
        });
    };

    for (const auto& region : regions) {
        scanRegion(region, kNeedleNarrow, 1);
        if (hits.size() >= 20) break;
        scanRegion(region, kNeedleWide, 2);
        if (hits.size() >= 20) break;
    }
    return hits;
}

}  // namespace

std::optional<std::wstring> FindCars5Path() {
    static std::optional<std::wstring> cache;
    if (cache) return cache;

    static const std::wstring kSuffix = L"userdata\\*\\228380\\local\\wreckfest\\cars5.ccrs";
    std::vector<std::wstring> candidates;

    // Testing knob: set WRECKFEST_TELEMETRY_FORCE_SCAN (to anything) in the
    // game's launch environment to skip the candidates below entirely and
    // exercise ScanMemoryForCars5Paths() even on a machine where the fast
    // path would otherwise always win -- e.g. via a Steam launch option,
    // `WRECKFEST_TELEMETRY_FORCE_SCAN=1 %command%`. Not meant for normal use.
    bool forceScan = GetEnvironmentVariableW(L"WRECKFEST_TELEMETRY_FORCE_SCAN", nullptr, 0) > 0;
    if (forceScan) {
        DebugLog(L"cars5.ccrs: WRECKFEST_TELEMETRY_FORCE_SCAN set -- skipping Steam/Proton/LocalLow "
                 L"candidates");
    }

    if (!forceScan) {
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

        // Storefront-agnostic: confirmed live on a non-Steam (Epic) install
        // -- %USERPROFILE%\AppData\LocalLow is a standard Windows per-user
        // data location keyed by publisher/game name, nothing Steam-specific
        // about it. The Steam userdata copy above may just be a cloud-sync
        // mirror of this one; either way, "most recently modified wins"
        // below picks whichever is actually current.
        {
            wchar_t buf[MAX_PATH];
            DWORD len = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
            if (len > 0 && len < MAX_PATH) {
                std::wstring localLow = std::wstring(buf, len) + L"\\AppData\\LocalLow";
                auto matches = Glob(localLow, L"THQNordic\\Wreckfest\\*\\wreckfest\\cars5.ccrs");
                candidates.insert(candidates.end(), matches.begin(), matches.end());
            }
        }
    }

    if (candidates.empty()) {
        DebugLog(L"cars5.ccrs: no Steam/Proton/LocalLow candidates found -- scanning live memory as a "
                 L"last resort (this can take a while)");
        auto scanStart = std::chrono::steady_clock::now();
        auto scanned = ScanMemoryForCars5Paths();
        auto scanMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                              scanStart)
                          .count();
        DebugLog((L"cars5.ccrs: memory scan took " + std::to_wstring(scanMs) + L"ms, found " +
                  std::to_wstring(scanned.size()) + L" candidate(s)")
                     .c_str());
        for (const auto& s : scanned) {
            DebugLog((L"cars5.ccrs: memory-scan candidate '" + s + L"'").c_str());
        }
        candidates.insert(candidates.end(), scanned.begin(), scanned.end());
    }

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
    cache = best.empty() ? std::nullopt : std::optional<std::wstring>(best);
    return cache;
}

}  // namespace wreckfest_telemetry
