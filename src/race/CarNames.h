#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "race/PlayerResult.h"

namespace wreckfest_telemetry {

// The local player's own car: resolves a live "VEHICLE_NAME_<id>_<variant>"
// key via the career-save/garage chain, then looks that exact key up in
// cars5.ccrs for its display name (byte-exact, e.g. "RoadSlayer"). Call once
// per new race.
std::optional<std::string> LocalPlayerCarName(uintptr_t tableBase);

// slot_index -> car name (Title Case) for every populated slot in the
// roster car-name table. Needs the local player's slot + already-resolved
// car name to validate a candidate table address.
std::map<int, std::string> ReadCarNames(uintptr_t moduleBase, std::optional<int> localSlot,
                                         const std::optional<std::string>& localCarName);

// Fills in every player's car name from LocalPlayerCarName (local) and
// ReadCarNames (everyone else). Call once per new race, after is_local is
// set on `players`.
void ResolveCarNames(uintptr_t moduleBase, uintptr_t tableBase, std::vector<PlayerResult>& players);

}  // namespace wreckfest_telemetry
