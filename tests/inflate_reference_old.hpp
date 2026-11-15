// REFERENCE COPIES of the three DEFLATE decoders as they were BEFORE they were merged into okay/Core/Inflate.hpp.
// Kept only for tests/test_inflate_differential.cpp (the old and the new decoder must agree). Do not use in the engine.
//   ref1 = okay::Inflate / InflateCore from engine/src/Core/ImageDecode.cpp
//   ref2 = okay::fbx::Inflate from engine/src/Render/FbxReader.cpp
//   ref3 = okay::ownhttp::InflateBody from engine/include/okay/Platform/OwnHttpInflate.hpp
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace ref1 {
constexpr int kInfFast = 10;

struct InfHuff {
    std::uint16_t fast[1 << kInfFast];   // (length << 9) | symbol; 0 = longer than kInfFast or unused
    std::uint16_t count[16];
    std::uint16_t symbol[288];
};

bool BuildInfHuff(InfHuff& h, const std::uint8_t* lens, int n) {
    std::memset(h.count, 0, sizeof(h.count));
    std::memset(h.fast, 0, sizeof(h.fast));
    for (int i = 0; i < n; ++i) ++h.count[lens[i]];
    h.count[0] = 0;
    int left = 1;
    for (int l = 1; l < 16; ++l) {
        left <<= 1;
        left -= h.count[l];
        if (left < 0) return false;   // over-subscribed
    }
    std::uint16_t offs[16];
    offs[1] = 0;
    for (int l = 1; l < 15; ++l) offs[l + 1] = static_cast<std::uint16_t>(offs[l] + h.count[l]);
    for (int i = 0; i < n; ++i) if (lens[i]) h.symbol[offs[lens[i]]++] = static_cast<std::uint16_t>(i);
    // Fast table: canonical codes, stored bit-reversed because DEFLATE sends codes MSB first
    // inside an LSB-first bit stream.
    int code = 0, idx = 0;
    for (int l = 1; l <= 15; ++l) {
        for (int k = 0; k < h.count[l]; ++k, ++code, ++idx) {
            if (l > kInfFast) continue;
            int rev = 0;
            for (int b = 0; b < l; ++b) if (code & (1 << b)) rev |= 1 << (l - 1 - b);
            for (int fill = rev; fill < (1 << kInfFast); fill += 1 << l)
                h.fast[fill] = static_cast<std::uint16_t>((l << 9) | h.symbol[idx]);
        }
        code <<= 1;
    }
    return true;
}

struct InfBits {
    const std::uint8_t* p;
    const std::uint8_t* end;
    std::uint64_t acc = 0;
    int n = 0;      // valid bits in acc
    int fake = 0;   // zero bits invented past the end of the input
    void Refill() {
        while (n <= 56) {
            if (p < end) acc |= static_cast<std::uint64_t>(*p++) << n;
            else fake += 8;
            n += 8;
        }
    }
    bool Overrun() const { return n < fake; }
    std::uint32_t Get(int k) {   // k <= 32, requires n >= k
        const std::uint32_t v = static_cast<std::uint32_t>(acc & ((std::uint64_t(1) << k) - 1));
        acc >>= k; n -= k;
        return v;
    }
    int Decode(const InfHuff& h) {   // requires n >= 15
        const int e = h.fast[acc & ((1 << kInfFast) - 1)];
        if (e) { const int l = e >> 9; acc >>= l; n -= l; return e & 511; }
        int code = 0, first = 0, index = 0;
        for (int l = 1; l <= 15; ++l) {
            code |= static_cast<int>((acc >> (l - 1)) & 1);
            const int c = h.count[l];
            if (code - c < first) { acc >>= l; n -= l; return h.symbol[index + (code - first)]; }
            index += c; first += c; first <<= 1; code <<= 1;
        }
        return -1;
    }
};

const std::uint16_t kLenBase[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const std::uint8_t kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const std::uint16_t kDistBase[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const std::uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

// stopAtMax: reaching exactly `maxOut` bytes counts as success even if the stream continues
// (PNG: only the expected pixel bytes matter). Otherwise exceeding maxOut fails.
bool InflateCore(const std::uint8_t* data, std::size_t size, bool zlibWrapper, std::vector<std::uint8_t>& out,
                 std::size_t maxOut, bool stopAtMax, std::size_t hint) {
    out.clear();
    if (zlibWrapper) {
        if (size < 2) return false;
        const unsigned cmf = data[0], flg = data[1];
        if ((cmf & 15) != 8 || ((cmf << 8) | flg) % 31 != 0 || (flg & 32)) return false;
        data += 2; size -= 2;
    }
    InfBits br;
    br.p = data; br.end = data + size;
    out.resize(std::min(maxOut, std::max<std::size_t>(hint, 1024)));
    std::size_t o = 0;
    auto room = [&](std::size_t need) -> bool {   // make space for `need` more bytes
        if (o + need <= out.size()) return true;
        if (o + need > maxOut) return false;
        out.resize(std::min(std::max(out.size() * 2, o + need), maxOut));
        return true;
    };
    InfHuff fixedLit, fixedDist, dynLit, dynDist;
    bool fixedReady = false;
    for (bool last = false; !last;) {
        if (stopAtMax && o == maxOut) break;
        br.Refill();
        last = br.Get(1) != 0;
        const std::uint32_t type = br.Get(2);
        if (type == 0) {
            br.Get(br.n & 7);   // to a byte boundary
            br.Refill();
            const std::uint32_t len = br.Get(16), nlen = br.Get(16);
            if ((len ^ 0xFFFFu) != nlen) return false;
            if (br.Overrun()) return false;
            if (!room(len)) return false;
            std::uint32_t left = len;
            while (left > 0 && br.n >= 8) { out[o++] = static_cast<std::uint8_t>(br.Get(8)); --left; }
            if (br.Overrun()) return false;
            if (left > 0) {   // accumulator is empty: copy straight from the input
                if (static_cast<std::size_t>(br.end - br.p) < left) return false;
                std::memcpy(out.data() + o, br.p, left);
                br.p += left; o += left;
            }
            continue;
        }
        if (type == 3) return false;
        const InfHuff* lit;
        const InfHuff* dist;
        if (type == 1) {
            if (!fixedReady) {
                std::uint8_t l[288];
                for (int i = 0; i < 144; ++i) l[i] = 8;
                for (int i = 144; i < 256; ++i) l[i] = 9;
                for (int i = 256; i < 280; ++i) l[i] = 7;
                for (int i = 280; i < 288; ++i) l[i] = 8;
                BuildInfHuff(fixedLit, l, 288);
                std::uint8_t d[30];
                for (int i = 0; i < 30; ++i) d[i] = 5;
                BuildInfHuff(fixedDist, d, 30);
                fixedReady = true;
            }
            lit = &fixedLit; dist = &fixedDist;
        } else {
            const int hlit = static_cast<int>(br.Get(5)) + 257;
            const int hdist = static_cast<int>(br.Get(5)) + 1;
            const int hclen = static_cast<int>(br.Get(4)) + 4;
            if (hlit > 286 || hdist > 30) return false;
            static const std::uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
            std::uint8_t cl[19] = {0};
            for (int i = 0; i < hclen; ++i) { br.Refill(); cl[order[i]] = static_cast<std::uint8_t>(br.Get(3)); }
            InfHuff clh;
            if (!BuildInfHuff(clh, cl, 19)) return false;
            std::uint8_t lens[286 + 30];
            int i = 0;
            while (i < hlit + hdist) {
                br.Refill();
                const int sym = br.Decode(clh);
                if (sym < 0 || br.Overrun()) return false;
                if (sym < 16) { lens[i++] = static_cast<std::uint8_t>(sym); continue; }
                int rep, val = 0;
                if (sym == 16) {
                    if (i == 0) return false;
                    val = lens[i - 1];
                    rep = 3 + static_cast<int>(br.Get(2));
                } else if (sym == 17) rep = 3 + static_cast<int>(br.Get(3));
                else rep = 11 + static_cast<int>(br.Get(7));
                if (i + rep > hlit + hdist) return false;
                while (rep--) lens[i++] = static_cast<std::uint8_t>(val);
            }
            if (lens[256] == 0) return false;   // no end-of-block code
            if (!BuildInfHuff(dynLit, lens, hlit)) return false;
            if (!BuildInfHuff(dynDist, lens + hlit, hdist)) return false;
            lit = &dynLit; dist = &dynDist;
        }
        for (;;) {
            br.Refill();
            int sym = br.Decode(*lit);
            if (sym < 0) return false;
            if (sym < 256) {
                if (!room(1)) { if (stopAtMax) return o == maxOut; return false; }
                out[o++] = static_cast<std::uint8_t>(sym);
            } else if (sym == 256) {
                break;
            } else {
                sym -= 257;
                if (sym >= 29) return false;
                const std::size_t len = kLenBase[sym] + br.Get(kLenExtra[sym]);
                const int ds = br.Decode(*dist);
                if (ds < 0 || ds >= 30) return false;
                const std::size_t d = kDistBase[ds] + br.Get(kDistExtra[ds]);
                if (d > o) return false;
                std::size_t n = len;
                if (stopAtMax && o + n > maxOut) n = maxOut - o;
                if (!room(n)) return false;
                std::uint8_t* dst = out.data() + o;
                const std::uint8_t* src = dst - d;
                for (std::size_t k = 0; k < n; ++k) dst[k] = src[k];
                o += n;
                if (stopAtMax && o == maxOut) break;
            }
            if (br.Overrun()) return false;
        }
        if (br.Overrun()) return false;
    }
    out.resize(o);
    return true;
}

bool Inflate(const std::uint8_t* data, std::size_t size, bool zlibWrapper, std::vector<std::uint8_t>& out, std::size_t maxOut) {
    if (!InflateCore(data, size, zlibWrapper, out, maxOut, false, size * 4)) { out.clear(); return false; }
    return true;
}
}  // namespace ref1

namespace ref2 {
struct BitIn {
    const std::uint8_t* p; std::size_t n, pos = 0; std::uint32_t buf = 0; int cnt = 0; bool bad = false;
    BitIn(const std::uint8_t* d, std::size_t len) : p(d), n(len) {}
    std::uint32_t Bits(int need) {
        while (cnt < need) {
            if (pos >= n) { bad = true; return 0; }
            buf |= (std::uint32_t)p[pos++] << cnt; cnt += 8;
        }
        const std::uint32_t v = need ? (buf & ((1u << need) - 1u)) : 0u;
        buf >>= need; cnt -= need;
        return v;
    }
};

struct Huff {
    std::uint16_t count[16];
    std::uint16_t sym[320];
    bool Build(const std::uint8_t* len, int n) {
        std::memset(count, 0, sizeof(count));
        for (int i = 0; i < n; ++i) ++count[len[i]];
        if (count[0] == n) return true;      // no codes: legal for an unused distance tree
        int left = 1;
        for (int l = 1; l < 16; ++l) { left <<= 1; left -= count[l]; if (left < 0) return false; }   // over-subscribed
        std::uint16_t offs[16]; offs[1] = 0;
        for (int l = 1; l < 15; ++l) offs[l + 1] = (std::uint16_t)(offs[l] + count[l]);
        for (int i = 0; i < n; ++i) if (len[i]) sym[offs[len[i]]++] = (std::uint16_t)i;
        return true;
    }
    int Decode(BitIn& in) const {
        int code = 0, first = 0, index = 0;
        for (int l = 1; l < 16; ++l) {
            code |= (int)in.Bits(1);
            if (in.bad) return -1;
            const int c = count[l];
            if (code - c < first) return sym[index + (code - first)];
            index += c; first += c; first <<= 1; code <<= 1;
        }
        return -1;
    }
};

const std::uint16_t kLenBase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
const std::uint8_t  kLenExtra[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
const std::uint16_t kDistBase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
const std::uint8_t  kDistExtra[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

bool InflateBlock(BitIn& in, const Huff& lit, const Huff& dist, std::vector<std::uint8_t>& out, std::size_t maxOut) {
    for (;;) {
        const int s = lit.Decode(in);
        if (s < 0) return false;
        if (s < 256) {
            if (out.size() >= maxOut) return false;
            out.push_back((std::uint8_t)s);
        } else if (s == 256) {
            return true;
        } else {
            const int li = s - 257;
            if (li >= 29) return false;
            const std::size_t len = kLenBase[li] + in.Bits(kLenExtra[li]);
            const int ds = dist.Decode(in);
            if (ds < 0 || ds >= 30) return false;
            const std::size_t d = kDistBase[ds] + in.Bits(kDistExtra[ds]);
            if (in.bad || d > out.size() || out.size() + len > maxOut) return false;
            const std::size_t from = out.size() - d;
            for (std::size_t k = 0; k < len; ++k) out.push_back(out[from + k]);   // may overlap: byte by byte
        }
    }
}

bool Inflate(const std::uint8_t* data, std::size_t n, std::vector<std::uint8_t>& out, std::size_t maxOut) {
    out.clear();
    if (n < 2) return false;
    if ((data[0] & 0x0F) != 8 || ((data[0] << 8) | data[1]) % 31 != 0 || (data[1] & 0x20)) return false;   // deflate, no preset dictionary
    BitIn in(data + 2, n - 2);
    for (;;) {
        const std::uint32_t last = in.Bits(1), type = in.Bits(2);
        if (in.bad) return false;
        if (type == 0) {
            in.buf = 0; in.cnt = 0;                       // drop the partial byte
            if (in.pos + 4 > in.n) return false;
            const std::size_t len = in.p[in.pos] | (in.p[in.pos + 1] << 8);
            const std::size_t nlen = in.p[in.pos + 2] | (in.p[in.pos + 3] << 8);
            in.pos += 4;
            if ((len ^ 0xFFFF) != nlen || in.pos + len > in.n || out.size() + len > maxOut) return false;
            out.insert(out.end(), in.p + in.pos, in.p + in.pos + len);
            in.pos += len;
        } else if (type == 1) {
            Huff lit, dist;
            std::uint8_t l[288];
            for (int i = 0; i < 144; ++i) l[i] = 8;
            for (int i = 144; i < 256; ++i) l[i] = 9;
            for (int i = 256; i < 280; ++i) l[i] = 7;
            for (int i = 280; i < 288; ++i) l[i] = 8;
            lit.Build(l, 288);
            std::uint8_t dl[30]; for (int i = 0; i < 30; ++i) dl[i] = 5;
            dist.Build(dl, 30);
            if (!InflateBlock(in, lit, dist, out, maxOut)) return false;
        } else if (type == 2) {
            const int nlen = (int)in.Bits(5) + 257, ndist = (int)in.Bits(5) + 1, ncode = (int)in.Bits(4) + 4;
            if (in.bad || nlen > 286 || ndist > 30) return false;
            static const std::uint8_t order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            std::uint8_t cl[19] = {0};
            for (int i = 0; i < ncode; ++i) cl[order[i]] = (std::uint8_t)in.Bits(3);
            Huff ch;
            if (in.bad || !ch.Build(cl, 19)) return false;
            std::uint8_t lens[320] = {0};
            int idx = 0;
            while (idx < nlen + ndist) {
                const int s = ch.Decode(in);
                if (s < 0) return false;
                if (s < 16) { lens[idx++] = (std::uint8_t)s; continue; }
                int rep, val = 0;
                if (s == 16) { if (idx == 0) return false; val = lens[idx - 1]; rep = 3 + (int)in.Bits(2); }
                else if (s == 17) rep = 3 + (int)in.Bits(3);
                else rep = 11 + (int)in.Bits(7);
                if (in.bad || idx + rep > nlen + ndist) return false;
                while (rep--) lens[idx++] = (std::uint8_t)val;
            }
            if (lens[256] == 0) return false;               // no end-of-block code
            Huff lit, dist;
            if (!lit.Build(lens, nlen) || !dist.Build(lens + nlen, ndist)) return false;
            if (!InflateBlock(in, lit, dist, out, maxOut)) return false;
        } else {
            return false;
        }
        if (last) return true;
    }
}

}  // namespace ref2


// ---------------------------------------------------------------------------
// okay::ownhttp inflate: gzip and zlib bodies for the HTTP client.
//
// Provenance: written from RFC 1950 (zlib), RFC 1951 (deflate) and RFC 1952 (gzip) ONLY. No decoder source
// (zlib, puff, miniz, stb_image, libdeflate or any other) was read, fetched, copied or ported for this file.
// An earlier version of this file was flagged by an outside review as closely following a public reference
// decoder; this is a rewrite with a different design (see docs/provenance_audit.md).
//
// Design: a table-driven canonical Huffman decoder.
//  - Codes are assigned as RFC 1951 section 3.2.2 describes (next_code per length, symbols in order).
//  - Deflate packs Huffman codes most-significant-bit first into a least-significant-bit-first stream, so
//    every code is bit-reversed once when the table is built. A 10-bit first-level table is then indexed
//    directly by the next 10 bits of the stream and gives (symbol, length) in one load.
//  - Codes longer than 10 bits (up to 15) take a slow path: the 15 next bits are reversed once and compared
//    against the per-length range [first code, first code + count) of that length.
//  - The bit reader keeps a 64-bit buffer that is refilled bytewise; unused whole bytes are given back at
//    the end of the stream so the trailer position is exact.
//  - Stored, fixed-table and dynamic-table blocks are three separate paths. The length and distance base
//    and extra-bit tables are derived from the rule in RFC 1951 section 3.2.5 at compile time, and a test
//    checks them against the printed table.
// This is compression, not cryptography. Hostile input is the normal case:
//  - the output is capped (`maxOut`), memory grows only up to that cap, so a zip bomb stops at the cap;
//  - every table is validated (over-subscribed or incomplete codes, bad lengths, back-references that
//    start before the output) and every read is bounds checked;
//  - the gzip CRC-32 and size, or the zlib Adler-32, must match; extra bytes after the stream are refused
//    (no multi-member gzip).
// Memory: the working buffer never grows past `maxOut`, but a successful decode copies it into the caller's string, and the
// caller holds the encoded body too (see Limits::maxEncodedBytes). So the peak is about three times the cap, not one.
// ---------------------------------------------------------------------------
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

namespace ref3 {

namespace inflate_detail {

// ---- RFC 1951 section 3.2.5: length and distance symbols, derived from the rule ------------------
// Length symbols 257..285 (index 0..28): 8 symbols with no extra bits, then four symbols per extra-bit
// count 1..5, then symbol 285 is the single length 258. Distance symbols 0..29: four with no extra bits,
// then two per extra-bit count 1..13. Each base is the previous base plus 2^(previous extra bits).
struct SymbolTables {
    uint16_t lenBase[29];
    uint8_t lenExtra[29];
    uint16_t distBase[30];
    uint8_t distExtra[30];
    constexpr SymbolTables() : lenBase{}, lenExtra{}, distBase{}, distExtra{} {
        for (int i = 0; i < 29; ++i) lenExtra[i] = uint8_t(i < 8 || i == 28 ? 0 : i / 4 - 1);
        lenBase[0] = 3;
        for (int i = 1; i < 29; ++i) lenBase[i] = uint16_t(lenBase[i - 1] + (1 << lenExtra[i - 1]));
        lenBase[28] = 258;   // the one exception: 285 means exactly 258, not 227 + 32
        for (int j = 0; j < 30; ++j) distExtra[j] = uint8_t(j < 4 ? 0 : j / 2 - 1);
        distBase[0] = 1;
        for (int j = 1; j < 30; ++j) distBase[j] = uint16_t(distBase[j - 1] + (1 << distExtra[j - 1]));
    }
};
inline constexpr SymbolTables kSym{};

// Order in which the code-length code's own lengths are stored (RFC 1951 section 3.2.7).
inline constexpr uint8_t kClOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

// ---- bit reader -----------------------------------------------------------------------------------
struct BitReader {
    const uint8_t* src; size_t len; size_t pos = 0;
    uint64_t buf = 0; int cnt = 0;     // bits in `buf`, least significant first; bits above `cnt` are zero
    bool starved = false;              // asked for more bits than the input has

    BitReader(const uint8_t* d, size_t n) : src(d), len(n) {}
    void Refill() {
        while (cnt <= 56 && pos < len) { buf |= uint64_t(src[pos++]) << cnt; cnt += 8; }
    }
    uint32_t Take(int n) {             // n <= 16
        if (cnt < n) {
            Refill();
            if (cnt < n) { starved = true; return 0; }
        }
        uint32_t v = uint32_t(buf & ((uint64_t(1) << n) - 1));
        buf >>= n; cnt -= n;
        return v;
    }
    void Drop(int n) { buf >>= n; cnt -= n; }
    // Throw away the rest of the current byte, and hand whole unread bytes back to the input.
    void AlignToByte() { Drop(cnt & 7); pos -= size_t(cnt >> 3); buf = 0; cnt = 0; }
    // Number of input bytes used once the stream is over (a partly used byte counts as used).
    size_t BytesUsed() const { return pos - size_t(cnt >> 3); }
};

inline uint32_t Reverse(uint32_t v, int bits) {
    uint32_t r = 0;
    for (int i = 0; i < bits; ++i) { r = (r << 1) | (v & 1); v >>= 1; }
    return r;
}

// ---- canonical Huffman decoder ---------------------------------------------------------------------
constexpr int kFastBits = 10;
constexpr int kMaxBits = 15;

struct Decoder {
    // quick[i]: (symbol << 4 | length) for the code whose reversed bits are the low kFastBits of i, 0 if none
    uint16_t quick[1 << kFastBits];
    uint16_t sorted[288];                 // symbols ordered by (length, symbol)
    uint16_t lowestCode[kMaxBits + 1];     // first canonical code of each length
    uint16_t slotBase[kMaxBits + 1];     // index in `sorted` of the first symbol of each length
    uint16_t countOf[kMaxBits + 1];
    int used = 0;                         // symbols that have a code

    enum class Shape { Oversubscribed, Incomplete, Complete };

    Shape Build(const uint8_t* lens, int n) {
        std::memset(quick, 0, sizeof quick);
        for (int l = 0; l <= kMaxBits; ++l) countOf[l] = 0;
        for (int i = 0; i < n; ++i) ++countOf[lens[i]];
        used = n - countOf[0];
        // Kraft sum in units of 2^-15: a prefix code is complete exactly when it reaches 2^15.
        uint32_t kraft = 0;
        for (int l = 1; l <= kMaxBits; ++l) kraft += uint32_t(countOf[l]) << (kMaxBits - l);
        if (kraft > (1u << kMaxBits)) return Shape::Oversubscribed;
        // RFC 1951 3.2.2: next_code[bits] = (next_code[bits-1] + bl_count[bits-1]) << 1
        uint32_t next[kMaxBits + 2]; uint32_t code = 0; uint16_t slot = 0;
        lowestCode[0] = 0; slotBase[0] = 0;
        for (int l = 1; l <= kMaxBits; ++l) {
            code = (code + (l > 1 ? countOf[l - 1] : 0)) << 1;
            next[l] = code; lowestCode[l] = uint16_t(code);
            slotBase[l] = slot; slot = uint16_t(slot + countOf[l]);
        }
        uint16_t fill[kMaxBits + 1];
        for (int l = 0; l <= kMaxBits; ++l) fill[l] = 0;
        for (int sym = 0; sym < n; ++sym) {
            int l = lens[sym];
            if (!l) continue;
            sorted[slotBase[l] + fill[l]++] = uint16_t(sym);
            uint32_t c = next[l]++;
            if (l <= kFastBits) {
                uint32_t rev = Reverse(c, l);
                for (uint32_t i = rev; i < (1u << kFastBits); i += (1u << l)) quick[i] = uint16_t(sym << 4 | l);
            }
        }
        return kraft == (1u << kMaxBits) ? Shape::Complete : Shape::Incomplete;
    }

    enum : int { kBadCode = -1, kStarved = -2 };

    // Returns the symbol, kBadCode (no such code) or kStarved (the input ends inside the code).
    int Next(BitReader& br) const {
        if (br.cnt < kMaxBits) br.Refill();
        uint16_t e = quick[br.buf & ((1u << kFastBits) - 1)];
        if (e) {
            int l = e & 15;
            if (l > br.cnt) return kStarved;
            br.Drop(l);
            return e >> 4;
        }
        // slow path: compare the next 15 bits, as a most-significant-first number, with each length's range
        uint32_t v = uint32_t(br.buf & ((1u << kMaxBits) - 1));
        v = Reverse(v, kMaxBits);
        for (int l = kFastBits + 1; l <= kMaxBits; ++l) {
            uint32_t prefix = v >> (kMaxBits - l);
            uint32_t k = prefix - lowestCode[l];
            if (prefix >= lowestCode[l] && k < countOf[l]) {
                if (l > br.cnt) return kStarved;
                br.Drop(l);
                return sorted[slotBase[l] + k];
            }
        }
        // no code matches; if the stream ended early the zero padding may be the reason
        return br.cnt < kMaxBits && br.pos >= br.len ? kStarved : kBadCode;
    }
};

struct FixedCodes {
    Decoder lit, dist;
    FixedCodes() {
        uint8_t l[288];
        for (int i = 0; i < 288; ++i) l[i] = uint8_t(i < 144 ? 8 : i < 256 ? 9 : i < 280 ? 7 : 8);
        lit.Build(l, 288);
        uint8_t d[32];
        for (int i = 0; i < 32; ++i) d[i] = 5;
        dist.Build(d, 32);
    }
};

// ---- output buffer: grows only up to the cap, never doubles past it --------------------------------
struct Output {
    std::unique_ptr<uint8_t[]> data; size_t size = 0, cap = 0, limit;
    explicit Output(size_t maxOut) : limit(maxOut) {}
    // Make room for `extra` more bytes (the caller has already checked size + extra <= limit).
    void Room(size_t extra) {
        size_t need = size + extra;
        if (need <= cap) return;
        size_t want = cap * 2 > need ? cap * 2 : need;
        if (want < 4096) want = 4096;
        if (want > limit) want = limit;
        if (want < need) want = need;
        std::unique_ptr<uint8_t[]> nb(new uint8_t[want]);
        if (size) std::memcpy(nb.get(), data.get(), size);
        data = std::move(nb); cap = want;
    }
};

// Decodes the symbols of one Huffman-coded block until its end-of-block symbol.
inline bool RunBlock(BitReader& br, const Decoder& lit, const Decoder& dist, Output& out, std::string& why) {
    static const char* const kTooBig = "decoded body larger than the limit";
    for (;;) {
        int sym = lit.Next(br);
        if (sym < 0) { why = sym == Decoder::kStarved ? "deflate data ends early" : "invalid deflate code"; return false; }
        if (sym < 256) {
            if (out.size >= out.limit) { why = kTooBig; return false; }
            out.Room(1);
            out.data[out.size++] = uint8_t(sym);
            continue;
        }
        if (sym == 256) return true;
        int li = sym - 257;
        if (li >= 29) { why = "invalid deflate length symbol"; return false; }
        size_t length = kSym.lenBase[li] + br.Take(kSym.lenExtra[li]);
        int ds = dist.Next(br);
        if (ds < 0) { why = ds == Decoder::kStarved ? "deflate data ends early" : "invalid deflate code"; return false; }
        if (ds >= 30) { why = "invalid deflate distance symbol"; return false; }
        size_t back = kSym.distBase[ds] + br.Take(kSym.distExtra[ds]);
        if (br.starved) { why = "deflate data ends early"; return false; }
        if (back > out.size) { why = "back-reference starts before the output"; return false; }
        if (length > out.limit - out.size) { why = kTooBig; return false; }
        out.Room(length);
        uint8_t* d = out.data.get() + out.size;
        const uint8_t* s = d - back;
        if (back >= length) std::memcpy(d, s, length);
        else for (size_t i = 0; i < length; ++i) d[i] = s[i];   // overlapping copy repeats the pattern
        out.size += length;
    }
}

// ---- the three block kinds --------------------------------------------------------------------------
inline bool StoredBlock(BitReader& br, Output& out, std::string& why) {
    br.AlignToByte();
    if (br.len - br.pos < 4) { why = "deflate data ends early"; return false; }
    const uint8_t* h = br.src + br.pos;
    uint32_t n = h[0] | (uint32_t(h[1]) << 8), inv = h[2] | (uint32_t(h[3]) << 8);
    br.pos += 4;
    if ((n ^ 0xffffu) != inv) { why = "stored block length check failed"; return false; }
    if (n > br.len - br.pos) { why = "deflate data ends early"; return false; }
    if (n > out.limit - out.size) { why = "decoded body larger than the limit"; return false; }
    out.Room(n);
    if (n) std::memcpy(out.data.get() + out.size, br.src + br.pos, n);
    out.size += n; br.pos += n;
    return true;
}

inline bool FixedBlock(BitReader& br, Output& out, std::string& why) {
    static const FixedCodes fixed;   // built once; thread-safe initialisation
    return RunBlock(br, fixed.lit, fixed.dist, out, why);
}

inline bool DynamicBlock(BitReader& br, Output& out, std::string& why) {
    int hlit = int(br.Take(5)) + 257, hdist = int(br.Take(5)) + 1, hclen = int(br.Take(4)) + 4;
    if (br.starved) { why = "deflate data ends early"; return false; }
    if (hlit > 286 || hdist > 30) { why = "deflate table sizes out of range"; return false; }
    uint8_t clLens[19] = {0};
    for (int i = 0; i < hclen; ++i) clLens[kClOrder[i]] = uint8_t(br.Take(3));
    if (br.starved) { why = "deflate data ends early"; return false; }
    Decoder cl;
    if (cl.Build(clLens, 19) != Decoder::Shape::Complete) { why = "deflate code-length code is not a complete prefix code"; return false; }

    const int total = hlit + hdist;
    uint8_t lens[286 + 30] = {0};
    int at = 0;
    while (at < total) {
        int sym = cl.Next(br);
        if (sym < 0) { why = sym == Decoder::kStarved ? "deflate data ends early" : "invalid deflate code"; return false; }
        if (sym < 16) { lens[at++] = uint8_t(sym); continue; }
        uint8_t value = 0; int run;
        if (sym == 16) {
            if (at == 0) { why = "deflate repeat with nothing to repeat"; return false; }
            value = lens[at - 1]; run = 3 + int(br.Take(2));
        } else if (sym == 17) run = 3 + int(br.Take(3));
        else run = 11 + int(br.Take(7));
        if (br.starved) { why = "deflate data ends early"; return false; }
        if (run > total - at) { why = "deflate code lengths overrun the table"; return false; }
        while (run--) lens[at++] = value;
    }
    if (lens[256] == 0) { why = "deflate block has no end-of-block code"; return false; }

    Decoder lit, dist;
    auto ls = lit.Build(lens, hlit);
    if (ls == Decoder::Shape::Oversubscribed) { why = "deflate literal code is over-subscribed"; return false; }
    if (ls == Decoder::Shape::Incomplete && lit.used != 1) { why = "deflate literal code is incomplete"; return false; }
    auto ds = dist.Build(lens + hlit, hdist);
    if (ds == Decoder::Shape::Oversubscribed) { why = "deflate distance code is over-subscribed"; return false; }
    if (ds == Decoder::Shape::Incomplete && dist.used > 1) { why = "deflate distance code is incomplete"; return false; }
    return RunBlock(br, lit, dist, out, why);
}

/// Raw deflate stream (RFC 1951). On success `out` holds the bytes and `consumed` the input bytes used.
inline bool Raw(const uint8_t* in, size_t n, std::string& out, size_t maxOut, size_t& consumed, std::string& why) {
    BitReader br(in, n);
    Output o(maxOut);
    bool ok = true, final;
    do {
        final = br.Take(1) != 0;
        uint32_t kind = br.Take(2);
        if (br.starved) { why = "deflate data ends early"; ok = false; break; }
        switch (kind) {
            case 0: ok = StoredBlock(br, o, why); break;
            case 1: ok = FixedBlock(br, o, why); break;
            case 2: ok = DynamicBlock(br, o, why); break;
            default: why = "reserved deflate block type"; ok = false; break;
        }
    } while (ok && !final);
    if (!ok) return false;
    consumed = br.BytesUsed();
    out.assign(reinterpret_cast<const char*>(o.data.get()), o.size);
    return true;
}

// ---- checksums ----------------------------------------------------------------------------------------
struct CrcTable {
    uint32_t t[256];
    constexpr CrcTable() : t{} {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
    }
};
inline constexpr CrcTable kCrc{};   // computed at compile time: no run-time initialisation, no data race

inline uint32_t Crc32(const std::string& s) {
    uint32_t c = 0xffffffffu;
    for (unsigned char ch : s) c = kCrc.t[(c ^ ch) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

inline uint32_t Adler32(const std::string& s) {
    constexpr uint32_t kMod = 65521;
    uint32_t a = 1, b = 0;
    size_t i = 0;
    while (i < s.size()) {
        size_t stop = i + 5552 < s.size() ? i + 5552 : s.size();   // largest run before 32-bit overflow
        for (; i < stop; ++i) { a += uint8_t(s[i]); b += a; }
        a %= kMod; b %= kMod;
    }
    return (b << 16) | a;
}

inline uint32_t Le32(const uint8_t* p) { return p[0] | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

} // namespace inflate_detail

/// Decode a gzip member (`gzip` true) or a zlib stream (`gzip` false). false with `why` set on any problem.
inline bool InflateBody(const std::string& in, bool gzip, size_t maxOut, std::string& out, std::string& why) {
    using namespace inflate_detail;
    out.clear();
    const uint8_t* p = reinterpret_cast<const uint8_t*>(in.data());
    const size_t n = in.size();
    size_t off = 0;
    if (gzip) {
        // RFC 1952 section 2.3: ID1 ID2 CM FLG MTIME(4) XFL OS, then optional fields, then data, CRC32, ISIZE
        if (n < 18 || p[0] != 0x1f || p[1] != 0x8b || p[2] != 8) { why = "not a gzip stream"; return false; }
        const unsigned flg = p[3];
        if (flg & 0xe0) { why = "reserved gzip flags"; return false; }
        off = 10;
        if (flg & 0x04) {   // FEXTRA
            if (n - off < 2) { why = "truncated gzip header"; return false; }
            off += 2 + (size_t(p[off]) | (size_t(p[off + 1]) << 8));
        }
        for (unsigned bit : {0x08u, 0x10u}) {   // FNAME, FCOMMENT: zero-terminated
            if (!(flg & bit)) continue;
            while (off < n && p[off] != 0) ++off;
            if (off >= n) { why = "truncated gzip header"; return false; }
            ++off;
        }
        if (flg & 0x02) off += 2;   // FHCRC (not checked)
        if (off > n || n - off < 8) { why = "truncated gzip header"; return false; }
    } else {
        // RFC 1950 section 2.2: CMF FLG; CM 8, window <= 32K, header check divisible by 31, no preset dictionary
        const uint32_t cmf = n > 0 ? p[0] : 0, flg = n > 1 ? p[1] : 0;
        if (n < 6 || (cmf & 0x0f) != 8 || (cmf >> 4) > 7 || ((cmf << 8) | flg) % 31 != 0 || (flg & 0x20)) { why = "not a zlib stream"; return false; }
        off = 2;
    }
    size_t used = 0;
    if (!Raw(p + off, n - off, out, maxOut, used, why)) { std::string().swap(out); return false; }
    off += used;
    bool good;
    if (gzip) {
        if (n - off != 8) { why = "trailing bytes after gzip data"; good = false; }
        else if (Le32(p + off) != Crc32(out) || Le32(p + off + 4) != uint32_t(out.size() & 0xffffffffu)) { why = "gzip checksum mismatch"; good = false; }
        else good = true;
    } else {
        if (n - off != 4) { why = "bad zlib trailer"; good = false; }
        else {
            uint32_t want = (uint32_t(p[off]) << 24) | (uint32_t(p[off + 1]) << 16) | (uint32_t(p[off + 2]) << 8) | p[off + 3];
            good = want == Adler32(out);
            if (!good) why = "zlib checksum mismatch";
        }
    }
    if (!good) std::string().swap(out);
    return good;
}

} // namespace ref3
