// Host-side check of the LZ4/cars5.ccrs decompression+extraction pipeline
// against a REAL save file -- no game or Windows APIs needed, since this
// exercises pure logic (FindCars5Path, the only Windows-specific piece, is
// deliberately not linked into this test target).
//
// Usage: cars5_tuning_test <path-to-cars5.ccrs> <car display name>
#include <cstdio>
#include <fstream>
#include <iterator>

#include "tuning/SaveFileTuning.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <cars5.ccrs> <car display name>\n", argv[0]);
        return 2;
    }

    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "couldn't open %s\n", argv[1]);
        return 1;
    }
    std::string buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::printf("read %zu bytes\n", buf.size());

    auto chunks = wreckfest_telemetry::DecompressCars5Chunks(buf);
    if (!chunks) {
        std::fprintf(stderr, "DecompressCars5Chunks failed\n");
        return 1;
    }
    std::printf("decompressed %zu chunks:", chunks->size());
    for (auto& c : *chunks) std::printf(" %zu", c.size());
    std::printf(" bytes\n");

    auto cars = wreckfest_telemetry::ExtractCars5Tuning(*chunks);
    auto displayNames = wreckfest_telemetry::ExtractCars5DisplayNames(*chunks);
    std::printf("found %zu cars with tuning data, %zu display names\n", cars.size(), displayNames.size());

    auto codename = wreckfest_telemetry::MatchCars5Codename(argv[2], displayNames);
    if (!codename) {
        std::fprintf(stderr, "no display-name match for %s\n", argv[2]);
        std::printf("available display names:\n");
        for (auto& [code, name] : displayNames) std::printf("  %s -> %s\n", code.c_str(), name.c_str());
        return 1;
    }
    std::printf("matched codename: %s\n", codename->c_str());

    auto carIt = cars.find(*codename);
    if (carIt != cars.end()) {
        std::printf("raw parts:\n");
        for (auto& [key, preset] : carIt->second) {
            std::printf("  %s -> %s\n", key.c_str(), preset.c_str());
        }
    }

    auto tuning = wreckfest_telemetry::ResolveTuningIndices(*chunks, argv[2]);
    std::printf("resolved tuning indices:\n");
    for (auto& [label, idx] : tuning) {
        std::printf("  %s = %d\n", label.c_str(), idx);
    }
    return 0;
}
