#include "strings/ServerName.h"

#include "debug/DebugLog.h"
#include "memory/ProcessMemory.h"
#include "strings/HashRegistry.h"
#include "strings/Utf8.h"

namespace wreckfest_telemetry {

namespace {

constexpr uintptr_t kServerNameLenOff = 0x0;
constexpr uintptr_t kServerNameTextOff = 0x8;
// The game caps names at 63 characters in practice; this only bounds the read.
constexpr uint64_t kServerNameMaxLen = 255;

// Debug-log rendering: non-ASCII bytes as '?', since the wide log stream
// can fail on them (and then stop writing altogether).
std::wstring AsciiForLog(const std::string& s) {
    std::wstring out;
    for (unsigned char c : s) out.push_back(c >= 0x20 && c < 0x7F ? static_cast<wchar_t>(c) : L'?');
    return out;
}

}  // namespace

std::optional<std::string> ReadOnlineServerName(uintptr_t tableBase) {
    if (tableBase == 0) return std::nullopt;

    auto client = HashRegistryLookup(tableBase, "CLIENT");
    auto slot = client ? ReadI32(*client) : std::nullopt;
    if (!slot) {
        DebugLog(L"server name: CLIENT unresolved -- treated as offline");
        return std::nullopt;
    }
    if (*slot < 0) {
        DebugLog(L"server name: offline (CLIENT slot -1)");
        return std::nullopt;
    }

    auto obj = HashRegistryLookup(tableBase, "server_name");
    auto len = obj ? ReadU64(*obj + kServerNameLenOff) : std::nullopt;
    auto text = obj ? ReadU64(*obj + kServerNameTextOff) : std::nullopt;
    if (!len || !text || *len == 0 || *len > kServerNameMaxLen || *text < 0x10000) {
        DebugLog((L"server name: online (CLIENT slot " + std::to_wstring(*slot) +
                  L") but the server_name object didn't resolve")
                     .c_str());
        return std::nullopt;
    }
    auto name = ReadCString(static_cast<uintptr_t>(*text), static_cast<size_t>(*len));
    if (!name || name->empty()) {
        DebugLog(L"server name: server_name text unreadable");
        return std::nullopt;
    }
    std::string result = SanitizeUtf8(*name);
    DebugLog((L"server name: online (CLIENT slot " + std::to_wstring(*slot) + L"), '" + AsciiForLog(result) + L"'")
                 .c_str());
    return result;
}

}  // namespace wreckfest_telemetry
