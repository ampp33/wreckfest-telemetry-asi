#include "tuning/SaveFileTuning.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>

#include "debug/DebugLog.h"
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

// Walks every well-formed "VEHICLE_NAME_<id>_<variant>" token in `chunks`,
// calling `visit(rawKey, displayName, codename)` for each -- shared by
// ExtractCars5DisplayNames() (folds every token into a codename -> name map)
// and FindVehicleNameKey() (stops at the first token matching one specific
// key). `visit` returns false to stop scanning early.
template <typename Visitor>
void ForEachVehicleNameToken(const std::vector<std::string>& chunks, Visitor visit) {
    static const std::string kPrefix = "VEHICLE_NAME_";

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
            // p now marks the end of the "VEHICLE_NAME_<id>_<variant>" token
            // -- immediately followed by two length-prefixed fields: display
            // name, then "<codename>:default...".
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

            std::string rawKey = chunk.substr(pos, p - pos);
            pos = q + 4 + codeLen;

            if (!visit(rawKey, displayName, codename)) {
                return;
            }
        }
    }
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
    std::map<std::string, std::string> names;
    ForEachVehicleNameToken(chunks, [&](const std::string&, const std::string& displayName,
                                         const std::string& codename) {
        names[codename] = displayName;
        return true;
    });
    return names;
}

std::optional<VehicleNameEntry> FindVehicleNameKey(const std::vector<std::string>& chunks,
                                                     const std::string& key) {
    std::optional<VehicleNameEntry> result;
    ForEachVehicleNameToken(chunks, [&](const std::string& rawKey, const std::string& displayName,
                                         const std::string& codename) {
        if (rawKey != key) return true;
        result = VehicleNameEntry{displayName, codename};
        return false;
    });
    return result;
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
    DebugLog((L"tuning: extracted " + std::to_wstring(displayNames.size()) + L" display name(s) and " +
              std::to_wstring(cars.size()) + L" car tuning record(s) from cars5.ccrs")
                 .c_str());
    auto codename = MatchCars5Codename(carName, displayNames);
    if (!codename) {
        DebugLog((L"tuning: no codename in cars5.ccrs matches display name '" + WidenAscii(carName) +
                  L"' exactly")
                     .c_str());
        return result;
    }
    auto carIt = cars.find(*codename);
    if (carIt == cars.end()) {
        DebugLog((L"tuning: matched codename '" + WidenAscii(*codename) +
                  L"' but no tuning record found for it")
                     .c_str());
        return result;
    }

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

namespace {

// Shared by ReadTuningFromSave() and FindVehicleNameKeyInSave() -- both need
// the same decompressed cars5.ccrs contents, just extracted differently.
std::optional<std::vector<std::string>> LoadCars5Chunks() {
    auto path = FindCars5Path();
    if (!path) {
        DebugLog(L"cars5.ccrs: path not found (Steam userdata glob missed)");
        return std::nullopt;
    }
    DebugLog((L"cars5.ccrs: using file at '" + *path + L"'").c_str());

    std::ifstream in(NarrowPath(*path), std::ios::binary);
    if (!in) {
        DebugLog(L"cars5.ccrs: failed to open for reading");
        return std::nullopt;
    }
    std::string buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (buf.empty()) {
        DebugLog(L"cars5.ccrs: read as empty");
        return std::nullopt;
    }

    auto chunks = DecompressCars5Chunks(buf);
    if (!chunks) {
        DebugLog(L"cars5.ccrs: failed to decompress (unexpected header or LZ4 chunk)");
    }
    return chunks;
}

}  // namespace

std::map<std::string, int> ReadTuningFromSave(const std::string& carName) {
    if (carName.empty()) return {};

    auto chunks = LoadCars5Chunks();
    if (!chunks) return {};

    auto result = ResolveTuningIndices(*chunks, carName);
    DebugLog((L"tuning: resolved " + std::to_wstring(result.size()) + L" field(s) for '" +
              WidenAscii(carName) + L"'")
                 .c_str());
    return result;
}

std::optional<VehicleNameEntry> FindVehicleNameKeyInSave(const std::string& key) {
    if (key.empty()) return std::nullopt;

    auto chunks = LoadCars5Chunks();
    if (!chunks) return std::nullopt;

    auto entry = FindVehicleNameKey(*chunks, key);
    if (!entry) {
        DebugLog((L"cars5.ccrs: no vehicle-name entry for key '" + WidenAscii(key) + L"'").c_str());
    } else {
        DebugLog((L"cars5.ccrs: key '" + WidenAscii(key) + L"' -> '" + WidenAscii(entry->display_name) +
                  L"' (codename '" + WidenAscii(entry->codename) + L"')")
                     .c_str());
    }
    return entry;
}

}  // namespace wreckfest_telemetry
