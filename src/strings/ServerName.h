#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace wreckfest_telemetry {

// Name of the online server the local player is connected to, raw as the
// game holds it (Wreckfest color codes like "^2"/"^:" included), or nullopt
// when offline or unresolved. Meant for the results screen, read once per
// race alongside everything else.
//
// The "server_name" registry object (+0x0 length, +0x8 text pointer) is
// never cleared -- it keeps the last server's name after leaving and
// through offline races -- so it's only trusted while CLIENT+0x0 (the
// local player's slot on the server) is >= 0. Verified live 2026-09-30:
// CLIENT+0x0 held the slot (0, 1 and 20 across three servers) through
// lobby, race and results, and read -1 throughout the menus and an offline
// race and its results. Slot 0 is a real online slot, hence >= 0.
std::optional<std::string> ReadOnlineServerName(uintptr_t tableBase);

}  // namespace wreckfest_telemetry
