#include "strings/AssistSettings.h"

#include <array>
#include <set>

#include "debug/DebugLog.h"
#include "memory/Offsets.h"
#include "memory/ProcessMemory.h"
#include "strings/HashRegistry.h"
#include "tuning/AssemblyTuning.h"

namespace wreckfest_telemetry {

using namespace offsets;

namespace {

constexpr std::array<const char*, 3> kShiftingLevels = {"automatic", "manual", "manual_clutch"};
constexpr std::array<const char*, 3> kAssistLevels = {"off", "half", "full"};  // shared by abs/tcs/stability

void SetIfInRange(std::map<std::string, std::string>& out, const char* key,
                   const std::optional<int32_t>& value, const std::array<const char*, 3>& levels) {
    if (value && *value >= 0 && static_cast<size_t>(*value) < levels.size()) {
        out[key] = levels[static_cast<size_t>(*value)];
    }
}

std::map<std::string, std::string> AssistsAt(uintptr_t obj) {
    std::map<std::string, std::string> result;
    SetIfInRange(result, "shifting", ReadI32(obj + ASSIST_SHIFTING_OFF), kShiftingLevels);
    SetIfInRange(result, "abs", ReadI32(obj + ASSIST_ABS_OFF), kAssistLevels);
    SetIfInRange(result, "traction_control", ReadI32(obj + ASSIST_TCS_OFF), kAssistLevels);
    SetIfInRange(result, "stability_control", ReadI32(obj + ASSIST_STABILITY_OFF), kAssistLevels);
    return result;
}

}  // namespace

std::map<std::string, std::string> ReadAssistSettings(uintptr_t tableBase) {
    if (tableBase == 0) return {};
    auto obj = HashRegistryLookup(tableBase, ASSISTS_REGISTRY_NAME);
    if (!obj) return {};
    return AssistsAt(*obj);
}

std::map<int, std::map<std::string, std::string>> ReadAssistsBySlot(uintptr_t moduleBase, uintptr_t tableBase) {
    std::map<int, std::map<std::string, std::string>> bySlot;
    std::set<int> duplicated;
    if (moduleBase == 0 || tableBase == 0) return bySlot;
    for (int i = 0; i < RACE_SIM_ARRAY_LEN; ++i) {
        auto sim = ReadU64(moduleBase + RACE_SIM_ARRAY_OFF + i * 8);
        if (!sim || *sim < 0x10000) continue;
        auto record = ReadU64(*sim + SIM_PLAYER_RECORD_OFF);
        if (!record || *record < 0x10000) continue;
        auto idx = ReadI32(*record + RECORD_ASSEMBLY_IDX_OFF);
        if (!idx || *idx <= 0 || static_cast<uint32_t>(*idx) >= REGISTRY_CAPACITY) continue;
        auto name = ReadCString(tableBase + NAME_ARR_OFF + static_cast<uint64_t>(*idx) * REGISTRY_STRIDE, 160);
        auto parsed = name ? ParseAssemblyName(*name) : std::nullopt;
        if (!parsed) {
            DebugLog((L"assists: sim[" + std::to_wstring(i) + L"] record's car index doesn't name an assembly -- skipped")
                         .c_str());
            continue;
        }
        if (parsed->ai) continue;
        auto obj = ReadU64(*record + RECORD_ASSISTS_OFF);
        auto assists = obj && *obj >= 0x10000 ? AssistsAt(*obj) : std::map<std::string, std::string>{};
        DebugLog((L"assists: sim[" + std::to_wstring(i) + L"] -> '" + WidenAscii(*name) + L"' -> " +
                  std::to_wstring(assists.size()) + L" of 4 resolved")
                     .c_str());
        if (assists.empty()) continue;
        if (!bySlot.emplace(parsed->slot, std::move(assists)).second) duplicated.insert(parsed->slot);
    }
    for (int slot : duplicated) {
        DebugLog((L"assists: slot " + std::to_wstring(slot) + L" read from more than one car -- dropped").c_str());
        bySlot.erase(slot);
    }
    return bySlot;
}

}  // namespace wreckfest_telemetry
