#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "json.hpp"

namespace wreckfest_telemetry {

// A car's installed engine and armor upgrades, from the same race assembly
// part paths as its tuning (see AssemblyTuning.h). Each assembly part path
// ends in a per-slot extension (".vfbu" = front bumper, ...), so a slot is
// identified by its extension wherever the file lives -- cars without armor
// options point their armor slots at "data/vehicle/shared/part/...".
// Verified live 2026-10-08 against the garage UI and two online races.
struct ArmorPart {
    std::string code;                 // file stem, e.g. "bumper_front3" -- debug log only, not in the JSON
    std::optional<std::string> name;  // display name in the game's language; nullopt if unresolved
    std::optional<int> weight_kg;     // what the part adds to the car (0 for "None" parts); nullopt if unresolved
};

struct CarParts {
    // "street"/"sport"/"race", or the raw engine file stem if it doesn't
    // fit the class-letter pattern. Empty if no engine part was found.
    std::string engine;
    // Engine sub-part -> raw preset name ("stock"/"street"/"sport"/"racing"
    // seen so far). Kept raw rather than mapped to a level: tournament
    // reward parts may use other preset names (unverified).
    std::map<std::string, std::string> engine_parts;
    // "front_bumper"/"rear_bumper"/"roll_cage"/"side_protector"/"window_bars".
    std::map<std::string, ArmorPart> armor;

    bool empty() const { return engine.empty() && engine_parts.empty() && armor.empty(); }
};

// Engine tier from an engine part file stem. The tier is the class letter
// of the installed block: c = street, b = sport, a = race. A car's native
// engine is named after its own class ("b_class(327)", "c_class"), the
// other options "engine_block_<letter>". nullopt for anything else.
std::optional<std::string> EngineTier(const std::string& stem);

// Resolves an armor part's name and weight_kg from its full path (as stored
// in the assembly), leaving either nullopt if it can't; `code` is filled in
// by PartsFromPartPaths.
using ArmorLookup = std::function<ArmorPart(const std::string& path)>;

// Parts from an assembly's part paths. Only paths for `codename` or the
// shared part directory count; the first path per slot wins. Pass a null
// `lookupArmor` to skip armor names/weights.
CarParts PartsFromPartPaths(const std::vector<std::string>& paths, const std::string& codename,
                            const ArmorLookup& lookupArmor);

// JSON for logs/the API: {"engine", "engine_parts", "armor": {slot: {"name", "weight_kg"}}},
// fields left out when unresolved, an unresolved armor name/weight sent as null.
nlohmann::json PartsToJson(const CarParts& parts);

}  // namespace wreckfest_telemetry
