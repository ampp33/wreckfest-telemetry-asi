# Host-side tests for pure logic (no Windows APIs, no game needed) --
# build and run natively on Linux with the normal (non-MinGW) compiler, for
# fast iteration on ported logic before it's verified live in-game. See plan
# Phase 6 (Lz4), Phase 9 (JSON payload shape), Phase 10 (queue file
# semantics).
#
# No test framework dependency (matches the project's "no package manager"
# vendoring convention) -- each test is a small standalone executable that
# exits non-zero on failure.

set(PURE_LOGIC_SOURCES
    native_stubs.cpp
    ${CMAKE_SOURCE_DIR}/src/tuning/Lz4Block.cpp
    ${CMAKE_SOURCE_DIR}/src/tuning/SaveFileTuning.cpp
    ${CMAKE_SOURCE_DIR}/third_party/lz4.c
)

add_executable(unit_tests unit_tests.cpp ${PURE_LOGIC_SOURCES})
target_compile_features(unit_tests PRIVATE cxx_std_20)
target_include_directories(unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(unit_tests SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
add_test(NAME unit_tests COMMAND unit_tests)

# Exercises the LZ4/cars5.ccrs pipeline against a REAL save file. Not run
# via add_test (needs a real file + car name passed on the command line) --
# invoke it directly, e.g.:
#   ./cars5_tuning_test ~/.local/share/Steam/userdata/<id>/228380/local/wreckfest/cars5.ccrs "RoadSlayer"
add_executable(cars5_tuning_test cars5_tuning_test.cpp ${PURE_LOGIC_SOURCES})
target_compile_features(cars5_tuning_test PRIVATE cxx_std_20)
target_include_directories(cars5_tuning_test PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_include_directories(cars5_tuning_test SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
