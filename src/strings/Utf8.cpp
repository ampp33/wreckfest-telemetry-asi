#include "strings/Utf8.h"

#include <cstdint>

namespace wreckfest_telemetry {

std::string SanitizeUtf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        size_t len = 0;
        uint32_t min = 0;
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
            ++i;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            len = 2;
            min = 0x80;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
            min = 0x800;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
            min = 0x10000;
        }
        bool valid = len > 0 && i + len <= s.size();
        uint32_t cp = valid ? (c & (0x7F >> len)) : 0;
        for (size_t k = 1; valid && k < len; ++k) {
            unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) valid = false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        // Reject overlong encodings, UTF-16 surrogates and out-of-range code points.
        if (valid && (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))) valid = false;
        if (valid) {
            out.append(s, i, len);
            i += len;
        } else {
            out.push_back('?');
            ++i;
        }
    }
    return out;
}

}  // namespace wreckfest_telemetry
