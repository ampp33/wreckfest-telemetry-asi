#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace wreckfest_telemetry {

uint32_t WfHash(const std::string& data, uint32_t seed = 0);

// module_base + PTR_DAT_OFFSET -> the engine's global name/object hash
// registry table. Stable for the process's lifetime; cached internally.
std::optional<uintptr_t> GetTableBase(uintptr_t moduleBase);

// Looks up a registered engine object (e.g. "CLIENT", "event_settings") by
// name through the hash registry.
std::optional<uintptr_t> HashRegistryLookup(uintptr_t tableBase, const std::string& name);

// Every registered {name, object} whose name starts with `prefix` and ends
// with `suffix`, for names that can't be hashed because part of them isn't
// known up front (e.g. a per-spawn id). Scans the whole registry, so call
// it sparingly (once per race, not per poll). Returns at most `maxResults`
// entries -- keep the pattern narrow: a bare prefix like "vehicle/" alone
// matches ~24k names in a full race.
std::vector<std::pair<std::string, uintptr_t>> RegistryEntriesMatching(uintptr_t tableBase, const std::string& prefix,
                                                                       const std::string& suffix,
                                                                       size_t maxResults = 64);

}  // namespace wreckfest_telemetry
