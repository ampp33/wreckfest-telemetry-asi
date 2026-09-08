#include "io/JsonlLog.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

namespace wreckfest_telemetry {

namespace {

std::string MsToStr(int ms) {
    if (ms <= 0) return "--:--.---";
    int m = ms / 60000;
    int rem = ms % 60000;
    int s = rem / 1000;
    int msR = rem % 1000;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d.%03d", m, s, msR);
    return buf;
}

}  // namespace

nlohmann::json PlayerToDict(const PlayerResult& p, bool includeLaps) {
    nlohmann::json d = {
        {"position", p.position},
        {"name", p.name},
        {"car", p.car},
        {"class", std::string(1, p.class_letter) + " " + std::to_string(p.class_rating)},
        {"best_lap_ms", p.best_lap_ms},
        {"total_time_ms", p.total_time_ms},
        {"dnf", p.dnf()},
        // laps_completed holds the engine's current lap NUMBER (1-indexed,
        // so one higher than laps actually completed) -- corrected here.
        {"laps_completed", std::max(0, p.laps_completed - 1)},
    };
    if (includeLaps) {
        d["lap_times_ms"] = p.lap_times_ms;
        std::vector<std::string> laps;
        laps.reserve(p.lap_times_ms.size());
        for (int t : p.lap_times_ms) laps.push_back(MsToStr(t));
        d["lap_times"] = laps;
    }
    return d;
}

nlohmann::json RaceToDict(const RaceResult& race, bool opponentLaps) {
    const PlayerResult* local = nullptr;
    std::vector<const PlayerResult*> others;
    for (const auto& p : race.players) {
        if (p.is_local) {
            local = &p;
        } else {
            others.push_back(&p);
        }
    }

    nlohmann::json d = {
        {"track", race.track},
        {"variation", race.variation},
        {"timestamp", race.timestamp},
        {"tuning", race.tuning},
        {"player", local ? PlayerToDict(*local) : nlohmann::json(nullptr)},
    };
    if (race.lap_count) d["lap_count"] = race.lap_count;
    if (race.opponent_count) d["opponent_count"] = race.opponent_count;

    nlohmann::json othersArr = nlohmann::json::array();
    for (const auto* p : others) othersArr.push_back(PlayerToDict(*p, opponentLaps));
    d["others"] = othersArr;

    return d;
}

std::optional<nlohmann::json> BuildApiPayload(const RaceResult& race) {
    const PlayerResult* local = nullptr;
    for (const auto& p : race.players) {
        if (p.is_local) {
            local = &p;
            break;
        }
    }
    if (!local) return std::nullopt;

    nlohmann::json payload = {
        {"track", race.track},
        {"variant", race.variation},
        {"vehicle", local->car},
        {"performance_index", local->class_rating},
        {"place", local->position},
        {"lap_time_ms", local->best_lap_ms},
        {"total_time_ms", local->total_time_ms},
    };

    auto tuning1Indexed = [&race](const char* category) -> std::optional<int> {
        auto it = race.tuning.find(category);
        return it != race.tuning.end() ? std::optional<int>(it->second + 1) : std::nullopt;
    };
    if (auto v = tuning1Indexed("SUSPENSION")) payload["suspension"] = *v;
    if (auto v = tuning1Indexed("GEARING")) payload["gear_ratio"] = *v;
    if (auto v = tuning1Indexed("DIFFERENTIAL")) payload["differential"] = *v;
    if (auto v = tuning1Indexed("BRAKES")) payload["brake_balance"] = *v;

    if (race.lap_count) payload["lap_count"] = race.lap_count;
    if (!local->lap_times_ms.empty()) payload["lap_times_ms"] = local->lap_times_ms;

    std::vector<const PlayerResult*> rosterPlayers;
    rosterPlayers.reserve(race.players.size());
    for (const auto& p : race.players) rosterPlayers.push_back(&p);
    std::sort(rosterPlayers.begin(), rosterPlayers.end(),
              [](const PlayerResult* a, const PlayerResult* b) { return a->position < b->position; });

    auto roster = nlohmann::json::array();
    for (const auto* p : rosterPlayers) roster.push_back(PlayerToDict(*p, false));
    payload["results_roster"] = roster;

    return payload;
}

bool AppendRaceLog(const RaceResult& race, const std::wstring& logPath, bool opponentLaps) {
    // Narrow (byte-oriented) stream even though the path is wide -- the
    // JSON payload is UTF-8 text; a wide stream's locale-driven codecvt
    // would risk mangling it on the way out.
    std::ofstream out(logPath.c_str(), std::ios::app | std::ios::binary);
    if (!out) return false;
    out << RaceToDict(race, opponentLaps).dump() << "\n";
    return static_cast<bool>(out);
}

}  // namespace wreckfest_telemetry
