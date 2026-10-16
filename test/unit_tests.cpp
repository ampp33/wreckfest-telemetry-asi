// Real assertion-based tests for the logic that doesn't need Windows APIs
// or a live game -- runs natively on Linux via `ctest`. The LZ4/cars5.ccrs
// pipeline's behavior against an actual save file is covered separately by
// cars5_tuning_test (manual, needs a real file).
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "io/LogTrim.h"
#include "lz4.h"
#include "strings/Utf8.h"
#include "tuning/AssemblyTuning.h"
#include "tuning/CarParts.h"
#include "tuning/Lz4Block.h"
#include "tuning/SaveFileTuning.h"
#include "tuning/TuningDisplay.h"
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

void TestParseAssemblyName() {
    using wreckfest_telemetry::ParseAssemblyName;

    // Names seen live 2026-09-29 (24-car AI race, local player in slot 0).
    auto local = ParseAssemblyName("vehicle/00/04_european_790483856/assembly.veas");
    Check(local && local->slot == 0 && local->codename == "04_european" && !local->ai,
          "ParseAssemblyName: local player's car");
    auto ai = ParseAssemblyName("vehicle/23/04_european_ai_729575408/assembly.veas");
    Check(ai && ai->slot == 23 && ai->codename == "04_european" && ai->ai,
          "ParseAssemblyName: AI car of the same model, '_ai' stripped from the codename");
    auto multi = ParseAssemblyName("vehicle/03/03_american_01_ai_736577360/assembly.veas");
    Check(multi && multi->codename == "03_american_01" && multi->ai, "ParseAssemblyName: codename with underscores");

    Check(!ParseAssemblyName("vehicle/00/04_european_790483856/assemblyRuntime.vear"),
          "ParseAssemblyName: other per-car resources rejected");
    Check(!ParseAssemblyName("vehicle/00/damage_contact_info"), "ParseAssemblyName: non-car entry rejected");
    Check(!ParseAssemblyName("vehicle/0x/04_european_790483856/assembly.veas"), "ParseAssemblyName: bad slot");
    Check(!ParseAssemblyName("vehicle/00/04_european/assembly.veas"), "ParseAssemblyName: missing spawn id");
}

void TestTuningFromPartPaths() {
    using wreckfest_telemetry::TuningFromPartPaths;
    using Tuning = std::map<std::string, int>;

    // Excerpt of a live assembly (Super Venom tuned 2-1-2-4 SUSP-GEAR-DIFF-BRAK).
    std::vector<std::string> paths = {
        "vehicle/00/26_race_car_790484880/data/vehicle/26_race_car/part/chassis.vecs",
        "data/vehicle/26_race_car/part/engine/a_class/air_filter/stock.vefi",
        "data/vehicle/26_race_car/part/gearbox/eshort.vege",
        "data/vehicle/26_race_car/part/transmission/soft.vetr",
        "data/vehicle/26_race_car/part/suspension/msoft.vesu",
        "data/vehicle/26_race_car/part/suspension/visual.vesv",
        "data/vehicle/26_race_car/part/brakes/mfront.vebr",
        "data/vehicle/26_race_car/part/steering.vest",
        "data/vehicle/shared/physics/steering/default_faster.vpst",
    };
    Check(TuningFromPartPaths(paths, "26_race_car") ==
              Tuning{{"GEARING", 0}, {"DIFFERENTIAL", 1}, {"SUSPENSION", 1}, {"BRAKES", 3}},
          "TuningFromPartPaths: all four categories, non-tuning parts ignored");
    Check(TuningFromPartPaths(paths, "04_european").empty(), "TuningFromPartPaths: another car's paths don't count");
    Check(TuningFromPartPaths({"data/vehicle/26_race_car/part/gearbox/bogus.vege"}, "26_race_car").empty(),
          "TuningFromPartPaths: unknown preset omitted");
}

void TestSanitizeUtf8() {
    using wreckfest_telemetry::SanitizeUtf8;

    // Raw server name seen live, color codes intact.
    std::string wwf = "^7WWF ^1| ^7Wednesday Wreck Fest ^1| ^7No Rules ^1| ^7Voting ^1";
    Check(SanitizeUtf8(wwf) == wwf, "SanitizeUtf8: ASCII with color codes unchanged");
    Check(SanitizeUtf8("Caf\xC3\xA9 \xE2\x9C\x93 \xF0\x9F\x8F\x81") == "Caf\xC3\xA9 \xE2\x9C\x93 \xF0\x9F\x8F\x81",
          "SanitizeUtf8: valid 2/3/4-byte sequences kept");
    Check(SanitizeUtf8("Caf\xE9") == "Caf?", "SanitizeUtf8: Latin-1 byte replaced");
    Check(SanitizeUtf8("a\xC3") == "a?", "SanitizeUtf8: truncated sequence replaced");
    Check(SanitizeUtf8("\xC0\xAF") == "??", "SanitizeUtf8: overlong encoding replaced");
    Check(SanitizeUtf8("\xED\xA0\x80") == "???", "SanitizeUtf8: UTF-16 surrogate replaced");
}

void TestMergeTuning() {
    using wreckfest_telemetry::MergeTuning;

    std::map<std::string, int> save = {{"SUSPENSION", 2}, {"GEARING", 4}, {"DIFFERENTIAL", 4}, {"BRAKES", 1}};
    auto merged = MergeTuning(save, {{"SUSPENSION", 4}, {"BRAKES", 0}});
    std::map<std::string, int> expected = {{"SUSPENSION", 4}, {"GEARING", 4}, {"DIFFERENTIAL", 4}, {"BRAKES", 0}};
    Check(merged == expected, "MergeTuning: overlay wins per category, base fills the rest");

    std::map<std::string, int> display = wreckfest_telemetry::ToDisplayTuning(expected);
    std::map<std::string, int> expectedDisplay = {{"SUSPENSION", 5}, {"GEARING", 5}, {"DIFFERENTIAL", 5}, {"BRAKES", 1}};
    Check(display == expectedDisplay, "ToDisplayTuning: every value shifted 0-4 -> 1-5");
    Check(expected["BRAKES"] == 0, "ToDisplayTuning: input left untouched");
}

void TestEngineTier() {
    using wreckfest_telemetry::EngineTier;
    Check(EngineTier("engine_block_c") == "street", "EngineTier: engine_block_c is street");
    Check(EngineTier("engine_block_b") == "sport", "EngineTier: engine_block_b is sport");
    Check(EngineTier("engine_block_a") == "race", "EngineTier: engine_block_a is race");
    Check(EngineTier("b_class(327)") == "sport", "EngineTier: native b-class engine is sport");
    Check(EngineTier("c_class(gt-r)") == "street", "EngineTier: native engine with a non-numeric suffix");
    Check(EngineTier("c_class") == "street", "EngineTier: bare native engine");
    Check(EngineTier("a_class(455)") == "race", "EngineTier: native a-class engine is race");
    Check(!EngineTier("bigrig"), "EngineTier: special engine unrecognised");
    Check(!EngineTier("engine_block_x"), "EngineTier: unknown class letter");
    Check(!EngineTier("c_classic"), "EngineTier: not a class name");
}

void TestPartsFromPartPaths() {
    using wreckfest_telemetry::PartsFromPartPaths;

    // Excerpt of a live RoadSlayer assembly (garage, 2026-10-08).
    std::vector<std::string> paths = {
        "vehicle/00/06_american_01_761324816/data/vehicle/06_american_01/part/chassis.vecs",
        "vehicle/00/06_american_01_761324816/data/vehicle/06_american_01/part/engine/b_class/engine_block_c.veen",
        "data/vehicle/06_american_01/part/engine/b_class/air_filter/racing.vefi",
        "data/vehicle/06_american_01/part/engine/b_class/pistons/sport.veps",
        "data/vehicle/06_american_01/part/engine/b_class/turbo/turbo.veaf",
        "data/vehicle/06_american_01/part/gearbox/std.vege",
        "data/vehicle/06_american_01/part/bumper_front/bumper_front3.vfbu",
        "data/vehicle/06_american_01/part/side_protector/dlc_side_protector03.vpsp",
        "data/vehicle/06_american_01/part/hood/hood0.vhod",
        "data/vehicle/04_european/part/bumper_rear/bumper_rear2.vrbu",
    };
    std::vector<std::string> looked;
    auto parts = PartsFromPartPaths(paths, "06_american_01", [&](const std::string& path) {
        looked.push_back(path);
        wreckfest_telemetry::ArmorPart part;
        if (path.find("bumper_front3") != std::string::npos) part = {"ignored", "Mesh Guard", 20};
        return part;
    });
    Check(parts.engine == "street", "PartsFromPartPaths: engine tier from instance-prefixed path");
    Check(parts.engine_parts == std::map<std::string, std::string>{{"air_filter", "racing"}, {"pistons", "sport"}},
          "PartsFromPartPaths: engine sub-parts raw, turbo/tuning ignored");
    Check(parts.armor.size() == 2, "PartsFromPartPaths: armor only, another car's path ignored");
    Check(parts.armor["front_bumper"].code == "bumper_front3" && parts.armor["front_bumper"].name == "Mesh Guard" &&
              parts.armor["front_bumper"].weight_kg == 20,
          "PartsFromPartPaths: armor code from the path, name + weight from the lookup");
    Check(parts.armor["side_protector"].code == "dlc_side_protector03" && !parts.armor["side_protector"].name &&
              !parts.armor["side_protector"].weight_kg,
          "PartsFromPartPaths: DLC armor, unresolved name + weight");
    Check(looked.size() == 2, "PartsFromPartPaths: details looked up for armor only");

    // School bus (no armor options): armor slots point at shared parts.
    auto bus = PartsFromPartPaths({"data/vehicle/schoolbus_loaner/part/engine/c_class/c_class.veen",
                                   "data/vehicle/shared/part/roll_cage/stock.vrol",
                                   "data/vehicle/shared/part/window_bar/none.vewb"},
                                  "schoolbus_loaner", nullptr);
    Check(bus.engine == "street" && bus.armor["roll_cage"].code == "stock" && bus.armor["window_bars"].code == "none",
          "PartsFromPartPaths: shared armor parts count");

    auto special = PartsFromPartPaths({"data/vehicle/bigrig/part/engine/bigrig/bigrig.veen"}, "bigrig", nullptr);
    Check(special.engine == "bigrig", "PartsFromPartPaths: unrecognised engine kept as its raw stem");
    Check(PartsFromPartPaths({}, "bigrig", nullptr).empty(), "PartsFromPartPaths: no paths, empty parts");
}

void TestPartsToJson() {
    wreckfest_telemetry::CarParts parts;
    parts.engine = "sport";
    parts.engine_parts = {{"ignition", "racing"}};
    parts.armor["roll_cage"] = {"roll_cage2", std::string("Driver Cage"), 30};
    parts.armor["window_bars"] = {"window_bar0", std::nullopt, std::nullopt};
    auto j = wreckfest_telemetry::PartsToJson(parts);
    Check(j.dump() ==
              R"({"armor":{"roll_cage":{"name":"Driver Cage","weight_kg":30},"window_bars":{"name":null,"weight_kg":null}},"engine":"sport","engine_parts":{"ignition":"racing"}})",
          "PartsToJson: shape, no code, unresolved name/weight as null");
    Check(wreckfest_telemetry::PartsToJson({}).empty(), "PartsToJson: empty parts, empty object");
}

void TestTrimToLastLines() {
    using wreckfest_telemetry::TrimToLastLines;
    auto lines = [](int from, int to) {
        std::string s;
        for (int i = from; i <= to; ++i) s += "{\"race\":" + std::to_string(i) + "}\n";
        return s;
    };
    Check(!TrimToLastLines("", 3, 1), "TrimToLastLines: empty file untouched");
    Check(!TrimToLastLines(lines(1, 4), 3, 1), "TrimToLastLines: within slack, untouched");
    Check(TrimToLastLines(lines(1, 5), 3, 1) == lines(3, 5), "TrimToLastLines: past slack, keeps the newest");
    Check(TrimToLastLines(lines(1, 4) + "{\"race\":5", 3, 1) == lines(3, 4) + "{\"race\":5",
          "TrimToLastLines: unterminated last line counts and is kept");
    Check(TrimToLastLines(lines(1, 3), 0, 0) == "", "TrimToLastLines: zero max drops everything");
}

}  // namespace

int main() {
    TestLz4RoundTrip();
    TestLz4RejectsGarbage();
    TestExtractCars5Tuning();
    TestExtractCars5DisplayNames();
    TestFindVehicleNameKey();
    TestMatchCars5CodenameExactOnly();
    TestParseAssemblyName();
    TestTuningFromPartPaths();
    TestSanitizeUtf8();
    TestMergeTuning();
    TestEngineTier();
    TestPartsFromPartPaths();
    TestPartsToJson();
    TestTrimToLastLines();

    if (g_failures == 0) {
        std::printf("all tests passed\n");
        return 0;
    }
    std::fprintf(stderr, "%d test(s) failed\n", g_failures);
    return 1;
}
