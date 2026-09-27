# Wreckfest Telemetry → Native ASI Plugin Port

## Context

Today, `wreckfest_telemetry.py` (3067 lines, stdlib-only Python) reads live Wreckfest race
telemetry by attaching to the game process externally: the user manually finds the game's PID
(`grep -l "^Wreckfest_x64" /proc/[0-9]*/comm`) and runs the script with `--pid <pid>`, which reads
`/proc/<pid>/mem` — the same interface a debugger uses — following a hand reverse-engineered
pointer chain to a live race-results struct. There is no existing auto-launch script; this manual
step happens every session.

The user wants this to "just work": drop a file into the Wreckfest install directory and have it
auto-load the moment the game starts, no manual PID-finding, no terminal. That requires an ASI
plugin — a Windows DLL (renamed `.asi`) that the Ultimate ASI Loader injects into
`Wreckfest_x64.exe` automatically on launch. Because Wreckfest runs under Proton (real Windows PE
code under Wine), this works the same way it would on native Windows, via a Steam launch-option
DLL override.

Running in-process changes the access model entirely: instead of external `/proc/<pid>/mem` reads,
the plugin does plain in-process pointer dereferences — simpler, but it also means a bug in this
code can crash the game itself (today, a Python bug just kills the external script). Per the user's
choice, this is a **full native port**: everything moves to C++ — memory reading, race-finalization
detection, JSONL logging, Supabase POST with offline retry/dead-letter queueing, and LZ4-based
save-file tuning parsing. No companion Python process at runtime; the existing Python script stays
untouched as a working reference/fallback.

This is a large port of deeply reverse-engineered, bug-hardened logic (see `PROJECT.md` in the
Python repo — the narrative history of *why* each offset/check exists). The plan below is phased so
each phase is independently verifiable live in the game, without needing later phases finished.

## Amendments (post-approval)

- **Comments**: keep C++ comments minimal — brief "what/why" where non-obvious, not the
  investigative-history narration `wreckfest_telemetry.py`'s comments carry (that history lives in
  `PROJECT.md`/the Python source; no need to re-narrate it in the port).
- **LZ4**: link against a real LZ4 library (e.g. system/vendored `liblz4`) instead of hand-porting
  `_lz4_decompress_block`. Dependencies are fine wherever they make the C++ cleaner — this isn't a
  zero-dependency project the way the Python script was.
- **Build portability**: the plugin DLL is always a Windows binary (it runs inside
  `Wreckfest_x64.exe`, itself Windows code whether hosted by native Windows or Proton/Wine — same
  binary works either way). What must be cross-platform is the *build tooling*: CMake configures
  cleanly both natively on Windows (MSVC) and via MinGW-w64 cross-compile from Linux. Implemented in
  Phase 0 via `cmake/mingw-w64-x86_64.cmake` (only needed on Linux) plus `if(MSVC)`/else compiler-flag
  branches in `CMakeLists.txt`.

## Decisions made

- **New sibling repo**: `wreckfest-telemetry-asi`, next to `wreckfest-telemetry`, matching how
  `wreckfest-shift-macro` is already a separate C++/CMake repo in this workspace. The Python tool
  stays untouched and usable throughout.
- **Output location**: `race_log.jsonl`, `config.json`, and the offline queue files live next to
  the compiled `.asi` itself (wherever it lands in the Wreckfest install dir) — no Windows
  special-folder lookup needed for these (unlike the save-file *read* path in Phase 6, which has no
  choice but to resolve a real Windows path).

## Repository layout

```
wreckfest-telemetry-asi/
├── CMakeLists.txt
├── cmake/mingw-w64-x86_64.cmake        # cross-compile toolchain file
├── third_party/
│   ├── README.md                       # provenance/license per vendored lib
│   └── json.hpp                        # nlohmann/json, single header, MIT
├── src/
│   ├── dllmain.cpp                     # DllMain: DLL_PROCESS_ATTACH -> CreateThread only
│   ├── worker/WorkerThread.cpp/.h      # main poll loop (replaces Python main())
│   ├── memory/
│   │   ├── ProcessMemory.cpp/.h        # module base, VirtualQuery region walk, SEH-guarded reads
│   │   ├── SentinelScan.cpp/.h         # fallback sentinel scan + clustering
│   │   └── Offsets.h                   # struct offsets/bit-flags, ported 1:1 + their Python comments
│   ├── race/
│   │   ├── PlayerResult.h              # struct mirror of PlayerResult/RaceResult
│   │   ├── PlayerScraper.cpp/.h        # validate/read/local-slot detection/rank/scrape_players
│   │   ├── LapSplits.cpp/.h            # stateful per-slot lap split accumulation
│   │   ├── RaceFinality.cpp/.h         # _race_is_final port
│   │   └── CarNames.cpp/.h             # car name table scan
│   ├── strings/
│   │   ├── HashRegistry.cpp/.h         # wf_hash, hash registry lookup, localized strings
│   │   └── TrackDetection.cpp/.h       # detect_track_and_variation
│   ├── tuning/
│   │   ├── LiveTuning.cpp/.h           # tune-screen slider reads
│   │   ├── Lz4Block.cpp/.h             # ported _lz4_decompress_block, line-for-line
│   │   └── SaveFileTuning.cpp/.h       # cars5.ccrs locate/decompress/scan/merge
│   ├── io/
│   │   ├── JsonlLog.cpp/.h             # race_to_dict / append_race_log
│   │   ├── Config.cpp/.h               # config.json load
│   │   └── Queue.cpp/.h                # pending/dead-letter queue, atomic rewrite, backoff flush
│   ├── net/HttpClient.cpp/.h           # WinHTTP POST wrapper, response parsing
│   └── debug/DebugConsole.cpp/.h       # optional AllocConsole logging, off by default
├── test/                                # host-side unit tests (Lz4, payload building) — build
│                                         # natively on Linux, no MinGW/game needed
├── config.json.example
└── README.md
```

Mirrors `wreckfest-shift-macro`'s `src/<concern>/` + `third_party/` vendoring conventions
(single-header libs with a `third_party/README.md` provenance entry, not a package manager).

## Build setup

- Install MinGW-w64 (`sudo apt install mingw-w64`) — not currently present on this machine;
  confirm `x86_64-w64-mingw32-g++`/`-windres` afterward.
- `cmake/mingw-w64-x86_64.cmake`: `CMAKE_SYSTEM_NAME Windows`, cross compiler paths,
  `CMAKE_FIND_ROOT_PATH_MODE_* ONLY`.
- Top-level `CMakeLists.txt`: C++20, `add_library(... SHARED ...)`, `-Wall -Wextra -Wpedantic`,
  `-O2` Release (matches `wreckfest-shift-macro/CMakeLists.txt`), output renamed to `.asi` via
  `SUFFIX` or a post-build rename step.
- **Verify early** whether this MinGW-w64/GCC version supports `__try`/`__except` (Windows SEH)
  directly — required for Phase 1's crash-safety wrapper. If not supported, fall back to the
  vectored-exception-handler API. This is a hard requirement, not optional — a bad memory read must
  never take the game down with it.

## ASI loader mechanics

Ultimate ASI Loader (ThirteenAG, prebuilt releases on GitHub): user drops a proxy DLL (renamed to
match a DLL the game already imports, e.g. `dinput8.dll`) into the Wreckfest install root, plus our
compiled `.asi` into its scripts folder (confirm exact folder convention against the loader's own
README — varies by release). Steam launch option `WINEDLLOVERRIDES="dinput8=n,b" %command%` makes
Wine prefer the native-side proxy DLL over its own built-in stub. The proxy DLL then `LoadLibrary`s
every `.asi` it finds. Our `DllMain` must do nothing but `CreateThread` and return immediately (no
blocking calls, no `LoadLibrary` inside `DllMain` — loader-lock rules) — all real work happens on
that worker thread.

## Phased implementation

Each phase states its own live-verifiable "done" check, independent of later phases.

**Phase 0 — Toolchain + build skeleton.** Empty DLL, renamed `.asi`, loads via Ultimate ASI Loader
and writes one line to a debug file on load. *Done: Wreckfest launches and plays normally with the
plugin present, and the file proves the worker thread ran once.*

**Phase 1 — In-process memory access + SEH safety.** `ProcessMemory`: `GetModuleHandleW` (replaces
`find_module_base`'s `/proc/maps` parse), `VirtualQuery`-based writable-region walk (replaces
`_writable_regions`), and SEH-wrapped read primitives (`ReadI32`/`ReadU64`/`ReadCString`, one wrap
each, built once here so nothing downstream repeats the boilerplate) — plus a coarse
`__try/__except` around the *entire* poll-tick body in the worker thread, as a second line of
defense. *Done: runs continuously in a live session for several minutes with zero crashes,
including a deliberate bad-address read to confirm the wrap catches it.*

**Phase 2 — Static pointer chain + slot struct read.** `Offsets.h` ported verbatim from Python's
constants block (`SLOT_STRIDE`, `MAX_PLAYERS`, `CHAIN_STATIC_OFFSET`, `CHAIN_ARRAY_OFFSET`, all
`OFF_*`, `STATUS_*_BIT`, `SENTINEL`, `MIN_LAP_MS`/`MAX_LAP_MS`) — copy the Python file's own inline
comments too, not just the numbers; cross-check against `PROJECT.md`. `PlayerScraper`:
`fast_find_slots`/`validate_entry`/`read_player` → `PlayerResult` struct per slot (local-player
flagging deferred to Phase 3). *Done: debug console (`AllocConsole`, wired up here) dumps all 24
slots' raw fields during a live race, matching the still-usable Python tool run side by side against
the same process.*

**Phase 3 — Sentinel scan fallback + local-player ID.** `SentinelScan` (`scan_for_sentinels`/
`cluster_sentinel_hits` via `VirtualQuery` region scan) and local-player resolution
(`_local_player_slot_index`, `_mark_local_player`, and *every* relaxed-fallback tier —
`_validate_local_slot_relaxed`, `_validate_any_slot_relaxed`, `_validate_slot_relaxed`,
`_count_still_racing` — these layers exist because CLIENT-based resolution proved unreliable live;
don't collapse them to just the primary path). Depends on Phase 4's hash registry for the CLIENT
lookup — implement that first or stub-and-revisit. *Done: fallback scan fires correctly right after
a fresh game start (chain not yet warm), and the correct slot is flagged local in a live multiplayer
session, verified against the actual player.*

**Phase 4 — Hash registry + track detection.** `HashRegistry` (`wf_hash`, table-base resolution,
bucket lookup, localized string resolution) and `TrackDetection`
(`detect_track_and_variation`/`read_race_settings`, structural chain read with legacy heuristic
fallback). Cross-check `PROJECT.md`'s track/variation section against the Python source. *Done:
debug console shows correct track + variation across several different tracks, including a
non-default layout (exercises the hash-registry path specifically).*

**Phase 5 — Car names, lap splits, ranking.** `CarNames` (stride-run table scan) and `LapSplits`
(the accumulator — **must be state that persists across the worker thread's whole lifetime**, e.g.
an `unordered_map` owned by a `WorkerLoopState` object constructed once at thread start, not a local
inside the poll loop — same treatment for `cached_addrs`/`scan_needed`, `confirmed_local_name`,
`last_fingerprint_seen`, `last_logged_fingerprint`, `first_poll`, and the flush-backoff timer
fields; Python gets this for free from one `while True` loop's locals, C++ needs it explicit).
`_STILL_RACING_COUNT` simplifies from Python's pid-keyed dict to a single counter (only ever one
game process in-process). Completes `scrape_players` via `_position_sort_key`/`_rank_players`.
*Done: a full live 3+ lap race shows correct accumulated lap splits per player, matching the Python
tool for the same race.*

**Phase 6 — LZ4 decompressor + save-file tuning.** Link a real LZ4 library (e.g. `liblz4`) for raw
block decompression instead of hand-porting `_lz4_decompress_block` — confirm its raw-block API
(no frame header) matches what `cars5.ccrs` actually contains; fall back to a direct port only if
it doesn't. Unit-test the decompression path in isolation (native Linux build, no game needed)
against fixture bytes captured from the Python tool. Then `_find_cars5_path`/`_decompress_cars5_chunks` (chained,
order-dependent — chunk N+1's back-references reach into chunk N's decompressed history, pass
`history` forward exactly as Python does) /`_extract_cars5_tuning`/`_extract_cars5_display_names`/
`_match_cars5_codename`/`read_tuning_from_save`. The two extraction patterns are simple fixed
literals, not general regex — hand-write byte scanners rather than `std::regex` (slow on multi-MB
buffers):
```
_CARS5_PART_PATH_RE    = data/vehicle/([a-zA-Z0-9_]+)/part/(gearbox|transmission|suspension|brakes)/([a-zA-Z]+)\.ve
_CARS5_VEHICLE_NAME_RE = VEHICLE_NAME_\d+_\d+   (literal prefix + digit runs; followed by two length-prefixed fields)
```
**Unknown to resolve early in this phase**: the Windows-side path for `cars5.ccrs` (Python's
`CARS5_PATH_GLOB` is the Linux/Proton host-side view; the DLL runs Windows-side inside the prefix
and needs the native path — likely under `Documents\My Games\Wreckfest\` or similar). Resolve via
`winepath -w` against the Python tool's resolved path, or log `SHGetKnownFolderPath` results from
inside the DLL once Phase 6 is running live — don't hardcode a guess; a wrong path silently disables
save-file tuning with no error (matches the Python design's graceful-degrade intent, but easy to
miss). *Done: for a car explicitly tuned then raced, the DLL's save-file read matches the Python
tool's `{suspension, gear_ratio, differential, brake_balance}` values for the same session.*

**Phase 7 — Live tuning-screen reads.** `LiveTuning` (`fnv1a`, `_find_tune_array`,
`_read_differential`, `_read_tuning_widgets`) plus `read_tuning_for_race`'s current merge logic:
live widget read is preferred over the save file (no save-rewrite lag), **except** an all-four-zero
live reading is treated as "tab never visited" and deferred to the save file instead (index 0 is a
real, persisted value — treating every individual zero as untrusted would throw away correct data;
only the all-zero *signature* is ambiguous). Note: an earlier "loadout array" live source was tried
for this and found actively unreliable — it's already been removed from the Python source as dead
code, so there is nothing further to port from it. Lower priority than Phase 6 (this only matters
while actively on the Tune screen; save-file tuning already covers the normal case). *Done: debug
console tracks slider changes live on the Tune screen, matching `--watch-tuning` output.*

**Phase 8 — Race finality + full poll-loop orchestration.** This is the highest-stakes logic to get
right; port `_race_is_final` and `main()`'s poll loop exactly as currently written (both read
directly and confirmed during planning — noted here since they're more evolved than a casual read
might suggest):
- `_race_is_final`: **hard veto** first — if `_STILL_RACING_COUNT` (real racers not yet classified)
  is nonzero, return `False` immediately, full stop, before any other check. **Primary**: every
  tracked player's `STATUS_CLASSIFIED_BIT` set AND still-racing count is 0 AND the player list is
  non-empty → `True` (this checks *all* players, not just local — a documented supersession of an
  even earlier local-only version). **Fallback**, reached only when *no* player has any status bits
  set at all: local player's own `finished` flag + a plausible best-lap bound
  (`MIN_LAP_MS`..`MAX_LAP_MS`), or an all-players `finished` check if local can't be identified.
- Poll loop layers two more gates on top before actually emitting: a **fingerprint-stability** check
  (local player's `(name, total_time_ms)` unchanged across consecutive polls — filters a mid-race
  status blip), an **already-logged guard** (skip re-emitting while the results screen lingers), a
  **first-poll adoption** (a race already finished when the plugin attaches is adopted as
  already-logged silently, not emitted — it has no lap splits, so emitting it would be a strictly
  worse duplicate), and an **identity-trust guard** (`confirmed_local_name` — once a name is
  established as local for the session, a race later resolving a *different* name as local is still
  logged to file for review but the API post is skipped, guarding the documented CLIENT-mislabeling
  bug).

Every one of these exists because of a specific observed live bug (documented inline in the Python
source around `_race_is_final` and `main()`'s poll loop) — read those comments directly as the spec
for this phase; don't re-derive or "simplify" any of them. *Done: exercised against the scenarios
that motivated each check — a 24-player race with a late straggler (local result still logs
promptly), a mid-race pause (must not spuriously log), attaching mid-results-screen (adopts
silently), and a normal full race (logs exactly once, at the right time).*

**Phase 9 — JSONL logging + API payload + WinHTTP POST.** Vendor `third_party/json.hpp`
(nlohmann/json, MIT) with a `third_party/README.md` entry matching the sibling project's format.
Port `race_to_dict`/`append_race_log`, `_race_to_api_payload` (only present optional fields
included, tuning values transformed 0-4 → 1-5 on the way out), and `_post_payload` via WinHTTP —
preserving its exact response semantics: **HTTP 200 does not imply success** (the real signal is an
in-body JSON `"success"` boolean); non-2xx is retryable only if `status >= 500` or `status` is 408
or 429; a non-JSON/non-dict body is treated as retryable (proxy misbehavior, not rejection); never
throws, returns `(ok, retryable, message)`. `api_key` is injected into the outgoing body only at
send time, never persisted to the queue file. **Spike early**: a minimal WinHTTP POST against the
real Supabase endpoint, to confirm TLS 1.2/Schannel works under this Proton version before building
the rest of the queue logic around it — if it doesn't, `race_log.jsonl` stays the complete source of
truth regardless (POST is already best-effort by design), so this is a degrade-not-break risk.
*Done: a manual POST from the DLL succeeds against the real backend and the row appears there; JSONL
output diffs clean against the Python tool's output for the same race.*

**Phase 10 — Offline queue + backoff flush, final wiring.** `Queue`: `pending_races.jsonl` lines as
`{queued_at, attempts, last_error, payload}` (never `api_key`), atomic rewrite
(`.tmp` + `FlushFileBuffers` + `MoveFileEx(..., MOVEFILE_REPLACE_EXISTING)`, empty queue deletes the
file rather than leaving one empty), `flush_queue` sends oldest-first and rewrites the queue file
after *every* individual send (a crash mid-flush can't double-post), stopping at the first retryable
failure but dead-lettering and continuing past permanent ones. `_post_or_queue`: drain any backlog
first, queue the new race directly if the backlog didn't fully drain (no extra timeout burned
proving what draining just proved). Backoff timer `(60, 120, 300, 900)` seconds via a monotonic
clock (`GetTickCount64`/`steady_clock`, not wall time). Drain any existing backlog at worker-thread
startup, before the main poll loop, matching Python's `main()`. *Done: simulate an offline stretch
(point `supabase_url` at an unreachable address), confirm races queue with atomic-write behavior
(kill mid-write, confirm no truncated file survives), restore connectivity, confirm oldest-first
drain with the queue file rewritten after each send.*

**Phase 11 — Hardening + packaging.** Confirm the Phase 1 coarse `__try/__except` actually wraps the
*entire* poll-tick body (deserves an explicit final review, not just at Phase 1). Debug console
gated behind a config flag, off by default. README covering Ultimate ASI Loader placement,
`WINEDLLOVERRIDES` launch option, and `config.json` setup. Multi-hour live soak test across several
real races (solo, online with a straggler, a mid-session tuning change) end to end from DLL load
through JSONL log through API POST. *Done: multi-hour session produces correct log + POSTs with zero
plugin-attributable crashes.*

## Known unknowns to verify against source, not guessed

1. Windows-side `cars5.ccrs` path (Phase 6) — via `winepath -w` or live `SHGetKnownFolderPath`.
2. `_CARS5_PART_PATH_RE`/`_CARS5_VEHICLE_NAME_RE` — captured above; re-confirm against the live
   Python source at implementation time.
3. Struct bit-flag/offset semantics — `PROJECT.md` is the authoritative narrative; port its
   reasoning into `Offsets.h`'s comments, not just the numbers.
4. Ultimate ASI Loader's current folder convention (install root vs. `scripts/`) — confirm against
   the loader's own README at download time.
5. MinGW-w64 SEH (`__try`/`__except`) support in the installed GCC version — verify early; have the
   vectored-exception-handler fallback ready if unsupported.
6. Wine/Proton WinHTTP + Schannel TLS 1.2 reliability — spike early in Phase 9; degrade-not-break.

## Verification approach throughout

- `test/` unit tests (Lz4 block decompression, JSON payload shape, queue file atomic-write/parse
  semantics) build and run natively on Linux — no MinGW or game needed, fast iteration for the pure
  logic.
- Every other phase is verified live against the real game, side by side with the still-functioning
  Python tool where useful (Phases 2, 5, 6) as a parity check, and against the specific historical
  bug scenarios documented in the Python source's comments where the logic exists *because of* one
  (Phase 8 especially).
- `PROJECT.md` and the live `wreckfest_telemetry.py` source are the ground truth throughout — this
  plan is a map, not the spec; re-read the relevant function bodies and their inline comments at
  each phase rather than trusting any summary, including this one.

## Critical files to reference while implementing

- `/home/ampp33/Projects/programming/wreckfest-telemetry/wreckfest_telemetry.py` — source of truth
  for every ported function; several (`_race_is_final`, `main()`'s poll loop, `read_tuning_for_race`)
  carry hard-won nuance that must not be "simplified" away.
- `/home/ampp33/Projects/programming/wreckfest-telemetry/PROJECT.md` — narrative ground truth for
  struct offsets, bit-flag semantics, and discovery history.
- `/home/ampp33/Projects/programming/wreckfest-shift-macro/CMakeLists.txt` and
  `third_party/README.md` — the CMake/MinGW/vendoring conventions to mirror.
- `/home/ampp33/Projects/programming/wreckfest-telemetry/config.json.example` — config schema to
  preserve as-is.
