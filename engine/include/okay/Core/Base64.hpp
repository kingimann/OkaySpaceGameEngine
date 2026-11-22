#pragma once
// Base64, written for this engine from RFC 4648: section 4 (the standard alphabet A-Z a-z 0-9 + /, '=' padding) and
// section 5 (the URL and file name safe alphabet, '-' and '_' in place of '+' and '/'). Header only, no allocation
// beyond the output.
//
// Three decoders, because the engine's readers have always differed in how forgiving they are, and each keeps its rule:
//   DecodeLenient  skips every character outside the alphabet ('=', spaces, line breaks, anything else). glTF data URIs,
//                  FBX embedded files and the voxel terrain's saved density and materials read this way.
//   DecodePadded   the strict form with padding: only alphabet characters and up to two '=' at the very end, the length
//                  a multiple of 4. The .okf font blobs read this way.
//   DecodeUnpadded the strict form without padding, as RFC 4648 section 3.2 allows: no '=', no stray character, a length
//                  that is not 1 more than a multiple of 4, and the unused low bits of the last character zero (so each
//                  byte string has one spelling). Session tickets read this way.
// Until 2026-11 each of those places carried its own copy (#422 audit item 24); tests/test_base64.cpp holds the old
// copies and checks these give the same bytes.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace okay {
namespace base64 {

enum class Alphabet { Standard, Url };

/// The 64 characters of an alphabet, in value order.
inline const char* Chars(Alphabet a) {
    return a == Alphabet::Url ? "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"
                              : "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

/// The value 0..63 of `c` in the alphabet, or -1 when it is not one of its characters.
inline int Value(char c, Alphabet a = Alphabet::Standard) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (a == Alphabet::Url) { if (c == '-') return 62; if (c == '_') return 63; }
    else { if (c == '+') return 62; if (c == '/') return 63; }
    return -1;
}

/// Encode `n` bytes. With `pad` the text is padded with '=' to a multiple of 4 characters.
inline std::string Encode(const std::uint8_t* p, std::size_t n, Alphabet a = Alphabet::Standard, bool pad = true) {
    const char* k = Chars(a);
    std::string o;
    o.reserve((n + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < n; i += 3) {
        const std::uint32_t v = ((std::uint32_t)p[i] << 16) | ((std::uint32_t)p[i + 1] << 8) | (std::uint32_t)p[i + 2];
        o += k[(v >> 18) & 63]; o += k[(v >> 12) & 63]; o += k[(v >> 6) & 63]; o += k[v & 63];
    }
    if (n - i == 1) {
        const std::uint32_t v = (std::uint32_t)p[i] << 16;
        o += k[(v >> 18) & 63]; o += k[(v >> 12) & 63];
        if (pad) o += "==";
    } else if (n - i == 2) {
        const std::uint32_t v = ((std::uint32_t)p[i] << 16) | ((std::uint32_t)p[i + 1] << 8);
        o += k[(v >> 18) & 63]; o += k[(v >> 12) & 63]; o += k[(v >> 6) & 63];
        if (pad) o += '=';
    }
    return o;
}
inline std::string Encode(const std::vector<std::uint8_t>& b, Alphabet a = Alphabet::Standard, bool pad = true) {
    return Encode(b.data(), b.size(), a, pad);
}

/// Decode, skipping every character that is not in the alphabet; the bytes are APPENDED to `out`. Whole groups of four
/// alphabet characters go through a fast path, so large embedded files decode quickly.
inline void DecodeLenient(const char* text, std::size_t n, std::vector<std::uint8_t>& out, Alphabet a = Alphabet::Standard) {
    struct Table {
        signed char t[256];
        explicit Table(Alphabet al) {
            for (int c = 0; c < 256; ++c) t[c] = (signed char)Value((char)c, al);
        }
    };
    static const Table kStd(Alphabet::Standard), kUrl(Alphabet::Url);
    const signed char* tab = a == Alphabet::Url ? kUrl.t : kStd.t;
    const unsigned char* p = (const unsigned char*)text;
    const std::size_t start = out.size();
    out.resize(start + n / 4 * 3 + 3);   // the most it can produce, trimmed at the end
    std::uint8_t* o = out.data() + start;
    std::size_t w = 0, i = 0;
    std::uint32_t acc = 0;
    int bits = 0;
    while (i < n) {
        if (bits == 0) {
            while (n - i >= 4) {
                const int c0 = tab[p[i]], c1 = tab[p[i + 1]], c2 = tab[p[i + 2]], c3 = tab[p[i + 3]];
                if ((c0 | c1 | c2 | c3) < 0) break;
                const std::uint32_t v = ((std::uint32_t)c0 << 18) | ((std::uint32_t)c1 << 12) | ((std::uint32_t)c2 << 6) | (std::uint32_t)c3;
                o[w] = (std::uint8_t)(v >> 16); o[w + 1] = (std::uint8_t)(v >> 8); o[w + 2] = (std::uint8_t)v;
                w += 3; i += 4;
            }
            if (i >= n) break;
        }
        const int v = tab[p[i++]];
        if (v < 0) continue;
        acc = ((acc << 6) | (std::uint32_t)v) & 0xFFFFFFu;
        bits += 6;
        if (bits >= 8) { bits -= 8; o[w++] = (std::uint8_t)((acc >> bits) & 0xFF); }
    }
    out.resize(start + w);
}
inline void DecodeLenient(const std::string& text, std::vector<std::uint8_t>& out, Alphabet a = Alphabet::Standard) {
    DecodeLenient(text.data(), text.size(), out, a);
}

/// Strict decode of padded text (see the top of this file). `out` is replaced. False on any broken rule.
inline bool DecodePadded(const char* text, std::size_t n, std::vector<std::uint8_t>& out, Alphabet a = Alphabet::Standard) {
    out.clear();
    std::uint32_t acc = 0;
    int have = 0, pad = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const char c = text[i];
        int v;
        if (c == '=') { ++pad; v = 0; }
        else {
            if (pad) return false;                          // data after padding
            v = Value(c, a);
            if (v < 0) return false;
        }
        if (pad > 2) return false;
        acc = (acc << 6) | (std::uint32_t)v;
        if (++have == 4) {
            out.push_back((std::uint8_t)(acc >> 16));
            if (pad < 2) out.push_back((std::uint8_t)(acc >> 8));
            if (pad < 1) out.push_back((std::uint8_t)acc);
            have = 0; acc = 0;
        }
    }
    return have == 0 && n % 4 == 0;
}
inline bool DecodePadded(const std::string& text, std::vector<std::uint8_t>& out, Alphabet a = Alphabet::Standard) {
    return DecodePadded(text.data(), text.size(), out, a);
}

/// Strict decode of unpadded text (see the top of this file). `out` is replaced. False on any broken rule.
inline bool DecodeUnpadded(const char* text, std::size_t n, std::vector<std::uint8_t>& out, Alphabet a = Alphabet::Url) {
    out.clear();
    if (n % 4 == 1) return false;
    std::uint32_t acc = 0;
    int bits = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const int v = Value(text[i], a);
        if (v < 0) return false;
        acc = (acc << 6) | (std::uint32_t)v;
        bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((std::uint8_t)((acc >> bits) & 0xFF)); }
    }
    return (acc & ((1u << bits) - 1)) == 0;   // the leftover bits must be zero
}
inline bool DecodeUnpadded(const std::string& text, std::vector<std::uint8_t>& out, Alphabet a = Alphabet::Url) {
    return DecodeUnpadded(text.data(), text.size(), out, a);
}

} // namespace base64
} // namespace okay
