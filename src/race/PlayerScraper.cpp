#include "race/PlayerScraper.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <set>
#include <tuple>

#include "memory/ProcessMemory.h"
#include "strings/HashRegistry.h"

namespace wreckfest_telemetry {

using namespace offsets;

std::optional<std::vector<uintptr_t>> FastFindSlots(uintptr_t moduleBase) {
    auto managerPtr = ReadU64(moduleBase + CHAIN_STATIC_OFFSET);
    if (!managerPtr || *managerPtr == 0) {
        return std::nullopt;
    }
    uintptr_t slot0 = static_cast<uintptr_t>(*managerPtr) + CHAIN_ARRAY_OFFSET;

    auto sentA = ReadI32(slot0 + OFF_SENTINEL_A);
    auto sentB = ReadI32(slot0 + OFF_SENTINEL_A + 4);
    if (!sentA || !sentB || *sentA != SENTINEL_A || *sentB != SENTINEL_B) {
        return std::nullopt;
    }

    std::vector<uintptr_t> slots;
    slots.reserve(MAX_PLAYERS);
    for (int n = 0; n < MAX_PLAYERS; ++n) {
        slots.push_back(slot0 + n * SLOT_STRIDE);
    }
    return slots;
}

std::optional<ValidatedSlot> ValidateEntry(uintptr_t addr) {
    auto totalTime = ReadI32(addr + OFF_TOTAL_TIME);
    auto bestLap = ReadI32(addr + OFF_BEST_LAP);
    auto classRating = ReadI32(addr + OFF_CLASS_RATING);
    if (!totalTime || !bestLap || !classRating) {
        return std::nullopt;
    }
    if (*bestLap < MIN_LAP_MS || *bestLap > MAX_LAP_MS) return std::nullopt;
    if (*totalTime < MIN_LAP_MS || *totalTime > MAX_TOTAL_MS) return std::nullopt;
    if (*bestLap > *totalTime) return std::nullopt;
    if (*classRating < 50 || *classRating > 600) return std::nullopt;

    return ValidatedSlot{addr, *totalTime, *bestLap, *classRating};
}

std::optional<PlayerResult> ReadPlayer(const ValidatedSlot& slot) {
    auto ptrOpt = ReadU64(slot.addr + OFF_PLAYER_PTR);
    if (!ptrOpt || *ptrOpt < MIN_HEAP_PTR || *ptrOpt > (uint64_t{1} << 47)) {
        return std::nullopt;
    }
    uintptr_t ptr = static_cast<uintptr_t>(*ptrOpt);

    auto rawName = ReadCString(ptr + POFF_NAME, 64);
    if (!rawName) {
        return std::nullopt;
    }
    std::string name = StripColorCodes(*rawName);
    if (name.empty()) {
        return std::nullopt;
    }

    auto flag = ReadI32(slot.addr + OFF_FINISHED_FLAG);
    uint32_t flagU = flag.has_value() ? static_cast<uint32_t>(*flag) : 0;
    bool finished = flag.has_value() && (flagU & FINISHED_BIT) != 0;
    int lapsCompleted = flag.has_value() ? static_cast<int>((flagU >> 8) & 0xFF) : 0;
    int finishPosition = flag.has_value() ? static_cast<int>((flagU & 0xFF) + 1) : 0;
    uint32_t statusFlags = static_cast<uint32_t>(ReadI32(slot.addr + OFF_STATUS_FLAGS).value_or(0));
    int lastLap = ReadI32(slot.addr + OFF_LAST_LAP).value_or(0);

    PlayerResult p;
    p.name = name;
    p.class_letter = ClassFromRating(slot.class_rating);
    p.class_rating = slot.class_rating;
    p.best_lap_ms = slot.best_lap_ms;
    p.total_time_ms = slot.total_time_ms;
    p.finished = finished;
    p.laps_completed = lapsCompleted;
    p.status_flags = statusFlags;
    p.last_lap_ms = lastLap;
    p.finish_position = finishPosition;
    return p;
}

std::string StripColorCodes(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    size_t i = 0;
    while (i < name.size()) {
        if (i + 2 < name.size() && name[i] == '*' && name[i + 1] == '^' &&
            std::isdigit(static_cast<unsigned char>(name[i + 2]))) {
            i += 3;
            continue;
        }
        if (i + 1 < name.size() && name[i] == '^' &&
            std::isdigit(static_cast<unsigned char>(name[i + 1]))) {
            i += 2;
            continue;
        }
        out.push_back(name[i]);
        ++i;
    }
    size_t start = out.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    size_t end = out.find_last_not_of(" \t\r\n");
    return out.substr(start, end - start + 1);
}

bool LooksGarbled(const std::string& name) {
    for (unsigned char c : name) {
        if (c < ' ') return true;
    }
    return false;
}

std::optional<int> LocalPlayerSlotIndex(uintptr_t tableBase) {
    auto clientObj = HashRegistryLookup(tableBase, "CLIENT");
    if (!clientObj) return std::nullopt;
    auto rawOpt = ReadI32(*clientObj);
    if (!rawOpt) return std::nullopt;
    int32_t raw = *rawOpt;

    // -1 (no network client) means solo/AI, seated at slot 0 -- but a
    // genuine online race's CLIENT can also transiently read -1 (e.g. once
    // the race ends and you're just viewing results). Trust the last known
    // real value for a short window rather than immediately mislabeling
    // whichever entry sits at slot 0.
    static std::optional<int32_t> lastGood;
    static std::chrono::steady_clock::time_point lastGoodAt;
    constexpr auto kStaleFallback = std::chrono::seconds(30);

    if (raw > -1) {
        lastGood = raw;
        lastGoodAt = std::chrono::steady_clock::now();
        return raw;
    }
    if (lastGood && (std::chrono::steady_clock::now() - lastGoodAt) < kStaleFallback) {
        return *lastGood;
    }
    return 0;
}

namespace {

std::optional<ValidatedSlot> ValidateSlotRelaxedCore(uintptr_t addr) {
    auto totalTime = ReadI32(addr + OFF_TOTAL_TIME);
    auto bestLap = ReadI32(addr + OFF_BEST_LAP);
    auto classRating = ReadI32(addr + OFF_CLASS_RATING);
    if (!totalTime || !bestLap || !classRating) return std::nullopt;
    if (*classRating < 50 || *classRating > 600) return std::nullopt;

    auto ptrOpt = ReadU64(addr + OFF_PLAYER_PTR);
    if (!ptrOpt || *ptrOpt < MIN_HEAP_PTR || *ptrOpt > (uint64_t{1} << 47)) return std::nullopt;
    auto rawName = ReadCString(static_cast<uintptr_t>(*ptrOpt) + POFF_NAME, 64);
    if (!rawName) return std::nullopt;
    std::string name = StripColorCodes(*rawName);
    if (name.empty() || LooksGarbled(name)) return std::nullopt;

    int tt = *totalTime;
    int bl = *bestLap;
    if (bl < MIN_LAP_MS || bl > MAX_LAP_MS) bl = 0;
    if (tt < MIN_LAP_MS || tt > MAX_TOTAL_MS) tt = 0;

    return ValidatedSlot{addr, tt, bl, *classRating};
}

}  // namespace

std::optional<ValidatedSlot> ValidateLocalSlotRelaxed(uintptr_t addr) {
    return ValidateSlotRelaxedCore(addr);
}

std::optional<ValidatedSlot> ValidateAnySlotRelaxed(uintptr_t addr) {
    auto status = ReadI32(addr + OFF_STATUS_FLAGS);
    if (!status || !(static_cast<uint32_t>(*status) & (STATUS_DNF_BIT | STATUS_RUN_COMPLETE_BIT))) {
        return std::nullopt;
    }
    return ValidateSlotRelaxedCore(addr);
}

PlayerResult ReadPlayerNativeOnly(const ValidatedSlot& slot) {
    auto flag = ReadI32(slot.addr + OFF_FINISHED_FLAG);
    uint32_t flagU = flag.has_value() ? static_cast<uint32_t>(*flag) : 0;
    bool finished = flag.has_value() && (flagU & FINISHED_BIT) != 0;
    int lapsCompleted = flag.has_value() ? static_cast<int>((flagU >> 8) & 0xFF) : 0;
    int finishPosition = flag.has_value() ? static_cast<int>((flagU & 0xFF) + 1) : 0;

    std::string name;
    auto ptrOpt = ReadU64(slot.addr + OFF_PLAYER_PTR);
    if (ptrOpt && *ptrOpt) {
        auto candidate = ReadCString(static_cast<uintptr_t>(*ptrOpt) + POFF_NAME, 64);
        if (candidate && !LooksGarbled(*candidate)) {
            name = StripColorCodes(*candidate);
        }
    }

    PlayerResult p;
    p.name = name;
    p.class_letter = ClassFromRating(slot.class_rating);
    p.class_rating = slot.class_rating;
    p.best_lap_ms = slot.best_lap_ms;
    p.total_time_ms = slot.total_time_ms;
    p.finished = finished;
    p.laps_completed = lapsCompleted;
    p.status_flags = static_cast<uint32_t>(ReadI32(slot.addr + OFF_STATUS_FLAGS).value_or(0));
    p.finish_position = finishPosition;
    p.last_lap_ms = ReadI32(slot.addr + OFF_LAST_LAP).value_or(0);
    return p;
}

int CountStillRacing(uintptr_t slot0) {
    int still = 0;
    for (int i = 0; i < MAX_PLAYERS; ++i) {
        uintptr_t addr = slot0 + static_cast<uintptr_t>(i) * SLOT_STRIDE;
        auto st = ReadI32(addr + OFF_STATUS_FLAGS);
        if (!st || (static_cast<uint32_t>(*st) & STATUS_CLASSIFIED_BIT)) continue;
        auto rating = ReadI32(addr + OFF_CLASS_RATING);
        if (!rating || *rating < 50 || *rating > 600) continue;
        auto ptrOpt = ReadU64(addr + OFF_PLAYER_PTR);
        if (!ptrOpt || *ptrOpt < MIN_HEAP_PTR || *ptrOpt > (uint64_t{1} << 47)) continue;
        auto nameOpt = ReadCString(static_cast<uintptr_t>(*ptrOpt) + POFF_NAME, 64);
        if (nameOpt) {
            std::string name = StripColorCodes(*nameOpt);
            if (!name.empty() && !LooksGarbled(name)) ++still;
        }
    }
    return still;
}

void MarkLocalPlayer(std::optional<uintptr_t> moduleBase, std::optional<uintptr_t> tableBase,
                      std::vector<std::pair<uintptr_t, PlayerResult>>& pairs,
                      std::optional<uintptr_t> slot0) {
    // STATUS_RUN_COMPLETE_BIT is deliberately not used for identity -- see
    // its docstring in Offsets.h. CLIENT is the only identity source.
    PlayerResult* localPlayer = nullptr;
    bool clientResolved = false;
    std::optional<int> localSlot;

    if (moduleBase && tableBase && slot0) {
        localSlot = LocalPlayerSlotIndex(*tableBase);
        if (localSlot && !(*localSlot >= 0 && *localSlot < MAX_PLAYERS)) {
            localSlot.reset();
        }
    }

    if (slot0 && localSlot) {
        clientResolved = true;
        for (auto& [addr, p] : pairs) {
            if (static_cast<int>((addr - *slot0) / SLOT_STRIDE) == *localSlot) {
                localPlayer = &p;
                break;
            }
        }
        if (localPlayer == nullptr) {
            uintptr_t addr = *slot0 + static_cast<uintptr_t>(*localSlot) * SLOT_STRIDE;
            // The relaxed tier is gated on at least one other racer already
            // having validated this tick -- an idle menu's leftover slot
            // data can otherwise read as a phantom 1-player "race".
            auto d = ValidateEntry(addr);
            bool relaxed = false;
            if (!d && !pairs.empty()) {
                d = ValidateLocalSlotRelaxed(addr);
                relaxed = d.has_value();
            }
            if (d) {
                PlayerResult np = ReadPlayerNativeOnly(*d);
                np.slot_index = *localSlot;
                // A relaxed salvage has no valid lap time, so it cannot
                // possibly have finished regardless of what the (unreliable)
                // finished-flag parity bit says.
                if (relaxed) {
                    np.finished = false;
                }
                pairs.emplace_back(addr, np);
                localPlayer = &pairs.back().second;
            }
        }
    }

    if (localPlayer == nullptr) {
        if (clientResolved || pairs.empty()) {
            return;
        }
        auto minIt = std::min_element(pairs.begin(), pairs.end(),
                                       [](const auto& a, const auto& b) { return a.first < b.first; });
        localPlayer = &minIt->second;
    }
    localPlayer->is_local = true;
}

std::vector<PlayerResult> RankPlayers(std::vector<PlayerResult> players) {
    std::vector<int> positions;
    positions.reserve(players.size());
    for (const auto& p : players) positions.push_back(p.finish_position);
    bool allPositive = std::all_of(positions.begin(), positions.end(), [](int v) { return v > 0; });
    std::set<int> uniquePositions(positions.begin(), positions.end());
    bool usable = allPositive && uniquePositions.size() == positions.size();

    if (usable) {
        std::sort(players.begin(), players.end(), [](const PlayerResult& a, const PlayerResult& b) {
            return a.finish_position < b.finish_position;
        });
    } else {
        std::sort(players.begin(), players.end(), [](const PlayerResult& a, const PlayerResult& b) {
            return std::make_tuple(a.dnf(), -a.laps_completed, a.total_time_ms) <
                   std::make_tuple(b.dnf(), -b.laps_completed, b.total_time_ms);
        });
    }
    return players;
}

}  // namespace wreckfest_telemetry
