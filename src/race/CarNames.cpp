#include "race/CarNames.h"

#include <algorithm>
#include <cctype>

#include "memory/Offsets.h"
#include "memory/ProcessMemory.h"
#include "memory/SehGuard.h"
#include "strings/HashRegistry.h"

namespace wreckfest_telemetry {

using namespace offsets;

namespace {

constexpr uintptr_t CAR_TABLE_FAST_OFFSET = 0x19fae50;  // module_base + this -> table slot 0, directly
constexpr int CAR_NAME_TABLE_STRIDE = 0x80;

std::string ToUpper(const std::string& s) {
    std::string out = s;
    for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

std::string ToTitleCase(const std::string& s) {
    std::string out = ToUpper(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    bool startOfWord = true;
    for (auto& c : out) {
        if (startOfWord && c >= 'a' && c <= 'z') {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        startOfWord = (c == ' ');
    }
    return out;
}

// [A-Z][A-Z ]{2,19}\x00, greedy with backtracking (matches the Python
// tool's _CAR_NAME_TOKEN_RE). Returns match start offsets.
std::vector<size_t> FindCarNameTokens(const uint8_t* data, size_t len) {
    std::vector<size_t> hits;
    size_t i = 0;
    while (i < len) {
        if (!(data[i] >= 'A' && data[i] <= 'Z')) {
            ++i;
            continue;
        }
        size_t runStart = i + 1;
        size_t maxExtend = std::min(len > runStart ? len - runStart : size_t{0}, size_t{19});
        size_t actualExtend = 0;
        while (actualExtend < maxExtend &&
               (data[runStart + actualExtend] == ' ' ||
                (data[runStart + actualExtend] >= 'A' && data[runStart + actualExtend] <= 'Z'))) {
            ++actualExtend;
        }
        bool matched = false;
        for (size_t l = actualExtend; l >= 2; --l) {
            size_t nulPos = runStart + l;
            if (nulPos < len && data[nulPos] == '\0') {
                hits.push_back(i);
                i = nulPos + 1;
                matched = true;
                break;
            }
            if (l == 2) break;
        }
        if (!matched) {
            ++i;
        }
    }
    return hits;
}

// All runs of hits spaced exactly `stride` apart, each >= minRun long
// (unlike ClusterSentinelHits, returns every run, not just the longest --
// other same-shape/same-stride tables exist elsewhere in memory).
std::vector<std::vector<uintptr_t>> AllStrideRuns(std::vector<uintptr_t> hits, uintptr_t stride,
                                                   size_t minRun = 2) {
    std::vector<std::vector<uintptr_t>> runs;
    if (hits.size() < minRun) return runs;
    std::sort(hits.begin(), hits.end());
    std::vector<uintptr_t> current{hits[0]};
    for (size_t i = 1; i < hits.size(); ++i) {
        if (hits[i] - current.back() == stride) {
            current.push_back(hits[i]);
        } else {
            if (current.size() >= minRun) runs.push_back(current);
            current = {hits[i]};
        }
    }
    if (current.size() >= minRun) runs.push_back(current);
    return runs;
}

std::optional<uintptr_t> FastCarNameTable(uintptr_t moduleBase, std::optional<int> localSlot,
                                           const std::optional<std::string>& localCarName) {
    if (!localSlot || !localCarName || localCarName->empty()) return std::nullopt;
    uintptr_t candidate = moduleBase + CAR_TABLE_FAST_OFFSET;
    auto val = ReadCString(candidate + static_cast<uintptr_t>(*localSlot) * CAR_NAME_TABLE_STRIDE, 32);
    if (!val) return std::nullopt;
    return ToUpper(*val) == ToUpper(*localCarName) ? std::optional<uintptr_t>(candidate) : std::nullopt;
}

std::optional<uintptr_t> FindCarNameTableStructural(uintptr_t moduleBase, int localSlot,
                                                      const std::string& localCarName) {
    std::string target = ToUpper(localCarName);
    auto regions = EnumerateWritableRegions();
    // Regions at/above module_base first -- the table lives just above the
    // executable image in practice, so this reaches it far faster than
    // plain address order.
    std::stable_partition(regions.begin(), regions.end(),
                           [moduleBase](const MemoryRegion& r) { return r.base >= moduleBase; });

    for (const auto& region : regions) {
        auto run = SehGuarded([&]() -> std::optional<uintptr_t> {
            auto hits = FindCarNameTokens(reinterpret_cast<const uint8_t*>(region.base), region.size);
            std::vector<uintptr_t> addrs(hits.begin(), hits.end());
            for (auto& a : addrs) a += region.base;
            for (auto& candidateRun : AllStrideRuns(addrs, CAR_NAME_TABLE_STRIDE)) {
                if (static_cast<size_t>(localSlot) >= candidateRun.size()) continue;
                auto val = ReadCString(candidateRun[static_cast<size_t>(localSlot)], 32);
                if (val && ToUpper(*val) == target) {
                    return candidateRun[0];
                }
            }
            return std::nullopt;
        });
        if (run && *run) {
            return *run;
        }
    }
    return std::nullopt;
}

std::optional<uintptr_t> GetCarNameTable(uintptr_t moduleBase, std::optional<int> localSlot,
                                          const std::optional<std::string>& localCarName) {
    static std::optional<uintptr_t> cache;
    if (cache) return cache;
    if (!localSlot || !localCarName || localCarName->empty()) return std::nullopt;

    if (auto fast = FastCarNameTable(moduleBase, localSlot, localCarName)) {
        cache = fast;
        return cache;
    }
    if (auto found = FindCarNameTableStructural(moduleBase, *localSlot, *localCarName)) {
        cache = found;
    }
    return cache;
}

}  // namespace

std::optional<std::string> LocalPlayerCarName(uintptr_t moduleBase, uintptr_t tableBase) {
    auto careerObj = HashRegistryLookup(tableBase, "save/career.cres");
    if (!careerObj) return std::nullopt;

    auto garageIdxOpt = ReadI32(*careerObj + 0x1c);
    auto vehicleIdOpt = ReadI32(*careerObj + 0x180);
    if (!garageIdxOpt || !vehicleIdOpt || *vehicleIdOpt < 0) return std::nullopt;

    auto garageObjOpt = ReadU64(tableBase + OBJ_ARR_OFF + static_cast<uintptr_t>(*garageIdxOpt) * REGISTRY_STRIDE);
    if (!garageObjOpt || !*garageObjOpt) return std::nullopt;

    auto vehiclesBaseOpt = ReadU64(*garageObjOpt);
    if (!vehiclesBaseOpt || !*vehiclesBaseOpt) return std::nullopt;

    uintptr_t carDef = static_cast<uintptr_t>(*vehiclesBaseOpt) + static_cast<uintptr_t>(*vehicleIdOpt) * 0x90;
    auto viewObjOpt = ReadU64(carDef);
    if (!viewObjOpt || !*viewObjOpt) return std::nullopt;

    auto keyPtrOpt = ReadU64(*viewObjOpt + 8);
    if (!keyPtrOpt || !*keyPtrOpt) return std::nullopt;

    auto key = ReadCString(static_cast<uintptr_t>(*keyPtrOpt), 64);
    if (!key || key->empty()) return std::nullopt;

    return ResolveLocalizedString(moduleBase, tableBase, *key);
}

std::map<int, std::string> ReadCarNames(uintptr_t moduleBase, std::optional<int> localSlot,
                                         const std::optional<std::string>& localCarName) {
    std::map<int, std::string> result;
    auto base = GetCarNameTable(moduleBase, localSlot, localCarName);
    if (!base) return result;

    for (int i = 0; i < MAX_PLAYERS; ++i) {
        auto val = ReadCString(*base + static_cast<uintptr_t>(i) * CAR_NAME_TABLE_STRIDE, 32);
        if (val && !val->empty()) {
            result[i] = ToTitleCase(*val);
        }
    }
    return result;
}

void ResolveCarNames(uintptr_t moduleBase, uintptr_t tableBase, std::vector<PlayerResult>& players) {
    PlayerResult* localPlayer = FindLocalPlayer(players);

    if (localPlayer) {
        if (auto resolved = LocalPlayerCarName(moduleBase, tableBase)) {
            localPlayer->car = *resolved;
        }
    }

    std::optional<std::string> localCarName = localPlayer ? std::optional<std::string>(localPlayer->car) : std::nullopt;
    std::optional<int> localSlot = localPlayer ? localPlayer->slot_index : std::nullopt;
    auto carNames = ReadCarNames(moduleBase, localSlot, localCarName);

    for (auto& p : players) {
        if (p.is_local) continue;
        if (p.slot_index && carNames.count(*p.slot_index)) {
            p.car = carNames[*p.slot_index];
        }
    }
}

}  // namespace wreckfest_telemetry
