#pragma once

#include <cstdint>

// Struct offsets/constants for the live race-results slot array, ported
// from wreckfest_telemetry.py. Full discovery history lives in that repo's
// PROJECT.md; only the load-bearing usage notes are kept here.
namespace wreckfest_telemetry::offsets {

constexpr int SLOT_STRIDE = 272;
constexpr int MAX_PLAYERS = 24;

constexpr uint64_t CHAIN_STATIC_OFFSET = 0x2cbcde0;  // module_base + this -> race_manager_ptr
constexpr uint64_t CHAIN_ARRAY_OFFSET = 0x1d84;      // race_manager_ptr + this -> slot[0]

// Offsets below are relative to a slot's total_time_ms address.
constexpr int OFF_TOTAL_TIME = 76;
constexpr int OFF_BEST_LAP = -4;
constexpr int OFF_CLASS_RATING = 16;
constexpr int OFF_SENTINEL_A = 20;  // always {65535, -1000000}
constexpr int OFF_PLAYER_PTR = 68;  // ptr64 -> player/car data block

// int32 at OFF_FINISHED_FLAG packs three fields:
//   byte 0 = finishing position (0-indexed, position = byte+1)
//   byte 1 = lap counter (real lap-completion count, not a finish flag)
//   bit 0x100 = low bit of byte 1 -- NOT a finish signal, just parity noise
constexpr int OFF_FINISHED_FLAG = -32;
constexpr int OFF_FINISH_POSITION = -32;  // byte 0 of the word above
constexpr int OFF_LAP_COUNTER = -31;      // byte 1
constexpr uint32_t FINISHED_BIT = 0x100;

constexpr int OFF_LAST_LAP = 8;     // most recently completed lap's time
constexpr int OFF_CURRENT_LAP = 4;  // still-running current lap timer, never a completed time

// Per-player status bitfield, populated only once results finalize.
constexpr int OFF_STATUS_FLAGS = -36;
// "Result classified" (finished/DNF/timed out). The real settled/not-settled
// signal -- terminal values are always odd, mid-race values always even.
constexpr uint32_t STATUS_CLASSIFIED_BIT = 0x01;
// "This racer did not finish". Verified reliable; no finisher has ever
// carried it.
constexpr uint32_t STATUS_DNF_BIT = 0x10;
// Marks a racer whose own run is complete. NOT an identity signal (does not
// mean "local player") -- only valid scoped to the already-identified local
// player, as a finality edge.
constexpr uint32_t STATUS_RUN_COMPLETE_BIT = 0x40;

constexpr int POFF_NAME = 0;  // offset from the player_ptr block to the name cstring

constexpr int32_t SENTINEL_A = 65535;
constexpr int32_t SENTINEL_B = -1000000;

constexpr int MIN_LAP_MS = 3000;
constexpr int MAX_LAP_MS = 3600000;
constexpr int MAX_TOTAL_MS = 36000000;
constexpr uint64_t MIN_HEAP_PTR = 0x10000000;

// Engine's global name -> object hash registry (every "system", e.g.
// event_settings, CLIENT).
constexpr uint64_t PTR_DAT_OFFSET = 0x127e7f8;   // module_base + this -> registry table_base
constexpr uint64_t BUCKET_ARR_OFF = 0x306020;    // table_base + this + bucket*8 -> head node ptr
constexpr uint64_t NAME_ARR_OFF = 0x40605c;      // table_base + this + idx*STRIDE -> registered name cstr
constexpr uint64_t OBJ_ARR_OFF = 0x406040;       // table_base + this + idx*STRIDE -> object ptr
constexpr int REGISTRY_STRIDE = 0x138;

constexpr uint64_t EVENT_SETTINGS_TRACK_FIELD_OFF = 0xb0;       // -> "<track>_<variation>" cstr
constexpr uint64_t EVENT_SETTINGS_BASE_TRACK_FIELD_OFF = 0xa0;  // -> "<track>" cstr (no variation)
constexpr uint64_t EVENT_SETTINGS_LAP_COUNT_OFF = 0x108;
constexpr uint64_t EVENT_SETTINGS_OPPONENTS_OFF = 0x10c;
constexpr uint64_t ENVIRONMENT_SYS_IDX_OFF = 0x18fbd08;  // module_base + this -> registry idx for track/environment system

// Driving-assist difficulty settings. Resolved through the hash registry by
// a name that is literally the persisted save file's own path
// ("save/assists.aids") -- this is the live object that file gets loaded
// into, a global settings singleton (unlike the Tune screen's per-widget
// objects, present for the whole session regardless of whether the Assists
// options screen was ever opened). All four fields are plain int32, no
// widget-hash indirection needed. Full 0-2 range verified live for all four
// by cycling each setting on-screen one step at a time.
constexpr const char* ASSISTS_REGISTRY_NAME = "save/assists.aids";
constexpr uint64_t ASSIST_SHIFTING_OFF = 0x00;   // 0=automatic, 1=manual, 2=manual+clutch
constexpr uint64_t ASSIST_ABS_OFF = 0x04;        // 0=off, 1=half, 2=full
constexpr uint64_t ASSIST_TCS_OFF = 0x08;        // 0=off, 1=half, 2=full
constexpr uint64_t ASSIST_STABILITY_OFF = 0x0c;  // 0=off, 1=half, 2=full

// Local player's current vehicle weight (kg), a plain float32. Resolved
// through the hash registry to a "career-carstats-temp" object -- the same
// per-car stats block that backs the Performance/Upgrades screen's stat
// panel (a horsepower-vs-RPM curve sits immediately before this field in
// the same struct). Despite the "-temp" in its name, verified live that it
// tracks whichever car is currently selected in the career garage (not a
// stale UI cache tied to the last-opened Performance screen), and survives
// through an entire race, reading correctly at the post-race results
// screen.
constexpr const char* CARSTATS_REGISTRY_NAME = "career-carstats-temp";
constexpr uint64_t CARSTATS_WEIGHT_OFF = 0x64;

}  // namespace wreckfest_telemetry::offsets
