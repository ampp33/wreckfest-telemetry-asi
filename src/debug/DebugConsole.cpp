#include "debug/DebugConsole.h"

#include <cstdio>
#include <windows.h>

namespace wreckfest_telemetry {

namespace {
bool g_consoleEnabled = false;
}

void EnableDebugConsole() {
    if (g_consoleEnabled || !AllocConsole()) {
        return;
    }
    freopen("CONOUT$", "w", stdout);
    freopen("CONOUT$", "w", stderr);
    g_consoleEnabled = true;
}

void DebugLog(const wchar_t* message) {
    if (!g_consoleEnabled) {
        return;
    }
    fwprintf(stdout, L"%s\n", message);
    fflush(stdout);
}

}  // namespace wreckfest_telemetry
