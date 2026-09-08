# Host-side unit tests for pure logic (no Windows APIs, no game needed) --
# build and run natively on Linux with the normal (non-MinGW) compiler, for
# fast iteration on ported logic before it's verified live in-game. See plan
# Phase 6 (Lz4), Phase 9 (JSON payload shape), Phase 10 (queue file
# semantics).
#
# No test framework dependency (matches the project's "no package manager"
# vendoring convention) -- each test is a small standalone executable that
# exits non-zero on failure, registered with add_test().

add_executable(smoke_test smoke_test.cpp)
target_compile_features(smoke_test PRIVATE cxx_std_20)
add_test(NAME smoke_test COMMAND smoke_test)
