// Stands in for the Windows-only pieces (Cars5Path.cpp) when linking test
// binaries natively on Linux. Never actually exercised by the tests -- they
// call the pure logic (DecompressCars5Chunks, ResolveTuningIndices, ...)
// directly with a file path/bytes supplied on the command line.
#include "tuning/SaveFileTuning.h"

namespace wreckfest_telemetry {

std::optional<std::wstring> FindCars5Path() { return std::nullopt; }

}  // namespace wreckfest_telemetry
