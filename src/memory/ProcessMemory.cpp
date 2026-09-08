#include "memory/ProcessMemory.h"

#include "memory/SehGuard.h"

namespace wreckfest_telemetry {

std::optional<uintptr_t> FindModuleBase(const wchar_t* moduleName) {
    HMODULE h = GetModuleHandleW(moduleName);
    if (h == nullptr) {
        return std::nullopt;
    }
    return reinterpret_cast<uintptr_t>(h);
}

std::vector<MemoryRegion> EnumerateWritableRegions() {
    std::vector<MemoryRegion> regions;
    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t addr = 0;
    while (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == sizeof(mbi)) {
        bool writable = mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE &&
                         (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_WRITECOPY);
        if (writable) {
            regions.push_back({reinterpret_cast<uintptr_t>(mbi.BaseAddress), mbi.RegionSize});
        }
        uintptr_t next = reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (next <= addr) {
            break;  // wrapped or zero-size region; stop rather than loop forever
        }
        addr = next;
    }
    return regions;
}

std::optional<int32_t> ReadI32(uintptr_t addr) {
    return SehGuarded([addr] { return *reinterpret_cast<volatile int32_t*>(addr); });
}

std::optional<uint64_t> ReadU64(uintptr_t addr) {
    return SehGuarded([addr] { return *reinterpret_cast<volatile uint64_t*>(addr); });
}

std::optional<std::string> ReadCString(uintptr_t addr, size_t maxLen) {
    auto raw = SehGuarded([addr, maxLen] {
        std::string s;
        s.reserve(maxLen);
        const char* p = reinterpret_cast<const char*>(addr);
        for (size_t i = 0; i < maxLen && p[i] != '\0'; ++i) {
            s.push_back(p[i]);
        }
        return s;
    });
    return raw;
}

}  // namespace wreckfest_telemetry
