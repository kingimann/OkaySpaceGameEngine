#pragma once
#include <cstdint>
#include <string>

namespace okay {

/// Decode the UTF-8 code point at `p` and advance `p` past it. Malformed or
/// truncated sequences yield U+FFFD and advance one byte, so a loop over bad
/// input always terminates. Call only while *p != '\0'.
inline std::uint32_t NextCodepoint(const char*& p) {
    const auto* s = reinterpret_cast<const unsigned char*>(p);
    const unsigned c = s[0];
    int len = 0;
    std::uint32_t cp = 0, min = 0;
    if (c < 0x80)              { ++p; return c; }
    else if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; min = 0x80; }
    else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; min = 0x800; }
    else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; min = 0x10000; }
    else { ++p; return 0xFFFD; }
    for (int i = 1; i < len; ++i) {
        if ((s[i] & 0xC0) != 0x80) { ++p; return 0xFFFD; }   // also stops at the terminator
        cp = (cp << 6) | (s[i] & 0x3F);
    }
    if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) { ++p; return 0xFFFD; }
    p += len;
    return cp;
}

/// Append `cp` to `out` as UTF-8.
inline void AppendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) { out += (char)cp; return; }
    if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); }
    else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); }
    else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); }
    out += (char)(0x80 | (cp & 0x3F));
}

/// Number of code points in a UTF-8 string.
inline std::size_t Utf8Length(const std::string& s) {
    std::size_t n = 0;
    for (const char* p = s.c_str(); *p;) { NextCodepoint(p); ++n; }
    return n;
}

} // namespace okay
