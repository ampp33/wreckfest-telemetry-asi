#pragma once

#include <cstdint>
#include <string>
#include <utility>

namespace wreckfest_telemetry {

// (lap_count, opponent_count), or (0, 0) if unresolved.
std::pair<int, int> ReadRaceSettings(uintptr_t tableBase);

// (track, variation). Falls back to snake-case-splitting the raw codename,
// then to ("Unknown Track", "") if nothing resolves.
std::pair<std::string, std::string> DetectTrackAndVariation(uintptr_t moduleBase, uintptr_t tableBase);

}  // namespace wreckfest_telemetry
