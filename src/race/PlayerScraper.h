#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "race/PlayerResult.h"

namespace wreckfest_telemetry {

// Follows the static pointer chain to the 24 player-slot addresses.
// nullopt if the chain is broken (stale after a game update -- see the
// sentinel-scan fallback, Phase 3).
std::optional<std::vector<uintptr_t>> FastFindSlots(uintptr_t moduleBase);

struct ValidatedSlot {
    uintptr_t addr;
    int total_time_ms;
    int best_lap_ms;
    int class_rating;
};

// Plausibility-checks the timing/rating fields at a candidate slot address.
// nullopt if the slot doesn't look like a real, populated race result.
std::optional<ValidatedSlot> ValidateEntry(uintptr_t addr);

// Reads the rest of a validated slot into a PlayerResult. is_local, car,
// position, and lap_times_ms are filled in by later stages (Phases 3-5).
std::optional<PlayerResult> ReadPlayer(const ValidatedSlot& slot);

std::string StripColorCodes(const std::string& name);

}  // namespace wreckfest_telemetry
