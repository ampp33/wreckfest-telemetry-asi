#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "race/GameMode.h"

namespace wreckfest_telemetry {

// (lap_count, opponent_count), or (0, 0) if unresolved.
std::pair<int, int> ReadRaceSettings(uintptr_t tableBase);

// Game mode (event_settings) + Tournament Mode flag, for deciding whether
// a finished event gets logged -- see GameMode.h. Read on the results
// screen: both still describe the event that just ended there.
EventInfo ReadEventInfo(uintptr_t moduleBase, uintptr_t tableBase);

// (track, variation). Falls back to snake-case-splitting the raw codename,
// then to ("Unknown Track", "") if nothing resolves.
std::pair<std::string, std::string> DetectTrackAndVariation(uintptr_t moduleBase, uintptr_t tableBase);

}  // namespace wreckfest_telemetry
