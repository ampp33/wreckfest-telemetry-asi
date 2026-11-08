#pragma once

#include <cstddef>
#include <deque>
#include <optional>
#include <string>

namespace wreckfest_telemetry {

// Decides, tick by tick, when a finished race gets emitted (logged + posted)
// -- once per race per session, however the scrape flickers around it.
// Pure logic (no game reads) so the tick sequences that used to duplicate
// races can be replayed in unit tests.
//
// The old gate remembered only the LAST emitted fingerprint and forgot it
// on any tick with no visible players. The results stay in memory well
// after the race, so either of these re-emitted the same race (observed in
// the races table: identical pairs a few seconds to a few minutes apart,
// some with re-read tuning/track that differed between the two copies):
//   - one empty tick (A, <none>, A, A) -- the reset re-armed A
//   - a fingerprint flip (A, B, B, A, A) -- e.g. a tick where the local
//     player wasn't identified, so B (the whole-field fingerprint) was
//     emitted and A no longer matched "last emitted"
// Every emitted fingerprint is now kept for the session (bounded, oldest
// dropped first), and an empty tick only breaks stability, never memory.
class RaceEmitGate {
public:
    enum class Decision {
        kNone,       // not final, not stable yet, or already emitted and still showing
        kEmit,       // caller emits the race, then calls MarkEmitted()
        kDuplicate,  // already emitted, reappeared after a gap -- the old gate would have re-emitted
    };

    // Call on a tick where no players were visible.
    void ObserveEmpty();

    // Call on a tick with visible players. A race is emitted once it's
    // final and its fingerprint has held for two consecutive ticks.
    Decision Observe(const std::string& fingerprint, bool raceFinal);

    void MarkEmitted(const std::string& fingerprint);

    // For kDuplicate's log line: whether the gap before this fingerprint's
    // current run was empty ticks (true) or a different fingerprint (false).
    bool LastGapWasEmpty() const { return runStartedAfterEmpty_; }

    static constexpr std::size_t kMaxRemembered = 64;

private:
    bool AlreadyEmitted(const std::string& fingerprint) const;

    std::optional<std::string> lastSeen_;
    int runLength_ = 0;
    bool runStartedAfterEmpty_ = false;
    bool firstPoll_ = true;
    std::deque<std::string> emitted_;
};

}  // namespace wreckfest_telemetry
