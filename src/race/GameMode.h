#pragma once

#include <optional>
#include <string>
#include <vector>

#include "race/PlayerResult.h"

namespace wreckfest_telemetry {

// Game-mode enum at event_settings+0x38. Names from the binary's own
// reflection table (GAMEMODE_*_E) and its .gmpl rule-set loader, which
// extends it past 9 with menu-only modes:
//   0 none            5 derby deathmatch (Deathmatch)   10 (team deathmatch rules)
//   1 racing          6 team derby (Team Deathmatch)    11 sprint cup
//   2 car tuning      7 demo                            12 tag
//   3 editor          8 team race                       13 team tag
//   4 derby (Last Man Standing)   9 elimination race
// The menu's "Banger Race" is plain racing (1). Verified live 2026-11-05 on
// the results screen: race 1 (offline and online), derby 4, tournament
// derby 5, team race 8, elimination race 9.
constexpr int kGameModeRacing = 1;
constexpr int kGameModeTeamRace = 8;
constexpr int kGameModeEliminationRace = 9;

// What the game says about the event that just ended. Each field is
// nullopt when unreadable.
struct EventInfo {
    std::optional<int> gameMode;
    std::optional<bool> tournamentMode;
};

// The logged name of a mode this tool logs ("race", "team_race",
// "elimination"), or nullopt for every other mode.
std::optional<std::string> LoggedGameModeName(int gameMode);

// Why a finished event isn't logged or posted, or nullopt if it is. Only
// racing, team races and elimination races outside Tournament Mode are
// logged -- tournament events can have any goal (and report a stale track),
// and an unreadable mode or flag is skipped too, so nothing is logged
// without positively being one of the logged modes.
std::optional<std::string> SkipReason(const EventInfo& info);

// An elimination race's lap count: laps completed by the winner. The lap
// setting the game holds doesn't apply -- the race runs until one car is
// left (eliminating last place every lap or every N seconds), so it ends
// mid-lap whenever the last opponent goes out. 0 if there's no position-1
// player.
int EliminationLapCount(const std::vector<PlayerResult>& players);

}  // namespace wreckfest_telemetry
