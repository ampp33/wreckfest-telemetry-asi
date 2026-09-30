#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace wreckfest_telemetry {

// Every car spawned for a race registers its own part list in the hash
// registry as "vehicle/<NN>/<codename>_<id>/assembly.veas", where NN is the
// car's two-digit player slot and <id> a per-spawn number. AI cars carry an
// "_ai" codename ("04_european_ai_<id>"). The assembly lists the parts the
// car was actually built with -- including a tune set on the pre-race
// screen after Restart, which cars5.ccrs doesn't hold yet -- and it lasts
// through the race to the results screen. Verified live 2026-09-29 (solo,
// Restart retune, and a 24-car AI race with the same model as an AI car).
struct AssemblyName {
    int slot = -1;
    std::string codename;  // "_ai" suffix stripped
    bool ai = false;
};

// Parses a registry name of the form above. nullopt for anything else.
std::optional<AssemblyName> ParseAssemblyName(const std::string& name);

// 0-4 index per category from an assembly's part paths
// ("data/vehicle/<codename>/part/<part>/<preset>.<ext>"). Only paths for
// `codename` count; the first path per category wins.
std::map<std::string, int> TuningFromPartPaths(const std::vector<std::string>& paths, const std::string& codename);

}  // namespace wreckfest_telemetry
