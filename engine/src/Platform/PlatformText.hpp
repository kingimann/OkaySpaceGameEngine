#pragma once
// Pure text helpers shared by the own backends (Win32 now, Cocoa later). No OS headers, so they
// are unit-tested on Linux (tests/test_platform_pure.cpp).
//
//  * UTF-8 <-> UTF-16 conversion that never produces ill-formed output: a lone surrogate or a
//    malformed UTF-8 byte becomes U+FFFD (the Win32 API silently drops or fails instead).
//  * Utf16Assembler: turns the stream of UTF-16 code units a window system delivers one at a
//    time (WM_CHAR on Windows, NSEvent characters on macOS) into UTF-8 text input, pairing
//    surrogates, dropping control characters and recovering from an unpaired surrogate.
//  * ToCrlf / FromCrlf: the Windows clipboard uses CR LF line ends, the engine uses LF.
#include <cstddef>
#include <cstdint>
#include <string>

namespace okay::platform::text {

inline void AppendUtf8(std::string& s, uint32_t cp) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp < 0xE000)) cp = 0xFFFD;
    if (cp < 0x80) s += (char)cp;
    else if (cp < 0x800) { s += (char)(0xC0 | (cp >> 6)); s += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { s += (char)(0xE0 | (cp >> 12)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
    else { s += (char)(0xF0 | (cp >> 18)); s += (char)(0x80 | ((cp >> 12) & 0x3F)); s += (char)(0x80 | ((cp >> 6) & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
}

inline std::string Utf16ToUtf8(const char16_t* w, size_t n) {
    std::string out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const uint32_t c = w[i];
        if (c >= 0xD800 && c < 0xDC00) {
            if (i + 1 < n && w[i + 1] >= 0xDC00 && w[i + 1] < 0xE000) {
                AppendUtf8(out, 0x10000 + ((c - 0xD800) << 10) + (w[i + 1] - 0xDC00));
                ++i;
            } else AppendUtf8(out, 0xFFFD);
        } else AppendUtf8(out, c);   // a lone low surrogate becomes U+FFFD inside AppendUtf8
    }
    return out;
}

inline std::u16string Utf8ToUtf16(const std::string& s) {
    std::u16string out;
    out.reserve(s.size());
    const size_t n = s.size();
    size_t i = 0;
    while (i < n) {
        const uint8_t b = (uint8_t)s[i];
        uint32_t cp = 0xFFFD;
        size_t len = 1;
        if (b < 0x80) cp = b;
        else if (b >= 0xC2 && b <= 0xF4) {
            const size_t need = b < 0xE0 ? 1 : b < 0xF0 ? 2 : 3;
            uint32_t v = b & (b < 0xE0 ? 0x1F : b < 0xF0 ? 0x0F : 0x07);
            bool ok = (i + need) < n;   // all continuation bytes must exist and be 10xxxxxx
            if (ok) for (size_t k = 1; k <= need; ++k) {
                const uint8_t c = (uint8_t)s[i + k];
                if ((c & 0xC0) != 0x80) { ok = false; break; }
                v = (v << 6) | (c & 0x3F);
            }
            if (ok) {
                const bool overlong = (need == 2 && v < 0x800) || (need == 3 && v < 0x10000);
                if (!overlong && v <= 0x10FFFF && !(v >= 0xD800 && v < 0xE000)) { cp = v; len = need + 1; }
            }
        }
        i += len;
        if (cp >= 0x10000) { cp -= 0x10000; out += (char16_t)(0xD800 + (cp >> 10)); out += (char16_t)(0xDC00 + (cp & 0x3FF)); }
        else out += (char16_t)cp;
    }
    return out;
}

// Feed one UTF-16 code unit at a time. Returns true and appends UTF-8 to `out` when the unit completes a
// printable character; false when it was held (a high surrogate), dropped (a control character) or was
// part of an unpaired pair (then U+FFFD is emitted for the orphan, once).
class Utf16Assembler {
public:
    bool Feed(char16_t c, std::string& out) {
        if (c >= 0xD800 && c < 0xDC00) {
            const bool hadOrphan = m_high != 0;
            m_high = c;
            if (hadOrphan) { AppendUtf8(out, 0xFFFD); return true; }
            return false;
        }
        if (c >= 0xDC00 && c < 0xE000) {
            if (m_high) {
                AppendUtf8(out, 0x10000 + (((uint32_t)m_high - 0xD800) << 10) + (c - 0xDC00));
                m_high = 0;
            } else AppendUtf8(out, 0xFFFD);
            return true;
        }
        bool wrote = false;
        if (m_high) { AppendUtf8(out, 0xFFFD); m_high = 0; wrote = true; }   // a high surrogate followed by a normal unit
        if (c >= 0x20 && c != 0x7F && !(c >= 0x80 && c < 0xA0)) { AppendUtf8(out, c); wrote = true; }
        return wrote;
    }
    void Reset() { m_high = 0; }
    bool Pending() const { return m_high != 0; }
private:
    char16_t m_high = 0;
};

inline std::string ToCrlf(const std::string& s) {
    std::string out;
    out.reserve(s.size() + s.size() / 16);
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\n' && (i == 0 || s[i - 1] != '\r')) out += '\r';
        out += s[i];
    }
    return out;
}
inline std::string FromCrlf(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n') continue;
        out += s[i];
    }
    return out;
}

} // namespace okay::platform::text
