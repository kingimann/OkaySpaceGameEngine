// WebP decoder (part of Core/ImageDecode). Written from the specifications only:
//   - RFC 9649 "WebP Image Format", section 2 (RIFF container: simple lossy, simple lossless, extended format with
//     VP8X, ALPH, ANIM/ANMF) and section 3 (WebP lossless bitstream: transforms, colour cache, LZ77 references,
//     prefix codes, meta prefix codes);
//   - RFC 6386 "VP8 Data Format and Decoding Guide", sections 7 to 15 (boolean decoder, frame header, key-frame
//     modes, intra prediction, coefficient tokens, dequantisation, inverse WHT / DCT, loop filter). Only key frames:
//     a WebP image is always one key frame. The RFC's reference decoder (its section 20) was not used as a model:
//     the structure here is our own. Its constant tables are data, kept apart in ImageDecodeWebpTables.hpp.
// No code from libwebp, libvpx, ffmpeg or any other project was read, copied or ported.
//
// Output is straight (not premultiplied) RGBA8. Lossy pictures are converted from Y'CbCr with the BT.601 studio-range
// equations (RFC 9649 section 2.5 says Rec. 601 SHOULD be used), the chroma planes upsampled with bilinear weights
// for centred 4:2:0 samples. An animated file yields its first frame placed on a transparent canvas.
//
// Hostile input is the normal case: every read is bounds-checked, sizes are capped before anything is allocated
// (16384 a side, the prefix-code tables of one image to kMaxCodeBytes), nothing recurses deeper than two levels
// (an entropy image inside the main image), and a truncated stream fails instead of inventing pixels.
#include "ImageDecodeInternal.hpp"
#include "ImageDecodeWebpTables.hpp"
#include "okay/Core/ImageDecode.hpp"
#include "okay/Core/Inflate.hpp"   // the LSB-first bit reader and bit reversal, shared with DEFLATE

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace okay {
namespace imgdec {

namespace {

bool Fail(std::string& err, const char* msg) { if (err.empty()) err = msg; return false; }

std::uint32_t Le16(const std::uint8_t* p) { return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8); }
std::uint32_t Le24(const std::uint8_t* p) { return Le16(p) | (std::uint32_t(p[2]) << 16); }
std::uint32_t Le32(const std::uint8_t* p) { return Le24(p) | (std::uint32_t(p[3]) << 24); }
int DivRoundUp(int num, int den) { return (num + den - 1) / den; }

// ======================================================================================================
// Lossless (VP8L), RFC 9649 section 3
// ======================================================================================================

using LsbReader = okay::inflate::detail::BitReader;

// Bits of any width up to 32 (the shared reader takes at most 16 at a time).
std::uint32_t Bits(LsbReader& br, int n) {
    if (n <= 16) return br.Take(n);
    const std::uint32_t lo = br.Take(16);
    return lo | (br.Take(n - 16) << 16);
}

constexpr int kCodeLenMax = 15;
// All prefix-code tables of one image together (the entropy image can ask for 65536 groups of five codes).
constexpr std::size_t kMaxCodeBytes = std::size_t(96) << 20;

// One canonical prefix code (RFC 9649 3.7: built the way RFC 1951 3.2.2 assigns codes). A direct table indexed by
// the next `fastBits` stream bits answers short codes; longer ones walk the per-length code ranges.
struct PrefixCode {
    int single = -1;                 // the only symbol of a one-symbol code (read with zero bits)
    int fastBits = 0;
    std::vector<std::uint32_t> fast; // (symbol << 4) | length, 0 = no code of at most fastBits bits
    std::uint16_t count[kCodeLenMax + 1] = {};
    std::uint32_t firstCode[kCodeLenMax + 1] = {};
    std::uint32_t firstSlot[kCodeLenMax + 1] = {};
    std::vector<std::uint16_t> bySlot;   // symbols ordered by (length, symbol)

    std::size_t Bytes() const { return fast.size() * 4 + bySlot.size() * 2 + sizeof(*this); }

    // Two one-bit codes as the "simple code length code" gives them (the two symbols may even be equal).
    void BuildPair(int a, int b) {
        single = -1; fastBits = 1;
        if (a > b) std::swap(a, b);
        fast.assign(2, 0);
        fast[0] = (std::uint32_t(a) << 4) | 1u;
        fast[1] = (std::uint32_t(b) << 4) | 1u;
        bySlot.clear();
        std::memset(count, 0, sizeof(count));
    }

    // False when the lengths do not form a complete code (a lone symbol is allowed: it takes no bits).
    bool Build(const std::uint8_t* lens, int n) {
        std::memset(count, 0, sizeof(count));
        single = -1;
        int used = 0, lastSym = 0, longest = 0;
        for (int s = 0; s < n; ++s) {
            if (lens[s] > kCodeLenMax) return false;
            if (lens[s]) { ++count[lens[s]]; ++used; lastSym = s; longest = std::max<int>(longest, lens[s]); }
        }
        if (used == 0) { single = 0; return true; }   // an empty code reads as symbol 0 (RFC 9649 3.7.2.1.1, note)
        if (used == 1) { single = lastSym; return true; }
        std::uint32_t kraft = 0;
        for (int l = 1; l <= kCodeLenMax; ++l) kraft += std::uint32_t(count[l]) << (kCodeLenMax - l);
        if (kraft != (1u << kCodeLenMax)) return false;
        fastBits = std::min(longest, 9);
        fast.assign(std::size_t(1) << fastBits, 0);
        std::uint32_t code = 0, slot = 0;
        std::uint32_t next[kCodeLenMax + 1] = {};
        for (int l = 1; l <= kCodeLenMax; ++l) {
            code = (code + (l > 1 ? count[l - 1] : 0)) << 1;
            firstCode[l] = next[l] = code;
            firstSlot[l] = slot;
            slot += count[l];
        }
        bySlot.assign(std::size_t(used), 0);
        std::uint32_t fill[kCodeLenMax + 1] = {};
        for (int s = 0; s < n; ++s) {
            const int l = lens[s];
            if (!l) continue;
            bySlot[firstSlot[l] + fill[l]++] = std::uint16_t(s);
            const std::uint32_t c = next[l]++;
            if (l <= fastBits) {
                const std::uint32_t rev = okay::inflate::detail::Reverse(c, l);
                for (std::uint32_t i = rev; i < (1u << fastBits); i += (1u << l)) fast[i] = (std::uint32_t(s) << 4) | std::uint32_t(l);
            }
        }
        return true;
    }

    // The next symbol, or -1 when the stream ends inside a code or no code matches.
    int Read(LsbReader& br) const {
        if (single >= 0) return single;
        if (br.cnt < kCodeLenMax) br.Refill();
        const std::uint32_t e = fast[br.buf & ((1u << fastBits) - 1)];
        if (e) {
            const int l = int(e & 15);
            if (l > br.cnt) { br.starved = true; return -1; }
            br.Drop(l);
            return int(e >> 4);
        }
        // Longer than the table: read the code most-significant bit first and test each length's range.
        std::uint32_t c = 0;
        for (int l = 1; l <= kCodeLenMax && l <= br.cnt; ++l) {
            c = (c << 1) | std::uint32_t((br.buf >> (l - 1)) & 1u);
            if (l > fastBits && c - firstCode[l] < count[l]) {
                br.Drop(l);
                return bySlot[firstSlot[l] + (c - firstCode[l])];
            }
        }
        br.starved = true;
        return -1;
    }
};

// Order of the code-length code lengths (RFC 9649 3.7.2.1.2).
constexpr std::uint8_t kClOrder[19] = {17, 18, 0, 1, 2, 3, 4, 5, 16, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

bool ReadPrefixCode(LsbReader& br, int alphabet, PrefixCode& out, std::string& err) {
    if (br.Take(1)) {   // simple code length code
        const int num = int(br.Take(1)) + 1;
        const int first8 = int(br.Take(1));
        const int s0 = int(br.Take(first8 ? 8 : 1));
        if (s0 >= alphabet) return Fail(err, "WebP lossless: prefix symbol out of range");
        if (num == 1) { out.single = s0; out.fast.clear(); out.bySlot.clear(); return !br.starved || Fail(err, "WebP lossless: truncated"); }
        const int s1 = int(br.Take(8));
        if (s1 >= alphabet) return Fail(err, "WebP lossless: prefix symbol out of range");
        out.BuildPair(s0, s1);
        return !br.starved || Fail(err, "WebP lossless: truncated");
    }
    std::uint8_t clLens[19] = {};
    const int numCl = 4 + int(br.Take(4));
    for (int i = 0; i < numCl; ++i) clLens[kClOrder[i]] = std::uint8_t(br.Take(3));
    PrefixCode clCode;
    if (!clCode.Build(clLens, 19)) return Fail(err, "WebP lossless: bad code length code");
    int maxSymbol = alphabet;
    if (br.Take(1)) {
        const int nbits = 2 + 2 * int(br.Take(3));
        maxSymbol = 2 + int(Bits(br, nbits));
        if (maxSymbol > alphabet) return Fail(err, "WebP lossless: max_symbol beyond the alphabet");
    }
    std::vector<std::uint8_t> lens(std::size_t(alphabet), 0);
    int i = 0, prev = 8;
    while (i < alphabet) {
        if (maxSymbol-- <= 0) break;
        const int c = clCode.Read(br);
        if (c < 0) return Fail(err, "WebP lossless: truncated code lengths");
        if (c < 16) {
            lens[std::size_t(i++)] = std::uint8_t(c);
            if (c) prev = c;
            continue;
        }
        int rep = 0, val = 0;
        if (c == 16) { rep = 3 + int(br.Take(2)); val = prev; }
        else if (c == 17) rep = 3 + int(br.Take(3));
        else rep = 11 + int(br.Take(7));
        if (i + rep > alphabet) return Fail(err, "WebP lossless: code length run past the alphabet");
        std::fill(lens.begin() + i, lens.begin() + i + rep, std::uint8_t(val));
        i += rep;
    }
    if (br.starved) return Fail(err, "WebP lossless: truncated code lengths");
    if (!out.Build(lens.data(), alphabet)) return Fail(err, "WebP lossless: incomplete or over-full prefix code");
    return true;
}

// Prefix-coded value (lengths and distances), RFC 9649 3.6.2.2.
std::uint32_t PrefixValue(LsbReader& br, int code) {
    if (code < 4) return std::uint32_t(code) + 1;
    const int extra = (code - 2) >> 1;
    const std::uint32_t offset = std::uint32_t(2 + (code & 1)) << extra;
    return offset + Bits(br, extra) + 1;
}


struct Vp8lDecoder {
    LsbReader br;
    std::string& err;
    std::size_t codeBytes = 0;
    Vp8lDecoder(const std::uint8_t* d, std::size_t n, std::string& e) : br(d, n), err(e) {}

    // One entropy-coded image of w x h pixels (ARGB, 0xAARRGGBB). `top` allows the meta prefix codes.
    bool ImageData(int w, int h, bool top, std::vector<std::uint32_t>& out) {
        const std::size_t total = std::size_t(w) * std::size_t(h);
        int cacheBits = 0;
        if (br.Take(1)) {
            cacheBits = int(br.Take(4));
            if (cacheBits < 1 || cacheBits > 11) return Fail(err, "WebP lossless: bad colour cache size");
        }
        int metaBits = 0, metaW = 0;
        std::vector<std::uint32_t> meta;
        int groups = 1;
        if (top && br.Take(1)) {
            metaBits = int(br.Take(3)) + 2;
            metaW = DivRoundUp(w, 1 << metaBits);
            const int metaH = DivRoundUp(h, 1 << metaBits);
            if (!ImageData(metaW, metaH, false, meta)) return false;
            std::uint32_t most = 0;
            for (std::uint32_t& m : meta) { m = (m >> 8) & 0xFFFFu; most = std::max(most, m); }
            groups = int(most) + 1;
        }
        if (br.starved) return Fail(err, "WebP lossless: truncated");
        const int cacheSize = cacheBits ? (1 << cacheBits) : 0;
        const int alphabets[5] = {256 + 24 + cacheSize, 256, 256, 256, 40};
        std::vector<PrefixCode> codes;
        // Each prefix code takes at least 4 bits, so a group at least 20: `groups` is bounded by the input before the
        // codes are allocated, and their table bytes are capped as they are built.
        if (std::size_t(groups) * 20 > (br.len - br.BytesUsed()) * 8 + 64) return Fail(err, "WebP lossless: more prefix codes than the data can hold");
        codes.resize(std::size_t(groups) * 5);
        for (std::size_t g = 0; g < codes.size(); ++g) {
            if (!ReadPrefixCode(br, alphabets[g % 5], codes[g], err)) return false;
            codeBytes += codes[g].Bytes();
            if (codeBytes > kMaxCodeBytes) return Fail(err, "WebP lossless: prefix codes too large");
        }
        std::vector<std::uint32_t> cache(std::size_t(cacheSize), 0);
        const int cacheShift = 32 - cacheBits;
        out.assign(total, 0);
        std::size_t pos = 0;
        int x = 0, y = 0;
        const PrefixCode* grp = codes.data();
        while (pos < total) {
            if (metaBits) grp = &codes[std::size_t(meta[std::size_t(y >> metaBits) * std::size_t(metaW) + std::size_t(x >> metaBits)]) * 5];
            const int s = grp[0].Read(br);
            if (s < 0) return Fail(err, "WebP lossless: truncated pixel data");
            if (s < 256) {
                const int r = grp[1].Read(br), b = grp[2].Read(br), a = grp[3].Read(br);
                if ((r | b | a) < 0) return Fail(err, "WebP lossless: truncated pixel data");
                const std::uint32_t px = (std::uint32_t(a) << 24) | (std::uint32_t(r) << 16) | (std::uint32_t(s) << 8) | std::uint32_t(b);
                out[pos++] = px;
                if (cacheSize) cache[(0x1e35a7bdu * px) >> cacheShift] = px;
                if (++x == w) { x = 0; ++y; }
            } else if (s < 256 + 24) {
                const std::uint32_t len = PrefixValue(br, s - 256);
                const int dsym = grp[4].Read(br);
                if (dsym < 0) return Fail(err, "WebP lossless: truncated pixel data");
                const std::uint32_t dcode = PrefixValue(br, dsym);
                long long dist;
                if (dcode > 120) dist = (long long)dcode - 120;
                else {
                    dist = (long long)vp8tab::kLosslessDistanceMap[dcode - 1][0] + (long long)vp8tab::kLosslessDistanceMap[dcode - 1][1] * w;
                    if (dist < 1) dist = 1;
                }
                if (br.starved) return Fail(err, "WebP lossless: truncated pixel data");
                if (dist > (long long)pos || len > total - pos) return Fail(err, "WebP lossless: backward reference out of range");
                for (std::uint32_t k = 0; k < len; ++k, ++pos) {
                    const std::uint32_t px = out[pos - std::size_t(dist)];
                    out[pos] = px;
                    if (cacheSize) cache[(0x1e35a7bdu * px) >> cacheShift] = px;
                }
                x += int(len % std::uint32_t(w));
                y += int(len / std::uint32_t(w));
                if (x >= w) { x -= w; ++y; }
            } else {
                const int idx = s - 256 - 24;
                if (idx >= cacheSize) return Fail(err, "WebP lossless: colour cache index out of range");
                out[pos++] = cache[std::size_t(idx)];
                if (++x == w) { x = 0; ++y; }
            }
        }
        if (br.starved) return Fail(err, "WebP lossless: truncated pixel data");
        return true;
    }
};

// Per-channel helpers of the predictor transform (RFC 9649 3.5.1).
inline std::uint32_t Avg2(std::uint32_t a, std::uint32_t b) { return (((a ^ b) & 0xFEFEFEFEu) >> 1) + (a & b); }
inline int Ch(std::uint32_t v, int sh) { return int((v >> sh) & 0xFF); }
inline int Clamp8(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }
inline std::uint32_t Select(std::uint32_t L, std::uint32_t T, std::uint32_t TL) {
    int pl = 0, pt = 0;
    for (int sh = 0; sh < 32; sh += 8) {
        const int est = Ch(L, sh) + Ch(T, sh) - Ch(TL, sh);
        pl += std::abs(est - Ch(L, sh));
        pt += std::abs(est - Ch(T, sh));
    }
    return pl < pt ? L : T;
}
inline std::uint32_t ClampAddSubFull(std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    std::uint32_t r = 0;
    for (int sh = 0; sh < 32; sh += 8) r |= std::uint32_t(Clamp8(Ch(a, sh) + Ch(b, sh) - Ch(c, sh))) << sh;
    return r;
}
inline std::uint32_t ClampAddSubHalf(std::uint32_t a, std::uint32_t b) {
    std::uint32_t r = 0;
    for (int sh = 0; sh < 32; sh += 8) r |= std::uint32_t(Clamp8(Ch(a, sh) + (Ch(a, sh) - Ch(b, sh)) / 2)) << sh;
    return r;
}
inline std::uint32_t AddPixels(std::uint32_t a, std::uint32_t b) {   // per channel, modulo 256
    return (((a & 0xFF00FF00u) + (b & 0xFF00FF00u)) & 0xFF00FF00u) | (((a & 0x00FF00FFu) + (b & 0x00FF00FFu)) & 0x00FF00FFu);
}

std::uint32_t Predict(int mode, std::uint32_t L, std::uint32_t T, std::uint32_t TR, std::uint32_t TL) {
    switch (mode) {
        case 1: return L;
        case 2: return T;
        case 3: return TR;
        case 4: return TL;
        case 5: return Avg2(Avg2(L, TR), T);
        case 6: return Avg2(L, TL);
        case 7: return Avg2(L, T);
        case 8: return Avg2(TL, T);
        case 9: return Avg2(T, TR);
        case 10: return Avg2(Avg2(L, TL), Avg2(T, TR));
        case 11: return Select(L, T, TL);
        case 12: return ClampAddSubFull(L, T, TL);
        case 13: return ClampAddSubHalf(Avg2(L, T), TL);
        default: return 0xFF000000u;   // mode 0 (and the unused 14, 15)
    }
}

struct Transform {
    int type = 0, bits = 0, width = 0;   // width: the image width when the transform was read
    std::vector<std::uint32_t> data;
};

void InversePredictor(const Transform& t, int h, std::vector<std::uint32_t>& px) {
    const int w = t.width, bw = DivRoundUp(w, 1 << t.bits);
    for (int y = 0; y < h; ++y) {
        std::uint32_t* row = &px[std::size_t(y) * std::size_t(w)];
        for (int x = 0; x < w; ++x) {
            std::uint32_t pred;
            if (y == 0) pred = x == 0 ? 0xFF000000u : row[x - 1];
            else if (x == 0) pred = row[x - w];
            else {
                const int mode = Ch(t.data[std::size_t(y >> t.bits) * std::size_t(bw) + std::size_t(x >> t.bits)], 8) & 15;
                // TR of the last column is the first pixel of the current row, which is exactly row[x - w + 1].
                pred = Predict(mode, row[x - 1], row[x - w], row[x - w + 1], row[x - w - 1]);
            }
            row[x] = AddPixels(row[x], pred);
        }
    }
}

inline int CtDelta(int t, int c) { return (int(std::int8_t(t)) * int(std::int8_t(c))) >> 5; }

void InverseColor(const Transform& t, int h, std::vector<std::uint32_t>& px) {
    const int w = t.width, bw = DivRoundUp(w, 1 << t.bits);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::uint32_t e = t.data[std::size_t(y >> t.bits) * std::size_t(bw) + std::size_t(x >> t.bits)];
            const int g2r = Ch(e, 0), g2b = Ch(e, 8), r2b = Ch(e, 16);
            std::uint32_t& p = px[std::size_t(y) * std::size_t(w) + std::size_t(x)];
            const int g = Ch(p, 8);
            const int r = (Ch(p, 16) + CtDelta(g2r, g)) & 0xFF;
            const int b = (Ch(p, 0) + CtDelta(g2b, g) + CtDelta(r2b, r)) & 0xFF;
            p = (p & 0xFF00FF00u) | (std::uint32_t(r) << 16) | std::uint32_t(b);
        }
    }
}

void InverseSubtractGreen(std::vector<std::uint32_t>& px) {
    for (std::uint32_t& p : px) {
        const std::uint32_t g = (p >> 8) & 0xFF;
        p = (p & 0xFF00FF00u) | ((((p >> 16) + g) & 0xFF) << 16) | (((p & 0xFF) + g) & 0xFF);
    }
}

// Expand the (possibly bundled) indices of a w_in-wide image to t.width pixels from the colour table.
void InverseIndexing(const Transform& t, int h, const std::vector<std::uint32_t>& in, std::vector<std::uint32_t>& out) {
    const int n = int(t.data.size());
    const int wb = t.bits, wOut = t.width, wIn = DivRoundUp(wOut, 1 << wb);
    const int per = 8 >> wb, mask = (1 << per) - 1, sub = (1 << wb) - 1;
    out.assign(std::size_t(wOut) * std::size_t(h), 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < wOut; ++x) {
            const int packed = Ch(in[std::size_t(y) * std::size_t(wIn) + std::size_t(x >> wb)], 8);
            const int idx = wb ? (packed >> ((x & sub) * per)) & mask : packed;
            out[std::size_t(y) * std::size_t(wOut) + std::size_t(x)] = idx < n ? t.data[std::size_t(idx)] : 0u;
        }
    }
}

// The image stream after the header: transforms, then the main image. Pixels as ARGB words.
bool DecodeVp8lStream(Vp8lDecoder& dec, int w, int h, std::vector<std::uint32_t>& argb) {
    std::string& err = dec.err;
    std::vector<Transform> ts;
    int seen = 0, xs = w;
    while (dec.br.Take(1)) {
        Transform t;
        t.type = int(dec.br.Take(2));
        if (seen & (1 << t.type)) return Fail(err, "WebP lossless: transform used twice");
        seen |= 1 << t.type;
        t.width = xs;
        if (t.type == 0 || t.type == 1) {
            t.bits = int(dec.br.Take(3)) + 2;
            if (!dec.ImageData(DivRoundUp(xs, 1 << t.bits), DivRoundUp(h, 1 << t.bits), false, t.data)) return false;
        } else if (t.type == 3) {
            const int n = int(dec.br.Take(8)) + 1;
            if (!dec.ImageData(n, 1, false, t.data)) return false;
            for (int i = 1; i < n; ++i) t.data[std::size_t(i)] = AddPixels(t.data[std::size_t(i)], t.data[std::size_t(i) - 1]);
            t.bits = n <= 2 ? 3 : (n <= 4 ? 2 : (n <= 16 ? 1 : 0));
            xs = DivRoundUp(xs, 1 << t.bits);
        }
        if (dec.br.starved) return Fail(err, "WebP lossless: truncated transform");
        ts.push_back(std::move(t));
    }
    if (!dec.ImageData(xs, h, true, argb)) return false;
    std::vector<std::uint32_t> tmp;
    for (auto it = ts.rbegin(); it != ts.rend(); ++it) {
        switch (it->type) {
            case 0: InversePredictor(*it, h, argb); break;
            case 1: InverseColor(*it, h, argb); break;
            case 2: InverseSubtractGreen(argb); break;
            default: InverseIndexing(*it, h, argb, tmp); argb.swap(tmp); break;
        }
    }
    return true;
}

bool Vp8lHeader(const std::uint8_t* d, std::size_t n, int& w, int& h, std::string& err) {
    if (n < 5 || d[0] != 0x2F) return Fail(err, "WebP lossless: bad signature");
    const std::uint32_t v = Le32(d + 1);
    w = int(v & 0x3FFF) + 1;
    h = int((v >> 14) & 0x3FFF) + 1;
    if ((v >> 29) != 0) return Fail(err, "WebP lossless: unknown version");
    return true;
}

bool DecodeVp8l(const std::uint8_t* d, std::size_t n, int& w, int& h, std::vector<std::uint32_t>* argb, std::string& err) {
    if (!Vp8lHeader(d, n, w, h, err)) return false;
    if (!argb) return true;
    Vp8lDecoder dec(d + 5, n - 5, err);
    return DecodeVp8lStream(dec, w, h, *argb);
}

// ======================================================================================================
// Lossy (VP8 key frame), RFC 6386
// ======================================================================================================

// Boolean entropy decoder (RFC 6386 section 7). `window` holds the not yet consumed input bits; its top byte above
// bit `shift` lines up with `range`. Running out of input feeds zero bytes and sets `ranOut`.
struct BoolDecoder {
    const std::uint8_t* p = nullptr;
    const std::uint8_t* end = nullptr;
    std::uint64_t window = 0;
    int shift = -8;
    std::uint32_t range = 255;
    bool ranOut = false;

    void Init(const std::uint8_t* b, const std::uint8_t* e) { p = b; end = e; window = 0; shift = -8; range = 255; ranOut = false; }
    void Fill() {
        while (shift < 0) {
            std::uint8_t byte = 0;
            if (p < end) byte = *p++;
            else ranOut = true;
            window = (window << 8) | byte;
            shift += 8;
        }
    }
    int Bool(int prob) {
        const std::uint32_t split = 1 + (((range - 1) * std::uint32_t(prob)) >> 8);
        Fill();
        const std::uint64_t big = std::uint64_t(split) << shift;
        int bit;
        if (window >= big) { bit = 1; range -= split; window -= big; }
        else { bit = 0; range = split; }
        while (range < 128) { range <<= 1; --shift; }
        return bit;
    }
    std::uint32_t Literal(int n) { std::uint32_t v = 0; while (n-- > 0) v = (v << 1) | std::uint32_t(Bool(128)); return v; }
    int Signed(int n) { const int v = int(Literal(n)); return Bool(128) ? -v : v; }   // magnitude, then sign
    int MaybeSigned(int n) { return Bool(128) ? Signed(n) : 0; }
    // Tree-coded value: tree entries > 0 are the next index, <= 0 are minus a leaf value (RFC 6386 section 8.1).
    int Tree(const std::int8_t* tree, const std::uint8_t* probs, int start = 0) {
        int i = start;
        while ((i = tree[i + Bool(probs[i >> 1])]) > 0) {}
        return -i;
    }
};

enum { kDcPred = 0, kVPred, kHPred, kTmPred, kBPred };
enum { kBDc = 0, kBTm, kBVe, kBHe, kBLd, kBRd, kBVr, kBVl, kBHd, kBHu };

// Trees of RFC 6386 sections 10, 11.2, 11.4 and 13.2.
constexpr std::int8_t kSegmentTree[6] = {2, 4, -0, -1, -2, -3};
constexpr std::int8_t kKfYmodeTree[8] = {-kBPred, 2, 4, 6, -kDcPred, -kVPred, -kHPred, -kTmPred};
constexpr std::int8_t kUvModeTree[6] = {-kDcPred, 2, -kVPred, 4, -kHPred, -kTmPred};
constexpr std::int8_t kBmodeTree[18] = {-kBDc, 2, -kBTm, 4, -kBVe, 6, 8, 12, -kBHe, 10, -kBRd, -kBVr, -kBLd, 14, -kBVl, 16, -kBHd, -kBHu};
constexpr std::uint8_t kKfYmodeProbs[4] = {145, 156, 163, 128};
constexpr std::uint8_t kKfUvModeProbs[3] = {142, 114, 183};
enum { kTok0 = 0, kTok1, kTok2, kTok3, kTok4, kCat1, kCat2, kCat3, kCat4, kCat5, kCat6, kTokEob };
constexpr std::int8_t kCoeffTree[22] = {-kTokEob, 2, -kTok0, 4, -kTok1, 6, 8, 12, -kTok2, 10, -kTok3, -kTok4,
                                        14, 16, -kCat1, -kCat2, 18, 20, -kCat3, -kCat4, -kCat5, -kCat6};
constexpr std::uint8_t kCoeffBands[16] = {0, 1, 2, 3, 6, 4, 5, 6, 6, 6, 6, 6, 6, 6, 6, 7};
constexpr std::uint8_t kZigzag[16] = {0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15};
constexpr std::uint8_t kCatProbs[6][12] = {{159, 0}, {165, 145, 0}, {173, 148, 140, 0}, {176, 155, 140, 135, 0},
                                           {180, 157, 141, 134, 130, 0}, {254, 254, 243, 230, 196, 177, 153, 140, 133, 130, 129, 0}};
constexpr int kCatBase[6] = {5, 7, 11, 19, 35, 67};

inline std::uint8_t Clip255(int v) { return std::uint8_t(v < 0 ? 0 : (v > 255 ? 255 : v)); }

// Inverse transforms, with the exact integer arithmetic RFC 6386 sections 14.3 and 14.4 require.
void InverseWht(const int in[16], int out[16]) {
    int t[16];
    for (int c = 0; c < 4; ++c) {   // columns first
        const int a = in[c] + in[12 + c], b = in[4 + c] + in[8 + c];
        const int d = in[4 + c] - in[8 + c], e = in[c] - in[12 + c];
        t[c] = a + b; t[4 + c] = d + e; t[8 + c] = a - b; t[12 + c] = e - d;
    }
    for (int r = 0; r < 4; ++r) {
        const int* s = t + 4 * r;
        const int a = s[0] + s[3], b = s[1] + s[2], d = s[1] - s[2], e = s[0] - s[3];
        out[4 * r + 0] = (a + b + 3) >> 3;
        out[4 * r + 1] = (d + e + 3) >> 3;
        out[4 * r + 2] = (a - b + 3) >> 3;
        out[4 * r + 3] = (e - d + 3) >> 3;
    }
}

// x * sqrt(2) * cos(pi / 8) and x * sqrt(2) * sin(pi / 8) in 16-bit fixed point (RFC 6386 section 14.4).
// 64-bit products: hostile coefficients can be far larger than any real encoder writes.
inline int MulCos(int x) { return x + int((std::int64_t(x) * 20091) >> 16); }
inline int MulSin(int x) { return int((std::int64_t(x) * 35468) >> 16); }

// Adds the inverse DCT of `c` to the 4x4 block at `dst` (stride `st`), clamping to 0..255.
void IdctAdd(const int c[16], std::uint8_t* dst, int st) {
    int t[16];
    for (int i = 0; i < 4; ++i) {   // vertical pass
        const int a = c[i] + c[8 + i], b = c[i] - c[8 + i];
        const int cc = MulSin(c[4 + i]) - MulCos(c[12 + i]);
        const int dd = MulCos(c[4 + i]) + MulSin(c[12 + i]);
        t[i] = a + dd; t[12 + i] = a - dd; t[4 + i] = b + cc; t[8 + i] = b - cc;
    }
    for (int r = 0; r < 4; ++r) {   // horizontal pass, rounding by 1/8
        const int* s = t + 4 * r;
        const int a = s[0] + s[2], b = s[0] - s[2];
        const int cc = MulSin(s[1]) - MulCos(s[3]);
        const int dd = MulCos(s[1]) + MulSin(s[3]);
        std::uint8_t* o = dst + r * st;
        o[0] = Clip255(o[0] + ((a + dd + 4) >> 3));
        o[3] = Clip255(o[3] + ((a - dd + 4) >> 3));
        o[1] = Clip255(o[1] + ((b + cc + 4) >> 3));
        o[2] = Clip255(o[2] + ((b - cc + 4) >> 3));
    }
}

// ---- intra prediction (RFC 6386 section 12). `d` is the block's top-left pixel inside a work buffer whose row above
// and column to the left already hold the edge pixels (127 / 129 outside the frame), stride `st`.
void PredictBlock(int mode, std::uint8_t* d, int st, int size, bool haveAbove, bool haveLeft) {
    const std::uint8_t* A = d - st;
    if (mode == kDcPred) {
        int sum = 0, shf = 0;
        const int lg = size == 16 ? 4 : 3;
        if (haveAbove) { for (int i = 0; i < size; ++i) sum += A[i]; shf += lg; }
        if (haveLeft) { for (int i = 0; i < size; ++i) sum += d[i * st - 1]; shf += lg; }
        int v = 128;
        if (shf) { if (haveAbove && haveLeft) shf = lg + 1; v = (sum + (1 << (shf - 1))) >> shf; }
        for (int r = 0; r < size; ++r) std::memset(d + r * st, v, std::size_t(size));
    } else if (mode == kVPred) {
        for (int r = 0; r < size; ++r) std::memcpy(d + r * st, A, std::size_t(size));
    } else if (mode == kHPred) {
        for (int r = 0; r < size; ++r) std::memset(d + r * st, d[r * st - 1], std::size_t(size));
    } else {   // TM_PRED
        const int P = A[-1];
        for (int r = 0; r < size; ++r)
            for (int c = 0; c < size; ++c) d[r * st + c] = Clip255(d[r * st - 1] + A[c] - P);
    }
}

inline std::uint8_t Avg3(int x, int y, int z) { return std::uint8_t((x + 2 * y + z + 2) >> 2); }
inline std::uint8_t Avg2b(int x, int y) { return std::uint8_t((x + y + 1) >> 1); }

// One 4x4 luma subblock (RFC 6386 section 12.3). The row above holds 8 pixels (4 above, 4 above-right).
void PredictSub(int mode, std::uint8_t* d, int st) {
    const std::uint8_t* A = d - st;   // A[-1] is the corner P
    const int L[4] = {d[-1], d[st - 1], d[2 * st - 1], d[3 * st - 1]};
    const int P = A[-1];
    // Edge pixels from the bottom of the left column, through the corner, along the top row.
    const int E[9] = {L[3], L[2], L[1], L[0], P, A[0], A[1], A[2], A[3]};
    std::uint8_t B[4][4];
    switch (mode) {
        case kBDc: {
            int v = 4;
            for (int i = 0; i < 4; ++i) v += A[i] + L[i];
            std::memset(B, v >> 3, sizeof(B));
            break;
        }
        case kBTm:
            for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) B[r][c] = Clip255(L[r] + A[c] - P);
            break;
        case kBVe:
            for (int c = 0; c < 4; ++c) B[0][c] = B[1][c] = B[2][c] = B[3][c] = Avg3(A[c - 1], A[c], A[c + 1]);
            break;
        case kBHe: {
            const int Lp[5] = {P, L[0], L[1], L[2], L[3]};
            for (int r = 0; r < 4; ++r) {
                const std::uint8_t v = r < 3 ? Avg3(Lp[r], Lp[r + 1], Lp[r + 2]) : Avg3(L[2], L[3], L[3]);
                std::memset(B[r], v, 4);
            }
            break;
        }
        case kBLd:
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c) {
                    const int k = r + c;   // anti-diagonal
                    B[r][c] = k < 6 ? Avg3(A[k], A[k + 1], A[k + 2]) : Avg3(A[6], A[7], A[7]);
                }
            break;
        case kBRd:
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c) {
                    const int k = 4 - r + c;   // diagonal through E[k]
                    B[r][c] = Avg3(E[k - 1], E[k], E[k + 1]);
                }
            break;
        case kBVr:
            B[3][0] = Avg3(E[1], E[2], E[3]);
            B[2][0] = Avg3(E[2], E[3], E[4]);
            B[3][1] = B[1][0] = Avg3(E[3], E[4], E[5]);
            B[2][1] = B[0][0] = Avg2b(E[4], E[5]);
            B[3][2] = B[1][1] = Avg3(E[4], E[5], E[6]);
            B[2][2] = B[0][1] = Avg2b(E[5], E[6]);
            B[3][3] = B[1][2] = Avg3(E[5], E[6], E[7]);
            B[2][3] = B[0][2] = Avg2b(E[6], E[7]);
            B[1][3] = Avg3(E[6], E[7], E[8]);
            B[0][3] = Avg2b(E[7], E[8]);
            break;
        case kBVl:
            B[0][0] = Avg2b(A[0], A[1]);
            B[1][0] = Avg3(A[0], A[1], A[2]);
            B[2][0] = B[0][1] = Avg2b(A[1], A[2]);
            B[1][1] = B[3][0] = Avg3(A[1], A[2], A[3]);
            B[2][1] = B[0][2] = Avg2b(A[2], A[3]);
            B[3][1] = B[1][2] = Avg3(A[2], A[3], A[4]);
            B[2][2] = B[0][3] = Avg2b(A[3], A[4]);
            B[3][2] = B[1][3] = Avg3(A[3], A[4], A[5]);
            B[2][3] = Avg3(A[4], A[5], A[6]);
            B[3][3] = Avg3(A[5], A[6], A[7]);
            break;
        case kBHd:
            B[3][0] = Avg2b(E[0], E[1]);
            B[3][1] = Avg3(E[0], E[1], E[2]);
            B[2][0] = B[3][2] = Avg2b(E[1], E[2]);
            B[2][1] = B[3][3] = Avg3(E[1], E[2], E[3]);
            B[2][2] = B[1][0] = Avg2b(E[2], E[3]);
            B[2][3] = B[1][1] = Avg3(E[2], E[3], E[4]);
            B[1][2] = B[0][0] = Avg2b(E[3], E[4]);
            B[1][3] = B[0][1] = Avg3(E[3], E[4], E[5]);
            B[0][2] = Avg3(E[4], E[5], E[6]);
            B[0][3] = Avg3(E[5], E[6], E[7]);
            break;
        default:   // kBHu
            B[0][0] = Avg2b(L[0], L[1]);
            B[0][1] = Avg3(L[0], L[1], L[2]);
            B[0][2] = B[1][0] = Avg2b(L[1], L[2]);
            B[0][3] = B[1][1] = Avg3(L[1], L[2], L[3]);
            B[1][2] = B[2][0] = Avg2b(L[2], L[3]);
            B[1][3] = B[2][1] = Avg3(L[2], L[3], L[3]);
            B[2][2] = B[2][3] = B[3][0] = B[3][1] = B[3][2] = B[3][3] = std::uint8_t(L[3]);
            break;
    }
    for (int r = 0; r < 4; ++r) std::memcpy(d + r * st, B[r], 4);
}

// ---- loop filter (RFC 6386 section 15) ------------------------------------------------------------------------
inline int S8(int v) { return v < -128 ? -128 : (v > 127 ? 127 : v); }   // clamp to a signed byte
inline int U2S(std::uint8_t v) { return int(v) - 128; }
inline std::uint8_t S2U(int v) { return std::uint8_t(S8(v) + 128); }

// Moves p0 and q0 towards each other; returns the amount subtracted from q0. `outer`: include p1 - q1.
inline int CommonAdjust(bool outer, std::uint8_t* px, int step) {
    const int p1 = U2S(px[-2 * step]), p0 = U2S(px[-step]), q0 = U2S(px[0]), q1 = U2S(px[step]);
    int a = S8((outer ? S8(p1 - q1) : 0) + 3 * (q0 - p0));
    const int b = S8(a + 3) >> 3;
    a = S8(a + 4) >> 3;
    px[0] = S2U(q0 - a);
    px[-step] = S2U(p0 + b);
    return a;
}

inline bool EdgeSmall(const std::uint8_t* px, int step, int edgeLimit) {
    return std::abs(px[-step] - px[0]) * 2 + std::abs(px[-2 * step] - px[step]) / 2 <= edgeLimit;
}
inline bool InteriorSmall(const std::uint8_t* px, int step, int I) {
    return std::abs(px[-4 * step] - px[-3 * step]) <= I && std::abs(px[-3 * step] - px[-2 * step]) <= I &&
           std::abs(px[-2 * step] - px[-step]) <= I && std::abs(px[3 * step] - px[2 * step]) <= I &&
           std::abs(px[2 * step] - px[step]) <= I && std::abs(px[step] - px[0]) <= I;
}
inline bool HighVariance(const std::uint8_t* px, int step, int t) {
    return std::abs(px[-2 * step] - px[-step]) > t || std::abs(px[step] - px[0]) > t;
}

// Filters `len` segments across an edge. `px` is the first pixel after the edge of the first segment; `step` moves
// across the edge, `along` to the next segment.
void SimpleEdge(std::uint8_t* px, int step, int along, int len, int edgeLimit) {
    for (int i = 0; i < len; ++i, px += along)
        if (EdgeSmall(px, step, edgeLimit)) CommonAdjust(true, px, step);
}
void SubblockEdge(std::uint8_t* px, int step, int along, int len, int edgeLimit, int I, int hevT) {
    for (int i = 0; i < len; ++i, px += along) {
        if (!EdgeSmall(px, step, edgeLimit) || !InteriorSmall(px, step, I)) continue;
        const bool hv = HighVariance(px, step, hevT);
        const int p1 = U2S(px[-2 * step]), q1 = U2S(px[step]);
        const int a = (CommonAdjust(hv, px, step) + 1) >> 1;
        if (!hv) { px[step] = S2U(q1 - a); px[-2 * step] = S2U(p1 + a); }
    }
}
void MacroblockEdge(std::uint8_t* px, int step, int along, int len, int edgeLimit, int I, int hevT) {
    for (int i = 0; i < len; ++i, px += along) {
        if (!EdgeSmall(px, step, edgeLimit) || !InteriorSmall(px, step, I)) continue;
        if (HighVariance(px, step, hevT)) { CommonAdjust(true, px, step); continue; }
        const int p2 = U2S(px[-3 * step]), p1 = U2S(px[-2 * step]), p0 = U2S(px[-step]);
        const int q0 = U2S(px[0]), q1 = U2S(px[step]), q2 = U2S(px[2 * step]);
        const int w = S8(S8(p1 - q1) + 3 * (q0 - p0));
        int a = S8((27 * w + 63) >> 7);
        px[0] = S2U(q0 - a); px[-step] = S2U(p0 + a);
        a = S8((18 * w + 63) >> 7);
        px[step] = S2U(q1 - a); px[-2 * step] = S2U(p1 + a);
        a = S8((9 * w + 63) >> 7);
        px[2 * step] = S2U(q2 - a); px[-3 * step] = S2U(p2 + a);
    }
}

struct MbInfo {
    std::uint8_t ymode = 0, uvmode = 0, segment = 0;
    bool skip = false, hasCoeffs = false;
    std::uint8_t bmodes[16] = {};
};

struct Vp8Frame {
    int w = 0, h = 0, mbw = 0, mbh = 0;
    int stY = 0, stC = 0;
    std::vector<std::uint8_t> Y, U, V;   // whole macroblocks (mbw*16 x mbh*16 and half that)
};

constexpr int kMaxPartitions = 8;

bool Vp8Header(const std::uint8_t* d, std::size_t n, int& w, int& h, std::string& err) {
    if (n < 10) return Fail(err, "WebP lossy: frame too short");
    const std::uint32_t tag = Le24(d);
    if (tag & 1) return Fail(err, "WebP lossy: not a key frame");
    if (d[3] != 0x9D || d[4] != 0x01 || d[5] != 0x2A) return Fail(err, "WebP lossy: bad start code");
    w = int(Le16(d + 6) & 0x3FFF);
    h = int(Le16(d + 8) & 0x3FFF);
    if (w < 1 || h < 1) return Fail(err, "WebP lossy: zero size");
    return true;
}

bool DecodeVp8Frame(const std::uint8_t* d, std::size_t n, Vp8Frame& f, std::string& err) {
    int w = 0, h = 0;
    if (!Vp8Header(d, n, w, h, err)) return false;
    const std::uint32_t tag = Le24(d);
    const std::size_t firstSize = tag >> 5;
    if (firstSize > n - 10) return Fail(err, "WebP lossy: first partition past the end");
    f.w = w; f.h = h; f.mbw = (w + 15) >> 4; f.mbh = (h + 15) >> 4;
    BoolDecoder br;
    br.Init(d + 10, d + 10 + firstSize);

    br.Literal(1);   // colour space (only 0 is defined; 1 decodes the same way)
    br.Literal(1);   // clamping type: we always clamp
    // segmentation (RFC 6386 9.3 and the table of 19.2; "abs" = 1 means absolute values)
    bool segOn = br.Bool(128), segMap = false, segAbs = false;
    int segQ[4] = {}, segLf[4] = {};
    std::uint8_t segProbs[3] = {255, 255, 255};
    if (segOn) {
        segMap = br.Bool(128);
        if (br.Bool(128)) {
            segAbs = br.Bool(128);
            for (int& q : segQ) q = br.MaybeSigned(7);
            for (int& l : segLf) l = br.MaybeSigned(6);
        }
        if (segMap) for (std::uint8_t& p : segProbs) p = br.Bool(128) ? std::uint8_t(br.Literal(8)) : 255;
    }
    const bool simpleFilter = br.Bool(128);
    const int filterLevel = int(br.Literal(6));
    const int sharpness = int(br.Literal(3));
    int refDelta0 = 0, modeDeltaB = 0;   // only the intra-frame and B_PRED deltas matter in a key frame
    const bool lfAdj = br.Bool(128);
    if (lfAdj && br.Bool(128)) {
        for (int i = 0; i < 4; ++i) { const int v = br.MaybeSigned(6); if (i == 0) refDelta0 = v; }
        for (int i = 0; i < 4; ++i) { const int v = br.MaybeSigned(6); if (i == 0) modeDeltaB = v; }
    }
    const int numParts = 1 << br.Literal(2);
    // token partitions: sizes (3 bytes each, all but the last) follow the first partition
    const std::uint8_t* partBase = d + 10 + firstSize;
    const std::size_t partBytes = n - 10 - firstSize;
    if (partBytes < std::size_t(numParts - 1) * 3) return Fail(err, "WebP lossy: partition table past the end");
    BoolDecoder parts[kMaxPartitions];
    {
        const std::uint8_t* q = partBase + (numParts - 1) * 3;
        const std::uint8_t* end = partBase + partBytes;
        for (int i = 0; i < numParts; ++i) {
            std::size_t sz = i < numParts - 1 ? Le24(partBase + 3 * i) : std::size_t(end - q);
            if (sz > std::size_t(end - q)) return Fail(err, "WebP lossy: partition past the end");
            parts[i].Init(q, q + sz);
            q += sz;
        }
    }
    // quantiser indices (9.6)
    const int qBase = int(br.Literal(7));
    const int dYdc = br.MaybeSigned(4), dY2dc = br.MaybeSigned(4), dY2ac = br.MaybeSigned(4);
    const int dUVdc = br.MaybeSigned(4), dUVac = br.MaybeSigned(4);
    br.Literal(1);   // refresh_entropy_probs: irrelevant for a single frame
    std::uint8_t coeffProbs[4][8][3][11];
    std::memcpy(coeffProbs, vp8tab::kDefaultCoeffProbs, sizeof(coeffProbs));
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 8; ++j)
            for (int k = 0; k < 3; ++k)
                for (int t = 0; t < 11; ++t)
                    if (br.Bool(vp8tab::kCoeffUpdateProbs[i][j][k][t])) coeffProbs[i][j][k][t] = std::uint8_t(br.Literal(8));
    const bool skipOn = br.Bool(128);
    const int skipProb = skipOn ? int(br.Literal(8)) : 0;
    if (br.ranOut) return Fail(err, "WebP lossy: truncated frame header");

    // Dequantisation factors per segment: luma, Y2, chroma (DC and AC each).
    struct Dq { int f[6]; const int* y() const { return f; } const int* y2() const { return f + 2; } const int* uv() const { return f + 4; } } dq[4];
    for (int s = 0; s < 4; ++s) {
        int q = qBase;
        if (segOn) q = segAbs ? segQ[s] : qBase + segQ[s];
        vp8tab::DequantFactors(q, dYdc, dY2dc, dY2ac, dUVdc, dUVac, dq[s].f);
    }

    const std::size_t mbCount = std::size_t(f.mbw) * std::size_t(f.mbh);
    std::vector<MbInfo> info(mbCount);
    f.stY = f.mbw * 16; f.stC = f.mbw * 8;
    f.Y.assign(std::size_t(f.stY) * std::size_t(f.mbh) * 16, 0);
    f.U.assign(std::size_t(f.stC) * std::size_t(f.mbh) * 8, 0);
    f.V.assign(f.U.size(), 0);

    // Contexts: sub-block modes above (4 per macroblock column) and to the left; "has coefficients" flags above and
    // left per plane (Y: 4, U: 2, V: 2, Y2: 1).
    std::vector<std::uint8_t> aboveB(std::size_t(f.mbw) * 4, kBDc);
    std::vector<std::uint8_t> aboveNz(std::size_t(f.mbw) * 9, 0);
    std::uint8_t ws[17 * 21];   // luma work area: row 0 = the row above (with the corner and 4 above-right pixels)
    std::uint8_t wc[2][9 * 9];  // chroma work areas
    int coeffs[25][16];

    for (int my = 0; my < f.mbh; ++my) {
        std::uint8_t leftB[4] = {kBDc, kBDc, kBDc, kBDc};
        std::uint8_t leftNz[9] = {};
        BoolDecoder& tb = parts[my & (numParts - 1)];
        for (int mx = 0; mx < f.mbw; ++mx) {
            MbInfo& mb = info[std::size_t(my) * std::size_t(f.mbw) + std::size_t(mx)];
            // ---- macroblock header (first partition)
            if (segMap) mb.segment = std::uint8_t(br.Tree(kSegmentTree, segProbs));
            mb.skip = skipOn ? br.Bool(skipProb) != 0 : false;
            mb.ymode = std::uint8_t(br.Tree(kKfYmodeTree, kKfYmodeProbs));
            std::uint8_t* aB = &aboveB[std::size_t(mx) * 4];
            if (mb.ymode == kBPred) {
                for (int b = 0; b < 16; ++b) {
                    const int A = b < 4 ? aB[b] : mb.bmodes[b - 4];
                    const int L = (b & 3) ? mb.bmodes[b - 1] : leftB[b >> 2];
                    mb.bmodes[b] = std::uint8_t(br.Tree(kBmodeTree, vp8tab::kBmodeProbs[A][L]));
                }
            } else {
                const std::uint8_t implied = mb.ymode == kVPred ? kBVe : (mb.ymode == kHPred ? kBHe : (mb.ymode == kTmPred ? kBTm : kBDc));
                std::memset(mb.bmodes, implied, 16);
            }
            for (int i = 0; i < 4; ++i) { aB[i] = mb.bmodes[12 + i]; leftB[i] = mb.bmodes[i * 4 + 3]; }
            mb.uvmode = std::uint8_t(br.Tree(kUvModeTree, kKfUvModeProbs));

            // ---- residual tokens (token partition)
            std::memset(coeffs, 0, sizeof(coeffs));
            std::uint8_t* aNz = &aboveNz[std::size_t(mx) * 9];
            const bool hasY2 = mb.ymode != kBPred;
            const Dq& q = dq[mb.segment];
            bool any = false;
            if (!mb.skip) {
                // One block: plane type (0 Y after Y2, 1 Y2, 2 chroma, 3 Y with DC), context from the neighbours.
                auto block = [&](int type, int ctx, int* out, const int* dqf) -> bool {
                    int i = type == 0 ? 1 : 0;
                    bool nz = false, prevZero = false;
                    for (; i < 16; ++i) {
                        const std::uint8_t* pr = coeffProbs[type][kCoeffBands[i]][ctx];
                        const int tok = tb.Tree(kCoeffTree, pr, prevZero ? 2 : 0);
                        if (tok == kTokEob) break;
                        int v = 0;
                        if (tok == kTok0) { ctx = 0; prevZero = true; continue; }
                        if (tok <= kTok4) v = tok;
                        else {
                            const std::uint8_t* cp = kCatProbs[tok - kCat1];
                            int e = 0;
                            while (*cp) e = e * 2 + tb.Bool(*cp++);
                            v = kCatBase[tok - kCat1] + e;
                        }
                        nz = true; prevZero = false;
                        ctx = v > 1 ? 2 : 1;
                        if (tb.Bool(128)) v = -v;
                        out[kZigzag[i]] = v * dqf[i > 0 ? 1 : 0];
                    }
                    return nz;
                };
                if (hasY2) {
                    const bool nz = block(1, aNz[8] + leftNz[8], coeffs[24], q.y2());
                    aNz[8] = leftNz[8] = nz;
                    any |= nz;
                }
                for (int b = 0; b < 16; ++b) {
                    const int bx = b & 3, by = b >> 2;
                    const bool nz = block(hasY2 ? 0 : 3, aNz[bx] + leftNz[by], coeffs[b], q.y());
                    aNz[bx] = leftNz[by] = nz;
                    any |= nz;
                }
                for (int b = 0; b < 8; ++b) {   // 4 U then 4 V
                    const int bx = (b & 1) + (b >= 4 ? 2 : 0), by = ((b & 3) >> 1) + (b >= 4 ? 2 : 0);
                    const bool nz = block(2, aNz[4 + bx] + leftNz[4 + by], coeffs[16 + b], q.uv());
                    aNz[4 + bx] = leftNz[4 + by] = nz;
                    any |= nz;
                }
            } else {
                // A skipped macroblock has empty blocks; the Y2 context only changes when the macroblock has a Y2.
                for (int i = 0; i < 8; ++i) aNz[i] = leftNz[i] = 0;
                if (hasY2) aNz[8] = leftNz[8] = 0;
            }
            mb.hasCoeffs = any;
            if (hasY2 && any) {
                int dc[16];
                InverseWht(coeffs[24], dc);
                for (int b = 0; b < 16; ++b) coeffs[b][0] = dc[b];
            }

            // ---- reconstruction: luma
            const int ox = mx * 16, oy = my * 16;
            const int WS = 21;
            ws[0] = my == 0 ? 127 : (mx == 0 ? 129 : f.Y[std::size_t(oy - 1) * f.stY + std::size_t(ox - 1)]);
            for (int i = 0; i < 20; ++i) {
                std::uint8_t v = 127;
                if (my > 0) {
                    int sx = ox + i;
                    if (i >= 16 && mx == f.mbw - 1) sx = ox + 15;
                    v = f.Y[std::size_t(oy - 1) * f.stY + std::size_t(sx)];
                }
                ws[1 + i] = v;
            }
            for (int r = 0; r < 16; ++r) ws[(1 + r) * WS] = mx == 0 ? 129 : f.Y[std::size_t(oy + r) * f.stY + std::size_t(ox - 1)];
            for (int k = 1; k < 4; ++k) std::memcpy(&ws[(4 * k) * WS + 17], &ws[17], 4);   // above-right of 7, 11, 15
            std::uint8_t* y0 = &ws[WS + 1];
            if (mb.ymode == kBPred) {
                for (int b = 0; b < 16; ++b) {
                    std::uint8_t* d4 = y0 + (b >> 2) * 4 * WS + (b & 3) * 4;
                    PredictSub(mb.bmodes[b], d4, WS);
                    if (any) IdctAdd(coeffs[b], d4, WS);
                }
            } else {
                PredictBlock(mb.ymode, y0, WS, 16, my > 0, mx > 0);
                if (any) for (int b = 0; b < 16; ++b) IdctAdd(coeffs[b], y0 + (b >> 2) * 4 * WS + (b & 3) * 4, WS);
            }
            for (int r = 0; r < 16; ++r) std::memcpy(&f.Y[std::size_t(oy + r) * f.stY + std::size_t(ox)], y0 + r * WS, 16);
            // ---- chroma
            for (int pl = 0; pl < 2; ++pl) {
                std::vector<std::uint8_t>& P = pl ? f.V : f.U;
                std::uint8_t* c = wc[pl];
                const int cx = mx * 8, cy = my * 8;
                c[0] = my == 0 ? 127 : (mx == 0 ? 129 : P[std::size_t(cy - 1) * f.stC + std::size_t(cx - 1)]);
                for (int i = 0; i < 8; ++i) c[1 + i] = my == 0 ? 127 : P[std::size_t(cy - 1) * f.stC + std::size_t(cx + i)];
                for (int r = 0; r < 8; ++r) c[(1 + r) * 9] = mx == 0 ? 129 : P[std::size_t(cy + r) * f.stC + std::size_t(cx - 1)];
                std::uint8_t* c0 = c + 10;
                PredictBlock(mb.uvmode, c0, 9, 8, my > 0, mx > 0);
                if (any) for (int b = 0; b < 4; ++b) IdctAdd(coeffs[16 + pl * 4 + b], c0 + (b >> 1) * 4 * 9 + (b & 1) * 4, 9);
                for (int r = 0; r < 8; ++r) std::memcpy(&P[std::size_t(cy + r) * f.stC + std::size_t(cx)], c0 + r * 9, 8);
            }
        }
        if (br.ranOut) return Fail(err, "WebP lossy: truncated first partition");
        if (tb.ranOut) return Fail(err, "WebP lossy: truncated token partition");
    }

    // ---- loop filter over the whole frame, macroblocks in raster order (RFC 6386 15.1). A frame level of 0 turns
    // it off for the whole frame, whatever the segments say.
    if (filterLevel > 0) {
        for (int my = 0; my < f.mbh; ++my) {
            for (int mx = 0; mx < f.mbw; ++mx) {
                const MbInfo& mb = info[std::size_t(my) * std::size_t(f.mbw) + std::size_t(mx)];
                int level = filterLevel;
                if (segOn) level = segAbs ? segLf[mb.segment] : level + segLf[mb.segment];
                level = std::max(0, std::min(63, level));
                if (lfAdj) {
                    level += refDelta0;
                    if (mb.ymode == kBPred) level += modeDeltaB;
                    level = std::max(0, std::min(63, level));
                }
                if (level == 0) continue;
                int I = level;
                if (sharpness) { I >>= sharpness > 4 ? 2 : 1; if (I > 9 - sharpness) I = 9 - sharpness; }
                if (I < 1) I = 1;
                const int hevT = level >= 40 ? 2 : (level >= 15 ? 1 : 0);
                const int mbLimit = (level + 2) * 2 + I, subLimit = level * 2 + I;
                const bool inner = mb.ymode == kBPred || mb.hasCoeffs;
                std::uint8_t* y = &f.Y[std::size_t(my) * 16 * f.stY + std::size_t(mx) * 16];
                std::uint8_t* u = &f.U[std::size_t(my) * 8 * f.stC + std::size_t(mx) * 8];
                std::uint8_t* v = &f.V[std::size_t(my) * 8 * f.stC + std::size_t(mx) * 8];
                const int sy = f.stY, sc = f.stC;
                if (simpleFilter) {
                    if (mx > 0) SimpleEdge(y, 1, sy, 16, mbLimit);
                    if (inner) for (int k = 4; k < 16; k += 4) SimpleEdge(y + k, 1, sy, 16, subLimit);
                    if (my > 0) SimpleEdge(y, sy, 1, 16, mbLimit);
                    if (inner) for (int k = 4; k < 16; k += 4) SimpleEdge(y + k * sy, sy, 1, 16, subLimit);
                    continue;
                }
                if (mx > 0) {
                    MacroblockEdge(y, 1, sy, 16, mbLimit, I, hevT);
                    MacroblockEdge(u, 1, sc, 8, mbLimit, I, hevT);
                    MacroblockEdge(v, 1, sc, 8, mbLimit, I, hevT);
                }
                if (inner) {
                    for (int k = 4; k < 16; k += 4) SubblockEdge(y + k, 1, sy, 16, subLimit, I, hevT);
                    SubblockEdge(u + 4, 1, sc, 8, subLimit, I, hevT);
                    SubblockEdge(v + 4, 1, sc, 8, subLimit, I, hevT);
                }
                if (my > 0) {
                    MacroblockEdge(y, sy, 1, 16, mbLimit, I, hevT);
                    MacroblockEdge(u, sc, 1, 8, mbLimit, I, hevT);
                    MacroblockEdge(v, sc, 1, 8, mbLimit, I, hevT);
                }
                if (inner) {
                    for (int k = 4; k < 16; k += 4) SubblockEdge(y + k * sy, sy, 1, 16, subLimit, I, hevT);
                    SubblockEdge(u + 4 * sc, sc, 1, 8, subLimit, I, hevT);
                    SubblockEdge(v + 4 * sc, sc, 1, 8, subLimit, I, hevT);
                }
            }
        }
    }
    return true;
}

// Y'CbCr (BT.601, studio range) to RGBA with centred bilinear chroma upsampling. Coefficients in 1/16384 units, from
// R = 1.164(Y-16) + 1.596(Cr-128), G = 1.164(Y-16) - 0.392(Cb-128) - 0.813(Cr-128), B = 1.164(Y-16) + 2.017(Cb-128).
void YuvToRgba(const Vp8Frame& f, std::uint8_t* rgba) {
    const int cw = (f.w + 1) >> 1, ch = (f.h + 1) >> 1;
    auto chroma = [&](const std::vector<std::uint8_t>& P, int x, int y) -> int {
        // chroma at luma (x, y), in 1/16 units
        const int cx = x >> 1, cy = y >> 1;
        const int nx = std::max(0, std::min(cw - 1, (x & 1) ? cx + 1 : cx - 1));
        const int ny = std::max(0, std::min(ch - 1, (y & 1) ? cy + 1 : cy - 1));
        const std::size_t st = std::size_t(f.stC);
        return 9 * P[std::size_t(cy) * st + std::size_t(cx)] + 3 * P[std::size_t(cy) * st + std::size_t(nx)] +
               3 * P[std::size_t(ny) * st + std::size_t(cx)] + P[std::size_t(ny) * st + std::size_t(nx)];
    };
    for (int y = 0; y < f.h; ++y) {
        for (int x = 0; x < f.w; ++x) {
            const int yy = (int(f.Y[std::size_t(y) * std::size_t(f.stY) + std::size_t(x)]) - 16) * 19077;
            const int cb = chroma(f.U, x, y) - 128 * 16, cr = chroma(f.V, x, y) - 128 * 16;   // x16
            // (yy + k * c / 16 + 8192) >> 14, with the /16 folded into the shift
            const int r = (yy * 16 + 26149 * cr + (8192 << 4)) >> 18;
            const int g = (yy * 16 - 6419 * cb - 13320 * cr + (8192 << 4)) >> 18;
            const int b = (yy * 16 + 33050 * cb + (8192 << 4)) >> 18;
            std::uint8_t* o = rgba + (std::size_t(y) * std::size_t(f.w) + std::size_t(x)) * 4;
            o[0] = Clip255(r); o[1] = Clip255(g); o[2] = Clip255(b); o[3] = 255;
        }
    }
}

// ======================================================================================================
// Alpha (ALPH chunk, RFC 9649 2.7.1.2)
// ======================================================================================================
bool DecodeAlpha(const std::uint8_t* d, std::size_t n, int w, int h, std::vector<std::uint8_t>& alpha, std::string& err) {
    if (n < 1) return Fail(err, "WebP: empty ALPH chunk");
    const int filter = (d[0] >> 2) & 3, method = d[0] & 3;
    const std::size_t count = std::size_t(w) * std::size_t(h);
    if (method == 0) {
        if (n - 1 < count) return Fail(err, "WebP: ALPH chunk too short");
        alpha.assign(d + 1, d + 1 + count);
    } else if (method == 1) {
        Vp8lDecoder dec(d + 1, n - 1, err);
        std::vector<std::uint32_t> argb;
        if (!DecodeVp8lStream(dec, w, h, argb)) return false;
        alpha.resize(count);
        for (std::size_t i = 0; i < count; ++i) alpha[i] = std::uint8_t((argb[i] >> 8) & 0xFF);
    } else {
        return Fail(err, "WebP: unknown ALPH compression");
    }
    if (filter == 0) return true;
    for (int y = 0; y < h; ++y) {
        std::uint8_t* row = &alpha[std::size_t(y) * std::size_t(w)];
        const std::uint8_t* up = y ? row - w : nullptr;
        for (int x = 0; x < w; ++x) {
            int pred;
            if (y == 0) pred = x ? row[x - 1] : 0;
            else if (x == 0) pred = up[0];
            else if (filter == 1) pred = row[x - 1];
            else if (filter == 2) pred = up[x];
            else pred = Clamp8(row[x - 1] + up[x] - up[x - 1]);
            row[x] = std::uint8_t(row[x] + pred);
        }
    }
    return true;
}

// ======================================================================================================
// Container (RFC 9649 section 2)
// ======================================================================================================
struct Chunk { std::uint32_t tag = 0; const std::uint8_t* data = nullptr; std::size_t size = 0; };
std::uint32_t Tag(const char* s) { return Le32(reinterpret_cast<const std::uint8_t*>(s)); }

// Chunks of [d, d + n), padded to even sizes. A chunk running past the end is cut to what is there.
std::vector<Chunk> Chunks(const std::uint8_t* d, std::size_t n) {
    std::vector<Chunk> out;
    std::size_t p = 0;
    while (n - p >= 8 && out.size() < 4096) {
        Chunk c;
        c.tag = Le32(d + p);
        const std::size_t sz = Le32(d + p + 4);
        c.data = d + p + 8;
        c.size = std::min(sz, n - p - 8);
        out.push_back(c);
        if (sz > n - p - 8) break;
        p += 8 + sz + (sz & 1);
        if (p > n) break;
    }
    return out;
}

// One frame: an optional ALPH chunk and a VP8 / VP8L chunk, as a w x h RGBA picture.
bool DecodeWebpFrame(const std::vector<Chunk>& cs, int& w, int& h, std::vector<std::uint8_t>* rgba, std::string& err) {
    const Chunk* alph = nullptr;
    for (const Chunk& c : cs) {
        if (c.tag == Tag("ALPH")) { if (!alph) alph = &c; continue; }
        if (c.tag == Tag("VP8L")) {
            std::vector<std::uint32_t> argb;
            if (!DecodeVp8l(c.data, c.size, w, h, rgba ? &argb : nullptr, err)) return false;
            if (!rgba) return true;
            rgba->resize(argb.size() * 4);
            for (std::size_t i = 0; i < argb.size(); ++i) {
                const std::uint32_t p = argb[i];
                std::uint8_t* o = &(*rgba)[i * 4];
                o[0] = std::uint8_t(p >> 16); o[1] = std::uint8_t(p >> 8); o[2] = std::uint8_t(p); o[3] = std::uint8_t(p >> 24);
            }
            return true;
        }
        if (c.tag == Tag("VP8 ")) {
            if (!Vp8Header(c.data, c.size, w, h, err)) return false;
            if (!rgba) return true;
            Vp8Frame f;
            if (!DecodeVp8Frame(c.data, c.size, f, err)) return false;
            rgba->resize(std::size_t(w) * std::size_t(h) * 4);
            YuvToRgba(f, rgba->data());
            if (alph) {
                std::vector<std::uint8_t> a;
                if (!DecodeAlpha(alph->data, alph->size, w, h, a, err)) return false;
                for (std::size_t i = 0; i < a.size(); ++i) (*rgba)[i * 4 + 3] = a[i];
            }
            return true;
        }
    }
    return Fail(err, "WebP: no image data");
}

} // namespace

bool DecodeWebp(const std::uint8_t* data, std::size_t size, int* width, int* height, std::vector<std::uint8_t>* rgba,
                std::string& err) {
    if (size < 12 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WEBP", 4) != 0) return Fail(err, "not a WebP file");
    const std::size_t riff = std::size_t(Le32(data + 4)) + 8;
    const std::size_t n = std::min(size, riff);
    if (n < 12) return Fail(err, "WebP: bad RIFF size");
    const std::vector<Chunk> cs = Chunks(data + 12, n - 12);
    if (cs.empty()) return Fail(err, "WebP: no chunks");
    auto checkDims = [&](int w, int h) {
        if (w < 1 || h < 1 || w > kMaxImageDimension || h > kMaxImageDimension) return Fail(err, "WebP dimensions out of range");
        if (width) *width = w;
        if (height) *height = h;
        return true;
    };
    int w = 0, h = 0;
    if (cs[0].tag != Tag("VP8X")) {   // simple formats
        if (!DecodeWebpFrame(cs, w, h, nullptr, err) || !checkDims(w, h)) return false;
        if (!rgba) return true;
        return DecodeWebpFrame(cs, w, h, rgba, err);
    }
    if (cs[0].size < 10) return Fail(err, "WebP: VP8X chunk too short");
    const std::uint8_t flags = cs[0].data[0];
    const int cw = int(Le24(cs[0].data + 4)) + 1, chh = int(Le24(cs[0].data + 7)) + 1;
    if (!checkDims(cw, chh)) return false;
    if (flags & 0x02) {   // animation: compose the first ANMF frame on a transparent canvas
        for (const Chunk& c : cs) {
            if (c.tag != Tag("ANMF")) continue;
            if (c.size < 16) return Fail(err, "WebP: ANMF chunk too short");
            const int fx = int(Le24(c.data)) * 2, fy = int(Le24(c.data + 3)) * 2;
            const int fw = int(Le24(c.data + 6)) + 1, fh = int(Le24(c.data + 9)) + 1;
            if (fx + fw > cw || fy + fh > chh) return Fail(err, "WebP: animation frame outside the canvas");
            if (!rgba) return true;
            const std::vector<Chunk> sub = Chunks(c.data + 16, c.size - 16);
            std::vector<std::uint8_t> frame;
            int iw = 0, ih = 0;
            if (!DecodeWebpFrame(sub, iw, ih, nullptr, err)) return false;
            if (iw != fw || ih != fh) return Fail(err, "WebP: frame size differs from its ANMF header");
            if (!DecodeWebpFrame(sub, iw, ih, &frame, err)) return false;
            rgba->assign(std::size_t(cw) * std::size_t(chh) * 4, 0);
            for (int y = 0; y < fh; ++y)
                std::memcpy(&(*rgba)[(std::size_t(fy + y) * std::size_t(cw) + std::size_t(fx)) * 4], &frame[std::size_t(y) * std::size_t(fw) * 4],
                            std::size_t(fw) * 4);
            return true;
        }
        return Fail(err, "WebP: animation without frames");
    }
    std::vector<Chunk> rest(cs.begin() + 1, cs.end());
    if (!DecodeWebpFrame(rest, w, h, nullptr, err)) return false;
    if (w != cw || h != chh) return Fail(err, "WebP: image size differs from the canvas");
    if (!rgba) return true;
    return DecodeWebpFrame(rest, w, h, rgba, err);
}

bool DecodeVp8Planes(const std::uint8_t* data, std::size_t size, int& width, int& height, std::vector<std::uint8_t>& y,
                     std::vector<std::uint8_t>& u, std::vector<std::uint8_t>& v, std::string& err) {
    Vp8Frame f;
    if (!Vp8Header(data, size, width, height, err)) return false;
    if (width > kMaxImageDimension || height > kMaxImageDimension) return Fail(err, "WebP dimensions out of range");
    if (!DecodeVp8Frame(data, size, f, err)) return false;
    const int cw = (f.w + 1) >> 1, ch = (f.h + 1) >> 1;
    y.resize(std::size_t(f.w) * std::size_t(f.h));
    u.resize(std::size_t(cw) * std::size_t(ch));
    v.resize(u.size());
    for (int r = 0; r < f.h; ++r) std::memcpy(&y[std::size_t(r) * std::size_t(f.w)], &f.Y[std::size_t(r) * std::size_t(f.stY)], std::size_t(f.w));
    for (int r = 0; r < ch; ++r) {
        std::memcpy(&u[std::size_t(r) * std::size_t(cw)], &f.U[std::size_t(r) * std::size_t(f.stC)], std::size_t(cw));
        std::memcpy(&v[std::size_t(r) * std::size_t(cw)], &f.V[std::size_t(r) * std::size_t(f.stC)], std::size_t(cw));
    }
    return true;
}

} // namespace imgdec
} // namespace okay
