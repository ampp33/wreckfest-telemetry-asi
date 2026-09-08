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
};

inline char ClassFromRating(int rating) {
    if (rating <= 100) return 'D';
    if (rating <= 200) return 'C';
    if (rating <= 250) return 'B';
    return 'A';
}

}  // namespace wreckfest_telemetry
