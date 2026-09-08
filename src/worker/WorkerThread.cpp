#include "worker/WorkerThread.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <utility>
#include <vector>

#include "io/PluginPaths.h"
#include "memory/Offsets.h"
#include "memory/ProcessMemory.h"
#include "memory/SehGuard.h"
#include "memory/SentinelScan.h"
#include "race/PlayerScraper.h"
#include "strings/HashRegistry.h"
#include "strings/TrackDetection.h"

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

// Dev-only continuous dump for eyeballing scrape_players()-equivalent output
// against a live race. Written to a file rather than a console -- a
// Wine-hosted AllocConsole window isn't reliably visible under Proton. Gets
// replaced by the real poll loop (race-finality detection, logging, POST)
// in Phase 8+.
void RunDebugLoop(HMODULE hModule) {
    std::wstring logPath = PluginFilePath(hModule, L"debug.log");

    auto base = FindModuleBase(L"Wreckfest_x64.exe");
    if (!base) {
        AppendMarker(hModule, L"debug: module base not found, skipping debug loop");
        return;
    }

    while (true) {
        std::ofstream out(logPath.c_str(), std::ios::app);

        auto slots = FastFindSlots(*base);
        bool usedFallback = false;
        if (!slots) {
            auto hits = ClusterSentinelHits(ScanForSentinels(EnumerateWritableRegions()));
            if (!hits.empty()) {
                slots = hits;
                usedFallback = true;
            }
        }
        if (!slots) {
            if (out) out << "[debug] no slots found (no race in progress?)\n";
            Sleep(2000);
            continue;
        }
        uintptr_t slot0 = *std::min_element(slots->begin(), slots->end());

        std::vector<std::pair<uintptr_t, PlayerResult>> pairs;
        for (uintptr_t addr : *slots) {
            auto validated = ValidateEntry(addr);
            if (!validated) validated = ValidateAnySlotRelaxed(addr);
            if (!validated) continue;
            auto player = ReadPlayer(*validated);
            if (!player) continue;
            pairs.emplace_back(addr, *player);
        }

        auto tableBase = GetTableBase(*base);
        MarkLocalPlayer(base, tableBase, pairs, slot0);
        int stillRacing = CountStillRacing(slot0);

        if (out) {
            out << "[debug] --- slot dump (" << pairs.size() << " players"
                << (usedFallback ? ", via sentinel scan" : ", via static chain")
                << ", still racing=" << stillRacing << ") ---\n";
        }

        if (tableBase) {
            auto [track, variation] = DetectTrackAndVariation(*base, *tableBase);
            auto [lapCount, opponentCount] = ReadRaceSettings(*tableBase);
            if (out) {
                out << "[debug] track=" << track << " variation=" << variation
                    << " laps=" << lapCount << " opponents=" << opponentCount << "\n";
            }
        }

        char line[256];
        for (const auto& [addr, player] : pairs) {
            std::snprintf(line, sizeof(line),
                          "  %-20s %-5s total=%dms best=%dms class=%c%d status=0x%02x "
                          "laps=%d finishPos=%d finished=%d",
                          player.name.c_str(), player.is_local ? "(you)" : "", player.total_time_ms,
                          player.best_lap_ms, player.class_letter, player.class_rating,
                          player.status_flags, player.laps_completed, player.finish_position,
                          player.finished);
            if (out) out << line << "\n";
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
    RunDebugLoop(hModule);

    return 0;
}

}  // namespace wreckfest_telemetry
