#include "race/RaceFinality.h"

#include <algorithm>

#include "memory/Offsets.h"

namespace wreckfest_telemetry {

using namespace offsets;

bool RaceIsFinal(const std::vector<PlayerResult>& players, int stillRacingCount) {
    // Hard veto, checked before anything else: any real racer still
    // lacking a terminal classification means the race is not over,
    // whatever other signal says.
    if (stillRacingCount > 0) return false;

    bool everyoneSettled =
        !players.empty() && std::all_of(players.begin(), players.end(), [](const PlayerResult& p) {
            return (p.status_flags & STATUS_CLASSIFIED_BIT) != 0;
        });
    if (everyoneSettled) return true;

    // Fallback, reached only when the status field is entirely unpopulated
    // for every racer (a mode that doesn't use it). If any racer has
    // status bits, the check above is the authority and its answer --
    // including "not yet" -- must stand.
    bool anyStatus = std::any_of(players.begin(), players.end(), [](const PlayerResult& p) {
        return p.status_flags != 0;
    });
    if (anyStatus) return false;

    const PlayerResult* local = nullptr;
    for (const auto& p : players) {
        if (p.is_local) {
            local = &p;
            break;
        }
    }
    if (local) {
        return local->finished && local->best_lap_ms >= MIN_LAP_MS && local->best_lap_ms <= MAX_LAP_MS;
    }
    return std::all_of(players.begin(), players.end(), [](const PlayerResult& p) { return p.finished; });
}

}  // namespace wreckfest_telemetry
