# Wreckfest Telemetry (ASI plugin)

Native C++ port of [wreckfest-telemetry](https://github.com/ampp33/wreckfest-telemetry) as an
auto-loading ASI plugin. Drop it into your Wreckfest install and it loads itself the moment the
game starts — no PID hunting, no terminal.

Status: **Phase 0** (build skeleton + proof-of-life marker file). See
`/home/ampp33/.claude/plans/take-a-look-at-idempotent-cupcake.md` for the full phased plan.

## Building

Requires CMake 3.16+ and a C++20 compiler.

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

Either way, output is `wreckfest_telemetry.asi`.

Host-side unit tests (pure logic, no Windows APIs) build separately and run natively on Linux:
```
cmake -B build-host .
cmake --build build-host
ctest --test-dir build-host
```

## Installing

1. Download [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader), drop the
   proxy DLL (e.g. `dinput8.dll`) into your Wreckfest install root, and `wreckfest_telemetry.asi`
   into its scripts folder per the loader's own README.
2. Copy `config.json.example` to `config.json` next to the `.asi` and fill in your API key, if you
   want races posted automatically.
3. **Steam Play (Proton) only**: add `WINEDLLOVERRIDES="dinput8=n,b" %command%` to Wreckfest's
   launch options, so Wine loads the real proxy DLL instead of its own built-in stub. Not needed on
   native Windows.

`race_log.jsonl`, `config.json`, and the offline retry queue all live next to the `.asi`.
