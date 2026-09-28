#include "tuning/LiveTuning.h"

#include <windows.h>

#include <cmath>
#include <optional>

#include "debug/DebugLog.h"
#include "memory/Offsets.h"
#include "memory/ProcessMemory.h"
#include "memory/SehGuard.h"
#include "strings/HashRegistry.h"
#include "tuning/SaveFileTuning.h"
#include "tuning/TuningDisplay.h"
#include "tuning/TuningMerge.h"

namespace wreckfest_telemetry {

using namespace offsets;

namespace {

constexpr size_t kDifferentialIdx = 2;  // position of "DIFFERENTIAL" in kTuneCategories

// Base of the tune array once found. Only the address is cached; values are
// re-read every call, and the whole signature is re-checked first since the
// array is freed whenever the Tune screen is left.
std::optional<uintptr_t> g_tuneArrayCache;

std::optional<uintptr_t> SliderTrackPtr(uintptr_t tableBase, const char* category) {
    std::string widget = std::string("TUNE_SLIDER_") + category + "_TRACK";
    auto ptr = HashRegistryLookup(tableBase, "menu/element/" + std::to_string(Fnv1a(widget)));
    if (!ptr || *ptr == 0) return std::nullopt;
    return ptr;
}

std::optional<int> SliderIndex(uintptr_t trackPtr) {
    auto frac = ReadF32(trackPtr + TUNE_TRACK_VALUE_OFF);
    if (!frac || !std::isfinite(*frac) || *frac < -0.01f || *frac > 1.01f) return std::nullopt;
    return static_cast<int>(std::lround(*frac * TUNE_MAX_INDEX));
}

std::optional<int64_t> TuneStructId(uintptr_t addr) {
    auto magic = ReadI32(addr);
    if (!magic || *magic != TUNE_ARR_MAGIC) return std::nullopt;
    auto id = ReadI32(addr + TUNE_ARR_ID_OFF);
    if (!id) return std::nullopt;
    return *id;
}

std::optional<int> TuneArrayValue(uintptr_t base, size_t idx) {
    auto v = ReadI32(base + idx * TUNE_ARR_STRIDE + TUNE_ARR_VAL_OFF);
    if (!v || *v < 0 || *v > TUNE_MAX_INDEX) return std::nullopt;
    return *v;
}

// Exactly four structs with sequential ids and in-range values, not part of
// a longer chain -- the magic alone is a generic engine tag shared by many
// unrelated arrays.
bool IsTuneArray(uintptr_t base) {
    std::optional<int64_t> firstId;
    std::optional<int64_t> prevId;
    for (size_t k = 0; k < kTuneCategories.size(); ++k) {
        auto id = TuneStructId(base + k * TUNE_ARR_STRIDE);
        if (!id || (prevId && *id != *prevId + 1)) return false;
        if (!TuneArrayValue(base, k)) return false;
        if (!firstId) firstId = id;
        prevId = id;
    }
    auto before = TuneStructId(base - TUNE_ARR_STRIDE);
    if (before && *before == *firstId - 1) return false;
    auto after = TuneStructId(base + kTuneCategories.size() * TUNE_ARR_STRIDE);
    if (after && *after == *prevId + 1) return false;
    return true;
}

bool IsCommittedWritable(const MEMORY_BASIC_INFORMATION& mbi) {
    constexpr DWORD kWritable = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return mbi.State == MEM_COMMIT && (mbi.Protect & kWritable) != 0 && (mbi.Protect & PAGE_GUARD) == 0;
}

// The run of address-contiguous committed, writable regions around `addr`.
// This approximates the single /proc/<pid>/maps mapping the Python tool
// scanned: Linux merges adjacent same-protection mappings that VirtualQuery
// reports as separate regions.
std::optional<MemoryRegion> ContiguousWritableSpan(uintptr_t addr) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) != sizeof(mbi) ||
        !IsCommittedWritable(mbi)) {
        return std::nullopt;
    }
    uintptr_t start = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    uintptr_t end = start + mbi.RegionSize;
    while (start > 0 && VirtualQuery(reinterpret_cast<LPCVOID>(start - 1), &mbi, sizeof(mbi)) == sizeof(mbi) &&
           IsCommittedWritable(mbi)) {
        start = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    }
    while (VirtualQuery(reinterpret_cast<LPCVOID>(end), &mbi, sizeof(mbi)) == sizeof(mbi) &&
           IsCommittedWritable(mbi)) {
        end = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
    }
    return MemoryRegion{start, end - start};
}

// First 4-byte-aligned address in [from, end) holding TUNE_ARR_MAGIC, or 0
// if none (or if the scan faults). Kept free of nested guarded reads, since
// SehGuarded() has a single jump buffer.
uintptr_t FindNextMagic(uintptr_t from, uintptr_t end) {
    auto hit = SehGuarded([from, end]() -> uintptr_t {
        for (uintptr_t p = from; p + 4 <= end; p += 4) {
            if (*reinterpret_cast<const volatile int32_t*>(p) == TUNE_ARR_MAGIC) return p;
        }
        return 0;
    });
    return hit ? *hit : 0;
}

uintptr_t Distance(uintptr_t a, uintptr_t b) { return a > b ? a - b : b - a; }

// Scans only the memory around an already-resolved slider widget -- a
// whole-address-space scan was seen live to match an unrelated array that
// happens to share the signature. Nearest match to the anchor wins.
std::optional<uintptr_t> FindTuneArray(uintptr_t anchor) {
    auto span = ContiguousWritableSpan(anchor);
    if (!span) {
        DebugLog(L"tune array: anchor widget isn't in committed writable memory");
        return std::nullopt;
    }
    uintptr_t end = span->base + span->size;
    std::optional<uintptr_t> best;
    size_t matches = 0;
    for (uintptr_t p = FindNextMagic(span->base, end); p != 0; p = FindNextMagic(p + 4, end)) {
        if (!IsTuneArray(p)) continue;
        ++matches;
        if (!best || Distance(p, anchor) < Distance(*best, anchor)) best = p;
    }
    DebugLog((L"tune array: scanned " + std::to_wstring(span->size / (1024 * 1024)) + L" MiB around anchor, " +
              std::to_wstring(matches) + L" match(es)")
                 .c_str());
    return best;
}

std::optional<int> ReadDifferential(std::optional<uintptr_t> anchor) {
    if (g_tuneArrayCache && !IsTuneArray(*g_tuneArrayCache)) {
        g_tuneArrayCache.reset();
    }
    if (!g_tuneArrayCache) {
        if (!anchor) return std::nullopt;
        g_tuneArrayCache = FindTuneArray(*anchor);
        if (!g_tuneArrayCache) return std::nullopt;
    }
    return TuneArrayValue(*g_tuneArrayCache, kDifferentialIdx);
}

// Debug-log rendering: 1-indexed (player-facing numbering), unlike the
// 0-indexed values held in memory.
std::wstring FormatTuning(const std::map<std::string, int>& tuning) {
    std::wstring out;
    for (const auto& [category, index] : ToDisplayTuning(tuning)) {
        if (!out.empty()) out += L", ";
        out += WidenAscii(category) + L"=" + std::to_wstring(index);
    }
    return out.empty() ? L"(none)" : out;
}

}  // namespace

std::map<std::string, int> ReadLiveTuning(uintptr_t tableBase) {
    std::map<std::string, int> result;
    if (tableBase == 0) return result;

    // Any resolved slider serves as the scan anchor for DIFFERENTIAL (they
    // share an arena), so a never-visited SUSPENSION tab doesn't block it.
    std::optional<uintptr_t> anchor;
    for (size_t i = 0; i < kTuneCategories.size(); ++i) {
        if (i == kDifferentialIdx) continue;
        auto track = SliderTrackPtr(tableBase, kTuneCategories[i]);
        if (!track) continue;
        if (!anchor) anchor = track;
        if (auto idx = SliderIndex(*track)) result[kTuneCategories[i]] = *idx;
    }
    if (auto diff = ReadDifferential(anchor)) {
        result[kTuneCategories[kDifferentialIdx]] = *diff;
    }
    return result;
}

std::map<std::string, int> ReadTuningForRace(uintptr_t tableBase, const std::string& carName) {
    auto live = ReadLiveTuning(tableBase);
    DebugLog((L"tuning: live read " + FormatTuning(live)).c_str());
    if (IsUninitializedLiveTuning(live)) {
        DebugLog(L"tuning: live read is all zeros (Tune screen likely never opened) -- deferring to save file");
        live.clear();
    }
    if (live.size() == kTuneCategories.size()) {
        return live;
    }

    std::map<std::string, int> save;
    if (!carName.empty()) {
        save = ReadTuningFromSave(carName);
    } else {
        DebugLog(L"tuning: local player's car name is empty -- save-file fallback skipped");
    }
    auto merged = MergeTuning(save, live);
    DebugLog((L"tuning: merged " + FormatTuning(merged)).c_str());
    return merged;
}

}  // namespace wreckfest_telemetry
