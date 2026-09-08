#pragma once

#include <map>
#include <optional>
#include <vector>

#include "race/PlayerResult.h"

namespace wreckfest_telemetry {

// Live per-lap split accumulation. The slot struct holds no per-lap array
// (see OFF_LAST_LAP) -- only the most recently completed lap -- so splits
// exist only for laps observed while this was running. Owns state across
// the whole worker-thread lifetime; construct one instance and keep it
// alive for the process's lifetime (do not reconstruct per poll).
class LapSplitTracker {
public:
    // Call every poll tick, for every player currently visible (not just
    // the local player -- each slot carries its own field). Resets a
    // slot's history when its lap counter goes backwards (a new race
    // reusing the same slot).
    void Accumulate(const std::vector<PlayerResult>& players);

    // Call once per finalized race. Attaches validated splits to
    // `players[i].lap_times_ms`, deriving the unrecorded final lap for a
    // finisher and dropping (not partially exporting) any player whose
    // splits fail either cross-check: count matches the lap counter, and
    // min(splits) == best_lap_ms.
    void Resolve(std::vector<PlayerResult>& players);

private:
    struct SlotState {
        std::optional<int> seen;
        std::vector<int> splits;
        int ctr = 0;
    };
    std::map<int, SlotState> slots_;
};

}  // namespace wreckfest_telemetry
