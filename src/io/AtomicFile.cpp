#include "io/AtomicFile.h"

#include <windows.h>

namespace wreckfest_telemetry {

bool AtomicWriteFile(const std::wstring& path, const std::string& content) {
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
    if (ok) FlushFileBuffers(h);
    CloseHandle(h);
    if (ok && written == content.size() && MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        return true;
    }
    DeleteFileW(tmp.c_str());
    return false;
}

}  // namespace wreckfest_telemetry
