#include "memory/SehGuard.h"

namespace wreckfest_telemetry {

namespace detail {
thread_local jmp_buf t_sehJumpBuf;
thread_local bool t_sehGuarded = false;
}  // namespace detail

namespace {

LONG CALLBACK VectoredHandler(PEXCEPTION_POINTERS info) {
    DWORD code = info->ExceptionRecord->ExceptionCode;
    bool isMemoryFault = code == EXCEPTION_ACCESS_VIOLATION ||
                          code == EXCEPTION_IN_PAGE_ERROR ||
                          code == EXCEPTION_DATATYPE_MISALIGNMENT;
    if (isMemoryFault && detail::t_sehGuarded) {
        longjmp(detail::t_sehJumpBuf, 1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void InstallSehGuard() {
    AddVectoredExceptionHandler(1, VectoredHandler);
}

}  // namespace wreckfest_telemetry
