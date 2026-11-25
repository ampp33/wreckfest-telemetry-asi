#include "race/RaceRoster.h"

#include <algorithm>
#include <utility>

namespace wreckfest_telemetry {

namespace {

void CopyCarInfo(const PlayerResult& from, PlayerResult& to) {
    to.car = from.car;
    to.ai = from.ai;
    to.parts = from.parts;
    to.tuning = from.tuning;
    to.assists = from.assists;
}

bool HasName(const std::vector<PlayerResult>& players, const std::string& name) {
    return std::any_of(players.begin(), players.end(), [&](const PlayerResult& p) { return p.name == name; });
}

}  // namespace

bool RaceRoster::Observe(const std::vector<PlayerResult>& present) {
    // Only the local player's clock: their slot is never wiped mid-race, so
    // it can't be a half-wiped leaver's slot read as a restart.
    for (const auto& p : present) {
        auto it = byName_.find(p.name);
        if (p.is_local && it != byName_.end() &&
            std::pair(p.laps_completed, p.total_time_ms) <
                std::pair(it->second.laps_completed, it->second.total_time_ms)) {
            byName_.clear();
        }
    }

    bool added = false;
    for (const auto& p : present) {
        auto it = byName_.find(p.name);
        if (it == byName_.end()) {
            if (p.total_time_ms > 0 && !(p.status_flags & offsets::STATUS_CLASSIFIED_BIT)) {
                byName_.emplace(p.name, p);
                added = true;
            }
            continue;
        }
        PlayerResult fresh = p;  // latest race state, car info kept from when it was read
        CopyCarInfo(it->second, fresh);
        it->second = std::move(fresh);
    }
    return added;
}

void RaceRoster::SetCarInfo(const std::vector<PlayerResult>& players) {
    for (const auto& p : players) {
        auto it = byName_.find(p.name);
        if (it != byName_.end()) CopyCarInfo(p, it->second);
    }
}

std::vector<PlayerResult> RaceRoster::Missing(const std::vector<PlayerResult>& results,
                                              const std::vector<PlayerResult>& present) const {
    std::vector<PlayerResult> missing;
    for (const auto& [name, p] : byName_) {
        if (HasName(results, name) || HasName(present, name)) continue;
        PlayerResult m = p;
        m.quit = true;
        m.is_local = false;
        m.finished = false;
        m.status_flags |= offsets::STATUS_DNF_BIT;
        missing.push_back(std::move(m));
    }
    std::stable_sort(missing.begin(), missing.end(), [](const PlayerResult& a, const PlayerResult& b) {
        return a.laps_completed > b.laps_completed;
    });
    return missing;
}

}  // namespace wreckfest_telemetry
