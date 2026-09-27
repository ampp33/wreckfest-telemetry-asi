#include "tuning/TuningMerge.h"

#include <algorithm>

namespace wreckfest_telemetry {

uint32_t Fnv1a(std::string_view data) {
    uint32_t h = 0x811c9dc5u;
    for (unsigned char b : data) {
        h = (h ^ b) * 0x1000193u;
    }
    return h;
}

bool IsUninitializedLiveTuning(const std::map<std::string, int>& live) {
    return !live.empty() && std::all_of(live.begin(), live.end(), [](const auto& kv) { return kv.second == 0; });
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
