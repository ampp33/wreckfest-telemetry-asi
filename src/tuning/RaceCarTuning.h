#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "race/PlayerResult.h"

namespace wreckfest_telemetry {

// One car spawned for the race, from its registry assembly (see
// AssemblyTuning.h). `tuning` holds whichever categories resolved, 0-4;
// always empty for an AI car, whose tuning isn't read.
struct RaceCar {
    std::string name;  // registry name, for logging
    int slot = -1;
    std::string codename;
    bool ai = false;
    std::map<std::string, int> tuning;
};

// Every race car's assembly currently registered. Call once per race, at
// the end -- it scans the whole registry.
std::vector<RaceCar> ReadRaceCars(uintptr_t tableBase);

// Sets each non-local player's `tuning` from the race car in their slot
// (so AI players get none). A slot with more than one car is ambiguous and
// skipped.
void AssignOpponentTunings(std::vector<PlayerResult>& players, const std::vector<RaceCar>& cars);

// Tuning of the local player's car as actually built for the race: the
// non-AI car in `localSlot` whose codename matches `codename` (when known);
// with no slot match, the only non-AI car of that model. cars5.ccrs (keyed
// by the race's own car name) fills any category that doesn't resolve --
// on its own it lags, missing a tune set after Restart or before backing
// out of the Tune screen.
std::map<std::string, int> ReadTuningForRace(const std::vector<RaceCar>& cars, const std::string& carName,
                                             const std::optional<std::string>& carKey, std::optional<int> localSlot);

}  // namespace wreckfest_telemetry
