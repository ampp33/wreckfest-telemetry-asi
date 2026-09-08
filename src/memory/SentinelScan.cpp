#include "memory/SentinelScan.h"

#include <algorithm>

#include "memory/Offsets.h"
#include "memory/SehGuard.h"

namespace wreckfest_telemetry {

using namespace offsets;

std::vector<uintptr_t> ScanForSentinels(const std::vector<MemoryRegion>& regions) {
    std::vector<uintptr_t> hits;
    for (const auto& region : regions) {
        if (region.size < 8) {
            continue;
        }
        auto regionHits = SehGuarded([&region] {
            std::vector<uintptr_t> found;
            const uint8_t* base = reinterpret_cast<const uint8_t*>(region.base);
            for (size_t i = 0; i + 8 <= region.size; ++i) {
                const int32_t* p = reinterpret_cast<const int32_t*>(base + i);
                if (p[0] == SENTINEL_A && p[1] == SENTINEL_B) {
                    found.push_back(region.base + i - OFF_SENTINEL_A);
                }
            }
            return found;
        });
        if (regionHits) {
            hits.insert(hits.end(), regionHits->begin(), regionHits->end());
        }
    }
    return hits;
}

std::vector<uintptr_t> ClusterSentinelHits(std::vector<uintptr_t> hits) {
    if (hits.size() < 3) {
        return hits;
    }
    std::sort(hits.begin(), hits.end());

    std::vector<uintptr_t> bestRun;
    std::vector<uintptr_t> currentRun{hits[0]};
    for (size_t i = 1; i < hits.size(); ++i) {
        if (hits[i] - currentRun.back() == static_cast<uintptr_t>(SLOT_STRIDE)) {
            currentRun.push_back(hits[i]);
        } else {
            if (currentRun.size() > bestRun.size()) bestRun = currentRun;
            currentRun = {hits[i]};
        }
    }
    if (currentRun.size() > bestRun.size()) bestRun = currentRun;

    return bestRun.size() >= 10 ? bestRun : hits;
}

}  // namespace wreckfest_telemetry
