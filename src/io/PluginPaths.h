#pragma once

#include <string>
#include <windows.h>

namespace wreckfest_telemetry {

// Directory the plugin DLL lives in. race_log.jsonl, config.json, the
// offline queue files, and debug markers all live here. Returns an empty
// string on failure.
std::wstring GetPluginDirectory(HMODULE hModule);

std::wstring PluginFilePath(HMODULE hModule, const wchar_t* fileName);

}  // namespace wreckfest_telemetry
