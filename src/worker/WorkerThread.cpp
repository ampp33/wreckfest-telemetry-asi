#include "worker/WorkerThread.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "debug/DebugLog.h"
#include "PluginVersion.h"
#include "io/Config.h"
#include "io/JsonlLog.h"
#include "io/PluginPaths.h"
#include "io/Queue.h"
#include "memory/ProcessMemory.h"
#include "memory/SehGuard.h"
#include "net/SupabaseClient.h"
#include "race/CarNames.h"
#include "race/GameMode.h"
#include "race/LapSplits.h"
#include "race/PlayerScraper.h"
#include "race/RaceEmitGate.h"
#include "race/RaceFinality.h"
#include "race/RaceRoster.h"
#include "strings/AssistSettings.h"
#include "strings/HashRegistry.h"
#include "strings/ServerName.h"
#include "strings/TrackDetection.h"
#include "strings/VehicleWeight.h"
#include "tuning/RaceCarTuning.h"

namespace wreckfest_telemetry {

namespace {

constexpr double kFlushBackoffSeconds[] = {60.0, 120.0, 300.0, 900.0};
constexpr double kPollIntervalSeconds = 0.5;
// The site treats a player as offline after 90s without one (3 missed pings).
constexpr double kHeartbeatIntervalSeconds = 30.0;

// Diagnostic builds only: log to debug_log.txt without needing debug.txt.
// Keep false for releases.
constexpr bool kAlwaysDebugLog = false;

// Remote source for {supabase_url, supabase_anon_key} -- see Config.h.
// wfracelog.com is GitHub Pages, which enforces HTTPS (a plain http://
// request 301s here) -- go straight to https:// rather than relying on
// WinHTTP's redirect-following. FetchRemoteApiDefaults() degrades to an
// empty result (API posting disabled for the run) if it can't be reached.
const wchar_t kRemoteConfigUrl[] = L"https://wfracelog.com/plugin/config.default.json";

void ApplyAssists(uintptr_t base, uintptr_t tableBase, std::vector<PlayerResult>& players) {
    auto assistsBySlot = ReadAssistsBySlot(base, tableBase);
    for (auto& p : players) {
        if (p.ai == false && p.slot_index && assistsBySlot.count(*p.slot_index)) {
            p.assists = assistsBySlot[*p.slot_index];
        }
    }
}

std::chrono::steady_clock::time_point SecondsFromNow(double seconds) {
    return std::chrono::steady_clock::now() +
           std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
}

void AppendMarker(HMODULE hModule, const std::wstring& line) {
    std::wstring path = PluginFilePath(hModule, L"asi_loaded.txt");
    if (path.empty()) return;
    std::wofstream out(path.c_str(), std::ios::app);
    if (out) out << line << L"\n";
}

// Local player's (name, total_time_ms) if identified, else the whole
// field's -- scoped to the local player alone whenever possible so a
// straggler elsewhere in the field can't hold this "unstable" forever
// (see RaceFinality.h and main()'s poll loop in the Python source).
std::string ComputeFingerprint(const std::vector<PlayerResult>& players) {
    const PlayerResult* local = FindLocalPlayer(players);
    if (local) {
        return local->name + "\x01" + std::to_string(local->total_time_ms);
    }
    std::string s;
    for (const auto& p : players) {
        s += p.name + "\x01" + std::to_string(p.total_time_ms) + "\x02";
    }
    return s;
}

struct WorkerLoopState {
    std::optional<std::vector<uintptr_t>> cachedAddrs;
    bool scanNeeded = true;
    RaceEmitGate emitGate;
    // Once a name is established as local for this session, a race later
    // resolving a DIFFERENT name as local is still logged to file for
    // review, but the API post is skipped -- guards against CLIENT
    // occasionally mislabeling a real other racer as local.
    std::optional<std::string> confirmedLocalName;
    LapSplitTracker lapSplits;
    RaceRoster roster;
    int flushBackoffIdx = 0;
    std::chrono::steady_clock::time_point nextFlushAt;
    int queuePending = 0;
    std::chrono::steady_clock::time_point nextHeartbeatAt;  // epoch: first tick sends one
};

void EmitRace(const RaceResult& race, const std::wstring& logPath, const ApiConfig& apiConfig,
              const std::wstring& queuePath, const std::wstring& deadPath, bool allowApi) {
    bool logged = AppendRaceLog(race, logPath, false);
    DebugLog((L"race logged: " + WidenAscii(race.track) + L" (" +
              WidenAscii(logged ? "ok" : "FAILED TO WRITE LOG FILE") + L")")
                 .c_str());
    if (logged) {
        int dropped = TrimRaceLog(logPath);
        if (dropped > 0) {
            DebugLog((L"race log trimmed: dropped the oldest " + std::to_wstring(dropped) + L" races").c_str());
        } else if (dropped < 0) {
            DebugLog(L"race log trim FAILED (file unreadable or locked) -- will retry after the next race");
        }
    }
    if (!allowApi) {
        DebugLog(L"API post skipped: local player identity unconfirmed this race");
        return;
    }
    auto payload = BuildApiPayload(race);
    if (!payload) {
        DebugLog(L"API post skipped: no local player identified");
        return;
    }
    // Tuning fields here are already 1-indexed. Logged before PostPayload()
    // adds api_key, so the key never reaches the log.
    std::string tuningFields;
    for (const char* field : {"suspension", "gear_ratio", "differential", "brake_balance"}) {
        if (!tuningFields.empty()) tuningFields += ", ";
        tuningFields += std::string(field) + "=" + (payload->contains(field) ? (*payload)[field].dump() : "(missing)");
    }
    DebugLog((L"API payload: vehicle='" + WidenAscii(payload->value("vehicle", "")) + L"' " +
              WidenAscii(tuningFields))
                 .c_str());
    // ensure_ascii: player names can be UTF-8, which the wide log stream
    // can fail to convert (and then stop writing altogether).
    DebugLog((L"API payload (full): " + WidenAscii(payload->dump(-1, ' ', true))).c_str());
    if (!ApiConfigComplete(apiConfig)) {
        // Config incomplete (api-key.txt missing and/or remote
        // config.default.json unreachable this run) -- queue rather than
        // drop. The queue file is drained on a later run's startup once
        // config loads successfully (see RunPollLoop's backlog drain),
        // so nothing racing during a config outage is lost.
        DebugLog(L"API config incomplete this run: race queued for later delivery");
        EnqueuePayload(queuePath, *payload, "queued: API config incomplete this run (api-key.txt and/or remote config unavailable)");
        return;
    }
    PostOrQueue(apiConfig, *payload, queuePath, deadPath);
}

struct PollContext {
    uintptr_t base;
    std::wstring logPath;
    std::wstring queuePath;
    std::wstring deadPath;
    ApiConfig apiConfig;
};

// Attempts to drain pending_races.jsonl right now (a no-op if the queue is
// empty or the config isn't complete this run) and reschedules the
// periodic backoff retry based on the outcome. Called both right after a
// race completes -- a fresh success/failure is as good a reason as any to
// retry a stale backlog immediately, rather than waiting out whatever's
// left of the backoff window -- and from the regular backoff timer below.
void AttemptQueueFlush(const PollContext& ctx, WorkerLoopState& state) {
    if (!ApiConfigComplete(ctx.apiConfig)) return;
    auto flushed = FlushQueue(ctx.apiConfig, ctx.queuePath, ctx.deadPath);
    state.queuePending = flushed.remaining;
    state.flushBackoffIdx = flushed.sent > 0
                                 ? 0
                                 : std::min(state.flushBackoffIdx + 1,
                                            static_cast<int>(std::size(kFlushBackoffSeconds)) - 1);
    state.nextFlushAt = SecondsFromNow(kFlushBackoffSeconds[state.flushBackoffIdx]);
}

// One full poll tick. Wrapped in SehGuarded by the caller as a coarse,
// second line of defense beyond the fine-grained per-read guards already
// inside ScrapePlayers/ResolveCarNames/etc. -- an unanticipated bad pointer
// chain anywhere in here must degrade to "skip this tick", never take the
// game down with it.
void RunOneTick(const PollContext& ctx, WorkerLoopState& state) {
    int stillRacing = 0;
    auto tableBase = GetTableBase(ctx.base);

    ScrapeResult scrape = state.scanNeeded
                              ? ScrapePlayers(ctx.base, tableBase, std::nullopt, state.lapSplits, stillRacing)
                              : ScrapePlayers(ctx.base, tableBase, state.cachedAddrs, state.lapSplits, stillRacing);
    state.scanNeeded = false;
    state.cachedAddrs = scrape.usedAddrs;
    if (!state.cachedAddrs) state.scanNeeded = true;

    // A racer's car info is read as they join the race: if they quit, it's
    // wiped long before results settle (see RaceRoster).
    if (state.roster.Observe(scrape.present) && tableBase) {
        auto present = scrape.present;
        ResolveCarNames(ctx.base, *tableBase, present);
        AssignRaceCars(present, ReadRaceCars(*tableBase));
        ApplyAssists(ctx.base, *tableBase, present);
        state.roster.SetCarInfo(present);
    }

    auto& players = scrape.players;
    if (!players.empty()) {
        std::string fingerprint = ComputeFingerprint(players);
        auto decision = state.emitGate.Observe(fingerprint, RaceIsFinal(players, stillRacing));

        if (decision == RaceEmitGate::Decision::kDuplicate) {
            const PlayerResult* local = FindLocalPlayer(players);
            DebugLog((L"race skipped: already logged this session (local player '" +
                      (local ? WidenAscii(local->name) : L"(unidentified)") + L"', reappeared after " +
                      (state.emitGate.LastGapWasEmpty() ? L"a tick with no players visible"
                                                         : L"a different result was read") +
                      L")")
                         .c_str());
        }

        // Only racing, team races and elimination races outside Tournament
        // Mode are logged (see GameMode.h). A skipped event is marked
        // emitted so it isn't re-checked every tick while its results stay
        // up -- and can't be logged later from stale results either.
        EventInfo eventInfo;
        std::optional<std::string> skipReason;
        if (decision == RaceEmitGate::Decision::kEmit) {
            eventInfo = ReadEventInfo(ctx.base, tableBase.value_or(0));
            skipReason = SkipReason(eventInfo);
        }
        if (skipReason) {
            DebugLog((L"race skipped: " + WidenAscii(*skipReason)).c_str());
            state.emitGate.MarkEmitted(fingerprint);
            state.roster.Clear();
        } else if (decision == RaceEmitGate::Decision::kEmit) {
            if (tableBase) {
                ResolveCarNames(ctx.base, *tableBase, players);
                int withCar = 0;
                for (const auto& p : players) {
                    if (!p.car.empty()) ++withCar;
                }
                DebugLog((L"car names: resolved for " + std::to_wstring(withCar) + L"/" +
                          std::to_wstring(players.size()) + L" player(s)")
                             .c_str());
            } else {
                DebugLog(L"car names: skipped -- hash-registry table base not resolved");
            }
            state.lapSplits.Resolve(players);

            const PlayerResult* local = FindLocalPlayer(players);

            std::string track = "Unknown Track";
            std::string variation;
            int lapCount = 0;
            int opponentCount = 0;
            std::map<std::string, std::string> assists;
            int vehicleWeightKg = 0;
            std::string serverName;
            if (tableBase) {
                serverName = ReadOnlineServerName(*tableBase).value_or("");
                std::tie(track, variation) = DetectTrackAndVariation(ctx.base, *tableBase);
                std::tie(lapCount, opponentCount) = ReadRaceSettings(*tableBase);
                assists = ReadAssistSettings(*tableBase);
                DebugLog((L"assists: " + std::to_wstring(assists.size()) + L" of 4 settings resolved").c_str());
                vehicleWeightKg = ReadVehicleWeight(*tableBase);
                DebugLog((L"vehicle weight: " + std::to_wstring(vehicleWeightKg) + L" kg").c_str());
            }

            // Every car's tuning from its own race assembly. The local
            // player's falls back to cars5.ccrs per category (it lags
            // behind a tune set after Restart or before backing out of the
            // Tune screen).
            auto raceCars = ReadRaceCars(tableBase.value_or(0));
            AssignRaceCars(players, raceCars);
            if (tableBase) ApplyAssists(ctx.base, *tableBase, players);
            std::map<std::string, int> tuning;
            if (local) {
                auto carKey = tableBase ? LocalPlayerCarKey(*tableBase, true) : std::nullopt;
                DebugLog((L"tuning: reading for race -- local player's car '" + WidenAscii(local->car) +
                          L"', selected car key '" + (carKey ? WidenAscii(*carKey) : L"(unresolved)") + L"'")
                             .c_str());
                tuning = ReadTuningForRace(raceCars, local->car, carKey, local->slot_index);
                FindLocalPlayer(players)->tuning = tuning;
            } else {
                DebugLog(L"tuning: skipped -- no local player identified");
            }

            RaceResult race;
            race.game_mode = *LoggedGameModeName(*eventInfo.gameMode);
            if (*eventInfo.gameMode == kGameModeEliminationRace) {
                int winnerLaps = EliminationLapCount(players);
                DebugLog((L"elimination race: lap count " + std::to_wstring(winnerLaps) +
                          L" (winner's laps) in place of the lap setting " + std::to_wstring(lapCount))
                             .c_str());
                if (winnerLaps > 0) lapCount = winnerLaps;
            }
            race.lap_count = lapCount;
            race.opponent_count = opponentCount;
            race.track = track;
            race.variation = variation;
            race.timestamp = NowTimestamp();
            race.tuning = tuning;
            race.assists = assists;
            race.vehicle_weight_kg = vehicleWeightKg;
            race.server_name = serverName;
            race.players = players;
            auto quit = state.roster.Missing(players, scrape.present);
            state.lapSplits.Resolve(quit);
            for (auto& p : quit) {
                DebugLog((L"QUIT: '" + WidenAscii(p.name) + L"' left the race on lap " +
                          std::to_wstring(p.laps_completed) + L" -- kept from earlier reads")
                             .c_str());
                p.position = static_cast<int>(race.players.size()) + 1;
                race.players.push_back(std::move(p));
            }

            bool identityTrusted = true;
            if (local) {
                if (!state.confirmedLocalName) {
                    state.confirmedLocalName = local->name;
                } else if (*state.confirmedLocalName != local->name) {
                    identityTrusted = false;
                }
            }

            EmitRace(race, ctx.logPath, ctx.apiConfig, ctx.queuePath, ctx.deadPath, identityTrusted);
            AttemptQueueFlush(ctx, state);
            state.emitGate.MarkEmitted(fingerprint);
            state.roster.Clear();
        }
    } else {
        state.emitGate.ObserveEmpty();
    }

    if (state.queuePending && std::chrono::steady_clock::now() >= state.nextFlushAt) {
        AttemptQueueFlush(ctx, state);
    }

    if (ApiConfigComplete(ctx.apiConfig) && !ctx.apiConfig.heartbeat_url.empty() &&
        std::chrono::steady_clock::now() >= state.nextHeartbeatAt) {
        state.nextHeartbeatAt = SecondsFromNow(kHeartbeatIntervalSeconds);
        auto hb = PostHeartbeat(ctx.apiConfig);
        if (!hb.ok) DebugLog((L"heartbeat failed: " + WidenAscii(hb.message)).c_str());
    }
}

void RunPollLoop(HMODULE hModule) {
    auto base = FindModuleBase(L"Wreckfest_x64.exe");
    AppendMarker(hModule, base ? L"worker thread started, module base resolved"
                               : L"worker thread started, module base NOT FOUND -- exiting");
    if (!base) return;

    PollContext ctx;
    ctx.base = *base;
    ctx.logPath = PluginFilePath(hModule, L"race_log.jsonl");
    ctx.queuePath = PluginFilePath(hModule, L"pending_races.jsonl");
    ctx.deadPath = PluginFilePath(hModule, L"failed_races.jsonl");

    // Opt-in diagnostics: a `debug.txt` marker file dropped next to the .asi
    // (same directory as the files above) turns on verbose logging to
    // debug_log.txt for this session -- see README's troubleshooting
    // section. DebugLog() calls everywhere else stay no-ops otherwise.
    // kAlwaysDebugLog skips the marker check, for diagnostic builds handed
    // to a player investigating a problem.
    std::wstring debugMarker = PluginFilePath(hModule, L"debug.txt");
    bool markerFound = !debugMarker.empty() && GetFileAttributesW(debugMarker.c_str()) != INVALID_FILE_ATTRIBUTES;
    if (kAlwaysDebugLog || markerFound) {
        InitDebugLog(PluginFilePath(hModule, L"debug_log.txt"));
        DebugLog(markerFound ? L"debug logging enabled (debug.txt marker found)"
                             : L"debug logging enabled (always on in this build)");
        DebugLog((L"plugin version " + WidenAscii(kPluginVersion) + L", built " + WidenAscii(__DATE__ " " __TIME__))
                     .c_str());
    }

    ctx.apiConfig.api_key = LoadApiKey(PluginFilePath(hModule, L"api-key.txt"));
    RemoteApiDefaults remote = FetchRemoteApiDefaults(kRemoteConfigUrl);
    ctx.apiConfig.supabase_url = remote.supabase_url;
    ctx.apiConfig.supabase_anon_key = remote.supabase_anon_key;
    ctx.apiConfig.heartbeat_url = remote.heartbeat_url;
    if (!ApiConfigComplete(ctx.apiConfig)) {
        DebugLog(L"API posting deferred this run: api-key.txt and/or remote config (wfracelog.com) unavailable "
                 L"-- races will be queued and sent on a future run once config loads");
    }

    WorkerLoopState state;
    // Drain any backlog left over from an offline session before doing
    // anything else -- the common case is "was offline last session,
    // online now".
    if (ApiConfigComplete(ctx.apiConfig) && !ReadQueue(ctx.queuePath).empty()) {
        state.queuePending = FlushQueue(ctx.apiConfig, ctx.queuePath, ctx.deadPath).remaining;
    }
    state.nextFlushAt = SecondsFromNow(kFlushBackoffSeconds[0]);

    while (true) {
        // Coarse safety net: an unanticipated bad pointer chain anywhere
        // in RunOneTick degrades to "skip this tick" rather than crashing
        // Wreckfest. The fine-grained per-read guards inside ScrapePlayers
        // etc. are the primary defense; this is the backstop.
        SehGuarded([&] {
            RunOneTick(ctx, state);
            return 0;
        });
        Sleep(static_cast<DWORD>(kPollIntervalSeconds * 1000));
    }
}

}  // namespace

DWORD WINAPI WorkerThreadMain(LPVOID param) {
    HMODULE hModule = static_cast<HMODULE>(param);
    InstallSehGuard();
    RunPollLoop(hModule);
    return 0;
}

}  // namespace wreckfest_telemetry
