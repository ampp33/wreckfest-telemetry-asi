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

// Every racer's assists (online included), keyed by car slot -- the NN of
// their car's "vehicle/NN/.../assembly.veas". Same labels as above. A car
// whose chain doesn't resolve, or whose slot appears twice, is left out.
std::map<int, std::map<std::string, std::string>> ReadAssistsBySlot(uintptr_t moduleBase, uintptr_t tableBase);

}  // namespace wreckfest_telemetry
