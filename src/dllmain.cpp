// Ultimate ASI Loader LoadLibrary()s this DLL (renamed .asi) when
// Wreckfest_x64.exe starts. DllMain stays minimal (loader-lock rules) --
// all real work happens on the worker thread.

#include <windows.h>

#include "worker/WorkerThread.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            CreateThread(nullptr, 0, wreckfest_telemetry::WorkerThreadMain,
                         hModule, 0, nullptr);
            break;
        case DLL_PROCESS_DETACH:
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
        default:
            break;
    }
    return TRUE;
}
