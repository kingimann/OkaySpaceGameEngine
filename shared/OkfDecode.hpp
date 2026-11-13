#pragma once
// OKF1: our own small container for embedded binary assets (the fonts), and its decoder. Header only,
// standard library only, no allocation beyond the output vector. Replaces the Dear ImGui base85 +
// stb_compress reader that engine/src/Graphics/EmbeddedFont.cpp used to carry.
//
// ---- Format --------------------------------------------------------------------------------------------------------
// Source form: an array of string literals, each a piece of one base64 text (RFC 4648 alphabet, '=' padding, no
// whitespace). Concatenate the pieces and decode the base64 to get the OKF1 blob:
//
//     offset 0   4 bytes   magic "OKF1"
//     offset 4   1 byte    method: 0 = the payload is the raw bytes, 1 = the payload is an LZ stream
//     offset 5   3 bytes   zero
//     offset 8   4 bytes   raw size, little endian (at most kMaxRaw)
//     offset 12  4 bytes   CRC-32 (IEEE 802.3, the zlib/PNG polynomial) of the raw bytes, little endian
//     offset 16  ...       payload
//
// LZ stream (method 1), a token format in the LZ4 family; the output is produced until it is `raw size` bytes long:
//
//     token       1 byte: high nibble = literal count L, low nibble = match length M - 4 (so matches are 4 or longer)
//     L extra     only if the high nibble is 15: bytes added to L until one is below 255 (the 255s and the last one all add)
//     literals    L bytes copied to the output
//                 -- if the output now has `raw size` bytes the stream ends here (the last token has no match) --
//     offset      2 bytes little endian, 1 .. bytes produced so far: the match starts this far back (may overlap itself)
//     M extra     only if the low nibble is 15: bytes added to M - 4 as for L
//     match       M bytes copied from (output end - offset), byte by byte
//
// The stream must end exactly at the end of the payload; anything else (a match before the start, an overrun, trailing
// bytes, a wrong size or CRC) is an error and Decode returns false without crashing. scripts/gen_embedded_fonts.py writes
// the data and has an independent Python reader; tests/test_embedded_font.cpp pins the decoded bytes by SHA-256.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace okf {

constexpr std::size_t kMaxRaw = 64u * 1024u * 1024u;   // refuse absurd sizes in a damaged header

inline std::uint32_t Crc32(const std::uint8_t* p, std::size_t n) {
    std::uint32_t table[256];
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        table[i] = c;
    }
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// Base64 (RFC 4648) of the concatenated lines -> bytes. False on any character outside the alphabet, on misplaced or
// missing padding, or on a length that is not a multiple of 4.
inline bool Base64Decode(const char* const* lines, std::size_t lineCount, std::vector<std::uint8_t>& out) {
    out.clear();
    std::uint32_t acc = 0;
    int have = 0, pad = 0;
    std::size_t chars = 0;
    for (std::size_t l = 0; l < lineCount; ++l) {
        for (const char* s = lines[l]; *s; ++s) {
            const char c = *s;
            int v;
            if (c >= 'A' && c <= 'Z') v = c - 'A';
            else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
            else if (c >= '0' && c <= '9') v = c - '0' + 52;
            else if (c == '+') v = 62;
            else if (c == '/') v = 63;
            else if (c == '=') { ++pad; v = 0; }
            else return false;
            if (pad && c != '=') return false;             // data after padding
            if (pad > 2) return false;
            ++chars;
            acc = (acc << 6) | static_cast<std::uint32_t>(v);
            if (++have == 4) {
                out.push_back(static_cast<std::uint8_t>(acc >> 16));
                if (pad < 2) out.push_back(static_cast<std::uint8_t>(acc >> 8));
                if (pad < 1) out.push_back(static_cast<std::uint8_t>(acc));
                have = 0; acc = 0;
            }
        }
    }
    return have == 0 && chars % 4 == 0;
}

// The OKF1 blob -> the raw bytes.
inline bool Unpack(const std::uint8_t* in, std::size_t n, std::vector<std::uint8_t>& out) {
    out.clear();
    if (n < 16 || in[0] != 'O' || in[1] != 'K' || in[2] != 'F' || in[3] != '1') return false;
    if (in[5] || in[6] || in[7]) return false;
    const std::uint32_t raw = static_cast<std::uint32_t>(in[8]) | static_cast<std::uint32_t>(in[9]) << 8 |
                              static_cast<std::uint32_t>(in[10]) << 16 | static_cast<std::uint32_t>(in[11]) << 24;
    const std::uint32_t crc = static_cast<std::uint32_t>(in[12]) | static_cast<std::uint32_t>(in[13]) << 8 |
                              static_cast<std::uint32_t>(in[14]) << 16 | static_cast<std::uint32_t>(in[15]) << 24;
    if (raw > kMaxRaw) return false;
    const std::uint8_t* p = in + 16;
    const std::uint8_t* const end = in + n;
    if (in[4] == 0) {                                      // stored
        if (static_cast<std::size_t>(end - p) != raw) return false;
        out.assign(p, end);
    } else if (in[4] == 1) {                               // LZ
        out.reserve(raw);
        auto readLen = [&](std::size_t& v) {               // the "extra" bytes of a length
            for (;;) {
                if (p >= end) return false;
                const std::uint8_t b = *p++;
                v += b;
                if (v > raw) return false;                 // no length can exceed the output size
                if (b != 255) return true;
            }
        };
        while (out.size() < raw) {
            if (p >= end) { out.clear(); return false; }
            const std::uint8_t token = *p++;
            std::size_t lits = token >> 4;
            if (lits == 15 && !readLen(lits)) { out.clear(); return false; }
            if (lits > static_cast<std::size_t>(end - p) || lits > raw - out.size()) { out.clear(); return false; }
            out.insert(out.end(), p, p + lits);
            p += lits;
            if (out.size() == raw) break;                  // last token: literals only
            if (end - p < 2) { out.clear(); return false; }
            const std::size_t off = static_cast<std::size_t>(p[0]) | static_cast<std::size_t>(p[1]) << 8;
            p += 2;
            std::size_t len = token & 15;
            if (len == 15 && !readLen(len)) { out.clear(); return false; }
            len += 4;
            if (off < 1 || off > out.size() || len > raw - out.size()) { out.clear(); return false; }
            std::size_t from = out.size() - off;
            for (std::size_t i = 0; i < len; ++i) out.push_back(out[from + i]);   // (push_back may reallocate: index, not iterator)
        }
        if (p != end) { out.clear(); return false; }       // trailing bytes
    } else {
        return false;
    }
    if (out.size() != raw || Crc32(out.data(), out.size()) != crc) { out.clear(); return false; }
    return true;
}

// The whole thing: the string-literal lines of an embedded asset -> the raw bytes.
inline bool DecodeLines(const char* const* lines, std::size_t lineCount, std::vector<std::uint8_t>& out) {
    std::vector<std::uint8_t> blob;
    if (!Base64Decode(lines, lineCount, blob)) { out.clear(); return false; }
    return Unpack(blob.data(), blob.size(), out);
}

} // namespace okf
