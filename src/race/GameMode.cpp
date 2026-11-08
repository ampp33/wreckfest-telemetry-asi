#include "race/GameMode.h"

namespace wreckfest_telemetry {

std::optional<std::string> LoggedGameModeName(int gameMode) {
    switch (gameMode) {
        case kGameModeRacing: return "race";
        case kGameModeTeamRace: return "team_race";
        case kGameModeEliminationRace: return "elimination";
        default: return std::nullopt;
    }
}

std::optional<std::string> SkipReason(const EventInfo& info) {
    if (!info.tournamentMode) return "tournament mode flag unreadable";
    if (*info.tournamentMode) return "tournament event";
    if (!info.gameMode) return "game mode unreadable";
    if (!LoggedGameModeName(*info.gameMode)) return "game mode " + std::to_string(*info.gameMode) + " isn't logged";
    return std::nullopt;
}

int EliminationLapCount(const std::vector<PlayerResult>& players) {
    for (const auto& p : players) {
        if (p.position == 1) return p.laps_completed;
    }
    return 0;
}

}  // namespace wreckfest_telemetry
