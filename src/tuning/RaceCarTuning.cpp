#include "tuning/RaceCarTuning.h"

#include <chrono>
#include <cmath>

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

// A part object (resolved by its path through the registry) is a list of
// properties, each a pointer to its "VEHICLE_UPGRADE_<FIELD>_<id>" loc key,
// then the localized value 0x10 later and the bare field name 0x18 later.
// The name, when present, has been the first such property -- usually at
// +0x58, but parts with extra leading properties (chain attachments etc.)
// push it as deep as +0x3338 (e.g. 05_american window_bar1). Scanning stops
// at the first upgrade property so a nameless part can't pick up the next
// object's name. A sweep of all 2178 armor parts loaded in the garage
// (2026-10-10) resolved every name with this bound; 0x200 missed 51.
constexpr uintptr_t kPartPropScanStart = 0x40;
constexpr uintptr_t kPartPropScanEnd = 0x4000;

// A part object's first field points to a record holding the part's
// weight in kg as a float at +0x100 -- what it adds to the car (0 for the
// "None" options). Verified live 2026-10-10 against the upgrades screen
// (e.g. Combat Crusher 120, Marauder Rammer 100) and a 100 kg drop in
// career-carstats-temp weight on removing the Marauder Rammer.
constexpr uintptr_t kPartWeightOff = 0x100;
constexpr float kPartMaxWeightKg = 2000.0f;

bool StartsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

std::optional<std::string> PartDisplayName(uintptr_t obj) {
    for (uintptr_t off = kPartPropScanStart; off < kPartPropScanEnd; off += 8) {
        auto keyPtr = ReadU64(obj + off);
        if (!keyPtr || *keyPtr < 0x10000) continue;
        auto key = ReadCString(static_cast<uintptr_t>(*keyPtr), 64);
        if (!key || !StartsWith(*key, "VEHICLE_UPGRADE_")) continue;
        if (!StartsWith(*key, "VEHICLE_UPGRADE_NAME_")) return std::nullopt;
        auto fieldPtr = ReadU64(obj + off + 0x18);
        auto field = fieldPtr ? ReadCString(static_cast<uintptr_t>(*fieldPtr), 32) : std::nullopt;
        if (!field || *field != "VEHICLE_UPGRADE_NAME") return std::nullopt;
        auto valuePtr = ReadU64(obj + off + 0x10);
        auto value = valuePtr && *valuePtr >= 0x10000 ? ReadCString(static_cast<uintptr_t>(*valuePtr), 64) : std::nullopt;
        if (!value || value->empty()) return std::nullopt;
        return value;
    }
    return std::nullopt;
}

std::optional<int> PartWeightKg(uintptr_t obj) {
    auto record = ReadU64(obj);
    if (!record || *record < 0x10000) return std::nullopt;
    auto weight = ReadF32(static_cast<uintptr_t>(*record) + kPartWeightOff);
    if (!weight || !std::isfinite(*weight) || *weight < 0.0f || *weight > kPartMaxWeightKg) return std::nullopt;
    return static_cast<int>(std::lround(*weight));
}

ArmorPart ArmorDetails(uintptr_t tableBase, const std::string& path) {
    ArmorPart part;
    auto obj = HashRegistryLookup(tableBase, path);
    if (!obj || *obj == 0) return part;
    part.name = PartDisplayName(static_cast<uintptr_t>(*obj));
    part.weight_kg = PartWeightKg(static_cast<uintptr_t>(*obj));
    return part;
}

std::wstring FormatParts(const CarParts& parts) {
    std::wstring out = L"engine=" + (parts.engine.empty() ? L"?" : WidenAscii(parts.engine)) + L" |";
    for (const auto& [slot, preset] : parts.engine_parts) out += L" " + WidenAscii(slot) + L"=" + WidenAscii(preset);
    out += L" | armor:";
    for (const auto& [slot, part] : parts.armor) {
        // Names can be non-ASCII in other game languages -- logged as a
        // resolved/unresolved marker only, to keep the wide log stream safe.
        out += L" " + WidenAscii(slot) + L"=" + WidenAscii(part.code) + (part.name ? L"(named, " : L"(no name, ") +
               (part.weight_kg ? std::to_wstring(*part.weight_kg) + L" kg)" : L"? kg)");
    }
    return out;
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
    using Clock = std::chrono::steady_clock;
    std::vector<RaceCar> cars;
    if (tableBase == 0) return cars;
    auto start = Clock::now();
    auto entries = RegistryEntriesMatching(tableBase, "vehicle/", "/assembly.veas");
    auto scanned = Clock::now();
    Clock::duration armorTime{};
    auto lookupArmor = [&](const std::string& path) {
        auto t = Clock::now();
        auto part = ArmorDetails(tableBase, path);
        armorTime += Clock::now() - t;
        return part;
    };
    for (const auto& [name, obj] : entries) {
        auto parsed = ParseAssemblyName(name);
        if (!parsed) continue;
        RaceCar car{name, parsed->slot, parsed->codename, parsed->ai, {}, {}};
        // AI cars are mostly built from AI-only part files with no presets
        // (typically only the gearbox resolves), so their tuning isn't read;
        // their parts aren't either, to keep the payload small.
        if (!car.ai) {
            auto paths = ReadAssemblyPaths(obj);
            car.tuning = TuningFromPartPaths(paths, car.codename);
            car.parts = PartsFromPartPaths(paths, car.codename, lookupArmor);
        }
        DebugLog((L"tuning: race car slot " + std::to_wstring(car.slot) + L" '" + WidenAscii(name) + L"' -> " +
                  (car.ai ? L"AI, tuning not read" : FormatTuning(car.tuning)))
                     .c_str());
        if (!car.ai) {
            DebugLog((L"parts: race car slot " + std::to_wstring(car.slot) + L" -> " + FormatParts(car.parts)).c_str());
        }
        cars.push_back(std::move(car));
    }
    auto ms = [](Clock::duration d) { return std::to_wstring(std::chrono::duration<double, std::milli>(d).count()); };
    DebugLog((L"tuning: " + std::to_wstring(cars.size()) + L" race car assemblies found -- registry scan " +
              ms(scanned - start) + L" ms, assemblies " + ms(Clock::now() - scanned) + L" ms (armor details " +
              ms(armorTime) + L" ms)")
                 .c_str());
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
            p.parts = match->parts;                     // empty for an AI car
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
