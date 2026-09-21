#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace wreckfest_telemetry {

// {"shifting", "abs", "traction_control", "stability_control"} -> label, for
// whichever of the four resolve to a plausible 0-2 index. Returns {} (not a
// partial/misleading map) if the registry object itself can't be resolved;
// a single field that reads out of range is just omitted.
std::map<std::string, std::string> ReadAssistSettings(uintptr_t tableBase);

}  // namespace wreckfest_telemetry
