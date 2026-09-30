#pragma once

#include <array>
#include <map>
#include <string>

namespace wreckfest_telemetry {

// Fixed order shared by the Tune screen and cars5.ccrs.
inline constexpr std::array<const char*, 4> kTuneCategories = {"SUSPENSION", "GEARING", "DIFFERENTIAL", "BRAKES"};

// `base` with `overlay` applied per category -- overlay wins wherever both exist.
std::map<std::string, int> MergeTuning(const std::map<std::string, int>& base,
                                       const std::map<std::string, int>& overlay);

}  // namespace wreckfest_telemetry
