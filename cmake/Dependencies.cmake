# Fetches pinned single-file dependencies at configure time instead of
# committing them to the repo. Cached under the build directory (persists
# across reconfigures; `rm -rf build` to force a re-fetch) and hash-verified
# for supply-chain safety. Sets DEPS_DIR for callers to use as an include
# directory / source file location.
#
# Needs network access on first configure. If that's ever a problem (an
# offline build environment), download these three files by hand into
# DEPS_DIR instead -- nothing else about the build depends on how they got
# there.

set(DEPS_DIR ${CMAKE_BINARY_DIR}/_deps)
file(MAKE_DIRECTORY ${DEPS_DIR})

function(fetch_dependency url dest hash)
    if(NOT EXISTS "${dest}")
        message(STATUS "Fetching ${dest}...")
        file(DOWNLOAD "${url}" "${dest}" EXPECTED_HASH SHA256=${hash} SHOW_PROGRESS)
    endif()
endfunction()

fetch_dependency(
    https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp
    ${DEPS_DIR}/json.hpp
    aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63
)
fetch_dependency(
    https://raw.githubusercontent.com/lz4/lz4/v1.10.0/lib/lz4.c
    ${DEPS_DIR}/lz4.c
    9396f7de527bc8435de9c7569fb7998e56545a84b4f3c2d808c0235c01774539
)
fetch_dependency(
    https://raw.githubusercontent.com/lz4/lz4/v1.10.0/lib/lz4.h
    ${DEPS_DIR}/lz4.h
    26b82efc53d1570f3b54eef02e9c4764c1ad374ff03cac04e2ced5ea4d4c552f
)
