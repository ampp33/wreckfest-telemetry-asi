#include "debug/DebugLog.h"

#include <ctime>
#include <fstream>
#include <iterator>

namespace wreckfest_telemetry {

namespace {
std::wofstream g_logFile;
bool g_enabled = false;
}  // namespace

void InitDebugLog(const std::wstring& logFilePath) {
    if (logFilePath.empty()) return;
    g_logFile.open(logFilePath.c_str(), std::ios::out | std::ios::trunc);
    g_enabled = static_cast<bool>(g_logFile);
}

void DebugLog(const wchar_t* message) {
    if (!g_enabled) return;

    // std::localtime, not localtime_s -- only ever called from the single
    // worker thread, and localtime_s's signature isn't consistent between
    // MSVC and MinGW's libstdc++.
    std::time_t now = std::time(nullptr);
    std::tm* local = std::localtime(&now);
    wchar_t stamp[16] = L"";
    if (local) {
        std::wcsftime(stamp, std::size(stamp), L"%H:%M:%S", local);
    }

    g_logFile << L"[" << stamp << L"] " << message << L"\n";
    g_logFile.flush();
}

std::wstring WidenAscii(const std::string& s) {
    return std::wstring(s.begin(), s.end());
}

}  // namespace wreckfest_telemetry
