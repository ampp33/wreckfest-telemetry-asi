#include "tuning/AssemblyTuning.h"

#include <cctype>

#include "tuning/SaveFileTuning.h"

namespace wreckfest_telemetry {

namespace {

bool AllDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

}  // namespace

std::optional<AssemblyName> ParseAssemblyName(const std::string& name) {
    static const std::string kPrefix = "vehicle/";
    static const std::string kSuffix = "/assembly.veas";
    if (name.size() < kPrefix.size() + 3 + kSuffix.size() || name.compare(0, kPrefix.size(), kPrefix) != 0 ||
        name.compare(name.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0) {
        return std::nullopt;
    }
    std::string slot = name.substr(kPrefix.size(), 2);
    if (!AllDigits(slot) || name[kPrefix.size() + 2] != '/') return std::nullopt;

    // "<codename>_<id>", with "_ai" before the id for AI cars.
    size_t segStart = kPrefix.size() + 3;
    std::string segment = name.substr(segStart, name.size() - kSuffix.size() - segStart);
    size_t idSep = segment.rfind('_');
    if (idSep == std::string::npos || idSep == 0 || !AllDigits(segment.substr(idSep + 1))) return std::nullopt;

    AssemblyName result;
    result.slot = std::stoi(slot);
    result.codename = segment.substr(0, idSep);
    static const std::string kAi = "_ai";
    if (result.codename.size() > kAi.size() &&
        result.codename.compare(result.codename.size() - kAi.size(), kAi.size(), kAi) == 0) {
        result.ai = true;
        result.codename.resize(result.codename.size() - kAi.size());
    }
    return result;
}

std::map<std::string, int> TuningFromPartPaths(const std::vector<std::string>& paths, const std::string& codename) {
    std::map<std::string, int> result;
    const std::string prefix = "data/vehicle/" + codename + "/part/";
    for (const auto& path : paths) {
        if (path.compare(0, prefix.size(), prefix) != 0) continue;
        size_t partEnd = path.find('/', prefix.size());
        if (partEnd == std::string::npos) continue;
        size_t presetEnd = path.find('.', partEnd + 1);
        if (presetEnd == std::string::npos) continue;
        auto index = TuningPresetIndex(path.substr(prefix.size(), partEnd - prefix.size()),
                                       path.substr(partEnd + 1, presetEnd - partEnd - 1));
        if (index && !result.count(index->first)) result[index->first] = index->second;
    }
    return result;
}

}  // namespace wreckfest_telemetry
