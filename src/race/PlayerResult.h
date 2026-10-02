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
    // Tuning of the car this player raced, category -> 0-4 index (convert
    // with ToDisplayTuning before output). Only the categories that
    // resolved; empty if none did.
    std::map<std::string, int> tuning;
    // Whether this racer is an AI bot, from their race car's registry
    // assembly (AI cars carry an "_ai" codename). nullopt if their car
    // couldn't be matched to their slot -- left out of the output rather
    // than guessed.
    std::optional<bool> ai;

    bool dnf() const { return (status_flags & offsets::STATUS_DNF_BIT) != 0; }
};

struct RaceResult {
    std::string track;
    std::string variation;
    std::string timestamp;
    std::vector<PlayerResult> players;
    std::map<std::string, int> tuning;  // category -> 0-4 index (convert with ToDisplayTuning before output)
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
    // Online server the race was run on, raw with its color codes -- see
    // ServerName.h. Empty for offline races (or unresolved), omitted from
    // output.
    std::string server_name;
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
