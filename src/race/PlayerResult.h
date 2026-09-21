#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "memory/Offsets.h"

namespace wreckfest_telemetry {

struct PlayerResult {
    int position = 0;
    std::string name;
    std::string car;
    char class_letter = 'D';
    int class_rating = 0;
    int best_lap_ms = 0;
    int total_time_ms = 0;
    std::vector<int> lap_times_ms;
    bool is_local = false;
    bool finished = false;
    int laps_completed = 0;
    uint32_t status_flags = 0;
    int last_lap_ms = 0;
    int finish_position = 0;
    std::optional<int> slot_index;

    bool dnf() const { return (status_flags & offsets::STATUS_DNF_BIT) != 0; }
};

struct RaceResult {
    std::string track;
    std::string variation;
    std::string timestamp;
    std::vector<PlayerResult> players;
    std::map<std::string, int> tuning;  // category -> 0-4 index
    int lap_count = 0;
    int opponent_count = 0;
    // Driving-assist difficulty settings in effect for this race -- see
    // AssistSettings.h. Empty if unresolved, omitted from output rather
    // than exported as misleading defaults.
    std::map<std::string, std::string> assists;
    // Local player's vehicle weight in kg -- see VehicleWeight.h. 0 means
    // "not resolved", omitted from output rather than exported as a
    // misleading zero.
    int vehicle_weight_kg = 0;
};

inline char ClassFromRating(int rating) {
    if (rating <= 100) return 'D';
    if (rating <= 200) return 'C';
    if (rating <= 250) return 'B';
    return 'A';
}

inline const PlayerResult* FindLocalPlayer(const std::vector<PlayerResult>& players) {
    for (const auto& p : players) {
        if (p.is_local) return &p;
    }
    return nullptr;
}

inline PlayerResult* FindLocalPlayer(std::vector<PlayerResult>& players) {
    for (auto& p : players) {
        if (p.is_local) return &p;
    }
    return nullptr;
}

}  // namespace wreckfest_telemetry
