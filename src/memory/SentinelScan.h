#pragma once

#include <cstdint>
#include <vector>

#include "memory/ProcessMemory.h"

namespace wreckfest_telemetry {

// Scans writable regions for the sentinel byte pattern that marks a
// player-slot's start, used when the static pointer chain (FastFindSlots)
// is stale after a game update. Returns candidate slot base addresses.
std::vector<uintptr_t> ScanForSentinels(const std::vector<MemoryRegion>& regions);

// Keeps the run of addresses spaced exactly SLOT_STRIDE apart (the real
// 24-slot array), discarding stray hits from UI mirrors/buffers. Returns
// the original list unchanged if no run of at least 10 is found.
std::vector<uintptr_t> ClusterSentinelHits(std::vector<uintptr_t> hits);

}  // namespace wreckfest_telemetry
