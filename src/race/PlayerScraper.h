#pragma once

#include <cstdint>
#include <optional>
#include <utility>
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
bool LooksGarbled(const std::string& name);

// CLIENT's slot index for the local human player, or nullopt if
// unresolved. -1 (no network client) falls back to slot 0, except for a
// short window after a genuine online reading, where the last known-good
// value is trusted over a transient -1 (see PlayerScraper.cpp).
std::optional<int> LocalPlayerSlotIndex(uintptr_t tableBase);

// Like ValidateEntry but with the lap-time plausibility floors dropped
// (clamped to 0 instead of rejected) -- for a slot whose occupancy is
// already proven some other way (CLIENT naming it, or a terminal status
// marker), where a 0-lap DNF would otherwise be invisible.
std::optional<ValidatedSlot> ValidateLocalSlotRelaxed(uintptr_t addr);
std::optional<ValidatedSlot> ValidateAnySlotRelaxed(uintptr_t addr);

// Builds a player entry from the slot's own native fields only, without
// player_ptr's normal MIN_HEAP_PTR floor -- salvage path for when the
// local player's own player_ptr fails the strict check ReadPlayer applies.
PlayerResult ReadPlayerNativeOnly(const ValidatedSlot& slot);

// Real racers (occupied, named seats) with no terminal status marker yet --
// the signal a validated/relaxed player list alone can't give, since a
// racer with no completed lap appears in neither.
int CountStillRacing(uintptr_t slot0);

// Resolves and flags the local player's entry in `pairs` in place, salvaging
// a degraded entry from the slot's own fields if CLIENT names a real slot
// that the normal read rejected. Falls back to the lowest slot address only
// if CLIENT couldn't resolve a slot index at all.
void MarkLocalPlayer(std::optional<uintptr_t> moduleBase, std::optional<uintptr_t> tableBase,
                      std::vector<std::pair<uintptr_t, PlayerResult>>& pairs,
                      std::optional<uintptr_t> slot0);

}  // namespace wreckfest_telemetry
