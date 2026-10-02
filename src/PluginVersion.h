#pragma once

namespace wreckfest_telemetry {

// The plugin's release version, e.g. "1.2.0" -- the git tag the release
// workflow builds from, minus its leading "v", passed in through CMake's PLUGIN_VERSION option
// (see .github/workflows/release.yml and the Dockerfile). Local builds
// default to "dev", so a test build is never mistaken for a release.
#ifdef WFT_PLUGIN_VERSION
inline constexpr const char* kPluginVersion = WFT_PLUGIN_VERSION;
#else
inline constexpr const char* kPluginVersion = "dev";
#endif

}  // namespace wreckfest_telemetry
