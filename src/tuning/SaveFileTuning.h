#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace wreckfest_telemetry {

// Locates cars5.ccrs. Tries, in order: the registry SteamPath + userdata
// glob (native Windows; a harmless no-op stub under Proton, since that
// registry key points at a minimal steamclient shim inside the prefix, not
// the real userdata), Proton's S: drive (mapped to the real, Linux-native
// Steam client install root), and Z: (Wine's default host-root mapping),
// matching the Python tool's own glob directly. Picks the most recently
// modified match if several are found. nullopt if none resolve.
std::optional<std::wstring> FindCars5Path();

// Parses the 20-byte header + chained-LZ4 chunk structure. nullopt if the
// header doesn't look like a cars5.ccrs file, or if any chunk fails to
// decompress (a bounds mismatch on the mini-header is not an error --
// yields the chunks found so far).
std::optional<std::vector<std::string>> DecompressCars5Chunks(const std::string& buf);

// codename -> {part-path key ("gearbox"/"transmission"/"suspension"/
// "brakes"): preset name}.
std::map<std::string, std::map<std::string, std::string>> ExtractCars5Tuning(
    const std::vector<std::string>& chunks);

// codename -> human display name (e.g. "supervan" -> "Supervan").
std::map<std::string, std::string> ExtractCars5DisplayNames(const std::vector<std::string>& chunks);

// Exact match only -- see SaveFileTuning.cpp for why a prefix-match
// fallback is actively dangerous here.
std::optional<std::string> MatchCars5Codename(const std::string& carName,
                                               const std::map<std::string, std::string>& displayNames);

// Resolves 0-4 preset indices (SUSPENSION/GEARING/DIFFERENTIAL/BRAKES) for
// `carName` from already-decompressed chunks. Split out from
// ReadTuningFromSave so the pure logic is testable without a real file.
std::map<std::string, int> ResolveTuningIndices(const std::vector<std::string>& chunks,
                                                 const std::string& carName);

// Current 0-4 index per tuning category for the owned car whose display
// name matches `carName` exactly, read straight from cars5.ccrs on disk.
// Empty map on any failure (file not found/parse error/no match) --
// fail-quiet, matches the Python tool.
std::map<std::string, int> ReadTuningFromSave(const std::string& carName);

}  // namespace wreckfest_telemetry
