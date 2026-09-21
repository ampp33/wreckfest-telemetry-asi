#include "strings/AssistSettings.h"

#include <array>

#include "memory/Offsets.h"
#include "memory/ProcessMemory.h"
#include "strings/HashRegistry.h"

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

}  // namespace

std::map<std::string, std::string> ReadAssistSettings(uintptr_t tableBase) {
    std::map<std::string, std::string> result;
    if (tableBase == 0) return result;

    auto obj = HashRegistryLookup(tableBase, ASSISTS_REGISTRY_NAME);
    if (!obj) return result;

    SetIfInRange(result, "shifting", ReadI32(*obj + ASSIST_SHIFTING_OFF), kShiftingLevels);
    SetIfInRange(result, "abs", ReadI32(*obj + ASSIST_ABS_OFF), kAssistLevels);
    SetIfInRange(result, "traction_control", ReadI32(*obj + ASSIST_TCS_OFF), kAssistLevels);
    SetIfInRange(result, "stability_control", ReadI32(*obj + ASSIST_STABILITY_OFF), kAssistLevels);
    return result;
}

}  // namespace wreckfest_telemetry
