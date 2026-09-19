#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace wreckfest_telemetry {

uint32_t WfHash(const std::string& data, uint32_t seed = 0);

// module_base + PTR_DAT_OFFSET -> the engine's global name/object hash
// registry table. Stable for the process's lifetime; cached internally.
std::optional<uintptr_t> GetTableBase(uintptr_t moduleBase);

// Looks up a registered engine object (e.g. "CLIENT", "event_settings") by
// name through the hash registry.
std::optional<uintptr_t> HashRegistryLookup(uintptr_t tableBase, const std::string& name);

}  // namespace wreckfest_telemetry
