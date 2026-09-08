#include "worker/WorkerThread.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "debug/DebugConsole.h"
#include "io/Config.h"
#include "io/JsonlLog.h"
#include "io/PluginPaths.h"
#include "io/Queue.h"
#include "memory/ProcessMemory.h"
#include "memory/SehGuard.h"
#include "race/CarNames.h"
#include "race/LapSplits.h"
#include "race/PlayerScraper.h"
#include "race/RaceFinality.h"
#include "strings/HashRegistry.h"
#include "strings/TrackDetection.h"
#include "tuning/SaveFileTuning.h"

namespace wreckfest_telemetry {

namespace {

constexpr double kFlushBackoffSeconds[] = {60.0, 120.0, 300.0, 900.0};
constexpr double kPollIntervalSeconds = 0.5;

// Widens an ASCII literal for DebugLog -- not for arbitrary UTF-8 content
// (player/track names), just our own fixed debug strings.
std::wstring WidenAscii(const std::string& s) {
    return std::wstring(s.begin(), s.end());
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
    std::optional<std::string> lastFingerprintSeen;
    std::optional<std::string> lastLoggedFingerprint;
    bool firstPoll = true;
    // Once a name is established as local for this session, a race later
    // resolving a DIFFERENT name as local is still logged to file for
    // review, but the API post is skipped -- guards against CLIENT
    // occasionally mislabeling a real other racer as local.
    std::optional<std::string> confirmedLocalName;
    LapSplitTracker lapSplits;
    int flushBackoffIdx = 0;
    std::chrono::steady_clock::time_point nextFlushAt;
    int queuePending = 0;
};

void EmitRace(const RaceResult& race, const std::wstring& logPath, const ApiConfig& apiConfig,
              const std::wstring& queuePath, const std::wstring& deadPath, bool allowApi) {
    bool logged = AppendRaceLog(race, logPath, false);
    DebugLog((L"race logged: " + WidenAscii(race.track) + L" (" +
              WidenAscii(logged ? "ok" : "FAILED TO WRITE LOG FILE") + L")")
                 .c_str());
    if (!allowApi) {
        DebugLog(L"API post skipped: local player identity unconfirmed this race");
        return;
    }
    if (!ApiConfigComplete(apiConfig)) return;
    auto payload = BuildApiPayload(race);
    if (!payload) {
        DebugLog(L"API post skipped: no local player identified");
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

    auto& players = scrape.players;
    if (!players.empty()) {
        std::string fingerprint = ComputeFingerprint(players);
        bool isStable = state.lastFingerprintSeen && *state.lastFingerprintSeen == fingerprint;
        bool raceFinal = RaceIsFinal(players, stillRacing);

        // A race already finished when the plugin attached is adopted as
        // already-logged rather than emitted -- it has no lap splits
        // (never watched from the start), so emitting it would be a
        // strictly worse duplicate of whatever's already in the log from
        // a prior session.
        if (state.firstPoll && raceFinal) {
            state.lastLoggedFingerprint = fingerprint;
        }
        state.firstPoll = false;

        if (raceFinal && isStable && (!state.lastLoggedFingerprint || *state.lastLoggedFingerprint != fingerprint)) {
            if (tableBase) {
                ResolveCarNames(ctx.base, *tableBase, players);
            }
            state.lapSplits.Resolve(players);

            const PlayerResult* local = FindLocalPlayer(players);

            std::string track = "Unknown Track";
            std::string variation;
            int lapCount = 0;
            int opponentCount = 0;
            if (tableBase) {
                std::tie(track, variation) = DetectTrackAndVariation(ctx.base, *tableBase);
                std::tie(lapCount, opponentCount) = ReadRaceSettings(*tableBase);
            }

            // Live Tune-screen widget reads (Phase 7) aren't ported yet --
            // save-file tuning alone can lag a just-changed setting until
            // the Tune screen is backed out of, a known, accepted gap
            // until that phase lands.
            std::map<std::string, int> tuning;
            if (local && !local->car.empty()) {
                tuning = ReadTuningFromSave(local->car);
            }

            RaceResult race;
            race.lap_count = lapCount;
            race.opponent_count = opponentCount;
            race.track = track;
            race.variation = variation;
            race.timestamp = NowTimestamp();
            race.tuning = tuning;
            race.players = players;

            bool identityTrusted = true;
            if (local) {
                if (!state.confirmedLocalName) {
                    state.confirmedLocalName = local->name;
                } else if (*state.confirmedLocalName != local->name) {
                    identityTrusted = false;
                }
            }

            EmitRace(race, ctx.logPath, ctx.apiConfig, ctx.queuePath, ctx.deadPath, identityTrusted);
            if (ApiConfigComplete(ctx.apiConfig)) {
                state.queuePending = static_cast<int>(ReadQueue(ctx.queuePath).size());
                state.flushBackoffIdx = 0;
                state.nextFlushAt = SecondsFromNow(kFlushBackoffSeconds[0]);
            }
            state.lastLoggedFingerprint = fingerprint;
        }
        state.lastFingerprintSeen = fingerprint;
    } else {
        state.lastFingerprintSeen.reset();
        state.lastLoggedFingerprint.reset();
    }

    if (state.queuePending && std::chrono::steady_clock::now() >= state.nextFlushAt) {
        auto flushed = FlushQueue(ctx.apiConfig, ctx.queuePath, ctx.deadPath);
        state.queuePending = flushed.remaining;
        state.flushBackoffIdx = flushed.sent > 0
                                     ? 0
                                     : std::min(state.flushBackoffIdx + 1,
                                                static_cast<int>(std::size(kFlushBackoffSeconds)) - 1);
        state.nextFlushAt = SecondsFromNow(kFlushBackoffSeconds[state.flushBackoffIdx]);
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
    ctx.apiConfig = LoadConfig(PluginFilePath(hModule, L"config.json"));
    if (ctx.apiConfig.debug_console) {
        EnableDebugConsole();
        DebugLog(L"wreckfest-telemetry-asi debug console enabled");
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
