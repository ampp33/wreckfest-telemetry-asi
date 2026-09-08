#pragma once

#include <optional>
#include <string>

#include "json.hpp"
#include "race/PlayerResult.h"

namespace wreckfest_telemetry {

// include_laps=false OMITS the lap-time keys entirely rather than emitting
// empty lists -- [] already means "splits couldn't be validated", so
// reusing it for "not logged by configuration" would conflate the two.
nlohmann::json PlayerToDict(const PlayerResult& p, bool includeLaps = true);

// opponentLaps=false (the default) omits others' lap splits -- roughly
// quadruples the logged size otherwise, and most consumers only care
// about the local player's.
nlohmann::json RaceToDict(const RaceResult& race, bool opponentLaps = false);

// Appends one race as a JSON-lines entry. Best-effort: returns false (and
// leaves the file untouched) on any I/O failure rather than throwing.
bool AppendRaceLog(const RaceResult& race, const std::wstring& logPath, bool opponentLaps = false);

// The API POST body. nullopt if no local player was identified (caller
// skips posting). Tuning categories are 0-4 internally, sent 1-5; a
// category/lap_count/lap_times_ms with nothing to report is omitted
// entirely, not sent as a null/empty placeholder.
std::optional<nlohmann::json> BuildApiPayload(const RaceResult& race);

}  // namespace wreckfest_telemetry
