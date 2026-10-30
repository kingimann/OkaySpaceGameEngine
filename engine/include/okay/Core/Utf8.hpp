#pragma once
// UTF-8 helpers for text players read in any language: walking code points,
// counting them and cutting a string on a code point boundary. Malformed bytes
// read as U+FFFD and advance one byte, so a loop over any string always ends.
#include <cstddef>
#include <cstdint>
#include <string>

namespace okay::utf8 {

constexpr std::uint32_t kReplacement = 0xFFFD;

/// The code point at `p`, advancing `p` past it (never past `end`).
inline std::uint32_t Next(const char*& p, const char* end) {
    const unsigned char c = (unsigned char)*p++;
    if (c < 0x80) return c;
    if (c < 0xC0) return kReplacement;                 // a continuation byte on its own
    const int extra = c < 0xE0 ? 1 : c < 0xF0 ? 2 : c < 0xF8 ? 3 : -1;
    if (extra < 0) return kReplacement;
    std::uint32_t cp = c & (0x3Fu >> extra);
    for (int k = 0; k < extra; ++k) {
        if (p >= end || ((unsigned char)*p & 0xC0) != 0x80) return kReplacement;
        cp = (cp << 6) | ((unsigned char)*p++ & 0x3F);
    }
    return cp;
}

/// The code point at byte `i` of `s`, advancing `i` past it.
inline std::uint32_t Next(const std::string& s, std::size_t& i) {
    const char* p = s.data() + i;
    const std::uint32_t cp = Next(p, s.data() + s.size());
    i = (std::size_t)(p - s.data());
    return cp;
}

/// Code points in `s`.
inline std::size_t Length(const std::string& s) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size(); ++n) Next(s, i);
    return n;
}

/// Byte offset where code point `n` starts (s.size() when `s` is shorter).
inline std::size_t Offset(const std::string& s, std::size_t n) {
    std::size_t i = 0;
    for (std::size_t k = 0; k < n && i < s.size(); ++k) Next(s, i);
    return i;
}

/// Append `cp` encoded as UTF-8.
inline void Append(std::string& out, std::uint32_t cp) {
    if (cp > 0x10FFFF) cp = kReplacement;
    if (cp < 0x80) { out += (char)cp; return; }
    if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); return; }
    if (cp < 0x10000) {
        out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
        return;
    }
    out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F));
    out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F));
}

/// True when every byte of `s` is ASCII.
inline bool IsAscii(const std::string& s) {
    for (char c : s) if ((unsigned char)c >= 0x80) return false;
    return true;
}

} // namespace okay::utf8
