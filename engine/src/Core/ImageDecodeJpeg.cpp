// JPEG decoder: baseline, extended-sequential and progressive Huffman (ITU T.81), written
// from the specification. 8-bit samples, 1 (grey) or 3 (YCbCr / RGB) components, any
// sampling factors, restart intervals, multiple scans. Output is RGBA8.
//
// Reconstruction: exact floating point inverse DCT (rounded), libjpeg-style "fancy"
// (triangle) chroma upsampling for 2:1 horizontal and/or vertical subsampling, box
// replication for other ratios, fixed-point JFIF YCbCr -> RGB.
#include "ImageDecodeInternal.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace okay {
namespace imgdec {
namespace {

const std::uint8_t kZig[64] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
    12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

// ---- Huffman table ---------------------------------------------------------------------------
constexpr int kFastBits = 9;

struct Huff {
    bool valid = false;
    std::uint16_t fast[1 << kFastBits];   // (length << 8) | symbol, 0 when the code is longer
    int maxcode[17];                      // largest code of each length, -1 when none
    int mincode[17];
    int valptr[17];
    std::uint8_t symbols[256];
};

bool BuildHuff(Huff& h, const std::uint8_t counts[16], const std::uint8_t* syms, int nsyms) {
    std::memset(h.fast, 0, sizeof(h.fast));
    std::memcpy(h.symbols, syms, static_cast<std::size_t>(nsyms));
    int code = 0, k = 0;
    for (int l = 1; l <= 16; ++l) {
        h.valptr[l] = k;
        h.mincode[l] = code;
        const int n = counts[l - 1];
        if (code + n > (1 << l)) return false;   // over-subscribed
        h.maxcode[l] = n ? code + n - 1 : -1;
        if (l <= kFastBits) {
            for (int i = 0; i < n; ++i) {
                const int c = code + i;
                const int first = c << (kFastBits - l);
                const int count = 1 << (kFastBits - l);
                for (int j = 0; j < count; ++j)
                    h.fast[first + j] = static_cast<std::uint16_t>((l << 8) | h.symbols[k + i]);
            }
        }
        k += n;
        code = (code + n) << 1;
    }
    h.valid = true;
    return true;
}

// ---- entropy-coded segment bit reader ------------------------------------------------------------
struct BitReader {
    const std::uint8_t* p = nullptr;
    const std::uint8_t* end = nullptr;
    std::uint64_t acc = 0;
    int bits = 0;        // valid bits in acc (low end)
    int fake = 0;        // how many of those are zero padding invented after the data ended
    bool stopped = false;   // reached a marker or the end of the data

    void Reset(const std::uint8_t* start, const std::uint8_t* e) { p = start; end = e; acc = 0; bits = 0; fake = 0; stopped = false; }

    void Fill() {
        while (bits <= 56) {
            std::uint32_t b = 0;
            if (!stopped) {
                if (p >= end) stopped = true;
                else {
                    b = *p;
                    if (b == 0xFF) {
                        const std::uint8_t n = (p + 1 < end) ? p[1] : 0xD9;   // lone FF at the very end acts as a marker
                        if (n == 0x00) p += 2;
                        else if (n == 0xFF) { ++p; continue; }              // fill byte
                        else stopped = true;                              // a real marker
                        if (stopped) b = 0;
                    } else ++p;
                }
            }
            if (stopped) { b = 0; fake += 8; }
            acc = (acc << 8) | b;
            bits += 8;
        }
    }
    bool Overrun() const { return bits < fake; }
    int Peek(int n) { return static_cast<int>((acc >> (bits - n)) & ((1u << n) - 1)); }
    int Get(int n) {   // n in 0..16
        if (n == 0) return 0;
        if (bits < n) Fill();
        const int v = Peek(n);
        bits -= n;
        return v;
    }
    int Bit() { if (bits < 1) Fill(); return Get(1); }
};

inline int Extend(int v, int n) { return v < (1 << (n - 1)) ? v - (1 << n) + 1 : v; }

// Returns the decoded symbol or -1.
inline int HuffDecode(BitReader& br, const Huff& h) {
    if (br.bits < 16) br.Fill();
    const int look = br.Peek(kFastBits);
    const int e = h.fast[look];
    if (e) { br.bits -= e >> 8; return e & 255; }
    for (int l = kFastBits + 1; l <= 16; ++l) {
        const int code = br.Peek(l);
        if (code <= h.maxcode[l]) {
            br.bits -= l;
            return h.symbols[h.valptr[l] + code - h.mincode[l]];
        }
    }
    return -1;
}

// ---- inverse DCT -------------------------------------------------------------------------------------
struct IdctTables {
    float c[8][8];   // c[u][x] = 0.5 * C(u) * cos((2x+1) u pi / 16)
    IdctTables() {
        const double pi = 3.14159265358979323846;
        for (int u = 0; u < 8; ++u)
            for (int x = 0; x < 8; ++x)
                c[u][x] = static_cast<float>(0.5 * (u == 0 ? 0.70710678118654752440 : 1.0) * std::cos((2 * x + 1) * u * pi / 16.0));
    }
};
const IdctTables& Tables() { static const IdctTables t; return t; }

// Round to nearest, level-shift by 128 and clamp to 0..255. The input is clamped first so a
// corrupt stream with absurd coefficients can never overflow the float-to-int conversion.
inline std::uint8_t ClampPix(float v) {
    if (v < -128.0f) v = -128.0f;
    if (v > 127.4999f) v = 127.4999f;
    return static_cast<std::uint8_t>(static_cast<int>(v + 128.5f));   // always positive: truncation is floor
}

// `coef` is natural order and not yet dequantised; `q` is the natural-order quantiser.
void IdctBlock(const std::int16_t* coef, const std::uint16_t* q, std::uint8_t* dst, int stride) {
    bool dcOnly = true;
    for (int i = 1; i < 64; ++i) if (coef[i]) { dcOnly = false; break; }
    if (dcOnly) {
        const std::uint8_t v = ClampPix(static_cast<float>(coef[0]) * q[0] * 0.125f);
        for (int y = 0; y < 8; ++y) std::memset(dst + y * stride, v, 8);
        return;
    }
    const float(*c)[8] = Tables().c;
    float tmp[64];
    // Rows: tmp[v*8 + x] = sum_u F[v][u] * c[u][x]
    for (int v = 0; v < 8; ++v) {
        float f[8];
        bool zero = true;
        for (int u = 0; u < 8; ++u) {
            f[u] = static_cast<float>(coef[v * 8 + u]) * static_cast<float>(q[v * 8 + u]);
            if (u && f[u] != 0.0f) zero = false;
        }
        float* o = tmp + v * 8;
        if (zero) { const float d = f[0] * c[0][0]; for (int x = 0; x < 8; ++x) o[x] = d; continue; }
        for (int x = 0; x < 4; ++x) {
            const float e = f[0] * c[0][x] + f[2] * c[2][x] + f[4] * c[4][x] + f[6] * c[6][x];
            const float od = f[1] * c[1][x] + f[3] * c[3][x] + f[5] * c[5][x] + f[7] * c[7][x];
            o[x] = e + od;
            o[7 - x] = e - od;
        }
    }
    // Columns.
    for (int x = 0; x < 8; ++x) {
        float out[8];
        for (int y = 0; y < 4; ++y) {
            const float e = tmp[0 * 8 + x] * c[0][y] + tmp[2 * 8 + x] * c[2][y] + tmp[4 * 8 + x] * c[4][y] + tmp[6 * 8 + x] * c[6][y];
            const float od = tmp[1 * 8 + x] * c[1][y] + tmp[3 * 8 + x] * c[3][y] + tmp[5 * 8 + x] * c[5][y] + tmp[7 * 8 + x] * c[7][y];
            out[y] = e + od;
            out[7 - y] = e - od;
        }
        for (int y = 0; y < 8; ++y) dst[y * stride + x] = ClampPix(out[y]);
    }
}

// ---- decoder -------------------------------------------------------------------------------------------
struct Comp {
    int id = 0, h = 1, v = 1, tq = 0;
    int w = 0, ht = 0;          // real sample dimensions
    int bw = 0, bh = 0;         // blocks in the MCU-padded grid
    int sbw = 0, sbh = 0;       // blocks covering the real samples (non-interleaved scans)
    int stride = 0;
    std::vector<std::uint8_t> plane;
    std::vector<std::int16_t> coef;   // progressive only
    std::uint16_t q[64];
    bool qLatched = false;
    int pred = 0;
};

class Decoder {
public:
    Decoder(const std::uint8_t* d, std::size_t n, std::string& err) : d_(d), n_(n), err_(err) {}

    bool Run(int* width, int* height, std::vector<std::uint8_t>* rgba) {
        const bool infoOnly = (rgba == nullptr);
        infoOnly_ = infoOnly;
        if (n_ < 4 || d_[0] != 0xFF || d_[1] != 0xD8) return Fail("not a JPEG");
        pos_ = 2;
        for (;;) {
            const int m = NextMarker();
            if (m < 0) break;   // end of data
            if (m == 0xD9) break;   // EOI
            if (m == 0xD8 || (m >= 0xD0 && m <= 0xD7) || m == 0x01) continue;   // standalone
            std::size_t segLen = 0;
            if (!SegmentLength(segLen)) return false;
            const std::uint8_t* seg = d_ + pos_ + 2;
            const std::size_t body = segLen - 2;
            switch (m) {
                case 0xC0: case 0xC1: case 0xC2:
                    if (!ParseSof(m, seg, body)) return false;
                    if (infoOnly) { *width = W_; *height = H_; return true; }
                    break;
                case 0xC3: case 0xC5: case 0xC6: case 0xC7: case 0xC8: case 0xC9: case 0xCA: case 0xCB:
                case 0xCD: case 0xCE: case 0xCF:
                    return Fail("unsupported JPEG process (lossless, hierarchical or arithmetic coding)");
                case 0xC4: if (!ParseDht(seg, body)) return false; break;
                case 0xCC: break;   // arithmetic conditioning: irrelevant to Huffman files
                case 0xDB: if (!ParseDqt(seg, body)) return false; break;
                case 0xDD:
                    if (body < 2) return Fail("bad DRI");
                    ri_ = (seg[0] << 8) | seg[1];
                    break;
                case 0xE0:
                    if (body >= 5 && std::memcmp(seg, "JFIF\0", 5) == 0) jfif_ = true;
                    break;
                case 0xEE:
                    if (body >= 12 && std::memcmp(seg, "Adobe", 5) == 0) adobe_ = seg[11];
                    break;
                case 0xDA: {
                    if (!seenSof_) return Fail("scan before frame header");
                    if (!ParseSosAndDecode(seg, body)) return false;
                    pos_ = scanEnd_;
                    ++scans_;
                    continue;   // pos_ already points at the next marker
                }
                default: break;   // APPn, COM, DNL...
            }
            pos_ += segLen;
        }
        if (infoOnly) return Fail("no frame header");
        if (!seenSof_) return Fail("no frame header");
        if (scans_ == 0) return Fail("no image data");
        if (progressive_) FinishProgressive();
        *width = W_; *height = H_;
        return Convert(*rgba);
    }

private:
    bool Fail(const char* m) { if (err_.empty()) err_ = m; return false; }

    // Finds the next marker byte pair. Returns -1 at the end of the data.
    int NextMarker() {
        while (pos_ < n_) {
            if (d_[pos_] != 0xFF) { ++pos_; continue; }
            std::size_t p = pos_ + 1;
            while (p < n_ && d_[p] == 0xFF) ++p;
            if (p >= n_) { pos_ = n_; return -1; }
            const int m = d_[p];
            pos_ = p + 1;
            if (m == 0x00) continue;   // stuffed byte outside a scan: junk
            return m;
        }
        return -1;
    }
    bool SegmentLength(std::size_t& len) {
        if (pos_ + 2 > n_) return Fail("truncated JPEG");
        len = (static_cast<std::size_t>(d_[pos_]) << 8) | d_[pos_ + 1];
        if (len < 2 || pos_ + len > n_) return Fail("truncated JPEG segment");
        return true;
    }

    bool ParseDqt(const std::uint8_t* s, std::size_t n) {
        while (n > 0) {
            const int pq = s[0] >> 4, tq = s[0] & 15;
            if (pq > 1 || tq > 3) return Fail("bad DQT");
            const std::size_t need = 1 + (pq ? 128 : 64);
            if (n < need) return Fail("bad DQT");
            for (int i = 0; i < 64; ++i) {
                const int v = pq ? ((s[1 + 2 * i] << 8) | s[2 + 2 * i]) : s[1 + i];
                qt_[tq][kZig[i]] = static_cast<std::uint16_t>(v);
            }
            qtValid_[tq] = true;
            s += need; n -= need;
        }
        return true;
    }

    bool ParseDht(const std::uint8_t* s, std::size_t n) {
        while (n > 0) {
            if (n < 17) return Fail("bad DHT");
            const int tc = s[0] >> 4, th = s[0] & 15;
            if (tc > 1 || th > 3) return Fail("bad DHT");
            int total = 0;
            for (int i = 0; i < 16; ++i) total += s[1 + i];
            if (total > 256 || n < static_cast<std::size_t>(17 + total)) return Fail("bad DHT");
            Huff& h = (tc == 0 ? dc_[th] : ac_[th]);
            h.valid = false;
            if (!BuildHuff(h, s + 1, s + 17, total)) return Fail("bad Huffman table");
            s += 17 + total; n -= 17 + total;
        }
        return true;
    }

    bool ParseSof(int marker, const std::uint8_t* s, std::size_t n) {
        if (seenSof_) return Fail("multiple frame headers");
        if (n < 6) return Fail("bad SOF");
        if (s[0] != 8) return Fail("unsupported JPEG sample precision (only 8-bit)");
        H_ = (s[1] << 8) | s[2];
        W_ = (s[3] << 8) | s[4];
        const int nc = s[5];
        if (W_ < 1 || H_ < 1) return Fail("bad JPEG dimensions");
        if (W_ > 16384 || H_ > 16384) return Fail("JPEG too large");
        if (nc == 4) return Fail("CMYK/YCCK JPEG not supported");
        if (nc != 1 && nc != 3) return Fail("unsupported JPEG component count");
        if (n < static_cast<std::size_t>(6 + 3 * nc)) return Fail("bad SOF");
        progressive_ = (marker == 0xC2);
        hmax_ = vmax_ = 1;
        for (int i = 0; i < nc; ++i) {
            Comp& c = comp_[i];
            c.id = s[6 + 3 * i];
            c.h = s[7 + 3 * i] >> 4;
            c.v = s[7 + 3 * i] & 15;
            c.tq = s[8 + 3 * i];
            if (c.h < 1 || c.h > 4 || c.v < 1 || c.v > 4 || c.tq > 3) return Fail("bad JPEG component");
            hmax_ = std::max(hmax_, c.h);
            vmax_ = std::max(vmax_, c.v);
        }
        for (int i = 0; i < nc; ++i)
            for (int j = i + 1; j < nc; ++j)
                if (comp_[i].id == comp_[j].id) return Fail("duplicate JPEG component id");
        nc_ = nc;
        if (nc == 1) { comp_[0].h = comp_[0].v = 1; hmax_ = vmax_ = 1; }   // a lone component is never subsampled
        mcux_ = (W_ + 8 * hmax_ - 1) / (8 * hmax_);
        mcuy_ = (H_ + 8 * vmax_ - 1) / (8 * vmax_);
        for (int i = 0; i < nc; ++i) {
            Comp& c = comp_[i];
            c.w = (W_ * c.h + hmax_ - 1) / hmax_;
            c.ht = (H_ * c.v + vmax_ - 1) / vmax_;
            c.bw = mcux_ * c.h;
            c.bh = mcuy_ * c.v;
            c.sbw = (c.w + 7) / 8;
            c.sbh = (c.ht + 7) / 8;
            c.stride = c.bw * 8;
            if (infoOnly_) continue;
            const std::size_t samples = static_cast<std::size_t>(c.bw) * 8 * static_cast<std::size_t>(c.bh) * 8;
            if (samples > (std::size_t(1) << 31)) return Fail("JPEG too large");
            if (progressive_) c.coef.assign(static_cast<std::size_t>(c.bw) * c.bh * 64, 0);
            else c.plane.assign(samples, 128);
        }
        seenSof_ = true;
        return true;
    }

    // ---- scans ----
    struct ScanComp { Comp* c; const Huff* dc; const Huff* ac; };

    bool ParseSosAndDecode(const std::uint8_t* s, std::size_t n) {
        if (n < 1) return Fail("bad SOS");
        const int ns = s[0];
        if (ns < 1 || ns > nc_ || n < static_cast<std::size_t>(1 + 2 * ns + 3)) return Fail("bad SOS");
        ScanComp sc[3];
        for (int i = 0; i < ns; ++i) {
            const int cid = s[1 + 2 * i];
            Comp* c = nullptr;
            for (int k = 0; k < nc_; ++k) if (comp_[k].id == cid) c = &comp_[k];
            if (!c) return Fail("scan refers to unknown component");
            for (int k = 0; k < i; ++k) if (sc[k].c == c) return Fail("component repeated in scan");
            const int td = s[2 + 2 * i] >> 4, ta = s[2 + 2 * i] & 15;
            if (td > 3 || ta > 3) return Fail("bad SOS table selector");
            sc[i] = {c, &dc_[td], &ac_[ta]};
        }
        const int ss = s[1 + 2 * ns], se = s[2 + 2 * ns];
        const int ah = s[3 + 2 * ns] >> 4, al = s[3 + 2 * ns] & 15;
        mode_ = 0;
        ss_ = 0; se_ = 63; al_ = 0;
        if (progressive_) {
            if (ss > se || se > 63 || ah > 13 || al > 13) return Fail("bad progressive scan parameters");
            if (ss == 0 && se != 0) return Fail("bad progressive scan parameters");
            if (ss > 0 && ns != 1) return Fail("progressive AC scan must have one component");
            ss_ = ss; se_ = se; al_ = al;
            if (ss == 0) mode_ = (ah == 0) ? 1 : 2;       // DC first / DC refine
            else mode_ = (ah == 0) ? 3 : 4;                // AC first / AC refine
        }
        for (int i = 0; i < ns; ++i) {
            Comp& c = *sc[i].c;
            if (!c.qLatched) {
                if (!qtValid_[c.tq]) return Fail("missing quantisation table");
                std::memcpy(c.q, qt_[c.tq], sizeof(c.q));
                c.qLatched = true;
            }
            const bool needDc = (!progressive_) || mode_ == 1;
            const bool needAc = (!progressive_) || mode_ == 3 || mode_ == 4;
            if (needDc && !sc[i].dc->valid) return Fail("missing Huffman table");
            if (needAc && !sc[i].ac->valid) return Fail("missing Huffman table");
        }
        const std::size_t dataStart = pos_ + 2 + n;   // just past this segment
        return DecodeScan(sc, ns, dataStart);
    }

    bool DecodeScan(ScanComp* sc, int ns, std::size_t dataStart) {
        BitReader br;
        br.Reset(d_ + dataStart, d_ + n_);
        for (int i = 0; i < ns; ++i) sc[i].c->pred = 0;
        eobrun_ = 0;
        int total, nx = 0;
        if (ns == 1) { nx = sc[0].c->sbw; total = nx * sc[0].c->sbh; }
        else total = mcux_ * mcuy_;
        for (int i = 0; i < total; ++i) {
            if (ns == 1) {
                if (!DecodeBlock(br, sc[0], i % nx, i / nx)) return false;
            } else {
                const int mx = i % mcux_, my = i / mcux_;
                for (int k = 0; k < ns; ++k) {
                    Comp& c = *sc[k].c;
                    for (int v = 0; v < c.v; ++v)
                        for (int h = 0; h < c.h; ++h)
                            if (!DecodeBlock(br, sc[k], mx * c.h + h, my * c.v + v)) return false;
                }
            }
            if (br.Overrun()) return Fail("truncated JPEG data");
            if (ri_ && (i + 1) % ri_ == 0 && i + 1 < total) {
                // Restart: drop remaining bits, find and consume the RSTn marker.
                const std::uint8_t* p = br.p;
                const std::uint8_t* e = d_ + n_;
                for (;;) {   // advance to the next real marker
                    while (p < e && *p != 0xFF) ++p;
                    if (p + 1 >= e) return Fail("missing restart marker");
                    if (p[1] == 0x00 || p[1] == 0xFF) { p += (p[1] == 0xFF ? 1 : 2); continue; }
                    break;
                }
                if (p[1] < 0xD0 || p[1] > 0xD7) return Fail("missing restart marker");
                br.Reset(p + 2, e);
                for (int k = 0; k < ns; ++k) sc[k].c->pred = 0;
                eobrun_ = 0;
            }
        }
        // Locate the next marker after the entropy-coded data.
        const std::uint8_t* p = br.p;
        const std::uint8_t* e = d_ + n_;
        while (p < e) {
            if (*p == 0xFF && p + 1 < e && p[1] != 0x00 && p[1] != 0xFF && !(p[1] >= 0xD0 && p[1] <= 0xD7)) break;
            ++p;
        }
        scanEnd_ = static_cast<std::size_t>(p - d_);
        return true;
    }

    bool DecodeBlock(BitReader& br, const ScanComp& sc, int bx, int by) {
        Comp& c = *sc.c;
        std::int16_t local[64];
        std::int16_t* blk;
        if (progressive_) blk = c.coef.data() + (static_cast<std::size_t>(by) * c.bw + bx) * 64;
        else { std::memset(local, 0, sizeof(local)); blk = local; }

        switch (mode_) {
            case 0: {   // sequential
                int t = HuffDecode(br, *sc.dc);
                if (t < 0 || t > 16) return Fail("bad Huffman code");
                int diff = t ? Extend(br.Get(t), t) : 0;
                c.pred += diff;
                blk[0] = static_cast<std::int16_t>(c.pred);
                for (int k = 1; k < 64;) {
                    const int rs = HuffDecode(br, *sc.ac);
                    if (rs < 0) return Fail("bad Huffman code");
                    const int r = rs >> 4, s = rs & 15;
                    if (s == 0) {
                        if (r != 15) break;
                        k += 16;
                        continue;
                    }
                    k += r;
                    if (k > 63) return Fail("bad JPEG coefficient index");
                    blk[kZig[k]] = static_cast<std::int16_t>(Extend(br.Get(s), s));
                    ++k;
                }
                IdctBlock(blk, c.q, c.plane.data() + static_cast<std::size_t>(by) * 8 * c.stride + bx * 8, c.stride);
                return true;
            }
            case 1: {   // DC first
                int t = HuffDecode(br, *sc.dc);
                if (t < 0 || t > 16) return Fail("bad Huffman code");
                int diff = t ? Extend(br.Get(t), t) : 0;
                c.pred += diff;
                blk[0] = static_cast<std::int16_t>(c.pred * (1 << al_));
                return true;
            }
            case 2:   // DC refine
                if (br.Bit()) blk[0] = static_cast<std::int16_t>(blk[0] | (1 << al_));
                return true;
            case 3: {   // AC first
                if (eobrun_ > 0) { --eobrun_; return true; }
                for (int k = ss_; k <= se_;) {
                    const int rs = HuffDecode(br, *sc.ac);
                    if (rs < 0) return Fail("bad Huffman code");
                    const int r = rs >> 4, s = rs & 15;
                    if (s == 0) {
                        if (r < 15) {
                            eobrun_ = (1 << r) - 1;
                            if (r) eobrun_ += br.Get(r);
                            break;
                        }
                        k += 16;
                        continue;
                    }
                    k += r;
                    if (k > 63) return Fail("bad JPEG coefficient index");
                    blk[kZig[k]] = static_cast<std::int16_t>(Extend(br.Get(s), s) * (1 << al_));
                    ++k;
                }
                return true;
            }
            default: {   // AC refine
                const int p1 = 1 << al_, m1 = -(1 << al_);
                int k = ss_;
                if (eobrun_ == 0) {
                    for (; k <= se_; ++k) {
                        const int rs = HuffDecode(br, *sc.ac);
                        if (rs < 0) return Fail("bad Huffman code");
                        int r = rs >> 4, s = rs & 15;
                        int newVal = 0;
                        if (s) {
                            if (s != 1) return Fail("bad JPEG refinement");
                            newVal = br.Bit() ? p1 : m1;
                        } else if (r != 15) {
                            eobrun_ = 1 << r;
                            if (r) eobrun_ += br.Get(r);
                            break;
                        }
                        // Skip r zero-history coefficients, refining the non-zero ones passed on the way.
                        do {
                            std::int16_t& cf = blk[kZig[k]];
                            if (cf != 0) {
                                if (br.Bit() && (cf & p1) == 0) cf = static_cast<std::int16_t>(cf + (cf >= 0 ? p1 : m1));
                            } else {
                                if (--r < 0) break;
                            }
                            ++k;
                        } while (k <= se_);
                        if (newVal) {
                            if (k > 63) return Fail("bad JPEG coefficient index");
                            blk[kZig[k]] = static_cast<std::int16_t>(newVal);
                        }
                    }
                }
                if (eobrun_ > 0) {
                    for (; k <= se_; ++k) {
                        std::int16_t& cf = blk[kZig[k]];
                        if (cf != 0 && br.Bit() && (cf & p1) == 0) cf = static_cast<std::int16_t>(cf + (cf >= 0 ? p1 : m1));
                    }
                    --eobrun_;
                }
                return true;
            }
        }
    }

    void FinishProgressive() {
        for (int i = 0; i < nc_; ++i) {
            Comp& c = comp_[i];
            if (!c.qLatched && qtValid_[c.tq]) { std::memcpy(c.q, qt_[c.tq], sizeof(c.q)); c.qLatched = true; }
            if (!c.qLatched) std::fill(c.q, c.q + 64, static_cast<std::uint16_t>(1));
            c.plane.assign(static_cast<std::size_t>(c.stride) * c.bh * 8, 128);
            for (int by = 0; by < c.bh; ++by)
                for (int bx = 0; bx < c.bw; ++bx)
                    IdctBlock(c.coef.data() + (static_cast<std::size_t>(by) * c.bw + bx) * 64, c.q,
                              c.plane.data() + static_cast<std::size_t>(by) * 8 * c.stride + bx * 8, c.stride);
            std::vector<std::int16_t>().swap(c.coef);
        }
    }

    // ---- upsampling and colour conversion ----
    // Writes W_ samples of component `c` for output row y.
    void UpsampleRow(const Comp& c, int y, std::uint8_t* out, std::vector<std::uint8_t>& tmp) const {
        const std::uint8_t* base = c.plane.data();
        const int cw = c.w;
        auto row = [&](int r) { return base + static_cast<std::size_t>(std::min(std::max(r, 0), c.ht - 1)) * c.stride; };
        if (c.h == hmax_ && c.v == vmax_) { std::memcpy(out, row(y), static_cast<std::size_t>(W_)); return; }
        const bool h2 = (hmax_ == 2 * c.h), v2 = (vmax_ == 2 * c.v);
        const bool hEq = (hmax_ == c.h), vEq = (vmax_ == c.v);
        if (h2 && vEq) {
            const std::uint8_t* in = row(y);
            tmp.resize(static_cast<std::size_t>(2 * cw + 2));
            std::uint8_t* o = tmp.data();
            if (cw == 1) { o[0] = o[1] = in[0]; }
            else {
                o[0] = in[0];
                o[1] = static_cast<std::uint8_t>((in[0] * 3 + in[1] + 2) >> 2);
                for (int i = 1; i < cw - 1; ++i) {
                    o[2 * i] = static_cast<std::uint8_t>((in[i] * 3 + in[i - 1] + 1) >> 2);
                    o[2 * i + 1] = static_cast<std::uint8_t>((in[i] * 3 + in[i + 1] + 2) >> 2);
                }
                o[2 * cw - 2] = static_cast<std::uint8_t>((in[cw - 1] * 3 + in[cw - 2] + 1) >> 2);
                o[2 * cw - 1] = in[cw - 1];
            }
            std::memcpy(out, o, static_cast<std::size_t>(W_));
            return;
        }
        if (hEq && v2) {
            const int r0 = y >> 1, r1 = (y & 1) ? r0 + 1 : r0 - 1;
            const std::uint8_t* a = row(r0);
            const std::uint8_t* b = row(r1);
            const int bias = (y & 1) ? 2 : 1;
            for (int x = 0; x < W_; ++x) out[x] = static_cast<std::uint8_t>((a[x] * 3 + b[x] + bias) >> 2);
            return;
        }
        if (h2 && v2) {
            const int r0 = y >> 1, r1 = (y & 1) ? r0 + 1 : r0 - 1;
            const std::uint8_t* a = row(r0);
            const std::uint8_t* b = row(r1);
            tmp.resize(static_cast<std::size_t>(2 * cw + 2));
            std::uint8_t* o = tmp.data();
            auto sum = [&](int i) { return a[i] * 3 + b[i]; };
            if (cw == 1) {
                o[0] = static_cast<std::uint8_t>((sum(0) * 4 + 8) >> 4);
                o[1] = static_cast<std::uint8_t>((sum(0) * 4 + 7) >> 4);
            } else {
                o[0] = static_cast<std::uint8_t>((sum(0) * 4 + 8) >> 4);
                o[1] = static_cast<std::uint8_t>((sum(0) * 3 + sum(1) + 7) >> 4);
                for (int i = 1; i < cw - 1; ++i) {
                    const int s = sum(i);
                    o[2 * i] = static_cast<std::uint8_t>((s * 3 + sum(i - 1) + 8) >> 4);
                    o[2 * i + 1] = static_cast<std::uint8_t>((s * 3 + sum(i + 1) + 7) >> 4);
                }
                o[2 * cw - 2] = static_cast<std::uint8_t>((sum(cw - 1) * 3 + sum(cw - 2) + 8) >> 4);
                o[2 * cw - 1] = static_cast<std::uint8_t>((sum(cw - 1) * 4 + 7) >> 4);
            }
            std::memcpy(out, o, static_cast<std::size_t>(W_));
            return;
        }
        // Any other ratio: replicate samples (box).
        const std::uint8_t* in = row(y * c.v / vmax_);
        for (int x = 0; x < W_; ++x) out[x] = in[x * c.h / hmax_];
    }

    bool Convert(std::vector<std::uint8_t>& rgba) {
        const std::size_t total = static_cast<std::size_t>(W_) * H_ * 4;
        rgba.assign(total, 255);
        std::vector<std::uint8_t> line[3];
        std::vector<std::uint8_t> tmp;
        for (int i = 0; i < nc_; ++i) line[i].resize(static_cast<std::size_t>(W_));
        bool ycc = true;
        if (nc_ == 3) {
            if (jfif_) ycc = true;                     // a JFIF header always means YCbCr
            else if (adobe_ >= 0) ycc = (adobe_ != 0);
            else ycc = !(comp_[0].id == 'R' && comp_[1].id == 'G' && comp_[2].id == 'B');
        }
        for (int y = 0; y < H_; ++y) {
            for (int i = 0; i < nc_; ++i) UpsampleRow(comp_[i], y, line[i].data(), tmp);
            std::uint8_t* o = rgba.data() + static_cast<std::size_t>(y) * W_ * 4;
            if (nc_ == 1) {
                for (int x = 0; x < W_; ++x) { const std::uint8_t g = line[0][x]; o[4 * x] = o[4 * x + 1] = o[4 * x + 2] = g; }
            } else if (!ycc) {
                for (int x = 0; x < W_; ++x) { o[4 * x] = line[0][x]; o[4 * x + 1] = line[1][x]; o[4 * x + 2] = line[2][x]; }
            } else {
                for (int x = 0; x < W_; ++x) {
                    const int Y = line[0][x], cb = line[1][x] - 128, cr = line[2][x] - 128;
                    const int r = Y + ((91881 * cr + 32768) >> 16);
                    const int g = Y + ((-22554 * cb - 46802 * cr + 32768) >> 16);
                    const int b = Y + ((116130 * cb + 32768) >> 16);
                    o[4 * x] = static_cast<std::uint8_t>(r < 0 ? 0 : (r > 255 ? 255 : r));
                    o[4 * x + 1] = static_cast<std::uint8_t>(g < 0 ? 0 : (g > 255 ? 255 : g));
                    o[4 * x + 2] = static_cast<std::uint8_t>(b < 0 ? 0 : (b > 255 ? 255 : b));
                }
            }
        }
        return true;
    }

    const std::uint8_t* d_;
    std::size_t n_;
    std::string& err_;
    std::size_t pos_ = 0;
    std::size_t scanEnd_ = 0;

    int W_ = 0, H_ = 0, nc_ = 0, hmax_ = 1, vmax_ = 1, mcux_ = 0, mcuy_ = 0;
    bool infoOnly_ = false;
    bool seenSof_ = false, progressive_ = false, jfif_ = false;
    int adobe_ = -1;
    int ri_ = 0;
    int scans_ = 0;
    Comp comp_[3];
    std::uint16_t qt_[4][64];
    bool qtValid_[4] = {false, false, false, false};
    Huff dc_[4], ac_[4];
    int mode_ = 0, ss_ = 0, se_ = 63, al_ = 0, eobrun_ = 0;
};

} // namespace

bool DecodeJpeg(const std::uint8_t* data, std::size_t size, int* width, int* height,
                std::vector<std::uint8_t>* rgba, std::string& err) {
    Decoder dec(data, size, err);
    return dec.Run(width, height, rgba);
}

} // namespace imgdec
} // namespace okay
