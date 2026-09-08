#pragma once

#include <windows.h>

#include <csetjmp>
#include <optional>

namespace wreckfest_telemetry {

// MinGW's GCC frontend doesn't support MSVC __try/__except syntax, so
// crash-safety for arbitrary in-process reads is emulated via a Vectored
// Exception Handler that longjmp()s back out of a guarded call on an access
// violation / in-page error / misaligned access.
//
// Call InstallSehGuard() once, early (worker thread startup). Not
// thread-safe for concurrent guarded calls on multiple threads -- this
// plugin only ever has one worker thread doing memory reads, so a single
// thread-local jump buffer is enough.
void InstallSehGuard();

namespace detail {
extern thread_local jmp_buf t_sehJumpBuf;
extern thread_local bool t_sehGuarded;
}  // namespace detail

// Runs `fn` and returns its result, or std::nullopt if it raised a
// hardware exception. `fn` must return a trivially-copyable type and must
// not own resources that need unwinding (no destructors will run on the
// exception path -- keep guarded calls to plain reads).
template <typename Fn>
auto SehGuarded(Fn&& fn) -> std::optional<decltype(fn())> {
    using Result = decltype(fn());
    if (setjmp(detail::t_sehJumpBuf) == 0) {
        detail::t_sehGuarded = true;
        Result result = fn();
        detail::t_sehGuarded = false;
        return result;
    }
    detail::t_sehGuarded = false;
    return std::nullopt;
}

}  // namespace wreckfest_telemetry
