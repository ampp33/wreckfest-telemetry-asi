#pragma once

#include <windows.h>

namespace wreckfest_telemetry {

// Runs on the thread spawned from DllMain. `param` is the plugin's own
// HMODULE. Phase 0: proves the thread runs via a marker file, then idles.
DWORD WINAPI WorkerThreadMain(LPVOID param);

}  // namespace wreckfest_telemetry
