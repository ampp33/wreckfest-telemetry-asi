#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wreckfest_telemetry {

struct MemoryRegion {
    uintptr_t base;
    size_t size;
};

// Base load address of a module already loaded into this process, or
// nullopt if it isn't loaded (equivalent to the Python tool's
// find_module_base(), which parsed /proc/<pid>/maps externally -- in-process
// this is just GetModuleHandleW).
std::optional<uintptr_t> FindModuleBase(const wchar_t* moduleName);

// Committed, private, writable regions in this process's address space.
// Equivalent to the Python tool's _writable_regions() (which parsed
// /proc/<pid>/maps), used by the sentinel-scan fallback.
std::vector<MemoryRegion> EnumerateWritableRegions();

// SEH-guarded primitive reads. Each returns std::nullopt on any fault
// (unmapped page, bad alignment, etc.) rather than crashing -- see
// memory/SehGuard.h.
std::optional<int32_t> ReadI32(uintptr_t addr);
std::optional<uint64_t> ReadU64(uintptr_t addr);
std::optional<float> ReadF32(uintptr_t addr);

// Reads up to maxLen bytes at addr and stops at the first NUL. Returns
// nullopt if the read faults before a NUL is found within maxLen bytes.
std::optional<std::string> ReadCString(uintptr_t addr, size_t maxLen = 64);

}  // namespace wreckfest_telemetry
