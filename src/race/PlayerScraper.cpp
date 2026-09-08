#include "race/PlayerScraper.h"

#include <cctype>

#include "memory/ProcessMemory.h"

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

}  // namespace wreckfest_telemetry
