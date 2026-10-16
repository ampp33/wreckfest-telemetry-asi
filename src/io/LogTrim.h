#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace wreckfest_telemetry {

// The last `maxLines` lines of a JSON-lines file's `content`, once it holds
// more than `maxLines + slack` -- nullopt (leave the file alone) otherwise.
// The slack is so a full log is rewritten once every `slack` races rather
// than on every one. A final line without a trailing newline (a write cut
// short) counts as a line.
std::optional<std::string> TrimToLastLines(const std::string& content, size_t maxLines, size_t slack);

}  // namespace wreckfest_telemetry
