#include "worker/WorkerThread.h"

#include <fstream>

#include "io/PluginPaths.h"
#include "memory/ProcessMemory.h"
#include "memory/SehGuard.h"

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

// Phase 1 proof-of-life: resolves the game module, counts writable regions,
// and confirms the SEH guard actually catches a bad read -- all logged to
// the marker file so this can be checked without a debugger attached.
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

}  // namespace

DWORD WINAPI WorkerThreadMain(LPVOID param) {
    HMODULE hModule = static_cast<HMODULE>(param);

    InstallSehGuard();
    RunPhase1SelfTest(hModule);

    // Phase 2+ replaces this with the real poll loop.
    while (true) {
        Sleep(500);
    }
    return 0;
}

}  // namespace wreckfest_telemetry
