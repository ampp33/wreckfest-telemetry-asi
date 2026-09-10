cmake_minimum_required(VERSION 3.16)

project(wreckfest_telemetry_asi
    VERSION 0.1.0
    DESCRIPTION "ASI plugin: auto-loading native port of wreckfest-telemetry"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include(cmake/Dependencies.cmake)

# ── Plugin DLL (Windows target only -- requires the MinGW toolchain file) ────
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    add_library(wreckfest_telemetry SHARED
        src/dllmain.cpp
        src/worker/WorkerThread.cpp
        src/debug/DebugConsole.cpp
        src/io/PluginPaths.cpp
        src/memory/SehGuard.cpp
        src/memory/ProcessMemory.cpp
        src/memory/SentinelScan.cpp
        src/race/PlayerScraper.cpp
        src/race/CarNames.cpp
        src/race/LapSplits.cpp
        src/strings/HashRegistry.cpp
        src/strings/TrackDetection.cpp
        src/tuning/Lz4Block.cpp
        src/tuning/SaveFileTuning.cpp
        src/tuning/Cars5Path.cpp
        src/race/RaceFinality.cpp
        src/io/JsonlLog.cpp
        src/io/Config.cpp
        src/io/Queue.cpp
        src/net/HttpClient.cpp
        src/net/SupabaseClient.cpp
        ${DEPS_DIR}/lz4.c
    )

    target_include_directories(wreckfest_telemetry PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )

    # json.hpp/lz4.{c,h} are fetched by cmake/Dependencies.cmake, not
    # committed -- see that file. Marked SYSTEM to suppress warnings from
    # code we don't own.
    target_include_directories(wreckfest_telemetry SYSTEM PRIVATE
        ${DEPS_DIR}
    )

    target_link_libraries(wreckfest_telemetry PRIVATE
        winhttp
        advapi32
    )

    if(MSVC)
        target_compile_options(wreckfest_telemetry PRIVATE /W4)
    else()
        target_compile_options(wreckfest_telemetry PRIVATE
            -Wall -Wextra -Wpedantic
            $<$<CONFIG:Release>:-O2>
        )
    endif()

    if(MINGW)
        # Wreckfest's install dir won't have the MinGW runtime DLLs --
        # statically link them in so the .asi has no dependency beyond
        # standard Windows system DLLs.
        target_link_options(wreckfest_telemetry PRIVATE
            -static-libgcc -static-libstdc++ -static
        )
    endif()

    # Ultimate ASI Loader expects a plain DLL with a .asi extension.
    set_target_properties(wreckfest_telemetry PROPERTIES
        SUFFIX ".asi"
        PREFIX ""
    )
else()
    message(STATUS "CMAKE_SYSTEM_NAME is not Windows -- skipping the plugin "
                    "DLL target. Configure with "
                    "-DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake to "
                    "build it. Only test/ (host-side unit tests) will build.")
endif()

# ── Host-side unit tests (pure logic: Lz4, JSON payload shape, queue file
# semantics) -- build natively on Linux, no MinGW/game needed, for fast
# iteration on ported logic before it's verified live in-game. ────────────
enable_testing()
add_subdirectory(test)
