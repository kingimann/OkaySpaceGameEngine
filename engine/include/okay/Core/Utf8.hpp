#pragma once
// UTF-8 helpers for text players read in any language: walking code points,
// counting them and cutting a string on a code point boundary. Malformed bytes
// read as U+FFFD and always advance at least one byte, so a loop over any string ends.
#include <cstddef>
#include <cstdint>
#include <string>

namespace okay::utf8 {

constexpr std::uint32_t kReplacement = 0xFFFD;

/// The code point at `p`, advancing `p` past it (never past `end`). Overlong forms,
/// surrogates (U+D800..DFFF), code points above U+10FFFF and bytes that can never
/// start a sequence (C0, C1, F5..FF) give U+FFFD. A truncated or broken sequence
/// consumes its valid leading bytes only (the "maximal subpart"), so the byte that
/// broke it is read again as the start of the next code point.
///
/// At or past `end` there is nothing to read: it returns 0 and leaves `p` at `end`
/// (it used to read one byte beyond the string, and the string overload walked its
/// index past the end).
inline std::uint32_t Next(const char*& p, const char* end) {
    if (p >= end) { p = end; return 0; }
    const unsigned char c = (unsigned char)*p++;
    if (c < 0x80) return c;
    if (c < 0xC2 || c > 0xF4) return kReplacement;     // continuation byte alone, C0/C1 overlong, F5..FF
    const int extra = c < 0xE0 ? 1 : c < 0xF0 ? 2 : 3;
    std::uint32_t cp = c & (0x3Fu >> extra);
    for (int k = 0; k < extra; ++k) {
        if (p >= end) return kReplacement;
        const unsigned char b = (unsigned char)*p;
        unsigned char lo = 0x80, hi = 0xBF;            // the second byte has tighter bounds for some leads
        if (k == 0) {
            if (c == 0xE0) lo = 0xA0;                  // no overlong 3-byte
            else if (c == 0xED) hi = 0x9F;             // no surrogates
            else if (c == 0xF0) lo = 0x90;             // no overlong 4-byte
            else if (c == 0xF4) hi = 0x8F;             // nothing above U+10FFFF
        }
        if (b < lo || b > hi) return kReplacement;
        ++p;
        cp = (cp << 6) | (b & 0x3F);
    }
    return cp;
}

/// The code point at byte `i` of `s`, advancing `i` past it.
inline std::uint32_t Next(const std::string& s, std::size_t& i) {
    if (i >= s.size()) { i = s.size(); return 0; }
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

/// Append `cp` encoded as UTF-8. Values that are not a Unicode scalar (above U+10FFFF
/// or a surrogate, U+D800..DFFF) are written as U+FFFD: encoding a surrogate as its
/// three bytes made text that no decoder, this one included, reads back as one character.
inline void Append(std::string& out, std::uint32_t cp) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = kReplacement;
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
