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

// Decides which live Tune-screen readings belong to which car.
//
// The slider widgets are shared UI state, not per-car: a slider only
// refreshes when its own tab is shown, so a tab not shown for the current
// car still holds the last car's value (verified live 2026-09-28 --
// RoadSlayer's 3-1-5-2 read back unchanged after switching to Super Venom,
// whose real tuning was 5-4-5-2; opening Super Venom's Tune screen fixed
// only the Suspension tab that was shown). A raw live read therefore can't
// be trusted over the save file.
//
// What *can* be trusted is a slider changing while a car is selected --
// either the player moving it, or its tab being shown for this car. Each
// poll's reading is diffed against the previous one, and only categories
// that changed while `carKey` was selected are attributed to it. The first
// reading ever seen is a baseline, never a change, so uninitialised
// widgets (all zeros before the Tune screen is first opened) and values
// carried over from another car are ignored.
class LiveTuningTracker {
public:
    // Feeds one poll's live reading, taken while `carKey` was selected.
    // Switching cars drops the previous car's changes; by then the Tune
    // screen has been backed out of, so cars5.ccrs already holds them.
    void Observe(const std::string& carKey, const std::map<std::string, int>& live);

    // Categories whose slider changed while `carKey` was selected, with
    // their latest value. Empty for any other car.
    std::map<std::string, int> ChangesFor(const std::string& carKey) const;

private:
    std::string car_;
    std::map<std::string, int> lastSeen_;
    std::map<std::string, int> changes_;
};

// `save` with `live` overlaid per category -- live wins wherever both exist.
std::map<std::string, int> MergeTuning(const std::map<std::string, int>& save,
                                       const std::map<std::string, int>& live);

}  // namespace wreckfest_telemetry
