#include "race/RaceEmitGate.h"

#include <algorithm>

namespace wreckfest_telemetry {

void RaceEmitGate::ObserveEmpty() {
    firstPoll_ = false;
    lastSeen_.reset();
    runLength_ = 0;
}

RaceEmitGate::Decision RaceEmitGate::Observe(const std::string& fingerprint, bool raceFinal) {
    if (lastSeen_ && *lastSeen_ == fingerprint) {
        ++runLength_;
    } else {
        runStartedAfterEmpty_ = !lastSeen_.has_value();
        runLength_ = 1;
    }
    lastSeen_ = fingerprint;

    // A race already finished when the plugin attached is adopted as
    // already-emitted rather than emitted -- it has no lap splits (never
    // watched from the start), so emitting it would be a strictly worse
    // duplicate of whatever's already in the log from a prior session.
    //
    // Only the plugin's very first poll counts: an empty tick clears it too
    // (ObserveEmpty), since the ASI loads with the game and first polls in
    // the menus. Clearing it only on the first tick with a visible player
    // dropped solo races -- a racer only becomes visible with a valid best
    // lap, so the first race of a session could first appear already final.
    if (firstPoll_ && raceFinal) MarkEmitted(fingerprint);
    firstPoll_ = false;

    if (!raceFinal || runLength_ < 2) return Decision::kNone;
    if (!AlreadyEmitted(fingerprint)) return Decision::kEmit;
    // Reported once per run, on the tick the old gate would have fired.
    return runLength_ == 2 ? Decision::kDuplicate : Decision::kNone;
}

void RaceEmitGate::MarkEmitted(const std::string& fingerprint) {
    if (AlreadyEmitted(fingerprint)) return;
    emitted_.push_back(fingerprint);
    if (emitted_.size() > kMaxRemembered) emitted_.pop_front();
}

bool RaceEmitGate::AlreadyEmitted(const std::string& fingerprint) const {
    return std::find(emitted_.begin(), emitted_.end(), fingerprint) != emitted_.end();
}

}  // namespace wreckfest_telemetry
