#include "tuning/RaceCarTuning.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <iterator>

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

std::wstring Hex(uint64_t v) {
    wchar_t buf[24];
    std::swprintf(buf, std::size(buf), L"0x%llX", static_cast<unsigned long long>(v));
    return buf;
}

// Debug-log rendering of text read from game memory: non-ASCII bytes
// (localized part names) as \xNN, since a non-ASCII wide char can put the
// log's wofstream into a failed state and silence the rest of the log.
std::wstring Escaped(const std::string& s) {
    std::wstring out;
    for (unsigned char c : s) {
        if (c >= 0x20 && c < 0x7F) {
            out += static_cast<wchar_t>(c);
        } else {
            wchar_t buf[8];
            std::swprintf(buf, std::size(buf), L"\\x%02X", c);
            out += buf;
        }
    }
    return out;
}

std::vector<std::string> ReadAssemblyPaths(uintptr_t assembly) {
    std::vector<std::string> paths;
    for (int i = 0; i < kAssemblyMaxEntries; ++i) {
        auto ptr = ReadU64(assembly + i * kAssemblyEntryStride + kAssemblyEntryPathOff);
        if (!ptr || *ptr < 0x10000) continue;
        auto path = ReadCString(static_cast<uintptr_t>(*ptr), 160);
        DebugLog((L"parts:   entry " + std::to_wstring(i) + L": " +
                  (path ? L"'" + Escaped(*path) + L"'" : L"(unreadable string at " + Hex(*ptr) + L")"))
                     .c_str());
        if (path && !path->empty()) paths.push_back(*path);
    }
    return paths;
}

// An armor part's registry object (looked up by its path) is only 0x40
// bytes; its first field points to the part's record. The record holds
// pointers to the part's own properties -- the name property at +0x10 --
// and the part's weight in kg as a float at +0x100: what it adds to the car
// (0 for the "None" options). Each property holds its
// "VEHICLE_UPGRADE_<FIELD>_<hash>_<len>" loc key at +0x8, the localized
// value at +0x18 and the bare field name at +0x20.
//
// Names used to be found by scanning forward from the object for the first
// name property, but everything past its 0x40 bytes is unrelated heap, so
// that picked up neighbouring parts' names wherever the allocator put them
// (wrong on native Windows, right by luck on Proton). Name link verified
// live 2026-11-08 (Windows/Epic, Venom: Stock Bumper x2, None x3, Avenger
// Bars) against the upgrades screen; weight verified 2026-10-10 (Combat
// Crusher 120, Marauder Rammer 100, a 100 kg carstats drop on removing it).
constexpr uintptr_t kRecordNamePropOff = 0x10;
constexpr uintptr_t kPropKeyOff = 0x08;
constexpr uintptr_t kPropValueOff = 0x18;
constexpr uintptr_t kPropFieldOff = 0x20;
constexpr uintptr_t kPartWeightOff = 0x100;
constexpr float kPartMaxWeightKg = 2000.0f;

bool StartsWith(const std::string& s, const char* prefix) { return s.rfind(prefix, 0) == 0; }

std::optional<std::string> ReadStringAt(uintptr_t ptrAddr, size_t maxLen) {
    auto ptr = ReadU64(ptrAddr);
    if (!ptr || *ptr < 0x10000) return std::nullopt;
    return ReadCString(static_cast<uintptr_t>(*ptr), maxLen);
}

// Logs why PartDisplayName() gave up -- debug log only.
std::nullopt_t NameFail(const std::wstring& why) {
    DebugLog((L"armor:     name unresolved: " + why).c_str());
    return std::nullopt;
}

std::optional<std::string> PartDisplayName(uintptr_t record) {
    auto prop = ReadU64(record + kRecordNamePropOff);
    if (!prop || *prop < 0x10000) return NameFail(L"bad name property pointer " + Hex(prop.value_or(0)));
    auto base = static_cast<uintptr_t>(*prop);
    auto key = ReadStringAt(base + kPropKeyOff, 64);
    auto field = ReadStringAt(base + kPropFieldOff, 32);
    // Both checked, so a layout change reads as unresolved rather than as some other string.
    if (!key || !StartsWith(*key, "VEHICLE_UPGRADE_NAME_") || !field || *field != "VEHICLE_UPGRADE_NAME") {
        return NameFail(L"property " + Hex(base) + L" has key " + (key ? L"'" + Escaped(*key) + L"'" : L"?") +
                        L", field " + (field ? L"'" + Escaped(*field) + L"'" : L"?"));
    }
    auto value = ReadStringAt(base + kPropValueOff, 64);
    if (!value || value->empty()) return NameFail(L"key '" + Escaped(*key) + L"' has no value");
    DebugLog((L"armor:     name '" + Escaped(*value) + L"' (key '" + Escaped(*key) + L"')").c_str());
    return value;
}

std::optional<int> PartWeightKg(uintptr_t record) {
    auto weight = ReadF32(record + kPartWeightOff);
    DebugLog((L"armor:     weight raw " + (weight ? std::to_wstring(*weight) : L"unreadable")).c_str());
    if (!weight || !std::isfinite(*weight) || *weight < 0.0f || *weight > kPartMaxWeightKg) return std::nullopt;
    return static_cast<int>(std::lround(*weight));
}

ArmorPart ArmorDetails(uintptr_t tableBase, const std::string& path) {
    ArmorPart part;
    auto obj = HashRegistryLookup(tableBase, path);
    if (!obj || *obj == 0) {
        DebugLog((L"armor:   '" + Escaped(path) + L"' -> NOT FOUND in registry").c_str());
        return part;
    }
    auto record = ReadU64(static_cast<uintptr_t>(*obj));
    DebugLog((L"armor:   '" + Escaped(path) + L"' -> object " + Hex(*obj) + L", record " + Hex(record.value_or(0)))
                 .c_str());
    if (!record || *record < 0x10000) return part;
    part.name = PartDisplayName(static_cast<uintptr_t>(*record));
    part.weight_kg = PartWeightKg(static_cast<uintptr_t>(*record));
    return part;
}

std::wstring FormatParts(const CarParts& parts) {
    std::wstring out = L"engine=" + (parts.engine.empty() ? L"?" : WidenAscii(parts.engine)) + L" |";
    for (const auto& [slot, preset] : parts.engine_parts) out += L" " + WidenAscii(slot) + L"=" + WidenAscii(preset);
    out += L" | armor:";
    for (const auto& [slot, part] : parts.armor) {
        out += L" " + WidenAscii(slot) + L"=" + WidenAscii(part.code) +
               (part.name ? L"('" + Escaped(*part.name) + L"', " : L"(no name, ") +
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
            DebugLog((L"parts: race car slot " + std::to_wstring(car.slot) + L" '" + Escaped(name) + L"' assembly " +
                      Hex(obj) + L" entries:")
                         .c_str());
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
            DebugLog((L"parts: player '" + Escaped(p.name) + L"' slot " + std::to_wstring(*p.slot_index) +
                      L" -> assembly '" + Escaped(match->name) + L"'")
                         .c_str());
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
