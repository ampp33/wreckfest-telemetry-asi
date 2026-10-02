#pragma once

#include <string>

namespace wreckfest_telemetry {

// `s` with every byte that isn't part of a well-formed UTF-8 sequence
// replaced by '?'. For free text read out of game memory (e.g. server
// names set by other people) before it reaches nlohmann::json, whose
// dump() throws on invalid UTF-8.
std::string SanitizeUtf8(const std::string& s);

}  // namespace wreckfest_telemetry
