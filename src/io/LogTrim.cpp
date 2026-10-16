#include "io/LogTrim.h"

#include <algorithm>

namespace wreckfest_telemetry {

std::optional<std::string> TrimToLastLines(const std::string& content, size_t maxLines, size_t slack) {
    size_t lines = static_cast<size_t>(std::count(content.begin(), content.end(), '\n'));
    if (!content.empty() && content.back() != '\n') ++lines;
    if (lines <= maxLines + slack) return std::nullopt;

    // Skip past the first (lines - maxLines) newlines.
    size_t start = 0;
    for (size_t dropped = 0; dropped < lines - maxLines; ++dropped) start = content.find('\n', start) + 1;
    return content.substr(start);
}

}  // namespace wreckfest_telemetry
