// Stands in for the Windows-only pieces (Cars5Path.cpp, DebugLog.cpp) when
// linking test binaries natively on Linux. Never actually exercised by the
// tests -- they call the pure logic (DecompressCars5Chunks,
// ResolveTuningIndices, ...) directly with a file path/bytes supplied on the
// command line.
#include "debug/DebugLog.h"
#include "tuning/SaveFileTuning.h"

namespace wreckfest_telemetry {

std::optional<std::wstring> FindCars5Path() { return std::nullopt; }

void InitDebugLog(const std::wstring&) {}
void DebugLog(const wchar_t*) {}
std::wstring WidenAscii(const std::string& s) { return std::wstring(s.begin(), s.end()); }

}  // namespace wreckfest_telemetry
