#pragma once

#include <vector>

#include "race/PlayerResult.h"

namespace wreckfest_telemetry {

// True once the results screen has been reached -- every real racer's
// status carries a terminal marker (STATUS_CLASSIFIED_BIT) and nobody is
// still circulating (stillRacingCount == 0, a hard veto checked first,
// unconditionally -- see CountStillRacing). Falls back to a parity check
// on the local player's own finished flag only when no player has any
// status bits populated at all (a mode that doesn't use the field), and to
// an all-players check if the local player can't be identified. Do not
// simplify this back to an all-players classified check -- see
// wreckfest_telemetry.py's docstring for the straggler bug that fix
// caused.
bool RaceIsFinal(const std::vector<PlayerResult>& players, int stillRacingCount);

}  // namespace wreckfest_telemetry
