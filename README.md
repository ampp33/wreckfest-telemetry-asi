# Wreckfest Telemetry (ASI plugin)

Native C++ port of [wreckfest-telemetry](https://github.com/ampp33/wreckfest-telemetry) as an
auto-loading ASI plugin. Drop it into your Wreckfest install and it loads itself the moment the
game starts — no PID hunting, no terminal.

It reads race results, car/track/tuning data, and lap splits directly from the game's own memory
(in-process, since it's loaded into the game itself), logs every finished race to
`race_log.jsonl`, and optionally POSTs results to a configured backend with an offline retry
queue. See `PROJECT.md` in the sibling `wreckfest-telemetry` repo for the full reverse-engineering
history behind the struct offsets and detection logic this ports.

## Building

Requires CMake 3.16+ and a C++20 compiler. Either way, output is `wreckfest_telemetry.asi`.

CMake fetches two small dependencies (nlohmann/json, LZ4) on first configure, hash-pinned to a
specific release each — see `cmake/Dependencies.cmake`. Needs network access once; cached under
`build/_deps/` after that, so a `docker run` against a bind-mounted `build/` (below) only fetches
once too. `rm -rf build` to force a re-fetch.

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

**On Windows** (MSVC):
```
cmake -B build
cmake --build build --config Release
```

**On Linux** (cross-compiles a Windows DLL via MinGW-w64):
```
sudo apt install mingw-w64
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Host-side tests

Pure logic (LZ4 round-trip, the cars5.ccrs byte-scanners), no Windows APIs or game needed — build
and run natively on Linux:
```
cmake -B build-host .
cmake --build build-host
ctest --test-dir build-host
```

`cars5_tuning_test` (built alongside, not run by `ctest`) exercises the same pipeline against a
*real* `cars5.ccrs` and prints what it finds — useful for checking a save file by hand:
```
./build-host/test/cars5_tuning_test ~/.local/share/Steam/userdata/<id>/228380/local/wreckfest/cars5.ccrs "Car Name"
```

## Installing

1. Download [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader). Wreckfest
   doesn't import `dinput8.dll` (the loader's usual default), so rename the loader DLL to
   `version.dll` instead and drop it into your Wreckfest install root (next to
   `Wreckfest_x64.exe`).
2. Create a `scripts` folder in the same directory and put `wreckfest_telemetry.asi` there.
3. Copy `config.json.example` to `config.json` next to the `.asi` and fill in your API key, if you
   want races posted automatically. Set `"debug_console": true` there for troubleshooting output
   (an `AllocConsole` window); leave it `false`/omitted for normal use.
4. **Steam Play (Proton) only**: add `WINEDLLOVERRIDES="version=n,b" %command%` to Wreckfest's
   launch options, so Wine loads the real proxy DLL instead of its own built-in stub. Not needed on
   native Windows.

`race_log.jsonl`, `config.json`, and the offline retry queue (`pending_races.jsonl`,
`failed_races.jsonl`) all live next to the `.asi`, i.e. in `<Wreckfest install>/scripts/`.

Verify it's working: launch the game and check for `scripts/asi_loaded.txt` — one line confirming
the worker thread started and whether it found `Wreckfest_x64.exe`'s module base.
