#include "strings/HashRegistry.h"

#include "debug/DebugLog.h"
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

std::optional<std::string> ResolveLocalizedString(uintptr_t moduleBase, uintptr_t tableBase,
                                                   const std::string& key) {
    auto bucketTablePtrOpt = ReadU64(moduleBase + LOC_HASH_TABLE_PTR_OFF);
    if (!bucketTablePtrOpt || *bucketTablePtrOpt == 0) {
        DebugLog(L"loc-string: bucket table pointer read failed (LOC_HASH_TABLE_PTR_OFF)");
        return std::nullopt;
    }
    uintptr_t bucketTablePtr = static_cast<uintptr_t>(*bucketTablePtrOpt);

    auto bucketArrayBaseOpt = ReadU64(bucketTablePtr);
    auto bucketCountOpt = ReadU64(bucketTablePtr + 8);
    if (!bucketArrayBaseOpt || !*bucketArrayBaseOpt || !bucketCountOpt || !*bucketCountOpt) {
        DebugLog(L"loc-string: bucket array base/count read failed");
        return std::nullopt;
    }
    uintptr_t bucketArrayBase = static_cast<uintptr_t>(*bucketArrayBaseOpt);
    uint64_t bucketCount = *bucketCountOpt;

    uint32_t h = WfHash(key);
    uint64_t bucket = h % bucketCount;
    auto nodeOpt = ReadU64(bucketArrayBase + bucket * 8);
    uintptr_t node = nodeOpt ? static_cast<uintptr_t>(*nodeOpt) : 0;
    size_t maxlen = key.size() + 4;

    std::optional<int32_t> idx;
    int seen = 0;
    while (node != 0 && seen < 64) {
        auto keyPtrOpt = ReadU64(node + 8);
        std::optional<std::string> cand;
        if (keyPtrOpt && *keyPtrOpt != 0) {
            cand = ReadCString(static_cast<uintptr_t>(*keyPtrOpt), maxlen);
        }
        if (cand && *cand == key) {
            idx = ReadI32(node + 0x10);
            break;
        }
        auto nextOpt = ReadU64(node);
        node = nextOpt ? static_cast<uintptr_t>(*nextOpt) : 0;
        ++seen;
    }
    if (!idx || *idx < 0) {
        DebugLog((L"loc-string: key '" + WidenAscii(key) + L"' not found in hash bucket chain").c_str());
        return std::nullopt;
    }

    auto locSysIdxOpt = ReadI32(moduleBase + LOC_SYS_IDX_OFF);
    if (!locSysIdxOpt) {
        DebugLog(L"loc-string: localization-system registry index read failed (LOC_SYS_IDX_OFF)");
        return std::nullopt;
    }
    auto locDataPtrOpt = ReadU64(tableBase + OBJ_ARR_OFF + static_cast<uintptr_t>(*locSysIdxOpt) * REGISTRY_STRIDE);
    if (!locDataPtrOpt || !*locDataPtrOpt) {
        DebugLog(L"loc-string: localization-system object read failed");
        return std::nullopt;
    }
    auto locDataArrayOpt = ReadU64(*locDataPtrOpt);
    if (!locDataArrayOpt || !*locDataArrayOpt) {
        DebugLog(L"loc-string: localization data array read failed");
        return std::nullopt;
    }
    uintptr_t entryPtr = static_cast<uintptr_t>(*locDataArrayOpt) + static_cast<uintptr_t>(*idx) * 0x60;

    auto segCountOpt = ReadI32(entryPtr + 0x18);
    auto segArrayBaseOpt = ReadU64(entryPtr + 0x10);
    if (!segCountOpt || *segCountOpt < 1 || !segArrayBaseOpt || !*segArrayBaseOpt) {
        DebugLog(L"loc-string: segment count/array read failed for resolved entry");
        return std::nullopt;
    }
    auto textPtrOpt = ReadU64(*segArrayBaseOpt + 8);  // segment 0: plain, non-parameterized string
    if (!textPtrOpt || !*textPtrOpt) {
        DebugLog(L"loc-string: text pointer read failed for segment 0");
        return std::nullopt;
    }
    return ReadCString(static_cast<uintptr_t>(*textPtrOpt), 64);
}

}  // namespace wreckfest_telemetry
