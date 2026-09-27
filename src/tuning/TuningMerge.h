#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>

namespace wreckfest_telemetry {

// Fixed order shared by the Tune screen, the live tune array, and cars5.ccrs.
inline constexpr std::array<const char*, 4> kTuneCategories = {"SUSPENSION", "GEARING", "DIFFERENTIAL", "BRAKES"};

uint32_t Fnv1a(std::string_view data);

// True for a non-empty live reading where every category is 0. That is what
// never-initialised slider widgets read in a session where the Tune screen
// was never opened, and it can't be told apart from real zeros, so the
// caller defers to the save file (which persists index 0 faithfully).
bool IsUninitializedLiveTuning(const std::map<std::string, int>& live);

// `save` with `live` overlaid per category -- live wins wherever both exist.
std::map<std::string, int> MergeTuning(const std::map<std::string, int>& save,
                                       const std::map<std::string, int>& live);

}  // namespace wreckfest_telemetry
