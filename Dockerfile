# Cross-compiles the ASI plugin to a Windows DLL using MinGW-w64. Output
# lands in ./build/wreckfest_telemetry.asi on the host via a bind mount --
# see README.md for the run command. Pass `-e PLUGIN_VERSION=X.Y.Z` to
# docker run to stamp a release version into the plugin (the release
# workflow does); it defaults to "dev".
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        cmake \
        mingw-w64 \
        ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
ENTRYPOINT ["sh", "-c", "\
    cmake -B build -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake \
        -DCMAKE_BUILD_TYPE=Release \
        -DPLUGIN_VERSION=\"${PLUGIN_VERSION:-dev}\" \
    && cmake --build build \
"]
