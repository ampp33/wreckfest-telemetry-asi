#pragma once

#include <map>
#include <string>

namespace wreckfest_telemetry {

// Copy of `tuning` with every value +1 (0-4 -> 1-5): the numbering Wreckfest
// players use for tune positions. Internal state and game-facing reads stay
// 0-indexed; anything written to a log/JSON file or sent to the API goes
// through this first.
std::map<std::string, int> ToDisplayTuning(const std::map<std::string, int>& tuning);

}  // namespace wreckfest_telemetry
