#include "tuning/TuningMerge.h"

namespace wreckfest_telemetry {

std::map<std::string, int> MergeTuning(const std::map<std::string, int>& base,
                                       const std::map<std::string, int>& overlay) {
    std::map<std::string, int> result = base;
    for (const auto& [category, index] : overlay) {
        result[category] = index;
    }
    return result;
}

}  // namespace wreckfest_telemetry
