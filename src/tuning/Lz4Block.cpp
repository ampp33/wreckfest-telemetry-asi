#include "tuning/Lz4Block.h"

#include "lz4.h"

namespace wreckfest_telemetry {

namespace {
constexpr int kMaxChunkOutput = 1 << 22;  // 4MB, matches the Python tool's per-block cap
}

std::optional<std::string> DecompressLz4Chunk(const std::string& compressed, const std::string& history) {
    std::string out(static_cast<size_t>(kMaxChunkOutput), '\0');
    const char* dictStart = history.empty() ? nullptr : history.data();
    int dictSize = static_cast<int>(history.size());

    int result = LZ4_decompress_safe_usingDict(compressed.data(), out.data(),
                                                static_cast<int>(compressed.size()), kMaxChunkOutput,
                                                dictStart, dictSize);
    if (result < 0) {
        return std::nullopt;
    }
    out.resize(static_cast<size_t>(result));
    return out;
}

}  // namespace wreckfest_telemetry
