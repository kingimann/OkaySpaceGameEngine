// A deliberately slow, obviously-correct DEFLATE / zlib / gzip reference for tests/test_inflate_differential.cpp.
//
// Written fresh from RFC 1950, RFC 1951 and RFC 1952 for this test, in its own structure, and NOT derived from any other
// decoder (not from engine/include/okay/Core/Inflate.hpp, not from the three decoders that Core/Inflate.hpp replaced, not
// from any outside project). The idea is the plainest one the RFC describes:
//  - bits are read ONE AT A TIME, least significant bit of each byte first (RFC 1951 3.1.1);
//  - a Huffman code is read one bit at a time, most significant code bit first, and after each bit we ask "is there a
//    symbol whose canonical code (RFC 1951 3.2.2, computed the long way) has exactly this length and value?";
//  - lengths, distances and the code-length alphabet come from the tables in RFC 1951 3.2.5 and 3.2.7 typed in as data.
// It follows the same acceptance POLICY as the engine decoder (that is what is being compared), which is stated here once:
//  - lenient (images, models): a Huffman code only has to be not over-subscribed; zlib: header checked, window size and the
//    Adler-32 trailer are not, bytes after the stream are ignored; PNG mode (stopAtMax): decoding stops the moment the
//    wanted number of bytes exist and everything after is ignored (a stored block that does not fit is still an error);
//  - strict (HTTP): the code-length code must be complete, the literal code complete (or exactly one code), the distance
//    code complete (or at most one code); zlib needs window <= 32K, Adler-32 and no extra bytes; gzip CRC-32 and ISIZE and
//    no extra bytes.
// Not for use outside tests.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace refslow {

struct Policy {
    size_t maxOut = size_t(1) << 31;
    bool lenient = false;      // false = strict
    bool stopAtMax = false;
};

namespace detail {

struct Reader {
    const uint8_t* p; size_t n; size_t bit = 0; bool eof = false;
    int Bit() {
        if (bit >= n * 8) { eof = true; return 0; }
        int b = (p[bit / 8] >> (bit % 8)) & 1;
        ++bit;
        return b;
    }
    uint32_t Bits(int count) {   // value's first bit read is its least significant bit (RFC 1951 3.1.1)
        uint32_t v = 0;
        for (int i = 0; i < count; ++i) v |= uint32_t(Bit()) << i;
        return v;
    }
    void ToByte() { bit = (bit + 7) / 8 * 8; }
};

struct Code {
    std::unordered_map<uint32_t, int> sym;   // key: (length << 16) | code value
    int used = 0;
    bool over = false, complete = false;
};

// RFC 1951 3.2.2, the long way: count lengths, find the first code of each length, number the symbols in order.
inline Code MakeCode(const std::vector<int>& len) {
    Code c;
    int count[16] = {0};
    for (int l : len) ++count[l];
    count[0] = 0;
    c.used = 0;
    for (int l = 1; l <= 15; ++l) c.used += count[l];
    // Kraft: sum of 2^-len must be <= 1 (over-subscribed otherwise) and == 1 (complete)
    long double kraft = 0, unit = 0.5L;
    for (int l = 1; l <= 15; ++l, unit /= 2) kraft += count[l] * unit;
    c.over = kraft > 1.0L;
    c.complete = kraft == 1.0L;
    if (c.over) return c;
    uint32_t first[16] = {0};
    uint32_t code = 0;
    for (int l = 1; l <= 15; ++l) { code = (code + count[l - 1]) << 1; first[l] = code; }
    uint32_t next[16];
    for (int l = 0; l < 16; ++l) next[l] = first[l];
    for (size_t s = 0; s < len.size(); ++s)
        if (len[s]) c.sym[(uint32_t(len[s]) << 16) | next[len[s]]++] = int(s);
    return c;
}

// -1 = end of input, -2 = no such code
inline int Decode(Reader& r, const Code& c) {
    uint32_t v = 0;
    for (int l = 1; l <= 15; ++l) {
        v = (v << 1) | uint32_t(r.Bit());
        if (r.eof) return -1;
        auto it = c.sym.find((uint32_t(l) << 16) | v);
        if (it != c.sym.end()) return it->second;
    }
    return -2;
}

// RFC 1951 3.2.5, typed in as data
static const int kLenBase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
static const int kLenBits[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
static const int kDistBase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
static const int kDistBits[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
// RFC 1951 3.2.7
static const int kClOrder[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};

inline uint32_t Crc32(const std::vector<uint8_t>& d) {   // RFC 1952 8.1.1, one bit at a time
    uint32_t c = 0xffffffffu;
    for (uint8_t b : d) { c ^= b; for (int k = 0; k < 8; ++k) c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : (c >> 1); }
    return ~c;
}
inline uint32_t Adler32(const std::vector<uint8_t>& d) {   // RFC 1950 8.2, the definition
    uint32_t a = 1, b = 0;
    for (uint8_t x : d) { a = (a + x) % 65521; b = (b + a) % 65521; }
    return (b << 16) | a;
}

// Inflate a raw stream. Returns false on any problem. `bytesUsed` = whole input bytes touched.
inline bool Raw(const uint8_t* in, size_t n, const Policy& pol, std::vector<uint8_t>& out, size_t& bytesUsed) {
    out.clear();
    Reader r{in, n};
    // fixed codes, RFC 1951 3.2.6
    static const Code fixedLit = [] { std::vector<int> fl(288); for (int i = 0; i < 288; ++i) fl[i] = i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8; return MakeCode(fl); }();
    static const Code fixedDist = MakeCode(std::vector<int>(32, 5));   // built once (the reference is only used single-threaded)
    bool final = false;
    while (!final) {
        if (pol.stopAtMax && out.size() == pol.maxOut) { bytesUsed = (r.bit + 7) / 8; return true; }
        final = r.Bit() != 0;
        uint32_t type = r.Bits(2);
        if (r.eof) return false;
        if (type == 3) return false;
        if (type == 0) {
            r.ToByte();
            uint32_t len = r.Bits(16), nlen = r.Bits(16);
            if (r.eof || (len ^ 0xffffu) != nlen) return false;
            if (len > pol.maxOut - out.size()) return false;
            for (uint32_t i = 0; i < len; ++i) { uint32_t b = r.Bits(8); if (r.eof) return false; out.push_back(uint8_t(b)); }
            continue;
        }
        Code litStore, distStore;
        const Code* lit = &fixedLit; const Code* dist = &fixedDist;
        if (type == 2) {
            int hlit = int(r.Bits(5)) + 257, hdist = int(r.Bits(5)) + 1, hclen = int(r.Bits(4)) + 4;
            if (r.eof || hlit > 286 || hdist > 30) return false;
            std::vector<int> cl(19, 0);
            for (int i = 0; i < hclen; ++i) cl[kClOrder[i]] = int(r.Bits(3));
            if (r.eof) return false;
            Code clc = MakeCode(cl);
            if (clc.over || (!pol.lenient && !clc.complete)) return false;
            std::vector<int> lens;
            while (int(lens.size()) < hlit + hdist) {
                int s = Decode(r, clc);
                if (s < 0) return false;
                if (s < 16) { lens.push_back(s); continue; }
                int rep, val = 0;
                if (s == 16) { if (lens.empty()) return false; val = lens.back(); rep = 3 + int(r.Bits(2)); }
                else if (s == 17) rep = 3 + int(r.Bits(3));
                else rep = 11 + int(r.Bits(7));
                if (r.eof || int(lens.size()) + rep > hlit + hdist) return false;
                for (int i = 0; i < rep; ++i) lens.push_back(val);
            }
            if (lens[256] == 0) return false;   // the block could never end
            litStore = MakeCode(std::vector<int>(lens.begin(), lens.begin() + hlit));
            distStore = MakeCode(std::vector<int>(lens.begin() + hlit, lens.end()));
            if (litStore.over || distStore.over) return false;
            if (!pol.lenient) {
                if (!litStore.complete && litStore.used != 1) return false;
                if (!distStore.complete && distStore.used > 1) return false;
            }
            lit = &litStore; dist = &distStore;
        }
        for (;;) {
            int s = Decode(r, *lit);
            if (s < 0) return false;
            if (s < 256) {
                if (out.size() >= pol.maxOut) { if (pol.stopAtMax) { bytesUsed = (r.bit + 7) / 8; return true; } return false; }
                out.push_back(uint8_t(s));
                if (pol.stopAtMax && out.size() == pol.maxOut) { bytesUsed = (r.bit + 7) / 8; return true; }
                continue;
            }
            if (s == 256) break;
            if (s > 285) return false;
            int length = kLenBase[s - 257] + int(r.Bits(kLenBits[s - 257]));
            int ds = Decode(r, *dist);
            if (ds < 0 || ds > 29) return false;
            size_t back = size_t(kDistBase[ds]) + r.Bits(kDistBits[ds]);
            if (r.eof) return false;
            if (back > out.size()) return false;
            size_t room = pol.maxOut - out.size();
            size_t take = size_t(length);
            if (take > room) { if (!pol.stopAtMax) return false; take = room; }
            for (size_t i = 0; i < take; ++i) out.push_back(out[out.size() - back]);
            if (pol.stopAtMax && out.size() == pol.maxOut) { bytesUsed = (r.bit + 7) / 8; return true; }
        }
    }
    bytesUsed = (r.bit + 7) / 8;
    return true;
}

inline bool Zlib(const uint8_t* in, size_t n, const Policy& pol, std::vector<uint8_t>& out) {
    out.clear();
    uint32_t cmf = n > 0 ? in[0] : 0, flg = n > 1 ? in[1] : 0;
    if (pol.lenient ? n < 2 : (n < 6 || (cmf >> 4) > 7)) return false;
    if ((cmf & 15) != 8 || (cmf * 256 + flg) % 31 != 0 || (flg & 32)) return false;
    size_t used = 0;
    if (!Raw(in + 2, n - 2, pol, out, used)) { out.clear(); return false; }
    if (pol.lenient) return true;
    size_t at = 2 + used;
    if (n - at != 4) { out.clear(); return false; }
    uint32_t want = (uint32_t(in[at]) << 24) | (uint32_t(in[at + 1]) << 16) | (uint32_t(in[at + 2]) << 8) | in[at + 3];
    if (want != Adler32(out)) { out.clear(); return false; }
    return true;
}

// gzip is always strict (RFC 1952 2.3: header, optional fields, deflate data, CRC-32, ISIZE, no second member)
inline bool Gzip(const uint8_t* in, size_t n, const Policy& pol, std::vector<uint8_t>& out) {
    out.clear();
    if (n < 18 || in[0] != 0x1f || in[1] != 0x8b || in[2] != 8) return false;
    uint32_t flg = in[3];
    if (flg & 0xe0) return false;
    size_t at = 10;
    if (flg & 4) {
        if (n - at < 2) return false;
        at += 2 + (size_t(in[at]) | (size_t(in[at + 1]) << 8));
    }
    for (uint32_t bit : {8u, 16u}) {
        if (!(flg & bit)) continue;
        while (at < n && in[at] != 0) ++at;
        if (at >= n) return false;
        ++at;
    }
    if (flg & 2) at += 2;
    if (at > n || n - at < 8) return false;
    size_t used = 0;
    if (!Raw(in + at, n - at, pol, out, used)) { out.clear(); return false; }
    at += used;
    if (n - at != 8) { out.clear(); return false; }
    uint32_t crc = in[at] | (uint32_t(in[at + 1]) << 8) | (uint32_t(in[at + 2]) << 16) | (uint32_t(in[at + 3]) << 24);
    uint32_t isz = in[at + 4] | (uint32_t(in[at + 5]) << 8) | (uint32_t(in[at + 6]) << 16) | (uint32_t(in[at + 7]) << 24);
    if (crc != Crc32(out) || isz != uint32_t(out.size() & 0xffffffffu)) { out.clear(); return false; }
    return true;
}

} // namespace detail

using detail::Gzip;
using detail::Raw;
using detail::Zlib;

} // namespace refslow
