#include "worker/WorkerThread.h"

#include <fstream>

#include "io/PluginPaths.h"

namespace wreckfest_telemetry {

namespace {

void WriteLoadedMarker(HMODULE hModule) {
    std::wstring path = PluginFilePath(hModule, L"asi_loaded.txt");
    if (path.empty()) {
        return;
    }
    std::wofstream out(path, std::ios::app);
    if (!out) {
        return;
    }
    out << L"wreckfest-telemetry-asi worker thread started\n";
}

}  // namespace

DWORD WINAPI WorkerThreadMain(LPVOID param) {
    HMODULE hModule = static_cast<HMODULE>(param);
    WriteLoadedMarker(hModule);

    // Phase 1+ replaces this with the real poll loop.
    while (true) {
        Sleep(500);
    }
    return 0;
}

}  // namespace wreckfest_telemetry
