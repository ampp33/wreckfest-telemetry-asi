#pragma once

#include <string>

// Opt-in diagnostic logging for troubleshooting the live-memory reads (car
// names, save-file tuning, ...) that can't be reproduced or tested outside a
// real Wreckfest session. Off (every call below is a no-op) unless a user
// drops a `debug.txt` marker file next to the .asi -- see InitDebugLog()'s
// call site in WorkerThread.cpp and the README's troubleshooting section.
namespace wreckfest_telemetry {

// Enables logging to `logFilePath` (truncated on open) if non-empty; a no-op
// otherwise. Call once, at startup.
void InitDebugLog(const std::wstring& logFilePath);

void DebugLog(const wchar_t* message);

// Widens an ASCII string for DebugLog -- not for arbitrary UTF-8 content,
// just the diagnostic strings (car codenames, track names, ...) that are
// ASCII in practice.
std::wstring WidenAscii(const std::string& s);

}  // namespace wreckfest_telemetry
