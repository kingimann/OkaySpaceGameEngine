// Image decoders written from the format specifications (PNG/zlib/DEFLATE RFC 1950-1951 and
// the W3C PNG spec, BMP, TGA 2.0, GIF89a, Radiance RGBE, Adobe PSD file format) plus
// ImageDecodeJpeg.cpp. No third-party code. Every decoder is bounds-checked: malformed or
// truncated input fails with an error string.
#include "okay/Core/ImageDecode.hpp"
#include "ImageDecodeInternal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace okay {

namespace {

constexpr std::size_t kMaxPixelBytes = std::size_t(1) << 31;   // 2 GiB of RGBA at most
constexpr std::size_t kMaxAnimationBytes = std::size_t(512) << 20;   // all GIF frames together

bool Fail(std::string& err, const char* msg) { if (err.empty()) err = msg; return false; }

// Checks dimensions and allocates the RGBA output (opaque white initialised by callers if needed).
bool AllocRgba(int w, int h, std::vector<std::uint8_t>& out, std::string& err) {
    if (w < 1 || h < 1 || w > kMaxImageDimension || h > kMaxImageDimension) return Fail(err, "image dimensions out of range");
    const std::size_t bytes = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4;
    if (bytes > kMaxPixelBytes) return Fail(err, "image too large");
    out.assign(bytes, 0);
    return true;
}

// A header can claim far more pixels than the file could possibly encode (a 40-byte TGA that
// says 16384 x 16384 would otherwise allocate a gigabyte of zeros). Real compression tops out
// well below 4096:1 (deflate ~1032:1, GIF LZW ~2700:1), so claims beyond that on large
// images are refused before anything is allocated.
bool SizePlausible(int w, int h, std::size_t fileBytes) {
    const unsigned long long rgba = static_cast<unsigned long long>(w) * static_cast<unsigned long long>(h) * 4ULL;
    return !(rgba > (64ULL << 20) && rgba / 4096ULL > fileBytes);
}

// Little bounds-checked byte reader.
struct Rd {
    const std::uint8_t* d;
    std::size_t n;
    std::size_t p = 0;
    bool ok = true;
    Rd(const std::uint8_t* data, std::size_t size) : d(data), n(size) {}
    std::size_t Left() const { return p <= n ? n - p : 0; }
    std::uint32_t U8() { if (p < n) return d[p++]; ok = false; return 0; }
    std::uint32_t U16le() { std::uint32_t a = U8(); return a | (U8() << 8); }
    std::uint32_t U16be() { std::uint32_t a = U8(); return (a << 8) | U8(); }
    std::uint32_t U32le() { std::uint32_t a = U16le(); return a | (U16le() << 16); }
    std::uint32_t U32be() { std::uint32_t a = U16be(); return (a << 16) | U16be(); }
    bool Skip(std::size_t k) { if (Left() < k) { ok = false; p = n; return false; } p += k; return true; }
    bool Has(std::size_t k) const { return Left() >= k; }
};

// ================================================================================================
// DEFLATE / zlib
// ================================================================================================
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

} // namespace

bool Inflate(const std::uint8_t* data, std::size_t size, bool zlibWrapper, std::vector<std::uint8_t>& out, std::size_t maxOut) {
    if (!InflateCore(data, size, zlibWrapper, out, maxOut, false, size * 4)) { out.clear(); return false; }
    return true;
}

namespace {

// ================================================================================================
// PNG
// ================================================================================================
struct PngInfo {
    int w = 0, h = 0, depth = 0, ctype = 0, interlace = 0;
};

int PngChannels(int ctype) {
    switch (ctype) { case 0: return 1; case 2: return 3; case 3: return 1; case 4: return 2; case 6: return 4; }
    return 0;
}

bool DecodePng(const std::uint8_t* data, std::size_t size, int& W, int& H, std::vector<std::uint8_t>* rgba, std::string& err) {
    static const std::uint8_t sig[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (size < 8 || std::memcmp(data, sig, 8) != 0) return Fail(err, "not a PNG");
    std::size_t pos = 8;
    PngInfo in;
    bool haveHdr = false;
    std::uint8_t pal[256 * 3];
    int palCount = 0;
    bool havePal = false;
    std::uint8_t trnsAlpha[256];
    int trnsCount = 0;
    bool haveKey = false;
    int key[3] = {0, 0, 0};
    std::vector<std::uint8_t> idat;
    bool sawIend = false;
    while (pos + 8 <= size && !sawIend) {
        std::size_t len = (std::size_t(data[pos]) << 24) | (std::size_t(data[pos + 1]) << 16) | (std::size_t(data[pos + 2]) << 8) | data[pos + 3];
        const std::uint8_t* type = data + pos + 4;
        pos += 8;
        if (len > size - pos) len = size - pos;   // truncated chunk: use what is there
        const std::uint8_t* body = data + pos;
        auto is = [&](const char* t) { return std::memcmp(type, t, 4) == 0; };
        if (!haveHdr) {
            if (!is("IHDR") || len < 13) return Fail(err, "PNG does not start with IHDR");
            in.w = static_cast<int>((std::uint32_t(body[0]) << 24) | (body[1] << 16) | (body[2] << 8) | body[3]);
            in.h = static_cast<int>((std::uint32_t(body[4]) << 24) | (body[5] << 16) | (body[6] << 8) | body[7]);
            in.depth = body[8]; in.ctype = body[9];
            const int comp = body[10], filt = body[11];
            in.interlace = body[12];
            if (in.w < 1 || in.h < 1 || in.w > kMaxImageDimension || in.h > kMaxImageDimension) return Fail(err, "PNG dimensions out of range");
            if (comp != 0 || filt != 0 || in.interlace > 1) return Fail(err, "bad PNG header");
            const int d = in.depth;
            bool okDepth = false;
            switch (in.ctype) {
                case 0: okDepth = d == 1 || d == 2 || d == 4 || d == 8 || d == 16; break;
                case 3: okDepth = d == 1 || d == 2 || d == 4 || d == 8; break;
                case 2: case 4: case 6: okDepth = d == 8 || d == 16; break;
                default: break;
            }
            if (!okDepth) return Fail(err, "bad PNG colour type or bit depth");
            haveHdr = true;
            if (!rgba) { W = in.w; H = in.h; return true; }
        } else if (is("PLTE")) {
            if (len % 3 != 0 || len > 768 || len == 0) return Fail(err, "bad PNG palette");
            palCount = static_cast<int>(len / 3);
            std::memcpy(pal, body, len);
            havePal = true;
        } else if (is("tRNS")) {
            if (in.ctype == 3) {
                trnsCount = static_cast<int>(std::min<std::size_t>(len, 256));
                std::memcpy(trnsAlpha, body, static_cast<std::size_t>(trnsCount));
            } else if (in.ctype == 0 && len >= 2) {
                key[0] = (body[0] << 8) | body[1]; haveKey = true;
            } else if (in.ctype == 2 && len >= 6) {
                for (int i = 0; i < 3; ++i) key[i] = (body[2 * i] << 8) | body[2 * i + 1];
                haveKey = true;
            }
        } else if (is("IDAT")) {
            idat.insert(idat.end(), body, body + len);
        } else if (is("IEND")) {
            sawIend = true;
        } else if (!(type[0] & 0x20) && !is("IHDR")) {
            return Fail(err, "unsupported critical PNG chunk");
        }
        pos += len + 4;   // body + CRC (not verified)
        if (pos > size) pos = size;
    }
    if (!haveHdr) return Fail(err, "not a PNG");
    if (in.ctype == 3 && !havePal) return Fail(err, "PNG palette missing");
    if (idat.empty()) return Fail(err, "PNG has no image data");

    const int ch = PngChannels(in.ctype);
    const int bpp = ch * in.depth;   // bits per pixel
    const int bytesPP = std::max(1, bpp / 8);
    static const int xs[7] = {0, 4, 0, 2, 0, 1, 0}, ys[7] = {0, 0, 4, 0, 2, 0, 1};
    static const int dxs[7] = {8, 8, 4, 4, 2, 2, 1}, dys[7] = {8, 8, 8, 4, 4, 2, 2};
    const int passes = in.interlace ? 7 : 1;
    std::size_t rawSize = 0;
    for (int ps = 0; ps < passes; ++ps) {
        int pw = in.w, ph = in.h;
        if (in.interlace) { pw = (in.w - xs[ps] + dxs[ps] - 1) / dxs[ps]; ph = (in.h - ys[ps] + dys[ps] - 1) / dys[ps]; }
        if (pw <= 0 || ph <= 0) continue;
        rawSize += static_cast<std::size_t>(ph) * (1 + (static_cast<std::size_t>(pw) * bpp + 7) / 8);
    }
    std::vector<std::uint8_t> raw;
    if (!InflateCore(idat.data(), idat.size(), true, raw, rawSize, true, rawSize) || raw.size() != rawSize)
        return Fail(err, "corrupt or truncated PNG image data");
    std::vector<std::uint8_t>().swap(idat);

    if (!AllocRgba(in.w, in.h, *rgba, err)) return false;
    std::uint8_t* outp = rgba->data();
    std::size_t off = 0;
    std::vector<std::uint8_t> zeroRow;
    for (int ps = 0; ps < passes; ++ps) {
        int pw = in.w, ph = in.h, x0 = 0, y0 = 0, dx = 1, dy = 1;
        if (in.interlace) { x0 = xs[ps]; y0 = ys[ps]; dx = dxs[ps]; dy = dys[ps]; pw = (in.w - x0 + dx - 1) / dx; ph = (in.h - y0 + dy - 1) / dy; }
        if (pw <= 0 || ph <= 0) continue;
        const std::size_t stride = (static_cast<std::size_t>(pw) * bpp + 7) / 8;
        zeroRow.assign(stride, 0);
        const std::uint8_t* prev = zeroRow.data();
        for (int row = 0; row < ph; ++row) {
            std::uint8_t* f = raw.data() + off;
            const int ft = f[0];
            std::uint8_t* cur = f + 1;
            switch (ft) {
                case 0: break;
                case 1: for (std::size_t i = bytesPP; i < stride; ++i) cur[i] = static_cast<std::uint8_t>(cur[i] + cur[i - bytesPP]); break;
                case 2: for (std::size_t i = 0; i < stride; ++i) cur[i] = static_cast<std::uint8_t>(cur[i] + prev[i]); break;
                case 3:
                    for (std::size_t i = 0; i < stride; ++i) {
                        const int a = i >= static_cast<std::size_t>(bytesPP) ? cur[i - bytesPP] : 0;
                        cur[i] = static_cast<std::uint8_t>(cur[i] + ((a + prev[i]) >> 1));
                    }
                    break;
                case 4:
                    for (std::size_t i = 0; i < stride; ++i) {
                        const int a = i >= static_cast<std::size_t>(bytesPP) ? cur[i - bytesPP] : 0;
                        const int b = prev[i];
                        const int c = i >= static_cast<std::size_t>(bytesPP) ? prev[i - bytesPP] : 0;
                        const int p = a + b - c;
                        const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
                        const int pr = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                        cur[i] = static_cast<std::uint8_t>(cur[i] + pr);
                    }
                    break;
                default: return Fail(err, "bad PNG filter type");
            }
            // Expand this scanline to RGBA.
            std::uint8_t* dst = outp + (static_cast<std::size_t>(y0) + static_cast<std::size_t>(row) * dy) * in.w * 4 + static_cast<std::size_t>(x0) * 4;
            const std::size_t dstep = static_cast<std::size_t>(dx) * 4;
            const int depth = in.depth;
            switch (in.ctype) {
                case 0:
                    for (int i = 0; i < pw; ++i, dst += dstep) {
                        int v, g;
                        if (depth == 16) { v = (cur[2 * i] << 8) | cur[2 * i + 1]; g = cur[2 * i]; }
                        else if (depth == 8) { v = g = cur[i]; }
                        else {
                            const int per = 8 / depth, shift = 8 - depth * (i % per + 1);
                            v = (cur[i / per] >> shift) & ((1 << depth) - 1);
                            g = depth == 1 ? v * 255 : (depth == 2 ? v * 85 : v * 17);
                        }
                        dst[0] = dst[1] = dst[2] = static_cast<std::uint8_t>(g);
                        dst[3] = (haveKey && v == key[0]) ? 0 : 255;
                    }
                    break;
                case 2:
                    for (int i = 0; i < pw; ++i, dst += dstep) {
                        if (depth == 8) {
                            dst[0] = cur[3 * i]; dst[1] = cur[3 * i + 1]; dst[2] = cur[3 * i + 2];
                            dst[3] = (haveKey && dst[0] == key[0] && dst[1] == key[1] && dst[2] == key[2]) ? 0 : 255;
                        } else {
                            const std::uint8_t* s = cur + 6 * i;
                            dst[0] = s[0]; dst[1] = s[2]; dst[2] = s[4];
                            dst[3] = (haveKey && ((s[0] << 8) | s[1]) == key[0] && ((s[2] << 8) | s[3]) == key[1] && ((s[4] << 8) | s[5]) == key[2]) ? 0 : 255;
                        }
                    }
                    break;
                case 3:
                    for (int i = 0; i < pw; ++i, dst += dstep) {
                        int idx;
                        if (depth == 8) idx = cur[i];
                        else { const int per = 8 / depth, shift = 8 - depth * (i % per + 1); idx = (cur[i / per] >> shift) & ((1 << depth) - 1); }
                        if (idx < palCount) { dst[0] = pal[3 * idx]; dst[1] = pal[3 * idx + 1]; dst[2] = pal[3 * idx + 2]; }
                        else { dst[0] = dst[1] = dst[2] = 0; }
                        dst[3] = idx < trnsCount ? trnsAlpha[idx] : 255;
                    }
                    break;
                case 4:
                    for (int i = 0; i < pw; ++i, dst += dstep) {
                        const std::uint8_t* s = cur + (depth == 8 ? 2 : 4) * i;
                        dst[0] = dst[1] = dst[2] = s[0];
                        dst[3] = depth == 8 ? s[1] : s[2];
                    }
                    break;
                default:   // 6
                    for (int i = 0; i < pw; ++i, dst += dstep) {
                        if (depth == 8) { const std::uint8_t* s = cur + 4 * i; dst[0] = s[0]; dst[1] = s[1]; dst[2] = s[2]; dst[3] = s[3]; }
                        else { const std::uint8_t* s = cur + 8 * i; dst[0] = s[0]; dst[1] = s[2]; dst[2] = s[4]; dst[3] = s[6]; }
                    }
                    break;
            }
            prev = cur;
            off += stride + 1;
        }
    }
    W = in.w; H = in.h;
    return true;
}

// ================================================================================================
// BMP
// ================================================================================================
int HighBit(std::uint32_t v) { int i = -1; while (v) { v >>= 1; ++i; } return i; }
int BitCount(std::uint32_t v) { int c = 0; while (v) { c += v & 1; v >>= 1; } return c; }

// Extracts a masked channel and stretches it to 8 bits.
int ScaleChannel(std::uint32_t v, int shift, int bits) {
    static const unsigned mul[9] = {0, 0xff, 0x55, 0x49, 0x11, 0x21, 0x41, 0x81, 0x01};
    static const unsigned sh[9] = {0, 0, 0, 1, 0, 2, 4, 6, 0};
    if (shift < 0) v <<= -shift; else v >>= shift;
    v &= 0xFF;
    v >>= (8 - bits);
    return static_cast<int>((v * mul[bits]) >> sh[bits]);
}

bool DecodeBmp(const std::uint8_t* data, std::size_t size, int& W, int& H, std::vector<std::uint8_t>* rgba, std::string& err) {
    Rd r(data, size);
    if (r.U8() != 'B' || r.U8() != 'M') return Fail(err, "not a BMP");
    r.Skip(8);
    const std::uint32_t offset = r.U32le();
    const std::uint32_t hsz = r.U32le();
    if (!r.ok) return Fail(err, "truncated BMP");
    if (hsz != 12 && hsz != 40 && hsz != 56 && hsz != 108 && hsz != 124) return Fail(err, "unsupported BMP header");
    long long w, h;
    if (hsz == 12) { w = r.U16le(); h = r.U16le(); }
    else { w = static_cast<std::int32_t>(r.U32le()); h = static_cast<std::int32_t>(r.U32le()); }
    if (r.U16le() != 1) return Fail(err, "bad BMP");
    const int bpp = static_cast<int>(r.U16le());
    std::uint32_t compress = 0, clrUsed = 0;
    std::uint32_t mr = 0, mg = 0, mb = 0, ma = 0;
    std::size_t hdrEnd = 14 + hsz;   // where palette / extra masks start
    if (hsz != 12) {
        compress = r.U32le();
        if (compress == 1 || compress == 2) return Fail(err, "BMP RLE compression not supported");
        if (compress >= 4) return Fail(err, "unsupported BMP compression");
        if (compress == 3 && bpp != 16 && bpp != 32) return Fail(err, "bad BMP");
        r.Skip(12);
        clrUsed = r.U32le();
        r.Skip(4);
        if (hsz == 40 || hsz == 56) {
            if (hsz == 56) r.Skip(16);
            if (bpp == 16 || bpp == 32) {
                if (compress == 3) {
                    mr = r.U32le(); mg = r.U32le(); mb = r.U32le();
                    hdrEnd += 12;
                    if (mr == mg && mg == mb) return Fail(err, "bad BMP");
                }
            }
        } else {
            mr = r.U32le(); mg = r.U32le(); mb = r.U32le(); ma = r.U32le();
            r.Skip(4 + 48);
            if (hsz == 124) r.Skip(16);
        }
    }
    if (!r.ok) return Fail(err, "truncated BMP");
    bool defaultMasks = false;
    if (compress == 0) {   // masks are implied for plain RGB
        if (bpp == 16) { mr = 31u << 10; mg = 31u << 5; mb = 31u; ma = 0; defaultMasks = true; }
        else if (bpp == 32) { mr = 0xFFu << 16; mg = 0xFFu << 8; mb = 0xFFu; ma = 0xFFu << 24; defaultMasks = true; }
        else if (hsz < 108) { mr = mg = mb = ma = 0; }
    }
    const bool topDown = h < 0;
    if (topDown) h = -h;
    if (w < 1 || h < 1 || w > kMaxImageDimension || h > kMaxImageDimension) return Fail(err, "BMP dimensions out of range");
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) return Fail(err, "unsupported BMP bit depth");
    if (!rgba) { W = static_cast<int>(w); H = static_cast<int>(h); return true; }
    if (offset < hdrEnd || offset > size) return Fail(err, "bad BMP data offset");

    // Palette.
    std::uint8_t pal[256][3];
    int palCount = 0;
    if (bpp < 16) {
        const std::size_t entry = (hsz == 12) ? 3 : 4;
        std::size_t avail = (offset - hdrEnd) / entry;
        palCount = static_cast<int>(std::min<std::size_t>(avail, 256));
        if (clrUsed && clrUsed < static_cast<std::uint32_t>(palCount)) palCount = static_cast<int>(clrUsed);
        if (palCount == 0) return Fail(err, "BMP palette missing");
        for (int i = 0; i < palCount; ++i) {
            const std::uint8_t* e = data + hdrEnd + i * entry;
            pal[i][0] = e[2]; pal[i][1] = e[1]; pal[i][2] = e[0];
        }
    }
    if (!AllocRgba(static_cast<int>(w), static_cast<int>(h), *rgba, err)) return false;
    const int iw = static_cast<int>(w), ih = static_cast<int>(h);
    const std::size_t rowBytes = ((static_cast<std::size_t>(iw) * bpp + 31) / 32) * 4;
    if (static_cast<std::size_t>(size - offset) / rowBytes < static_cast<std::size_t>(ih)) {
        rgba->clear();
        return Fail(err, "truncated BMP pixel data");
    }
    int rsh = 0, gsh = 0, bsh = 0, ash = 0, rcnt = 0, gcnt = 0, bcnt = 0, acnt = 0;
    bool easy32 = false;
    if (bpp >= 16 && bpp != 24) {
        if (bpp == 32 && mb == 0xFF && mg == 0xFF00 && mr == 0x00FF0000 && ma == 0xFF000000) easy32 = true;
        if (!easy32) {
            if (!mr || !mg || !mb) { rgba->clear(); return Fail(err, "bad BMP channel masks"); }
            rsh = HighBit(mr) - 7; rcnt = BitCount(mr);
            gsh = HighBit(mg) - 7; gcnt = BitCount(mg);
            bsh = HighBit(mb) - 7; bcnt = BitCount(mb);
            ash = HighBit(ma) - 7; acnt = BitCount(ma);
            if (rcnt > 8 || gcnt > 8 || bcnt > 8 || acnt > 8) { rgba->clear(); return Fail(err, "bad BMP channel masks"); }
        }
    }
    // An all-zero alpha channel in a plain 32-bit BMP means "no alpha".
    unsigned allA = (bpp == 32 && defaultMasks) ? 0 : 255;
    for (int y = 0; y < ih; ++y) {
        const std::uint8_t* src = data + offset + static_cast<std::size_t>(y) * rowBytes;
        std::uint8_t* dst = rgba->data() + static_cast<std::size_t>(topDown ? y : ih - 1 - y) * iw * 4;
        for (int x = 0; x < iw; ++x, dst += 4) {
            if (bpp < 16) {
                int idx;
                if (bpp == 8) idx = src[x];
                else if (bpp == 4) idx = (src[x >> 1] >> ((x & 1) ? 0 : 4)) & 15;
                else idx = (src[x >> 3] >> (7 - (x & 7))) & 1;
                if (idx < palCount) { dst[0] = pal[idx][0]; dst[1] = pal[idx][1]; dst[2] = pal[idx][2]; }
                else dst[0] = dst[1] = dst[2] = 0;
                dst[3] = 255;
            } else if (bpp == 24) {
                dst[0] = src[3 * x + 2]; dst[1] = src[3 * x + 1]; dst[2] = src[3 * x]; dst[3] = 255;
            } else if (easy32) {
                dst[0] = src[4 * x + 2]; dst[1] = src[4 * x + 1]; dst[2] = src[4 * x]; dst[3] = src[4 * x + 3];
                allA |= dst[3];
            } else {
                std::uint32_t v;
                if (bpp == 16) v = src[2 * x] | (src[2 * x + 1] << 8);
                else v = src[4 * x] | (src[4 * x + 1] << 8) | (src[4 * x + 2] << 16) | (std::uint32_t(src[4 * x + 3]) << 24);
                dst[0] = static_cast<std::uint8_t>(ScaleChannel(v & mr, rsh, rcnt));
                dst[1] = static_cast<std::uint8_t>(ScaleChannel(v & mg, gsh, gcnt));
                dst[2] = static_cast<std::uint8_t>(ScaleChannel(v & mb, bsh, bcnt));
                const unsigned a = ma ? static_cast<unsigned>(ScaleChannel(v & ma, ash, acnt)) : 255u;
                dst[3] = static_cast<std::uint8_t>(a);
                allA |= a;
            }
        }
    }
    if (allA == 0) for (std::size_t i = 3; i < rgba->size(); i += 4) (*rgba)[i] = 255;
    W = iw; H = ih;
    return true;
}

// ================================================================================================
// TGA
// ================================================================================================
// Components per pixel for a TGA pixel size, or 0 when unsupported.
int TgaComp(int bits, bool grey, bool* rgb16) {
    if (rgb16) *rgb16 = false;
    switch (bits) {
        case 8: return 1;
        case 15: case 16:
            if (grey) return 2;
            if (rgb16) *rgb16 = true;
            return 3;
        case 24: return 3;
        case 32: return 4;
    }
    return 0;
}

bool TgaHeaderOk(const std::uint8_t* d, std::size_t n, int* w, int* h) {
    if (n < 18) return false;
    const int cmType = d[1], imType = d[2];
    if (cmType > 1) return false;
    const int cmBits = d[7];
    const int bits = d[16];
    int comp;
    if (cmType == 1) {
        if (imType != 1 && imType != 9) return false;
        if (cmBits != 8 && cmBits != 15 && cmBits != 16 && cmBits != 24 && cmBits != 32) return false;
        if (bits != 8 && bits != 16) return false;
        comp = TgaComp(cmBits, false, nullptr);
    } else {
        if (imType != 2 && imType != 3 && imType != 10 && imType != 11) return false;
        comp = TgaComp(bits, imType == 3 || imType == 11, nullptr);
    }
    if (!comp) return false;
    const int ww = d[12] | (d[13] << 8), hh = d[14] | (d[15] << 8);
    if (ww < 1 || hh < 1) return false;
    if (w) *w = ww;
    if (h) *h = hh;
    return true;
}

bool DecodeTga(const std::uint8_t* data, std::size_t size, int& W, int& H, std::vector<std::uint8_t>* rgba, std::string& err) {
    int w, h;
    if (!TgaHeaderOk(data, size, &w, &h)) return Fail(err, "not a TGA");
    if (w > kMaxImageDimension || h > kMaxImageDimension) return Fail(err, "TGA dimensions out of range");
    if (!rgba) { W = w; H = h; return true; }
    Rd r(data, size);
    const int idLen = r.U8();
    const int cmType = r.U8();
    int imType = r.U8();
    const int palStart = r.U16le();
    const int palLen = r.U16le();
    const int palBits = r.U8();
    r.Skip(4);   // origin
    r.Skip(4);   // size, already read
    const int bits = r.U8();
    const int desc = r.U8();
    const bool rle = imType >= 8;
    if (rle) imType -= 8;
    const bool indexed = cmType == 1;
    const bool topLeft = (desc >> 5) & 1;
    bool rgb16 = false;
    const int comp = indexed ? TgaComp(palBits, false, &rgb16) : TgaComp(bits, imType == 3, &rgb16);
    r.Skip(idLen);
    if (!r.ok) return Fail(err, "truncated TGA");
    if (!AllocRgba(w, h, *rgba, err)) return false;

    std::vector<std::uint8_t> palette;   // RGBA entries
    auto readPixel = [&](std::uint8_t* px) {   // reads one source pixel into RGBA
        std::uint8_t raw[4] = {0, 0, 0, 255};
        if (rgb16) {
            const std::uint32_t v = r.U16le();
            raw[0] = static_cast<std::uint8_t>((((v >> 10) & 31) * 255) / 31);
            raw[1] = static_cast<std::uint8_t>((((v >> 5) & 31) * 255) / 31);
            raw[2] = static_cast<std::uint8_t>(((v & 31) * 255) / 31);
        } else {
            std::uint8_t b[4] = {0, 0, 0, 0};
            for (int j = 0; j < comp; ++j) b[j] = static_cast<std::uint8_t>(r.U8());
            if (comp == 1) { raw[0] = raw[1] = raw[2] = b[0]; }
            else if (comp == 2) { raw[0] = raw[1] = raw[2] = b[0]; raw[3] = b[1]; }
            else { raw[0] = b[2]; raw[1] = b[1]; raw[2] = b[0]; raw[3] = comp == 4 ? b[3] : 255; }
        }
        std::memcpy(px, raw, 4);
    };
    if (indexed) {
        if (palLen == 0) return Fail(err, "TGA palette empty");
        // Entries sit right after the id field; the first-entry index only shifts the numbering.
        palette.resize(static_cast<std::size_t>(palLen) * 4);
        for (int i = 0; i < palLen; ++i) readPixel(&palette[static_cast<std::size_t>(i) * 4]);
        if (!r.ok) { rgba->clear(); return Fail(err, "truncated TGA palette"); }
    }
    (void)palStart;
    const std::size_t pixels = static_cast<std::size_t>(w) * h;
    std::uint8_t cur[4] = {0, 0, 0, 255};
    int runLeft = 0;
    bool repeating = false;
    for (std::size_t i = 0; i < pixels; ++i) {
        bool fetch = true;
        if (rle) {
            if (runLeft == 0) {
                const int cmd = r.U8();
                runLeft = 1 + (cmd & 127);
                repeating = (cmd >> 7) != 0;
            } else if (repeating) fetch = false;
        }
        if (fetch) {
            if (indexed) {
                int idx = (bits == 8) ? static_cast<int>(r.U8()) : static_cast<int>(r.U16le());
                if (idx >= palLen) idx = 0;
                std::memcpy(cur, &palette[static_cast<std::size_t>(idx) * 4], 4);
            } else readPixel(cur);
        }
        if (!r.ok) { rgba->clear(); return Fail(err, "truncated TGA pixel data"); }
        --runLeft;
        const int y = static_cast<int>(i / w), x = static_cast<int>(i % w);
        const int dy = topLeft ? y : h - 1 - y;
        std::memcpy(rgba->data() + (static_cast<std::size_t>(dy) * w + x) * 4, cur, 4);
    }
    W = w; H = h;
    return true;
}

// ================================================================================================
// Radiance HDR
// ================================================================================================
bool DecodeHdr(const std::uint8_t* data, std::size_t size, int& W, int& H, std::vector<std::uint8_t>* rgba, std::string& err) {
    Rd r(data, size);
    auto readLine = [&](std::string& line) {
        line.clear();
        while (r.Has(1)) {
            const char c = static_cast<char>(r.U8());
            if (c == '\n') return true;
            if (line.size() > 1024) return false;
            line.push_back(c);
        }
        return false;
    };
    std::string line;
    if (!readLine(line) || (line != "#?RADIANCE" && line != "#?RGBE")) return Fail(err, "not a Radiance HDR");
    bool formatOk = false;
    for (;;) {
        if (!readLine(line)) return Fail(err, "truncated HDR header");
        if (line.empty()) break;
        if (line == "FORMAT=32-bit_rle_rgbe") formatOk = true;
    }
    if (!formatOk) return Fail(err, "unsupported HDR format");
    if (!readLine(line)) return Fail(err, "truncated HDR header");
    int h = 0, w = 0;
    if (std::sscanf(line.c_str(), "-Y %d +X %d", &h, &w) != 2) { h = w = 0; }
    if (w < 1 || h < 1 || w > kMaxImageDimension || h > kMaxImageDimension) return Fail(err, "unsupported HDR orientation or size");
    if (!rgba) { W = w; H = h; return true; }
    if (!AllocRgba(w, h, *rgba, err)) return false;
    std::vector<std::uint8_t> scan(static_cast<std::size_t>(w) * 4);
    const float gamma = 1.0f / 2.2f;
    for (int y = 0; y < h; ++y) {
        bool flat = (w < 8 || w >= 32768);
        std::uint8_t first[4];
        if (!flat) {
            if (!r.Has(4)) return Fail(err, "truncated HDR data");
            for (int i = 0; i < 4; ++i) first[i] = static_cast<std::uint8_t>(r.U8());
            if (first[0] != 2 || first[1] != 2 || (first[2] & 0x80)) flat = true;
            else if (((first[2] << 8) | first[3]) != w) return Fail(err, "corrupt HDR scanline");
        }
        if (flat) {
            std::size_t start = 0;
            if (w >= 8 && w < 32768) { std::memcpy(scan.data(), first, 4); start = 4; }
            const std::size_t need = scan.size() - start;
            if (!r.Has(need)) return Fail(err, "truncated HDR data");
            std::memcpy(scan.data() + start, data + r.p, need);
            r.p += need;
        } else {
            for (int c = 0; c < 4; ++c) {
                int x = 0;
                while (x < w) {
                    int count = static_cast<int>(r.U8());
                    if (!r.ok) return Fail(err, "truncated HDR data");
                    if (count > 128) {
                        count -= 128;
                        const std::uint8_t v = static_cast<std::uint8_t>(r.U8());
                        if (!r.ok || count > w - x) return Fail(err, "corrupt HDR scanline");
                        while (count--) scan[static_cast<std::size_t>(x++) * 4 + c] = v;
                    } else {
                        if (count == 0 || count > w - x || !r.Has(static_cast<std::size_t>(count))) return Fail(err, "corrupt HDR scanline");
                        while (count--) scan[static_cast<std::size_t>(x++) * 4 + c] = static_cast<std::uint8_t>(r.U8());
                    }
                }
            }
        }
        std::uint8_t* dst = rgba->data() + static_cast<std::size_t>(y) * w * 4;
        for (int x = 0; x < w; ++x, dst += 4) {
            const std::uint8_t* s = &scan[static_cast<std::size_t>(x) * 4];
            if (s[3] == 0) { dst[0] = dst[1] = dst[2] = 0; dst[3] = 255; continue; }
            const float f = std::ldexp(1.0f, s[3] - (128 + 8));
            for (int k = 0; k < 3; ++k) {
                const float v = s[k] * f;
                float z = static_cast<float>(std::pow(static_cast<double>(v), static_cast<double>(gamma))) * 255.0f + 0.5f;
                if (z < 0) z = 0;
                if (z > 255) z = 255;
                dst[k] = static_cast<std::uint8_t>(z);
            }
            dst[3] = 255;
        }
    }
    W = w; H = h;
    return true;
}

// ================================================================================================
// PSD (flattened composite, RGB, 8 or 16 bit, raw or PackBits)
// ================================================================================================
bool DecodePsd(const std::uint8_t* data, std::size_t size, int& W, int& H, std::vector<std::uint8_t>* rgba, std::string& err) {
    Rd r(data, size);
    if (r.U32be() != 0x38425053) return Fail(err, "not a PSD");
    if (r.U16be() != 1) return Fail(err, "unsupported PSD version");
    r.Skip(6);
    const int channels = static_cast<int>(r.U16be());
    const std::uint32_t h = r.U32be(), w = r.U32be();
    const int depth = static_cast<int>(r.U16be());
    const int mode = static_cast<int>(r.U16be());
    if (!r.ok) return Fail(err, "truncated PSD");
    if (channels < 1 || channels > 16) return Fail(err, "unsupported PSD channel count");
    if (depth != 8 && depth != 16) return Fail(err, "PSD bit depth is not 8 or 16");
    if (mode != 3) return Fail(err, "PSD is not in RGB colour mode");
    if (w < 1 || h < 1 || w > static_cast<std::uint32_t>(kMaxImageDimension) || h > static_cast<std::uint32_t>(kMaxImageDimension))
        return Fail(err, "PSD dimensions out of range");
    if (!rgba) { W = static_cast<int>(w); H = static_cast<int>(h); return true; }
    for (int i = 0; i < 3; ++i) { const std::uint32_t l = r.U32be(); if (!r.Skip(l)) return Fail(err, "truncated PSD"); }
    const int compression = static_cast<int>(r.U16be());
    if (!r.ok || compression > 1) return Fail(err, "unsupported PSD compression");
    if (!AllocRgba(static_cast<int>(w), static_cast<int>(h), *rgba, err)) return false;
    const std::size_t pixels = static_cast<std::size_t>(w) * h;
    const std::size_t bps = depth / 8;
    std::vector<std::uint8_t> rowBuf(static_cast<std::size_t>(w) * bps);
    std::vector<std::uint16_t> rleLens;
    if (compression) {
        const std::size_t cnt = static_cast<std::size_t>(h) * channels;
        if (!r.Has(cnt * 2)) { rgba->clear(); return Fail(err, "truncated PSD"); }
        rleLens.resize(cnt);
        for (std::size_t i = 0; i < cnt; ++i) rleLens[i] = static_cast<std::uint16_t>(r.U16be());
    }
    for (int ch = 0; ch < 4; ++ch) {
        if (ch >= channels) {
            for (std::size_t i = 0; i < pixels; ++i) (*rgba)[i * 4 + ch] = (ch == 3) ? 255 : 0;
            continue;
        }
        for (std::uint32_t y = 0; y < h; ++y) {
            if (!compression) {
                if (!r.Has(rowBuf.size())) { rgba->clear(); return Fail(err, "truncated PSD"); }
                std::memcpy(rowBuf.data(), data + r.p, rowBuf.size());
                r.p += rowBuf.size();
            } else {
                // PackBits: a control byte n: 0..127 copy n+1 literal bytes, 129..255 repeat the next byte 257-n times.
                const std::size_t lenField = rleLens[static_cast<std::size_t>(ch) * h + y];
                if (!r.Has(lenField)) { rgba->clear(); return Fail(err, "truncated PSD"); }
                const std::uint8_t* s = data + r.p;
                std::size_t sp = 0, outp = 0;
                while (outp < rowBuf.size()) {
                    if (sp >= lenField) { rgba->clear(); return Fail(err, "corrupt PSD RLE data"); }
                    const int n = s[sp++];
                    if (n == 128) continue;
                    if (n < 128) {
                        const std::size_t cnt = static_cast<std::size_t>(n) + 1;
                        if (cnt > rowBuf.size() - outp || sp + cnt > lenField) { rgba->clear(); return Fail(err, "corrupt PSD RLE data"); }
                        std::memcpy(&rowBuf[outp], s + sp, cnt);
                        sp += cnt; outp += cnt;
                    } else {
                        const std::size_t cnt = 257 - static_cast<std::size_t>(n);
                        if (cnt > rowBuf.size() - outp || sp >= lenField) { rgba->clear(); return Fail(err, "corrupt PSD RLE data"); }
                        std::memset(&rowBuf[outp], s[sp++], cnt);
                        outp += cnt;
                    }
                }
                r.p += lenField;
            }
            std::uint8_t* dst = rgba->data() + static_cast<std::size_t>(y) * w * 4 + ch;
            for (std::uint32_t x = 0; x < w; ++x) dst[x * 4] = rowBuf[x * bps];   // 16-bit keeps the high byte
        }
    }
    // Un-matte: Photoshop stores the composite pre-blended against white where alpha < 1.
    if (channels >= 4) {
        for (std::size_t i = 0; i < pixels; ++i) {
            std::uint8_t* px = rgba->data() + i * 4;
            if (px[3] != 0 && px[3] != 255) {
                const float a = px[3] / 255.0f, ra = 1.0f / a, inv = 255.0f * (1.0f - ra);
                for (int k = 0; k < 3; ++k) {
                    // Round the product to a float before adding: a compiler that fuses the multiply and add (FMA) gets a
                    // slightly different value, which flips a truncated byte. The pixels must not depend on the CPU.
                    volatile float prod = px[k] * ra;
                    const float v = prod + inv;
                    px[k] = static_cast<std::uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
                }
            }
        }
    }
    W = static_cast<int>(w); H = static_cast<int>(h);
    return true;
}

// ================================================================================================
// GIF
// ================================================================================================
struct GifReader {
    const std::uint8_t* d;
    std::size_t n;
    Rd r;
    std::string& err;
    int w = 0, h = 0, bgIndex = 0;
    bool hasGlobal = false;
    std::uint8_t gpal[256][3];
    std::vector<std::uint8_t> canvas, background;   // RGBA canvases
    std::vector<std::uint8_t> history;
    int frameNo = 0;
    int prevDispose = 0;
    // Graphic control state for the next image.
    int gceFlags = 0, gceDelay = 0, gceTransparent = 0;
    bool gceTransp = false;

    GifReader(const std::uint8_t* data, std::size_t size, std::string& e) : d(data), n(size), r(data, size), err(e) {}

    bool Header() {
        if (n < 13 || std::memcmp(d, "GIF8", 4) != 0 || (d[4] != '7' && d[4] != '9') || d[5] != 'a') return Fail(err, "not a GIF");
        r.p = 6;
        w = static_cast<int>(r.U16le()); h = static_cast<int>(r.U16le());
        const int flags = static_cast<int>(r.U8());
        bgIndex = static_cast<int>(r.U8());
        r.U8();
        std::memset(gpal, 0, sizeof(gpal));
        if (w < 1 || h < 1 || w > kMaxImageDimension || h > kMaxImageDimension) return Fail(err, "GIF dimensions out of range");
        if (flags & 0x80) {
            const int cnt = 2 << (flags & 7);
            if (!r.Has(static_cast<std::size_t>(cnt) * 3)) return Fail(err, "truncated GIF");
            for (int i = 0; i < cnt; ++i) for (int k = 0; k < 3; ++k) gpal[i][k] = static_cast<std::uint8_t>(r.U8());
            hasGlobal = true;
        }
        return true;
    }

    // Decodes the next image into `canvas` (composited). Sets `got` false at the trailer.
    bool NextFrame(bool& got) {
        got = false;
        const std::size_t pcount = static_cast<std::size_t>(w) * h;
        if (frameNo == 0) {
            canvas.assign(pcount * 4, 0);
            background.assign(pcount * 4, 0);
            history.assign(pcount, 0);
        } else {
            // Disposal of the previous frame. Methods 2 (restore to background) and 3 (restore to
            // previous) both put back what was on the canvas before that frame was drawn, which is
            // exactly what `background` holds. (Method 1 and 0 leave the pixels in place.)
            if (prevDispose == 2 || prevDispose == 3) {
                for (std::size_t i = 0; i < pcount; ++i) if (history[i]) std::memcpy(&canvas[i * 4], &background[i * 4], 4);
            }
            background = canvas;
        }
        std::fill(history.begin(), history.end(), 0);
        for (;;) {
            const int tag = static_cast<int>(r.U8());
            if (!r.ok) return Fail(err, "truncated GIF");
            if (tag == 0x3B) return true;   // trailer
            if (tag == 0x21) {
                const int ext = static_cast<int>(r.U8());
                if (ext == 0xF9) {
                    const int len = static_cast<int>(r.U8());
                    if (len == 4) {
                        gceFlags = static_cast<int>(r.U8());
                        gceDelay = static_cast<int>(r.U16le()) * 10;
                        gceTransparent = static_cast<int>(r.U8());
                        gceTransp = (gceFlags & 1) != 0;
                    } else r.Skip(static_cast<std::size_t>(len));
                }
                for (;;) {   // sub-blocks
                    const int len = static_cast<int>(r.U8());
                    if (!r.ok) return Fail(err, "truncated GIF");
                    if (len == 0) break;
                    if (!r.Skip(static_cast<std::size_t>(len))) return Fail(err, "truncated GIF");
                }
                continue;
            }
            if (tag != 0x2C) return Fail(err, "corrupt GIF");
            const int fx = static_cast<int>(r.U16le()), fy = static_cast<int>(r.U16le());
            const int fw = static_cast<int>(r.U16le()), fh = static_cast<int>(r.U16le());
            const int lflags = static_cast<int>(r.U8());
            if (!r.ok) return Fail(err, "truncated GIF");
            if (fx + fw > w || fy + fh > h) return Fail(err, "corrupt GIF frame bounds");
            std::uint8_t lpal[256][3];
            const std::uint8_t (*pal)[3];
            if (lflags & 0x80) {
                const int cnt = 2 << (lflags & 7);
                if (!r.Has(static_cast<std::size_t>(cnt) * 3)) return Fail(err, "truncated GIF");
                for (int i = 0; i < cnt; ++i) for (int k = 0; k < 3; ++k) lpal[i][k] = static_cast<std::uint8_t>(r.U8());
                for (int i = cnt; i < 256; ++i) lpal[i][0] = lpal[i][1] = lpal[i][2] = 0;
                pal = lpal;
            } else if (hasGlobal) pal = gpal;
            else return Fail(err, "GIF has no colour table");
            const int minCode = static_cast<int>(r.U8());
            if (!r.ok || minCode < 1 || minCode > 11) return Fail(err, "corrupt GIF LZW size");
            // Gather the sub-blocks.
            std::vector<std::uint8_t> blob;
            for (;;) {
                const int len = static_cast<int>(r.U8());
                if (!r.ok) return Fail(err, "truncated GIF");
                if (len == 0) break;
                if (!r.Has(static_cast<std::size_t>(len))) return Fail(err, "truncated GIF");
                blob.insert(blob.end(), d + r.p, d + r.p + len);
                r.p += static_cast<std::size_t>(len);
            }
            if (!Lzw(blob, minCode, pal, fx, fy, fw, fh, (lflags & 0x40) != 0)) return false;
            if (frameNo == 0 && bgIndex > 0) {
                for (std::size_t i = 0; i < pcount; ++i) {
                    if (!history[i]) {
                        canvas[i * 4] = gpal[bgIndex][0]; canvas[i * 4 + 1] = gpal[bgIndex][1];
                        canvas[i * 4 + 2] = gpal[bgIndex][2]; canvas[i * 4 + 3] = 255;
                    }
                }
            }
            prevDispose = (gceFlags & 0x1C) >> 2;
            frameDelay = gceDelay;
            gceFlags = 0; gceDelay = 0; gceTransp = false;
            ++frameNo;
            got = true;
            return true;
        }
    }
    int frameDelay = 0;

    bool Lzw(const std::vector<std::uint8_t>& blob, int minCode, const std::uint8_t (*pal)[3], int fx, int fy, int fw, int fh, bool interlaced) {
        const int clear = 1 << minCode, eoi = clear + 1;
        std::uint16_t prefix[4096];
        std::uint8_t suffix[4096], firstc[4096];
        for (int i = 0; i < clear; ++i) { prefix[i] = 0xFFFF; suffix[i] = firstc[i] = static_cast<std::uint8_t>(i); }
        int avail = clear + 2, codeSize = minCode + 1, prev = -1;
        std::uint32_t bits = 0;
        int nbits = 0;
        std::size_t bp = 0;
        int cx = 0, cy = 0;   // position within the frame
        auto rowFor = [&](int y) {      // frame row -> destination row for interlaced images
            if (!interlaced) return y;
            // Pass 1: rows 0,8,..; pass 2: 4,12,..; pass 3: 2,6,..; pass 4: 1,3,..
            int cnt1 = (fh + 7) / 8, cnt2 = (fh + 3) / 8, cnt3 = (fh + 1) / 4;
            if (y < cnt1) return y * 8;
            y -= cnt1;
            if (y < cnt2) return 4 + y * 8;
            y -= cnt2;
            if (y < cnt3) return 2 + y * 4;
            y -= cnt3;
            return 1 + y * 2;
        };
        std::uint8_t stack[4097];
        auto put = [&](int idx) {
            if (cy >= fh) return;
            const int dy = fy + rowFor(cy);
            const std::size_t pos = static_cast<std::size_t>(dy) * w + (fx + cx);
            history[pos] = 1;
            const bool transparent = gceTransp && idx == gceTransparent;
            if (!transparent) {
                canvas[pos * 4] = pal[idx][0]; canvas[pos * 4 + 1] = pal[idx][1]; canvas[pos * 4 + 2] = pal[idx][2]; canvas[pos * 4 + 3] = 255;
            }
            if (++cx >= fw) { cx = 0; ++cy; }
        };
        auto emit = [&](int code) {
            int sp = 0;
            for (int c = code; c != 0xFFFF && sp < 4097; c = prefix[c]) stack[sp++] = suffix[c];
            while (sp > 0) put(stack[--sp]);
        };
        if (fw == 0 || fh == 0) return true;
        for (;;) {
            while (nbits < codeSize) {
                if (bp >= blob.size()) { bits = 0; goto done; }
                bits |= static_cast<std::uint32_t>(blob[bp++]) << nbits;
                nbits += 8;
            }
            {
                const int code = static_cast<int>(bits & ((1u << codeSize) - 1));
                bits >>= codeSize; nbits -= codeSize;
                if (code == clear) { avail = clear + 2; codeSize = minCode + 1; prev = -1; continue; }
                if (code == eoi) break;
                if (prev < 0) {
                    if (code >= clear) return Fail(err, "corrupt GIF LZW data");
                    emit(code);
                    prev = code;
                    continue;
                }
                int emitCode;
                if (code < avail) {
                    emitCode = code;
                    if (avail < 4096) {
                        prefix[avail] = static_cast<std::uint16_t>(prev); suffix[avail] = firstc[code]; firstc[avail] = firstc[prev];
                        ++avail;
                    }
                } else if (code == avail && avail < 4096) {
                    prefix[avail] = static_cast<std::uint16_t>(prev); suffix[avail] = firstc[prev]; firstc[avail] = firstc[prev];
                    emitCode = avail++;
                } else return Fail(err, "corrupt GIF LZW data");
                emit(emitCode);
                prev = code;
                if (avail == (1 << codeSize) && codeSize < 12) ++codeSize;
            }
        }
    done:
        return true;
    }
};

bool DecodeGifFirst(const std::uint8_t* data, std::size_t size, int& W, int& H, std::vector<std::uint8_t>* rgba, std::string& err) {
    GifReader g(data, size, err);
    if (!g.Header()) return false;
    if (!rgba) { W = g.w; H = g.h; return true; }
    if (static_cast<std::size_t>(g.w) * g.h * 4 > kMaxPixelBytes) return Fail(err, "GIF too large");
    bool got = false;
    if (!g.NextFrame(got)) return false;
    if (!got) return Fail(err, "GIF has no frames");
    *rgba = std::move(g.canvas);
    W = g.w; H = g.h;
    return true;
}

} // namespace

bool DecodeGifFrames(const std::uint8_t* data, std::size_t size, DecodedAnimation& out, std::string* error) {
    std::string err;
    out = DecodedAnimation();
    GifReader g(data, size, err);
    bool ok = g.Header();
    if (ok && !SizePlausible(g.w, g.h, size)) ok = Fail(err, "GIF header claims more pixels than the file can hold");
    if (ok) {
        const std::size_t frameBytes = static_cast<std::size_t>(g.w) * g.h * 4;
        std::vector<std::uint8_t> frames;
        std::vector<int> delays;
        for (;;) {
            bool got = false;
            if (!g.NextFrame(got)) { ok = false; break; }
            if (!got) break;
            if ((delays.size() + 1) * frameBytes > kMaxAnimationBytes) { ok = Fail(err, "GIF animation too large"); break; }
            frames.insert(frames.end(), g.canvas.begin(), g.canvas.end());
            delays.push_back(g.frameDelay);
        }
        if (ok && delays.empty()) ok = Fail(err, "GIF has no frames");
        if (ok) { out.width = g.w; out.height = g.h; out.frames = std::move(frames); out.delaysMs = std::move(delays); }
    }
    if (!ok && error) *error = err.empty() ? "GIF decode failed" : err;
    return ok;
}

ImageFormat DetectImageFormat(const std::uint8_t* d, std::size_t n) {
    static const std::uint8_t png[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (n >= 8 && std::memcmp(d, png, 8) == 0) return ImageFormat::PNG;
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8) return ImageFormat::JPEG;
    if (n >= 6 && std::memcmp(d, "GIF8", 4) == 0 && (d[4] == '7' || d[4] == '9') && d[5] == 'a') return ImageFormat::GIF;
    if (n >= 2 && d[0] == 'B' && d[1] == 'M') return ImageFormat::BMP;
    if (n >= 4 && std::memcmp(d, "8BPS", 4) == 0) return ImageFormat::PSD;
    if ((n >= 10 && std::memcmp(d, "#?RADIANCE", 10) == 0) || (n >= 6 && std::memcmp(d, "#?RGBE", 6) == 0)) return ImageFormat::HDR;
    if (TgaHeaderOk(d, n, nullptr, nullptr)) return ImageFormat::TGA;
    return ImageFormat::Unknown;
}

namespace {
bool Dispatch(const std::uint8_t* data, std::size_t size, ImageFormat fmt, int& w, int& h, std::vector<std::uint8_t>* rgba, std::string& err) {
    switch (fmt) {
        case ImageFormat::PNG: return DecodePng(data, size, w, h, rgba, err);
        case ImageFormat::JPEG: return imgdec::DecodeJpeg(data, size, &w, &h, rgba, err);
        case ImageFormat::BMP: return DecodeBmp(data, size, w, h, rgba, err);
        case ImageFormat::TGA: return DecodeTga(data, size, w, h, rgba, err);
        case ImageFormat::GIF: return DecodeGifFirst(data, size, w, h, rgba, err);
        case ImageFormat::HDR: return DecodeHdr(data, size, w, h, rgba, err);
        case ImageFormat::PSD: return DecodePsd(data, size, w, h, rgba, err);
        default: break;
    }
    return Fail(err, "unrecognised image format");
}
} // namespace

bool GetImageInfo(const std::uint8_t* data, std::size_t size, int* width, int* height, ImageFormat* format) {
    const ImageFormat fmt = DetectImageFormat(data, size);
    if (format) *format = fmt;
    int w = 0, h = 0;
    std::string err;
    if (!Dispatch(data, size, fmt, w, h, nullptr, err)) return false;
    if (width) *width = w;
    if (height) *height = h;
    return true;
}

bool DecodeImage(const std::uint8_t* data, std::size_t size, DecodedImage& out, std::string* error) {
    out = DecodedImage();
    std::string err;
    int w = 0, h = 0;
    if (data == nullptr || size == 0) { if (error) *error = "empty image data"; return false; }
    const ImageFormat fmt = DetectImageFormat(data, size);
    std::vector<std::uint8_t> rgba;
    if (Dispatch(data, size, fmt, w, h, nullptr, err) && !SizePlausible(w, h, size)) {
        if (error) *error = "image header claims more pixels than the file can hold";
        return false;
    }
    err.clear();
    if (!Dispatch(data, size, fmt, w, h, &rgba, err)) {
        if (error) *error = err.empty() ? "image decode failed" : err;
        return false;
    }
    out.width = w; out.height = h; out.rgba = std::move(rgba);
    return true;
}

namespace {
bool ReadWholeFile(const std::string& path, std::vector<std::uint8_t>& buf, std::string* error, std::size_t maxBytes) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { if (error) *error = "cannot open file"; return false; }
    bool ok = false;
    if (std::fseek(f, 0, SEEK_END) == 0) {
        const long len = std::ftell(f);
        if (len >= 0 && static_cast<unsigned long long>(len) <= maxBytes && std::fseek(f, 0, SEEK_SET) == 0) {
            buf.resize(static_cast<std::size_t>(len));
            ok = buf.empty() || std::fread(buf.data(), 1, buf.size(), f) == buf.size();
        }
    }
    std::fclose(f);
    if (!ok && error) *error = "cannot read file";
    return ok;
}
} // namespace

bool DecodeImageFile(const std::string& path, DecodedImage& out, std::string* error) {
    out = DecodedImage();
    std::vector<std::uint8_t> buf;
    if (!ReadWholeFile(path, buf, error, std::size_t(1) << 30)) return false;
    return DecodeImage(buf.data(), buf.size(), out, error);
}

bool GetImageFileInfo(const std::string& path, int* width, int* height, ImageFormat* format) {
    std::vector<std::uint8_t> buf;
    if (!ReadWholeFile(path, buf, nullptr, std::size_t(1) << 30)) return false;
    return GetImageInfo(buf.data(), buf.size(), width, height, format);
}

} // namespace okay
