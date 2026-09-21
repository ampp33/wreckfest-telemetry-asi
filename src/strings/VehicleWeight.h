#pragma once

#include <cstdint>

namespace wreckfest_telemetry {

// Local player's current vehicle weight in kg, live. Returns 0 (never a
// guessed placeholder) if the registry object can't be resolved or the
// value reads outside a plausible car-weight range.
int ReadVehicleWeight(uintptr_t tableBase);

}  // namespace wreckfest_telemetry
