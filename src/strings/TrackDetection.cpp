#include "strings/TrackDetection.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <vector>

#include "memory/Offsets.h"
#include "memory/ProcessMemory.h"
#include "strings/HashRegistry.h"

namespace wreckfest_telemetry {

using namespace offsets;

namespace {

const std::vector<std::string>& KnownTracks() {
    static const std::vector<std::string> tracks = {
        "Big Valley Speedway", "Bleak City", "Bloomfield Speedway", "Bonebreaker Valley",
        "Boulder Bank Circuit", "Clayridge Circuit", "Crash Canyon", "Deathloop",
        "Devil's Canyon", "Dirt Devil Stadium", "Drytown Desert Circuit", "Eagles Peak Motorpark",
        "Espedalen Raceway", "FinnCross Circuit", "Fire Rock Raceway", "Firwood Motocenter",
        "Hellride", "Hillstreet Circuit", "Hilltop Stadium", "Kingston Raceway",
        "Maasten Motocenter", "Madman Stadium", "Midwest Motocenter", "Motorcity Circuit",
        "Mudford Motorpark", "Northfolk Ring", "Northland Raceway", "Pinehills Raceway",
        "Rally Trophy", "Rattlesnake Racepark", "Rockfield Roughspot", "Rosenheim Raceway",
        "Sandstone Raceway", "Savolax Sandpit", "Torsdalen Circuit", "Tribend Speedway",
        "Vale Falls Circuit", "Wrecknado",
    };
    return tracks;
}

const std::vector<std::string>& KnownVariations() {
    // Longer/compound names must precede any string they start with.
    static const std::vector<std::string> variations = {
        "Race Track Reverse", "Race Track", "Outer Oval Loop", "Outer Oval",
        "Asphalt Oval Reverse", "Asphalt Oval", "Short Route Reverse", "Short Route",
        "Main Route Reverse", "Main Route", "Rally Circuit Reverse", "Rally Circuit",
        "Outer Route Reverse", "Outer Route", "Racing Track Reverse", "Racing Track",
        "Short Circuit Reverse", "Short Circuit", "Main Circuit Reverse", "Main Circuit",
        "Alt Route Reverse", "Alt Route", "Inner Route Reverse", "Inner Route",
        "Figure 8", "Free Route", "Dirt Oval", "Mud Oval", "Wild Circuit",
        "Inner Oval", "Oval", "Special Stage", "Reverse Circuit",
        "Trophy Circuit", "Dirt Speedway", "Open Circuit",
    };
    return variations;
}

const std::map<std::string, std::pair<std::string, std::string>>& KnownCodenames() {
    static const std::map<std::string, std::pair<std::string, std::string>> codenames = {
        {"crm02_1", {"Devil's Canyon", "Race Track"}},
        {"dirt_speedway_dirt_oval", {"Bloomfield Speedway", "Dirt Oval"}},
    };
    return codenames;
}

std::string Snake(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool lastWasSep = false;
    for (char c : s) {
        char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        bool isAlnum = (lower >= 'a' && lower <= 'z') || (lower >= '0' && lower <= '9');
        if (isAlnum) {
            out.push_back(lower);
            lastWasSep = false;
        } else if (!lastWasSep && !out.empty()) {
            out.push_back('_');
            lastWasSep = true;
        }
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    return out;
}

struct TrackSnakeTables {
    std::map<std::string, std::string> trackBySnake;
    std::map<std::string, std::string> variationBySnake;
    std::vector<std::string> trackSnakeKeysByLenDesc;
};

const TrackSnakeTables& SnakeTables() {
    static const TrackSnakeTables tables = [] {
        TrackSnakeTables t;
        for (const auto& track : KnownTracks()) {
            t.trackBySnake[Snake(track)] = track;
        }
        for (const auto& variation : KnownVariations()) {
            t.variationBySnake[Snake(variation)] = variation;
        }
        for (const auto& [snake, _] : t.trackBySnake) {
            t.trackSnakeKeysByLenDesc.push_back(snake);
        }
        std::sort(t.trackSnakeKeysByLenDesc.begin(), t.trackSnakeKeysByLenDesc.end(),
                  [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
        return t;
    }();
    return tables;
}

std::pair<std::string, std::string> SplitTrackVariation(const std::string& raw) {
    const auto& knownCodenames = KnownCodenames();
    auto itKnown = knownCodenames.find(raw);
    if (itKnown != knownCodenames.end()) {
        return itKnown->second;
    }
    const auto& tables = SnakeTables();
    for (const auto& snakeTrack : tables.trackSnakeKeysByLenDesc) {
        if (raw == snakeTrack) {
            return {tables.trackBySnake.at(snakeTrack), ""};
        }
        std::string prefix = snakeTrack + "_";
        if (raw.size() >= prefix.size() && raw.compare(0, prefix.size(), prefix) == 0) {
            std::string variationKey = raw.substr(prefix.size());
            auto vit = tables.variationBySnake.find(variationKey);
            std::string variation = vit != tables.variationBySnake.end() ? vit->second : "";
            return {tables.trackBySnake.at(snakeTrack), variation};
        }
    }
    return {"", ""};
}

std::optional<uintptr_t> ResolveEnvironmentObject(uintptr_t moduleBase, uintptr_t tableBase,
                                                    const std::string& baseCodename) {
    auto sysIdxOpt = ReadI32(moduleBase + ENVIRONMENT_SYS_IDX_OFF);
    if (!sysIdxOpt) return std::nullopt;
    auto categoryObjOpt = ReadU64(tableBase + OBJ_ARR_OFF + static_cast<uintptr_t>(*sysIdxOpt) * REGISTRY_STRIDE);
    if (!categoryObjOpt || !*categoryObjOpt) return std::nullopt;
    uintptr_t categoryObj = static_cast<uintptr_t>(*categoryObjOpt);

    auto countOpt = ReadI32(categoryObj + 8);
    auto arrayBaseOpt = ReadU64(categoryObj);
    if (!countOpt || *countOpt <= 0 || !arrayBaseOpt || !*arrayBaseOpt) return std::nullopt;
    uintptr_t arrayBase = static_cast<uintptr_t>(*arrayBaseOpt);

    uint32_t h = WfHash(baseCodename);
    for (int i = 0; i < *countOpt; ++i) {
        auto idxOpt = ReadI32(arrayBase + static_cast<uintptr_t>(i) * 0x20 + 8);
        if (!idxOpt) continue;
        auto candidateObjOpt = ReadU64(tableBase + OBJ_ARR_OFF + static_cast<uintptr_t>(*idxOpt) * REGISTRY_STRIDE);
        if (!candidateObjOpt || !*candidateObjOpt) continue;
        uintptr_t candidateObj = static_cast<uintptr_t>(*candidateObjOpt);

        auto storedHashOpt = ReadI32(candidateObj + 0x78);
        if (!storedHashOpt || static_cast<uint32_t>(*storedHashOpt) != h) continue;

        auto strPtrOpt = ReadU64(candidateObj + 0x70);
        if (strPtrOpt && *strPtrOpt) {
            auto s = ReadCString(static_cast<uintptr_t>(*strPtrOpt), 64);
            if (s && *s == baseCodename) {
                return candidateObj;
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> ResolveEnvironmentDisplayName(uintptr_t envObj) {
    auto subOpt = ReadU64(envObj + 8);
    if (!subOpt || !*subOpt) return std::nullopt;
    auto namePtrOpt = ReadU64(*subOpt + 0x18);
    if (!namePtrOpt || !*namePtrOpt) return std::nullopt;
    return ReadCString(static_cast<uintptr_t>(*namePtrOpt), 64);
}

}  // namespace

std::pair<int, int> ReadRaceSettings(uintptr_t tableBase) {
    if (tableBase == 0) return {0, 0};
    auto obj = HashRegistryLookup(tableBase, "event_settings");
    if (!obj) return {0, 0};

    int laps = ReadI32(*obj + EVENT_SETTINGS_LAP_COUNT_OFF).value_or(0);
    int opps = ReadI32(*obj + EVENT_SETTINGS_OPPONENTS_OFF).value_or(0);
    if (laps <= 0 || laps > 100) laps = 0;
    if (opps < 0 || opps > MAX_PLAYERS) opps = 0;
    return {laps, opps};
}

std::pair<std::string, std::string> DetectTrackAndVariation(uintptr_t moduleBase, uintptr_t tableBase) {
    if (tableBase == 0) return {"Unknown Track", ""};

    auto objPtrOpt = HashRegistryLookup(tableBase, "event_settings");
    if (!objPtrOpt) return {"Unknown Track", ""};
    uintptr_t objPtr = *objPtrOpt;

    auto basePtrOpt = ReadU64(objPtr + EVENT_SETTINGS_BASE_TRACK_FIELD_OFF);
    std::string baseCodename;
    if (basePtrOpt && *basePtrOpt) {
        auto s = ReadCString(static_cast<uintptr_t>(*basePtrOpt), 64);
        if (s) baseCodename = *s;
    }
    if (!baseCodename.empty()) {
        std::string variation = ResolveEnvironmentDisplayName(objPtr).value_or("");
        auto envObjOpt = ResolveEnvironmentObject(moduleBase, tableBase, baseCodename);
        std::optional<std::string> track =
            envObjOpt ? ResolveEnvironmentDisplayName(*envObjOpt) : std::nullopt;
        if (track && !track->empty()) {
            return {*track, variation};
        }
    }

    auto namePtrOpt = ReadU64(objPtr + EVENT_SETTINGS_TRACK_FIELD_OFF);
    if (!namePtrOpt || !*namePtrOpt) return {"Unknown Track", ""};
    auto rawOpt = ReadCString(static_cast<uintptr_t>(*namePtrOpt), 64);
    if (!rawOpt || rawOpt->empty()) return {"Unknown Track", ""};

    auto [track, variation] = SplitTrackVariation(*rawOpt);
    if (!track.empty()) return {track, variation};
    return {"Unknown track (codename: " + *rawOpt + ")", ""};
}

}  // namespace wreckfest_telemetry
