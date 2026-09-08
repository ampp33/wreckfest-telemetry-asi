#include "tuning/SaveFileTuning.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>

#include "tuning/Lz4Block.h"

namespace wreckfest_telemetry {

namespace {

bool IsCodenameChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// Truncating wide->narrow conversion for ifstream's path argument -- the
// wchar_t* overload MinGW's libstdc++ accepts as an extension isn't
// portable to a native (test) build, and every path FindCars5Path() can
// produce (drive letters, "Program Files", numeric Steam IDs, the fixed
// "cars5.ccrs" suffix) is ASCII in practice.
std::string NarrowPath(const std::wstring& wide) {
    std::string out;
    out.reserve(wide.size());
    for (wchar_t c : wide) out.push_back(static_cast<char>(c));
    return out;
}

}  // namespace

std::optional<std::vector<std::string>> DecompressCars5Chunks(const std::string& buf) {
    if (buf.size() < 20 || buf.compare(4, 4, "srcc") != 0) {
        return std::nullopt;
    }
    uint32_t fieldA;
    std::memcpy(&fieldA, buf.data() + 12, 4);
    size_t off = 20;
    if (off + fieldA > buf.size()) return std::nullopt;

    auto first = DecompressLz4Chunk(buf.substr(off, fieldA), "");
    if (!first) return std::nullopt;
    std::vector<std::string> chunks{*first};
    off += fieldA;
    std::string history = *first;

    while (off + 8 <= buf.size()) {
        uint32_t chunkLen;
        std::memcpy(&chunkLen, buf.data() + off, 4);
        off += 8;  // 4-byte length + 4-byte checksum (checksum unused)
        if (chunkLen == 0 || off + chunkLen > buf.size()) break;

        auto chunk = DecompressLz4Chunk(buf.substr(off, chunkLen), history);
        if (!chunk) return std::nullopt;
        chunks.push_back(*chunk);
        off += chunkLen;
        history += *chunk;
    }
    return chunks;
}

std::map<std::string, std::map<std::string, std::string>> ExtractCars5Tuning(
    const std::vector<std::string>& chunks) {
    static const std::vector<std::string> kKeys = {"gearbox", "transmission", "suspension", "brakes"};
    std::map<std::string, std::map<std::string, std::string>> cars;

    for (const auto& chunk : chunks) {
        size_t pos = 0;
        while ((pos = chunk.find("data/vehicle/", pos)) != std::string::npos) {
            size_t p = pos + 13;  // strlen("data/vehicle/")
            size_t codenameStart = p;
            while (p < chunk.size() && IsCodenameChar(chunk[p])) ++p;
            if (p == codenameStart) {
                ++pos;
                continue;
            }
            std::string codename = chunk.substr(codenameStart, p - codenameStart);

            if (chunk.compare(p, 6, "/part/") != 0) {
                pos = codenameStart;
                continue;
            }
            p += 6;

            std::string matchedKey;
            for (const auto& k : kKeys) {
                if (chunk.compare(p, k.size(), k) == 0) {
                    matchedKey = k;
                    p += k.size();
                    break;
                }
            }
            if (matchedKey.empty()) {
                pos = codenameStart;
                continue;
            }
            if (p >= chunk.size() || chunk[p] != '/') {
                pos = codenameStart;
                continue;
            }
            ++p;

            size_t presetStart = p;
            while (p < chunk.size() && std::isalpha(static_cast<unsigned char>(chunk[p]))) ++p;
            if (p == presetStart) {
                pos = codenameStart;
                continue;
            }
            std::string preset = chunk.substr(presetStart, p - presetStart);

            if (chunk.compare(p, 3, ".ve") != 0) {
                pos = codenameStart;
                continue;
            }
            p += 3;

            cars[codename][matchedKey] = preset;
            pos = p;
        }
    }
    return cars;
}

std::map<std::string, std::string> ExtractCars5DisplayNames(const std::vector<std::string>& chunks) {
    static const std::string kPrefix = "VEHICLE_NAME_";
    std::map<std::string, std::string> names;

    for (const auto& chunk : chunks) {
        size_t pos = 0;
        while ((pos = chunk.find(kPrefix, pos)) != std::string::npos) {
            size_t p = pos + kPrefix.size();
            size_t d1Start = p;
            while (p < chunk.size() && std::isdigit(static_cast<unsigned char>(chunk[p]))) ++p;
            if (p == d1Start || p >= chunk.size() || chunk[p] != '_') {
                ++pos;
                continue;
            }
            ++p;
            size_t d2Start = p;
            while (p < chunk.size() && std::isdigit(static_cast<unsigned char>(chunk[p]))) ++p;
            if (p == d2Start) {
                ++pos;
                continue;
            }
            // Match end -- immediately followed by two length-prefixed
            // fields: display name, then "<codename>:default...".
            if (p + 4 > chunk.size()) {
                pos = p;
                continue;
            }
            uint32_t nameLen;
            std::memcpy(&nameLen, chunk.data() + p, 4);
            if (nameLen == 0 || nameLen >= 64 || p + 4 + nameLen > chunk.size()) {
                pos = p;
                continue;
            }
            std::string displayName = chunk.substr(p + 4, nameLen);
            size_t q = p + 4 + nameLen;

            if (q + 4 > chunk.size()) {
                pos = p;
                continue;
            }
            uint32_t codeLen;
            std::memcpy(&codeLen, chunk.data() + q, 4);
            if (codeLen == 0 || codeLen >= 64 || q + 4 + codeLen > chunk.size()) {
                pos = p;
                continue;
            }
            std::string codeField = chunk.substr(q + 4, codeLen);
            std::string codename = codeField.substr(0, codeField.find(':'));

            names[codename] = displayName;
            pos = q + 4 + codeLen;
        }
    }
    return names;
}

std::optional<std::string> MatchCars5Codename(const std::string& carName,
                                               const std::map<std::string, std::string>& displayNames) {
    for (const auto& [codename, name] : displayNames) {
        if (name == carName) return codename;
    }
    return std::nullopt;
}

std::map<std::string, int> ResolveTuningIndices(const std::vector<std::string>& chunks,
                                                 const std::string& carName) {
    std::map<std::string, int> result;

    auto cars = ExtractCars5Tuning(chunks);
    auto displayNames = ExtractCars5DisplayNames(chunks);
    auto codename = MatchCars5Codename(carName, displayNames);
    if (!codename) return result;
    auto carIt = cars.find(*codename);
    if (carIt == cars.end()) return result;

    struct PresetEntry {
        std::string key;
        std::string label;
        std::vector<std::string> presets;
    };
    static const std::vector<PresetEntry> kPresetTable = {
        {"gearbox", "GEARING", {"eshort", "short", "std", "wide", "ewide"}},
        {"transmission", "DIFFERENTIAL", {"open", "soft", "limited", "stiff", "locked"}},
        {"suspension", "SUSPENSION", {"soft", "msoft", "standard", "mhard", "hard"}},
        {"brakes", "BRAKES", {"rear", "mrear", "stock", "mfront", "front"}},
    };
    for (const auto& entry : kPresetTable) {
        auto partIt = carIt->second.find(entry.key);
        if (partIt == carIt->second.end()) continue;
        auto presetIt = std::find(entry.presets.begin(), entry.presets.end(), partIt->second);
        if (presetIt != entry.presets.end()) {
            result[entry.label] = static_cast<int>(std::distance(entry.presets.begin(), presetIt));
        }
    }
    return result;
}

std::map<std::string, int> ReadTuningFromSave(const std::string& carName) {
    if (carName.empty()) return {};

    auto path = FindCars5Path();
    if (!path) return {};

    std::ifstream in(NarrowPath(*path), std::ios::binary);
    if (!in) return {};
    std::string buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (buf.empty()) return {};

    auto chunks = DecompressCars5Chunks(buf);
    if (!chunks) return {};

    return ResolveTuningIndices(*chunks, carName);
}

}  // namespace wreckfest_telemetry
