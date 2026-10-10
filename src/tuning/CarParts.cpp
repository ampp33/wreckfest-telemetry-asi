#include "tuning/CarParts.h"

#include "strings/Utf8.h"

namespace wreckfest_telemetry {

namespace {

enum class SlotKind { Engine, EnginePart, Armor };

struct Slot {
    const char* ext;
    const char* key;
    SlotKind kind;
};

constexpr Slot kSlots[] = {
    {"veen", "engine", SlotKind::Engine},
    {"vefi", "air_filter", SlotKind::EnginePart},
    {"veco", "cooling", SlotKind::EnginePart},
    {"veim", "intake_manifold", SlotKind::EnginePart},
    {"vefs", "fuel_system", SlotKind::EnginePart},
    {"veig", "ignition", SlotKind::EnginePart},
    {"veex", "exhaust", SlotKind::EnginePart},
    {"veem", "exhaust_manifold", SlotKind::EnginePart},
    {"veva", "valves", SlotKind::EnginePart},
    {"veca", "camshaft", SlotKind::EnginePart},
    {"veps", "pistons", SlotKind::EnginePart},
    {"vfbu", "front_bumper", SlotKind::Armor},
    {"vrbu", "rear_bumper", SlotKind::Armor},
    {"vrol", "roll_cage", SlotKind::Armor},
    {"vpsp", "side_protector", SlotKind::Armor},
    {"vewb", "window_bars", SlotKind::Armor},
};

const Slot* SlotForExtension(const std::string& ext) {
    for (const auto& slot : kSlots) {
        if (ext == slot.ext) return &slot;
    }
    return nullptr;
}

}  // namespace

std::optional<std::string> EngineTier(const std::string& stem) {
    static const std::string kBlock = "engine_block_";
    char letter = 0;
    if (stem.size() == kBlock.size() + 1 && stem.compare(0, kBlock.size(), kBlock) == 0) {
        letter = stem.back();
    } else if (stem.size() >= 7 && stem.compare(1, 6, "_class") == 0 && (stem.size() == 7 || stem[7] == '(')) {
        letter = stem[0];
    }
    switch (letter) {
        case 'a': return "race";
        case 'b': return "sport";
        case 'c': return "street";
        default: return std::nullopt;
    }
}

CarParts PartsFromPartPaths(const std::vector<std::string>& paths, const std::string& codename,
                            const std::function<std::optional<std::string>(const std::string&)>& lookupName) {
    CarParts parts;
    // Some entries carry a per-spawn "vehicle/NN/<codename>_<id>/" prefix
    // ahead of "data/...", so match the directory anywhere in the path.
    const std::string own = "data/vehicle/" + codename + "/part/";
    const std::string shared = "data/vehicle/shared/part/";
    for (const auto& path : paths) {
        if (path.find(own) == std::string::npos && path.find(shared) == std::string::npos) continue;
        size_t slash = path.rfind('/');
        size_t dot = path.rfind('.');
        if (slash == std::string::npos || dot == std::string::npos || dot < slash) continue;
        const Slot* slot = SlotForExtension(path.substr(dot + 1));
        if (!slot) continue;
        std::string stem = path.substr(slash + 1, dot - slash - 1);
        if (stem.empty()) continue;

        switch (slot->kind) {
            case SlotKind::Engine:
                if (parts.engine.empty()) parts.engine = EngineTier(stem).value_or(stem);
                break;
            case SlotKind::EnginePart:
                parts.engine_parts.emplace(slot->key, stem);
                break;
            case SlotKind::Armor:
                if (!parts.armor.count(slot->key)) {
                    parts.armor[slot->key] = ArmorPart{stem, lookupName ? lookupName(path) : std::nullopt};
                }
                break;
        }
    }
    return parts;
}

nlohmann::json PartsToJson(const CarParts& parts) {
    nlohmann::json d = nlohmann::json::object();
    if (!parts.engine.empty()) d["engine"] = SanitizeUtf8(parts.engine);
    if (!parts.engine_parts.empty()) {
        nlohmann::json engineParts = nlohmann::json::object();
        for (const auto& [slot, preset] : parts.engine_parts) engineParts[slot] = SanitizeUtf8(preset);
        d["engine_parts"] = engineParts;
    }
    if (!parts.armor.empty()) {
        nlohmann::json armor = nlohmann::json::object();
        for (const auto& [slot, part] : parts.armor) {
            armor[slot] = {{"code", SanitizeUtf8(part.code)},
                           {"name", part.name ? nlohmann::json(SanitizeUtf8(*part.name)) : nlohmann::json(nullptr)}};
        }
        d["armor"] = armor;
    }
    return d;
}

}  // namespace wreckfest_telemetry
