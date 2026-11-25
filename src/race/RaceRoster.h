#pragma once

#include <map>
#include <string>
#include <vector>

#include "race/PlayerResult.h"

namespace wreckfest_telemetry {

// Everyone who raced in the current race, by name, with the car info read
// while they were still there. A racer who quits has their result slot, car
// and assembly wiped within seconds (verified live 2026-11-13, online), so
// this is the only record of them once results settle -- see Missing().
class RaceRoster {
public:
    // `present`: every occupied slot this tick (ScrapeResult::present). A
    // racer is added once their own race clock is running and they're
    // unclassified, so lobby, grid and results-screen leftovers never enter.
    // Starts over when the local player's clock goes backwards (a new race
    // without the last one logged, e.g. quit or restart). Returns true if
    // anyone was added -- their car info needs reading while they're still
    // present (SetCarInfo).
    bool Observe(const std::vector<PlayerResult>& present);
    // Stores the car/ai/parts/tuning/assists read for these players.
    void SetCarInfo(const std::vector<PlayerResult>& players);
    // Racers in neither the final results nor a slot, flagged QUIT (and DNF),
    // most laps first, with their last-seen race state.
    std::vector<PlayerResult> Missing(const std::vector<PlayerResult>& results,
                                      const std::vector<PlayerResult>& present) const;
    void Clear() { byName_.clear(); }

private:
    std::map<std::string, PlayerResult> byName_;
};

}  // namespace wreckfest_telemetry
