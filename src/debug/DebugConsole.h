#pragma once

namespace wreckfest_telemetry {

// Optional AllocConsole debug output for local testing. Off by default.
void EnableDebugConsole();
void DebugLog(const wchar_t* message);

}  // namespace wreckfest_telemetry
