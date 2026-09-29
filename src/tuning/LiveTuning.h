#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include "tuning/TuningMerge.h"

namespace wreckfest_telemetry {

// Current 0-4 index per tuning category, read live off the Tune-screen
// slider widgets (plus the transient tune array for DIFFERENTIAL). A
// category that can't be resolved or reads out of range is omitted, not
// guessed. Reflects slider changes immediately, unlike cars5.ccrs, which is
// only rewritten when the player backs out of the Tune screen.
std::map<std::string, int> ReadLiveTuning(uintptr_t tableBase);

// One poll: reads the live sliders and feeds them to `tracker` under the
// currently selected car's key. Call every tick, so slider changes are
// seen while they happen -- see LiveTuningTracker for why a single read at
// race end can't be trusted.
void PollLiveTuning(uintptr_t tableBase, LiveTuningTracker& tracker);

// Tuning to attach to a just-finished race: the save file (keyed by the
// race's own car), overlaid with any slider changes `tracker` saw while
// that car (`carKey`) was selected. Those cover a change made on the Tune
// screen that cars5.ccrs doesn't hold yet, because it's only rewritten on
// backing out.
std::map<std::string, int> ReadTuningForRace(const std::string& carName, const std::optional<std::string>& carKey,
                                             const LiveTuningTracker& tracker);

}  // namespace wreckfest_telemetry
