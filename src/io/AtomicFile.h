#pragma once

#include <string>

namespace wreckfest_telemetry {

// Replaces `path` with `content` via a temp file + FlushFileBuffers +
// MoveFileEx, so a crash mid-write can never leave a truncated file: the
// old contents stay in place until the new ones are fully on disk. False
// (old file untouched, temp file removed) on any failure -- e.g. another
// process holding `path` open without delete sharing.
bool AtomicWriteFile(const std::wstring& path, const std::string& content);

}  // namespace wreckfest_telemetry
