#include "strings/HashRegistry.h"

#include "memory/Offsets.h"
#include "memory/ProcessMemory.h"

namespace wreckfest_telemetry {

using namespace offsets;

namespace {
constexpr uint32_t HASH_MUL = 0x5bd1e995u;
std::optional<uintptr_t> g_tableBaseCache;
}  // namespace

uint32_t WfHash(const std::string& data, uint32_t seed) {
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data.data());
    size_t length = data.size();
    uint32_t h = static_cast<uint32_t>(length) ^ seed;
    size_t pos = 0;
    size_t n4 = length / 4;
    size_t rem = length % 4;

    for (size_t i = 0; i < n4; ++i) {
        uint32_t k = static_cast<uint32_t>(bytes[pos]) | (static_cast<uint32_t>(bytes[pos + 1]) << 8) |
                     (static_cast<uint32_t>(bytes[pos + 2]) << 16) |
                     (static_cast<uint32_t>(bytes[pos + 3]) << 24);
        pos += 4;
        uint32_t km = k * HASH_MUL;
        uint32_t mixed = ((km >> 0x18) ^ km) * HASH_MUL;
        h = (h * HASH_MUL) ^ mixed;
    }
    if (rem == 3) {
        h ^= static_cast<uint32_t>(bytes[pos + 2]) << 0x10;
        h ^= static_cast<uint32_t>(bytes[pos + 1]) << 8;
        h = (bytes[pos] ^ h) * HASH_MUL;
    } else if (rem == 2) {
        h ^= static_cast<uint32_t>(bytes[pos + 1]) << 8;
        h = (bytes[pos] ^ h) * HASH_MUL;
    } else if (rem == 1) {
        h = (bytes[pos] ^ h) * HASH_MUL;
    }
    uint32_t h2 = ((h >> 0xd) ^ h) * HASH_MUL;
    return (h2 >> 0xf) ^ h2;
}

std::optional<uintptr_t> GetTableBase(uintptr_t moduleBase) {
    if (g_tableBaseCache) {
        return g_tableBaseCache;
    }
    auto tb = ReadU64(moduleBase + PTR_DAT_OFFSET);
    if (tb && *tb != 0) {
        g_tableBaseCache = static_cast<uintptr_t>(*tb);
    }
    return g_tableBaseCache;
}

std::optional<uintptr_t> HashRegistryLookup(uintptr_t tableBase, const std::string& name) {
    uint32_t h = WfHash(name);
    uint32_t bucket = h & 0x1FFFF;
    auto nodeOpt = ReadU64(tableBase + BUCKET_ARR_OFF + bucket * 8);
    uintptr_t node = nodeOpt ? static_cast<uintptr_t>(*nodeOpt) : 0;
    size_t maxlen = name.size() + 4;

    int seen = 0;
    while (node != 0 && seen < 64) {
        auto idxOpt = ReadI32(node);
        if (!idxOpt) {
            return std::nullopt;
        }
        uintptr_t idx = static_cast<uintptr_t>(*idxOpt);
        auto cand = ReadCString(tableBase + NAME_ARR_OFF + idx * REGISTRY_STRIDE, maxlen);
        if (cand && *cand == name) {
            auto obj = ReadU64(tableBase + OBJ_ARR_OFF + idx * REGISTRY_STRIDE);
            return obj ? std::optional<uintptr_t>(static_cast<uintptr_t>(*obj)) : std::nullopt;
        }
        auto nextOpt = ReadU64(node + 8);
        node = nextOpt ? static_cast<uintptr_t>(*nextOpt) : 0;
        ++seen;
    }
    return std::nullopt;
}

}  // namespace wreckfest_telemetry
