#include "tuning/TuningDisplay.h"

namespace wreckfest_telemetry {

std::map<std::string, int> ToDisplayTuning(const std::map<std::string, int>& tuning) {
    std::map<std::string, int> result;
    for (const auto& [category, index] : tuning) {
        result[category] = index + 1;
    }
    return result;
}

}  // namespace wreckfest_telemetry
