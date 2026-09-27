// Real assertion-based tests for the logic that doesn't need Windows APIs
// or a live game -- runs natively on Linux via `ctest`. The LZ4/cars5.ccrs
// pipeline's behavior against an actual save file is covered separately by
// cars5_tuning_test (manual, needs a real file).
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

#include "lz4.h"
#include "tuning/Lz4Block.h"
#include "tuning/SaveFileTuning.h"
#include "tuning/TuningMerge.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++g_failures;
    }
}

void TestLz4RoundTrip() {
    std::string original =
        "the quick brown fox jumps over the lazy dog, the quick brown fox jumps over the lazy dog";
    std::string compressed(static_cast<size_t>(LZ4_compressBound(static_cast<int>(original.size()))), '\0');
    int compressedSize = LZ4_compress_default(original.data(), compressed.data(),
                                               static_cast<int>(original.size()),
                                               static_cast<int>(compressed.size()));
    Check(compressedSize > 0, "Lz4RoundTrip: compress succeeded");
    compressed.resize(static_cast<size_t>(compressedSize));

    auto decompressed = wreckfest_telemetry::DecompressLz4Chunk(compressed, "");
    Check(decompressed.has_value(), "Lz4RoundTrip: decompress succeeded");
    if (decompressed) {
        Check(*decompressed == original, "Lz4RoundTrip: decompressed bytes match original");
    }
}

void TestLz4RejectsGarbage() {
    auto result = wreckfest_telemetry::DecompressLz4Chunk(std::string("\xff\xff\xff\xff", 4), "");
    Check(!result.has_value(), "Lz4RejectsGarbage: malformed block returns nullopt, not garbage output");
}

// codename/key/preset triples, exactly the grammar _CARS5_PART_PATH_RE
// matches: "data/vehicle/<codename>/part/<key>/<preset>.ve".
void TestExtractCars5Tuning() {
    std::string chunk =
        "junk before data/vehicle/06_american_01/part/gearbox/std.ve more junk "
        "data/vehicle/06_american_01/part/brakes/mrear.ve "
        "data/vehicle/16_race_car/part/suspension/mhard.ve trailing junk";
    auto cars = wreckfest_telemetry::ExtractCars5Tuning({chunk});

    Check(cars.size() == 2, "ExtractCars5Tuning: found both codenames");
    Check(cars["06_american_01"]["gearbox"] == "std", "ExtractCars5Tuning: gearbox parsed");
    Check(cars["06_american_01"]["brakes"] == "mrear", "ExtractCars5Tuning: brakes parsed");
    Check(cars["16_race_car"]["suspension"] == "mhard", "ExtractCars5Tuning: second codename parsed");
}

// "VEHICLE_NAME_<id>_<n>" + length-prefixed display name + length-prefixed
// "<codename>:default..." field.
void TestExtractCars5DisplayNames() {
    auto lenPrefixed = [](const std::string& s) {
        uint32_t len = static_cast<uint32_t>(s.size());
        std::string out(reinterpret_cast<const char*>(&len), 4);
        return out + s;
    };
    std::string chunk = "VEHICLE_NAME_12_3" + lenPrefixed("Supervan") + lenPrefixed("06_supervan:default_x");
    auto names = wreckfest_telemetry::ExtractCars5DisplayNames({chunk});

    Check(names.size() == 1, "ExtractCars5DisplayNames: found one entry");
    Check(names["06_supervan"] == "Supervan", "ExtractCars5DisplayNames: codename maps to display name");
}

void TestFindVehicleNameKey() {
    auto lenPrefixed = [](const std::string& s) {
        uint32_t len = static_cast<uint32_t>(s.size());
        std::string out(reinterpret_cast<const char*>(&len), 4);
        return out + s;
    };
    std::string chunk = "VEHICLE_NAME_12_3" + lenPrefixed("Supervan") + lenPrefixed("06_supervan:default_x");

    auto found = wreckfest_telemetry::FindVehicleNameKey({chunk}, "VEHICLE_NAME_12_3");
    Check(found.has_value(), "FindVehicleNameKey: exact key found");
    if (found) {
        Check(found->display_name == "Supervan", "FindVehicleNameKey: display name matches");
        Check(found->codename == "06_supervan", "FindVehicleNameKey: codename matches");
    }

    auto missing = wreckfest_telemetry::FindVehicleNameKey({chunk}, "VEHICLE_NAME_99_9");
    Check(!missing.has_value(), "FindVehicleNameKey: no match for a key that isn't present");
}

void TestMatchCars5CodenameExactOnly() {
    // The exact scenario wreckfest_telemetry.py's docstring warns about: a
    // prefix match would wrongly return the base car for its RS variant.
    std::map<std::string, std::string> names = {
        {"16_european", "Hammerhead"},
        {"16_race_car", "Hammerhead RS"},
    };
    auto match = wreckfest_telemetry::MatchCars5Codename("Hammerhead", names);
    Check(match.has_value() && *match == "16_european", "MatchCars5Codename: exact match, not a prefix hit");

    auto noMatch = wreckfest_telemetry::MatchCars5Codename("Hammer", names);
    Check(!noMatch.has_value(), "MatchCars5Codename: no match for an unrelated prefix");
}

void TestFnv1aMatchesPython() {
    // Reference values from wreckfest_telemetry.py's fnv1a().
    using wreckfest_telemetry::Fnv1a;
    Check(Fnv1a("") == 2166136261u, "Fnv1a: empty input is the offset basis");
    Check(Fnv1a("TUNE_SLIDER_SUSPENSION_TRACK") == 452048435u, "Fnv1a: SUSPENSION slider");
    Check(Fnv1a("TUNE_SLIDER_GEARING_TRACK") == 1300644595u, "Fnv1a: GEARING slider");
    Check(Fnv1a("TUNE_SLIDER_BRAKES_TRACK") == 1089512086u, "Fnv1a: BRAKES slider");
}

void TestLiveTuningMerge() {
    using wreckfest_telemetry::IsUninitializedLiveTuning;
    using wreckfest_telemetry::MergeTuning;

    std::map<std::string, int> allZero = {{"SUSPENSION", 0}, {"GEARING", 0}, {"DIFFERENTIAL", 0}, {"BRAKES", 0}};
    Check(IsUninitializedLiveTuning(allZero), "IsUninitializedLiveTuning: all four zero");
    Check(IsUninitializedLiveTuning({{"SUSPENSION", 0}, {"GEARING", 0}, {"BRAKES", 0}}),
          "IsUninitializedLiveTuning: all resolved categories zero");
    Check(!IsUninitializedLiveTuning({{"SUSPENSION", 0}, {"GEARING", 2}}),
          "IsUninitializedLiveTuning: a real zero mixed with other values is kept");
    Check(!IsUninitializedLiveTuning({}), "IsUninitializedLiveTuning: empty reading");

    std::map<std::string, int> save = {{"SUSPENSION", 2}, {"GEARING", 4}, {"DIFFERENTIAL", 4}, {"BRAKES", 1}};
    auto merged = MergeTuning(save, {{"SUSPENSION", 4}, {"BRAKES", 0}});
    std::map<std::string, int> expected = {{"SUSPENSION", 4}, {"GEARING", 4}, {"DIFFERENTIAL", 4}, {"BRAKES", 0}};
    Check(merged == expected, "MergeTuning: live wins per category, save fills the rest");
}

}  // namespace

int main() {
    TestLz4RoundTrip();
    TestLz4RejectsGarbage();
    TestExtractCars5Tuning();
    TestExtractCars5DisplayNames();
    TestFindVehicleNameKey();
    TestMatchCars5CodenameExactOnly();
    TestFnv1aMatchesPython();
    TestLiveTuningMerge();

    if (g_failures == 0) {
        std::printf("all tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed\n", g_failures);
    return 1;
}
