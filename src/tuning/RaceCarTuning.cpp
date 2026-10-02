#include "tuning/RaceCarTuning.h"

#include "debug/DebugLog.h"
#include "memory/ProcessMemory.h"
#include "strings/HashRegistry.h"
#include "tuning/AssemblyTuning.h"
#include "tuning/SaveFileTuning.h"
#include "tuning/TuningDisplay.h"
#include "tuning/TuningMerge.h"

namespace wreckfest_telemetry {

namespace {

// Assembly layout: an array of 0x20-byte part entries, each holding a
// pointer to the part's path string at +0x10. ~40 entries in practice
// (engine parts, tuning parts, tires, body parts); non-path slots and
// other cars' paths are filtered out by TuningFromPartPaths().
constexpr uintptr_t kAssemblyEntryStride = 0x20;
constexpr uintptr_t kAssemblyEntryPathOff = 0x10;
constexpr int kAssemblyMaxEntries = 64;

std::vector<std::string> ReadAssemblyPaths(uintptr_t assembly) {
    std::vector<std::string> paths;
    for (int i = 0; i < kAssemblyMaxEntries; ++i) {
        auto ptr = ReadU64(assembly + i * kAssemblyEntryStride + kAssemblyEntryPathOff);
        if (!ptr || *ptr < 0x10000) continue;
        auto path = ReadCString(static_cast<uintptr_t>(*ptr), 160);
        if (path && !path->empty()) paths.push_back(*path);
    }
    return paths;
}

// Debug-log rendering: 1-indexed (player-facing numbering), unlike the
// 0-indexed values held internally.
std::wstring FormatTuning(const std::map<std::string, int>& tuning) {
    std::wstring out;
    for (const auto& [category, index] : ToDisplayTuning(tuning)) {
        if (!out.empty()) out += L", ";
        out += WidenAscii(category) + L"=" + std::to_wstring(index);
    }
    return out.empty() ? L"(none)" : out;
}

}  // namespace

std::vector<RaceCar> ReadRaceCars(uintptr_t tableBase) {
    std::vector<RaceCar> cars;
    if (tableBase == 0) return cars;
    for (const auto& [name, obj] : RegistryEntriesMatching(tableBase, "vehicle/", "/assembly.veas")) {
        auto parsed = ParseAssemblyName(name);
        if (!parsed) continue;
        RaceCar car{name, parsed->slot, parsed->codename, parsed->ai, {}};
        // AI cars are mostly built from AI-only part files with no presets
        // (typically only the gearbox resolves), so their tuning isn't read.
        if (!car.ai) car.tuning = TuningFromPartPaths(ReadAssemblyPaths(obj), car.codename);
        DebugLog((L"tuning: race car slot " + std::to_wstring(car.slot) + L" '" + WidenAscii(name) + L"' -> " +
                  (car.ai ? L"AI, tuning not read" : FormatTuning(car.tuning)))
                     .c_str());
        cars.push_back(std::move(car));
    }
    DebugLog((L"tuning: " + std::to_wstring(cars.size()) + L" race car assemblies found").c_str());
    return cars;
}

void AssignRaceCars(std::vector<PlayerResult>& players, const std::vector<RaceCar>& cars) {
    for (auto& p : players) {
        if (!p.slot_index) continue;
        const RaceCar* match = nullptr;
        int matches = 0;
        for (const auto& car : cars) {
            if (car.slot == *p.slot_index) {
                match = &car;
                ++matches;
            }
        }
        if (matches == 1) {
            p.ai = match->ai;
            if (!p.is_local) p.tuning = match->tuning;  // empty for an AI car
        } else if (matches > 1) {
            DebugLog((L"tuning: slot " + std::to_wstring(*p.slot_index) + L" has " + std::to_wstring(matches) +
                      L" race car assemblies -- ambiguous, '" + WidenAscii(p.name) + L"' left unmatched")
                         .c_str());
        }
    }
}

std::map<std::string, int> ReadTuningForRace(const std::vector<RaceCar>& cars, const std::string& carName,
                                             const std::optional<std::string>& carKey, std::optional<int> localSlot) {
    std::optional<std::string> codename;
    if (carKey) {
        if (auto entry = FindVehicleNameKeyInSave(*carKey)) codename = entry->codename;
    }
    DebugLog((L"tuning: local slot " + (localSlot ? std::to_wstring(*localSlot) : L"(unknown)") + L", codename '" +
              (codename ? WidenAscii(*codename) : L"(unknown)") + L"'")
                 .c_str());

    const RaceCar* chosen = nullptr;
    if (localSlot) {
        for (const auto& car : cars) {
            if (!car.ai && car.slot == *localSlot) chosen = &car;
        }
        if (chosen && codename && chosen->codename != *codename) {
            DebugLog((L"tuning: WARNING -- slot " + std::to_wstring(*localSlot) + L"'s race car is codename '" +
                      WidenAscii(chosen->codename) + L"', not the local car's -- ignored")
                         .c_str());
            chosen = nullptr;
        }
    }
    if (!chosen && codename) {
        // No slot match: fall back to the only non-AI car of this model, if
        // there's exactly one (another player in the same model makes it
        // ambiguous).
        const RaceCar* only = nullptr;
        int matches = 0;
        for (const auto& car : cars) {
            if (!car.ai && car.codename == *codename) {
                only = &car;
                ++matches;
            }
        }
        if (matches == 1) {
            chosen = only;
            DebugLog(L"tuning: no slot match -- using the only non-AI race car of this model");
        } else if (matches > 1) {
            DebugLog((L"tuning: no slot match and " + std::to_wstring(matches) +
                      L" non-AI race cars of this model -- ambiguous, ignored")
                         .c_str());
        }
    }
    std::map<std::string, int> raceCar;
    if (chosen) {
        raceCar = chosen->tuning;
        DebugLog((L"tuning: local race car '" + WidenAscii(chosen->name) + L"' -> " + FormatTuning(raceCar)).c_str());
    } else {
        DebugLog(L"tuning: no race car found for the local player");
    }

    std::map<std::string, int> save;
    if (raceCar.size() < kTuneCategories.size()) {
        if (!carName.empty()) {
            save = ReadTuningFromSave(carName);
        } else {
            DebugLog(L"tuning: local player's car name is empty -- save-file read skipped");
        }
        DebugLog((L"tuning: save file " + FormatTuning(save)).c_str());
    }

    auto merged = MergeTuning(save, raceCar);
    DebugLog((L"tuning: final " + FormatTuning(merged)).c_str());
    for (const char* category : kTuneCategories) {
        std::wstring source = raceCar.count(category) ? L"race car assembly"
                              : save.count(category)  ? L"save file"
                                                      : L"MISSING (not posted)";
        DebugLog((L"tuning:   " + WidenAscii(category) + L" from " + source).c_str());
    }
    return merged;
}

}  // namespace wreckfest_telemetry
