#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace wreckfest_telemetry {

// Current 0-4 index per tuning category, read live off the Tune-screen
// slider widgets (plus the transient tune array for DIFFERENTIAL). A
// category that can't be resolved or reads out of range is omitted, not
// guessed. Reflects slider changes immediately, unlike cars5.ccrs, which is
// only rewritten when the player backs out of the Tune screen.
std::map<std::string, int> ReadLiveTuning(uintptr_t tableBase);

// Tuning to attach to a just-finished race: the live reading, falling back
// per category to the save file (keyed by the race's own car) for anything
// live doesn't have. An all-zero live reading is treated as uninitialised
// widgets and discarded -- see IsUninitializedLiveTuning().
std::map<std::string, int> ReadTuningForRace(uintptr_t tableBase, const std::string& carName);

}  // namespace wreckfest_telemetry
