#include "tuning/TuningMerge.h"

namespace wreckfest_telemetry {

uint32_t Fnv1a(std::string_view data) {
    uint32_t h = 0x811c9dc5u;
    for (unsigned char b : data) {
        h = (h ^ b) * 0x1000193u;
    }
    return h;
}

void LiveTuningTracker::Observe(const std::string& carKey, const std::map<std::string, int>& live) {
    if (carKey != car_) {
        car_ = carKey;
        changes_.clear();
    }
    // lastSeen_ deliberately survives a car switch: a slider still holding
    // the previous car's value must not count as a change for the new one.
    for (const auto& [category, index] : live) {
        auto it = lastSeen_.find(category);
        if (it != lastSeen_.end() && it->second != index) {
            changes_[category] = index;
        }
        lastSeen_[category] = index;
    }
}

std::map<std::string, int> LiveTuningTracker::ChangesFor(const std::string& carKey) const {
    if (carKey.empty() || carKey != car_) return {};
    return changes_;
}

std::map<std::string, int> MergeTuning(const std::map<std::string, int>& save,
                                       const std::map<std::string, int>& live) {
    std::map<std::string, int> result = save;
    for (const auto& [category, index] : live) {
        result[category] = index;
    }
    return result;
}

}  // namespace wreckfest_telemetry
