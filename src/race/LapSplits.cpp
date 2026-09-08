#include "race/LapSplits.h"

#include <algorithm>
#include <numeric>

#include "memory/Offsets.h"

namespace wreckfest_telemetry {

using namespace offsets;

void LapSplitTracker::Accumulate(const std::vector<PlayerResult>& players) {
    for (const auto& p : players) {
        if (!p.slot_index) continue;
        int slotIndex = *p.slot_index;

        bool firstSight = slots_.find(slotIndex) == slots_.end();
        SlotState& st = slots_[slotIndex];
        if (p.laps_completed < st.ctr) {  // counter went backwards -> new race
            st.seen.reset();
            st.splits.clear();
            firstSight = true;
        }
        st.ctr = p.laps_completed;
        int v = p.last_lap_ms;

        if (firstSight) {
            // OFF_LAST_LAP already holds a value on first sight; whether it's
            // real or stale depends on laps completed so far: 0 -> stale
            // (previous race's final lap), 1 -> real lap 1 (the normal case,
            // since a racer isn't visible until their first valid lap),
            // 2+ -> attached mid-race, earlier laps unrecoverable.
            int lapsDone = std::max(0, p.laps_completed - 1);
            if (lapsDone == 1 && v != 0 && v >= MIN_LAP_MS && v <= MAX_LAP_MS) {
                st.splits.push_back(v);
            }
            st.seen = v;
            continue;
        }
        if (v != 0 && v >= MIN_LAP_MS && v <= MAX_LAP_MS && (!st.seen || v != *st.seen)) {
            st.splits.push_back(v);
            st.seen = v;
        }
    }
}

void LapSplitTracker::Resolve(std::vector<PlayerResult>& players) {
    for (auto& p : players) {
        if (!p.slot_index) continue;
        auto it = slots_.find(*p.slot_index);
        if (it == slots_.end()) continue;

        std::vector<int> splits = it->second.splits;
        int expected = std::max(0, p.laps_completed - 1);
        if (splits.empty() || expected == 0) continue;

        // The final lap is never written to OFF_LAST_LAP (crossing the
        // finish ends the race rather than starting a new lap) -- derive it
        // as total - sum(observed), valid only for an actual finisher (a
        // DNF's clock freezes mid-lap, not at a lap boundary).
        if (!p.dnf() && splits.size() == static_cast<size_t>(expected - 1)) {
            int sum = std::accumulate(splits.begin(), splits.end(), 0);
            int remainder = p.total_time_ms - sum;
            if (remainder >= MIN_LAP_MS && remainder <= MAX_LAP_MS) {
                splits.push_back(remainder);
            }
        }

        if (splits.size() != static_cast<size_t>(expected)) continue;
        if (p.best_lap_ms != 0 && *std::min_element(splits.begin(), splits.end()) != p.best_lap_ms) continue;

        p.lap_times_ms = splits;
    }
}

}  // namespace wreckfest_telemetry
