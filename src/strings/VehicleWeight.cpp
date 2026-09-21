#include "strings/VehicleWeight.h"

#include <cmath>

#include "memory/Offsets.h"
#include "memory/ProcessMemory.h"
#include "strings/HashRegistry.h"

namespace wreckfest_telemetry {

using namespace offsets;

int ReadVehicleWeight(uintptr_t tableBase) {
    if (tableBase == 0) return 0;

    auto obj = HashRegistryLookup(tableBase, CARSTATS_REGISTRY_NAME);
    if (!obj) return 0;

    auto weight = ReadF32(*obj + CARSTATS_WEIGHT_OFF);
    if (!weight || *weight < 200.0f || *weight > 15000.0f) return 0;
    return static_cast<int>(std::lround(*weight));
}

}  // namespace wreckfest_telemetry
