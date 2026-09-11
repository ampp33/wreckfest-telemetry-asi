# Wreckfest Telemetry (ASI plugin)

Auto-loading ASI plugin that reads your Wreckfest race results live, straight out of the game's own memory, and prints/logs/POSTs each completed race automatically.  This is meant to be paired with the [Wreckfest Race Log](https://wfracelog.com/) site to automatically log and track your races.

It reads full race results, car/track/tuning data, and lap splits directly from the game's own memory, logs every finished race to `race_log.jsonl`, and optionally posts results to [Wreckfest Race Log](https://wfracelog.com/), with an offline retry queue.

## Installation

1. Download the latest release zip from the
   [Releases](https://github.com/ampp33/wreckfest-telemetry-asi/releases) page. It already bundles
   **Ultimate ASI Loader**'s `version.dll` and a `scripts/` folder containing
   `wreckfest_telemetry-<version>.asi` (the version is baked into the filename so you can tell
   what you have installed later) and `api-key.txt`.
1. Extract it into your Wreckfest install root directory (next to `Wreckfest_x64.exe`), so
   `version.dll` and `scripts/` land there.
1. Go to the [Wreckfest Race Log API Keys](https://wfracelog.com/#/settings/api-keys) page and
   generate an API key.
1. Open `scripts/api-key.txt` and replace the placeholder text with the API key you generated
   (one line, nothing else).
1. **Steam Play (Proton) only**: add `WINEDLLOVERRIDES="version=n,b" %command%` to Wreckfest's
   launch options, so Wine loads the real proxy DLL instead of its own built-in stub. Not needed on
   native Windows.

**Verifing it's working**: Complete a race in Wreckfest, the race log entry should appear at the top of the table in the [races](https://wfracelog.com/#/races) page.

## Building

Requires CMake 3.16+ and a C++20 compiler. Either way, output is `wreckfest_telemetry.asi`.

### Docker (recommended)

No local toolchain needed beyond Docker itself.

```bash
docker build -t wreckfest-telemetry-asi-builder .
docker run --rm --user "$(id -u):$(id -g)" -v "$(pwd):/src" -v "$(pwd)/build:/src/build" \
    wreckfest-telemetry-asi-builder
```

`--user "$(id -u):$(id -g)"` keeps the output files owned by you instead of root. Output:
`build/wreckfest_telemetry.asi`.

### Local

**Windows** (MSVC):
```
cmake -B build
cmake --build build --config Release
```

**Linux** (cross-compiles a Windows DLL via MinGW-w64):
```
sudo apt install mingw-w64
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Appendix

`race_log.jsonl`, `api-key.txt`, and the offline retry queue (`pending_races.jsonl`,
`failed_races.jsonl`) all live next to the `.asi`, i.e. in `<Wreckfest install>/scripts/`.