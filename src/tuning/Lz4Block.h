#pragma once

#include <optional>
#include <string>

namespace wreckfest_telemetry {

// Decompresses one raw LZ4 block (no frame header) that may back-reference
// into `history` (previously decompressed chunks, concatenated) -- cars5.ccrs
// chains chunks this way. nullopt if the block is malformed or its
// decompressed size exceeds the per-chunk cap (4MB, matching the Python
// tool's own ceiling).
std::optional<std::string> DecompressLz4Chunk(const std::string& compressed, const std::string& history);

}  // namespace wreckfest_telemetry
