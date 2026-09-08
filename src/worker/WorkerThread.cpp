#include "worker/WorkerThread.h"

#include <cstdio>
#include <fstream>

#include "io/PluginPaths.h"
#include "memory/Offsets.h"
#include "memory/ProcessMemory.h"
#include "memory/SehGuard.h"
#include "memory/SentinelScan.h"
#include "race/PlayerScraper.h"

namespace wreckfest_telemetry {

namespace {

void AppendMarker(HMODULE hModule, const std::wstring& line) {
    std::wstring path = PluginFilePath(hModule, L"asi_loaded.txt");
    if (path.empty()) {
        return;
    }
    std::wofstream out(path.c_str(), std::ios::app);
    if (!out) {
        return;
    }
    out << line << L"\n";
}

void RunPhase1SelfTest(HMODULE hModule) {
    AppendMarker(hModule, L"phase1: worker thread started");

    auto base = FindModuleBase(L"Wreckfest_x64.exe");
    AppendMarker(hModule, base ? L"phase1: module base resolved" : L"phase1: module base NOT FOUND");

    auto regions = EnumerateWritableRegions();
    AppendMarker(hModule, L"phase1: writable regions = " + std::to_wstring(regions.size()));

    auto badRead = ReadI32(0x1);
    AppendMarker(hModule, badRead.has_value() ? L"phase1: SEH GUARD FAILED (bad read returned a value)"
                                               : L"phase1: SEH guard caught the bad read (PASS)");

    auto goodRead = ReadI32(reinterpret_cast<uintptr_t>(&hModule));
    AppendMarker(hModule, goodRead.has_value() ? L"phase1: guarded read of valid memory OK"
                                                : L"phase1: guarded read of valid memory unexpectedly failed");
}

// Dev-only continuous dump for eyeballing slot reads against a live race,
// side by side with the Python tool. Written to a file rather than a
// console -- a Wine-hosted AllocConsole window isn't reliably visible under
// Proton. Gets replaced by the real poll loop (race-finality detection,
// logging, POST) in Phase 8+.
void RunPhase2DebugLoop(HMODULE hModule) {
    std::wstring logPath = PluginFilePath(hModule, L"phase2_debug.log");

    auto base = FindModuleBase(L"Wreckfest_x64.exe");
    if (!base) {
        AppendMarker(hModule, L"phase2: module base not found, skipping debug loop");
        return;
    }
    {
        std::ofstream out(logPath.c_str(), std::ios::app);
        if (out) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "[phase2] module base = 0x%016llx",
                          static_cast<unsigned long long>(*base));
            out << buf << "\n";
        }
    }

    while (true) {
        std::ofstream out(logPath.c_str(), std::ios::app);

        // Diagnostics: walk the chain by hand so we can see exactly where
        // it breaks, rather than only FastFindSlots' pass/fail verdict.
        auto managerPtr = ReadU64(*base + offsets::CHAIN_STATIC_OFFSET);
        if (out) {
            if (managerPtr) {
                char buf[192];
                uintptr_t slot0 = static_cast<uintptr_t>(*managerPtr) + offsets::CHAIN_ARRAY_OFFSET;
                auto sentA = ReadI32(slot0 + offsets::OFF_SENTINEL_A);
                auto sentB = ReadI32(slot0 + offsets::OFF_SENTINEL_A + 4);
                std::string sentAStr = sentA ? std::to_string(*sentA) : std::string("READ-FAILED");
                std::string sentBStr = sentB ? std::to_string(*sentB) : std::string("READ-FAILED");
                std::snprintf(buf, sizeof(buf),
                              "[phase2] manager_ptr=0x%016llx slot0=0x%016llx sentA=%s sentB=%s",
                              static_cast<unsigned long long>(*managerPtr),
                              static_cast<unsigned long long>(slot0),
                              sentAStr.c_str(), sentBStr.c_str());
                out << buf << "\n";
            } else {
                out << "[phase2] manager_ptr read FAILED (bad address or unreadable)\n";
            }
        }

        auto slots = FastFindSlots(*base);
        bool usedFallback = false;
        if (!slots) {
            auto regions = EnumerateWritableRegions();
            auto hits = ClusterSentinelHits(ScanForSentinels(regions));
            if (out) {
                out << "[phase2] static chain stale, sentinel scan found " << hits.size()
                    << " candidates across " << regions.size() << " regions\n";
            }
            if (!hits.empty()) {
                slots = hits;
                usedFallback = true;
            }
        }
        if (!slots) {
            if (out) out << "[phase2] no slots found (no race in progress?)\n";
            Sleep(2000);
            continue;
        }

        if (out) {
            out << "[phase2] --- slot dump (" << slots->size() << " candidate slots"
                << (usedFallback ? ", via sentinel scan" : ", via static chain") << ") ---\n";
        }
        int validCount = 0;
        char line[256];
        for (uintptr_t addr : *slots) {
            auto validated = ValidateEntry(addr);
            if (!validated) {
                continue;
            }
            auto player = ReadPlayer(*validated);
            if (!player) {
                continue;
            }
            ++validCount;
            std::snprintf(line, sizeof(line),
                          "  %-20s total=%dms best=%dms class=%c%d status=0x%02x "
                          "laps=%d finishPos=%d finished=%d",
                          player->name.c_str(), player->total_time_ms, player->best_lap_ms,
                          player->class_letter, player->class_rating, player->status_flags,
                          player->laps_completed, player->finish_position, player->finished);
            if (out) out << line << "\n";
        }
        if (validCount == 0 && out) {
            out << "  (no valid players this tick)\n";
        }
        out.close();
        Sleep(2000);
    }
}

}  // namespace

DWORD WINAPI WorkerThreadMain(LPVOID param) {
    HMODULE hModule = static_cast<HMODULE>(param);

    InstallSehGuard();
    RunPhase1SelfTest(hModule);
    RunPhase2DebugLoop(hModule);

    return 0;
}

}  // namespace wreckfest_telemetry
