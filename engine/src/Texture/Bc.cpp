// okay::tex block compression. Written from the block layouts and our own endpoint search; see Bc.hpp.
#include "okay/Texture/Bc.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>

namespace okay::tex {

int BcBlockBytes(BcFormat f) { return (f == BcFormat::BC1 || f == BcFormat::BC4) ? 8 : 16; }

float BcBytesPerPixel(BcFormat f) { return BcBlockBytes(f) / 16.0f; }

const char* BcName(BcFormat f) {
    switch (f) { case BcFormat::BC1: return "bc1"; case BcFormat::BC3: return "bc3"; case BcFormat::BC4: return "bc4"; case BcFormat::BC5: return "bc5"; }
    return "?";
}

bool ParseBcFormat(const char* name, BcFormat* out) {
    if (!name) return false;
    std::string n;
    for (const char* p = name; *p; ++p) n += char(std::tolower(static_cast<unsigned char>(*p)));
    static const BcFormat all[] = {BcFormat::BC1, BcFormat::BC3, BcFormat::BC4, BcFormat::BC5};
    for (BcFormat f : all) if (n == BcName(f)) { if (out) *out = f; return true; }
    return false;
}

std::size_t BcImageBytes(BcFormat f, int w, int h) {
    if (w <= 0 || h <= 0) return 0;
    std::size_t bw = (std::size_t(w) + 3) / 4, bh = (std::size_t(h) + 3) / 4;
    if (bw > (std::size_t(1) << 28) || bh > (std::size_t(1) << 28)) return 0;
    return bw * bh * std::size_t(BcBlockBytes(f));
}

namespace {

// ---- BC1 -----------------------------------------------------------------------------------------------------

struct Rgb { int r, g, b; };

Rgb Expand565(unsigned c) {
    unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return {int((r << 3) | (r >> 2)), int((g << 2) | (g >> 4)), int((b << 3) | (b >> 2))};
}

// The decoder's palette; `transparent` is set when index 3 means "no colour".
void Bc1Palette(unsigned c0, unsigned c1, Rgb pal[4], bool* transparent) {
    Rgb a = Expand565(c0), b = Expand565(c1);
    pal[0] = a; pal[1] = b;
    if (c0 > c1) {
        pal[2] = {(2 * a.r + b.r) / 3, (2 * a.g + b.g) / 3, (2 * a.b + b.b) / 3};
        pal[3] = {(a.r + 2 * b.r) / 3, (a.g + 2 * b.g) / 3, (a.b + 2 * b.b) / 3};
        *transparent = false;
    } else {
        pal[2] = {(a.r + b.r) / 2, (a.g + b.g) / 2, (a.b + b.b) / 2};
        pal[3] = {0, 0, 0};
        *transparent = true;
    }
}

inline unsigned Quant565(double r, double g, double b) {
    auto q = [](double v, int maxv) { int i = int(std::floor(v / 255.0 * maxv + 0.5)); return unsigned(i < 0 ? 0 : (i > maxv ? maxv : i)); };
    return (q(r, 31) << 11) | (q(g, 63) << 5) | q(b, 31);
}

struct Block1 {
    int px[16][3];
    bool opaque[16];
    int count = 0;      // opaque texels
};

inline int Dist2(const Rgb& p, const int* c) {
    int dr = p.r - c[0], dg = p.g - c[1], db = p.b - c[2];
    return dr * dr + dg * dg + db * db;
}

// Best indices for endpoints (c0,c1); returns total error. Texels that are transparent take index 3 (needs 3-colour mode).
long long Bc1Fit(const Block1& B, unsigned c0, unsigned c1, int idx[16]) {
    Rgb pal[4]; bool tr;
    Bc1Palette(c0, c1, pal, &tr);
    const int n = tr ? 3 : 4;
    long long err = 0;
    for (int i = 0; i < 16; ++i) {
        if (!B.opaque[i]) { idx[i] = 3; continue; }
        int best = 0, bd = Dist2(pal[0], B.px[i]);
        for (int k = 1; k < n; ++k) { int d = Dist2(pal[k], B.px[i]); if (d < bd) { bd = d; best = k; } }
        idx[i] = best; err += bd;
    }
    return err;
}

struct Cand { unsigned c0, c1; long long err; int idx[16]; };

// Evaluate an endpoint pair, fixing the ordering required by the mode (4 colour: c0 > c1; 3 colour: c0 <= c1).
void Consider(const Block1& B, bool threeColor, unsigned a, unsigned b, Cand* best) {
    unsigned c0 = a, c1 = b;
    if (threeColor) { if (c0 > c1) std::swap(c0, c1); }
    else {
        if (c0 == c1) return;                  // would decode in 3 colour mode; the solid case is handled apart
        if (c0 < c1) std::swap(c0, c1);
    }
    int idx[16];
    long long e = Bc1Fit(B, c0, c1, idx);
    if (e < best->err) { best->err = e; best->c0 = c0; best->c1 = c1; std::memcpy(best->idx, idx, sizeof idx); }
}

// Least squares endpoints for the current indices; false if the system is degenerate.
bool Bc1Refine(const Block1& B, const Cand& cur, bool threeColor, double* e0, double* e1) {
    double aa = 0, ab = 0, bb = 0, ra[3] = {0, 0, 0}, rb[3] = {0, 0, 0};
    for (int i = 0; i < 16; ++i) {
        if (!B.opaque[i]) continue;
        double al;                               // weight of endpoint 0 (endpoint 1 gets 1 - al)
        int k = cur.idx[i];
        if (threeColor) al = k == 0 ? 1.0 : (k == 1 ? 0.0 : 0.5);
        else al = k == 0 ? 1.0 : (k == 1 ? 0.0 : (k == 2 ? 2.0 / 3.0 : 1.0 / 3.0));
        double be = 1.0 - al;
        aa += al * al; ab += al * be; bb += be * be;
        for (int c = 0; c < 3; ++c) { ra[c] += al * B.px[i][c]; rb[c] += be * B.px[i][c]; }
    }
    double det = aa * bb - ab * ab;
    if (std::fabs(det) < 1e-9) return false;
    for (int c = 0; c < 3; ++c) {
        e0[c] = (ra[c] * bb - rb[c] * ab) / det;
        e1[c] = (rb[c] * aa - ra[c] * ab) / det;
    }
    return true;
}

} // namespace

void DecodeBc1Block(const std::uint8_t in[8], std::uint8_t rgba[64]) {
    unsigned c0 = in[0] | (unsigned(in[1]) << 8), c1 = in[2] | (unsigned(in[3]) << 8);
    Rgb pal[4]; bool tr;
    Bc1Palette(c0, c1, pal, &tr);
    std::uint32_t bits = in[4] | (std::uint32_t(in[5]) << 8) | (std::uint32_t(in[6]) << 16) | (std::uint32_t(in[7]) << 24);
    for (int i = 0; i < 16; ++i) {
        unsigned k = (bits >> (2 * i)) & 3u;
        rgba[i * 4 + 0] = std::uint8_t(pal[k].r); rgba[i * 4 + 1] = std::uint8_t(pal[k].g); rgba[i * 4 + 2] = std::uint8_t(pal[k].b);
        rgba[i * 4 + 3] = (tr && k == 3) ? 0 : 255;
    }
}

void EncodeBc1Block(const std::uint8_t rgba[64], std::uint8_t out[8], const BcEncodeOptions& opt) {
    Block1 B;
    bool anyTransparent = false;
    for (int i = 0; i < 16; ++i) {
        B.px[i][0] = rgba[i * 4]; B.px[i][1] = rgba[i * 4 + 1]; B.px[i][2] = rgba[i * 4 + 2];
        B.opaque[i] = !(opt.bc1AlphaCutoff > 0 && rgba[i * 4 + 3] < opt.bc1AlphaCutoff);
        if (B.opaque[i]) ++B.count; else anyTransparent = true;
    }
    const bool three = anyTransparent;
    unsigned c0 = 0, c1 = 0;
    int idx[16];
    for (int i = 0; i < 16; ++i) idx[i] = 3;
    if (B.count == 0) { c0 = 0; c1 = 0; for (int i = 0; i < 16; ++i) idx[i] = 3; }   // all transparent: 3 colour mode, index 3
    else {
        // mean and covariance of the opaque texels
        double mean[3] = {0, 0, 0};
        for (int i = 0; i < 16; ++i) if (B.opaque[i]) for (int c = 0; c < 3; ++c) mean[c] += B.px[i][c];
        for (int c = 0; c < 3; ++c) mean[c] /= B.count;
        double cov[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
        for (int i = 0; i < 16; ++i) if (B.opaque[i]) {
            double d[3] = {B.px[i][0] - mean[0], B.px[i][1] - mean[1], B.px[i][2] - mean[2]};
            for (int a = 0; a < 3; ++a) for (int b = 0; b < 3; ++b) cov[a][b] += d[a] * d[b];
        }
        // power iteration for the main axis, started from the covariance column of the widest channel (a start
        // vector that is exactly perpendicular to the main axis would never converge)
        int k = 0;
        if (cov[1][1] > cov[k][k]) k = 1;
        if (cov[2][2] > cov[k][k]) k = 2;
        double axis[3] = {cov[0][k], cov[1][k], cov[2][k]};
        if (axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2] < 1e-9) { axis[0] = axis[1] = axis[2] = 1; }
        for (int it = 0; it < 8; ++it) {
            double n[3];
            for (int a = 0; a < 3; ++a) n[a] = cov[a][0] * axis[0] + cov[a][1] * axis[1] + cov[a][2] * axis[2];
            double len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if (len < 1e-12) break;
            for (int a = 0; a < 3; ++a) axis[a] = n[a] / len;
        }
        double tmin = 1e30, tmax = -1e30;
        for (int i = 0; i < 16; ++i) if (B.opaque[i]) {
            double t = (B.px[i][0] - mean[0]) * axis[0] + (B.px[i][1] - mean[1]) * axis[1] + (B.px[i][2] - mean[2]) * axis[2];
            tmin = std::min(tmin, t); tmax = std::max(tmax, t);
        }
        Cand best; best.err = (1ll << 60); best.c0 = best.c1 = 0;
        for (int i = 0; i < 16; ++i) best.idx[i] = 0;
        // solid block: both endpoints on the colour; 3 colour mode lets c0 == c1 stay valid
        {
            unsigned s = Quant565(mean[0], mean[1], mean[2]);
            int ix[16];
            long long e = Bc1Fit(B, s, s, ix);        // c0 == c1 decodes in 3 colour mode; every used index is the colour
            best.err = e; best.c0 = best.c1 = s; std::memcpy(best.idx, ix, sizeof ix);
        }
        for (double inset : {0.0, 1.0 / 16.0}) {
            double lo = tmin + (tmax - tmin) * inset, hi = tmax - (tmax - tmin) * inset;
            double p0[3], p1[3];
            for (int c = 0; c < 3; ++c) { p0[c] = mean[c] + axis[c] * hi; p1[c] = mean[c] + axis[c] * lo; }
            Consider(B, three, Quant565(p0[0], p0[1], p0[2]), Quant565(p1[0], p1[1], p1[2]), &best);
            if (opt.quality < 1) break;
        }
        if (opt.quality >= 1) {
            for (int round = 0; round < 4; ++round) {
                double e0[3], e1[3];
                if (!Bc1Refine(B, best, three, e0, e1)) break;
                long long before = best.err;
                Consider(B, three, Quant565(e0[0], e0[1], e0[2]), Quant565(e1[0], e1[1], e1[2]), &best);
                if (best.err >= before) break;
            }
        }
        if (opt.quality >= 2) {
            for (int pass = 0; pass < 2; ++pass) {
                bool improved = false;
                for (int which = 0; which < 2; ++which)
                    for (int ch = 0; ch < 3; ++ch)
                        for (int dir = -1; dir <= 1; dir += 2) {
                            unsigned ep = which ? best.c1 : best.c0;
                            int sh = ch == 0 ? 11 : (ch == 1 ? 5 : 0), mx = ch == 1 ? 63 : 31;
                            int v = int((ep >> sh) & unsigned(mx)) + dir;
                            if (v < 0 || v > mx) continue;
                            unsigned np = (ep & ~(unsigned(mx) << sh)) | (unsigned(v) << sh);
                            long long before = best.err;
                            Consider(B, three, which ? best.c0 : np, which ? np : best.c1, &best);
                            if (best.err < before) improved = true;
                        }
                if (!improved) break;
            }
        }
        c0 = best.c0; c1 = best.c1;
        std::memcpy(idx, best.idx, sizeof idx);
    }
    std::uint32_t bits = 0;
    for (int i = 0; i < 16; ++i) bits |= std::uint32_t(idx[i] & 3) << (2 * i);
    out[0] = std::uint8_t(c0 & 255); out[1] = std::uint8_t(c0 >> 8);
    out[2] = std::uint8_t(c1 & 255); out[3] = std::uint8_t(c1 >> 8);
    out[4] = std::uint8_t(bits); out[5] = std::uint8_t(bits >> 8); out[6] = std::uint8_t(bits >> 16); out[7] = std::uint8_t(bits >> 24);
}

// ---- BC4 -----------------------------------------------------------------------------------------------------

namespace {

void Bc4Palette(int a0, int a1, int pal[8]) {
    pal[0] = a0; pal[1] = a1;
    if (a0 > a1) {
        for (int k = 2; k < 8; ++k) pal[k] = ((8 - k) * a0 + (k - 1) * a1) / 7;
    } else {
        for (int k = 2; k < 6; ++k) pal[k] = ((6 - k) * a0 + (k - 1) * a1) / 5;
        pal[6] = 0; pal[7] = 255;
    }
}

long long Bc4Fit(const std::uint8_t v[16], int a0, int a1, int idx[16]) {
    int pal[8];
    Bc4Palette(a0, a1, pal);
    long long err = 0;
    for (int i = 0; i < 16; ++i) {
        int best = 0, bd = 1 << 30;
        for (int k = 0; k < 8; ++k) { int d = (pal[k] - v[i]) * (pal[k] - v[i]); if (d < bd) { bd = d; best = k; } }
        idx[i] = best; err += bd;
    }
    return err;
}

struct Cand4 { int a0, a1; long long err; int idx[16]; };

void Consider4(const std::uint8_t v[16], int a0, int a1, bool eightMode, Cand4* best) {
    a0 = std::min(255, std::max(0, a0)); a1 = std::min(255, std::max(0, a1));
    if (eightMode) { if (a0 < a1) std::swap(a0, a1); if (a0 == a1) return; }
    else { if (a0 > a1) std::swap(a0, a1); }
    int idx[16];
    long long e = Bc4Fit(v, a0, a1, idx);
    if (e < best->err) { best->err = e; best->a0 = a0; best->a1 = a1; std::memcpy(best->idx, idx, sizeof idx); }
}

bool Refine4(const std::uint8_t v[16], const Cand4& cur, bool eightMode, double* e0, double* e1) {
    double aa = 0, ab = 0, bb = 0, ra = 0, rb = 0;
    for (int i = 0; i < 16; ++i) {
        int k = cur.idx[i];
        double al;
        if (eightMode) al = k == 0 ? 1.0 : (k == 1 ? 0.0 : (8 - k) / 7.0);
        else {
            if (k >= 6) continue;                      // fixed 0 / 255 codes carry no endpoint information
            al = k == 0 ? 1.0 : (k == 1 ? 0.0 : (6 - k) / 5.0);
        }
        double be = 1.0 - al;
        aa += al * al; ab += al * be; bb += be * be; ra += al * v[i]; rb += be * v[i];
    }
    double det = aa * bb - ab * ab;
    if (std::fabs(det) < 1e-9) return false;
    *e0 = (ra * bb - rb * ab) / det;
    *e1 = (rb * aa - ra * ab) / det;
    return true;
}

} // namespace

void DecodeBc4Block(const std::uint8_t in[8], std::uint8_t v[16]) {
    int pal[8];
    Bc4Palette(in[0], in[1], pal);
    std::uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= std::uint64_t(in[2 + i]) << (8 * i);
    for (int i = 0; i < 16; ++i) v[i] = std::uint8_t(pal[(bits >> (3 * i)) & 7u]);
}

void EncodeBc4Block(const std::uint8_t v[16], std::uint8_t out[8], const BcEncodeOptions& opt) {
    int lo = 255, hi = 0, ilo = 255, ihi = 0;
    bool hasInner = false;
    for (int i = 0; i < 16; ++i) {
        lo = std::min<int>(lo, v[i]); hi = std::max<int>(hi, v[i]);
        if (v[i] != 0 && v[i] != 255) { hasInner = true; ilo = std::min<int>(ilo, v[i]); ihi = std::max<int>(ihi, v[i]); }
    }
    Cand4 best; best.err = 1ll << 60; best.a0 = best.a1 = 0;
    for (int i = 0; i < 16; ++i) best.idx[i] = 0;
    // solid: a0 == a1 in the 6 value mode (palette includes the value at index 0)
    if (lo == hi) {
        best.a0 = best.a1 = lo;
        best.err = Bc4Fit(v, lo, lo, best.idx);
    } else {
        Consider4(v, hi, lo, true, &best);                     // 8 value mode on the full range
        if (hasInner) Consider4(v, ilo, ihi, false, &best);    // 6 value mode: extremes come free as codes 6 and 7
        else Consider4(v, lo, hi, false, &best);               // only 0 and 255: codes 6 / 7 hold them exactly
        if (opt.quality >= 1) {
            for (int mode = 0; mode < 2; ++mode) {
                const bool eight = mode == 0;
                // start each mode from its own best candidate
                Cand4 cur; cur.err = 1ll << 60; cur.a0 = cur.a1 = 0;
                if (eight) Consider4(v, hi, lo, true, &cur);
                else Consider4(v, hasInner ? ilo : lo, hasInner ? ihi : hi, false, &cur);
                if (cur.err == (1ll << 60)) continue;
                for (int round = 0; round < 4; ++round) {
                    double e0, e1;
                    if (!Refine4(v, cur, eight, &e0, &e1)) break;
                    long long before = cur.err;
                    Consider4(v, int(std::floor(e0 + 0.5)), int(std::floor(e1 + 0.5)), eight, &cur);
                    if (cur.err >= before) break;
                }
                if (opt.quality >= 2) {
                    for (int pass = 0; pass < 3; ++pass) {
                        bool imp = false;
                        for (int d0 = -2; d0 <= 2; ++d0)
                            for (int d1 = -2; d1 <= 2; ++d1) {
                                if (!d0 && !d1) continue;
                                long long before = cur.err;
                                Consider4(v, cur.a0 + d0, cur.a1 + d1, eight, &cur);
                                if (cur.err < before) imp = true;
                            }
                        if (!imp) break;
                    }
                }
                if (cur.err < best.err) best = cur;
            }
        }
    }
    out[0] = std::uint8_t(best.a0); out[1] = std::uint8_t(best.a1);
    std::uint64_t bits = 0;
    for (int i = 0; i < 16; ++i) bits |= std::uint64_t(best.idx[i] & 7) << (3 * i);
    for (int i = 0; i < 6; ++i) out[2 + i] = std::uint8_t(bits >> (8 * i));
}

// ---- whole images ----------------------------------------------------------------------------------------------

std::vector<std::uint8_t> EncodeBc(const std::uint8_t* rgba, int w, int h, BcFormat f, const BcEncodeOptions& opt) {
    std::vector<std::uint8_t> out;
    const std::size_t bytes = BcImageBytes(f, w, h);
    if (!rgba || bytes == 0) return out;
    const int bw = (w + 3) / 4, bh = (h + 3) / 4;
    out.resize(bytes);
    std::uint8_t* o = out.data();
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            std::uint8_t blk[64];
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    int sx = std::min(bx * 4 + x, w - 1), sy = std::min(by * 4 + y, h - 1);
                    std::memcpy(&blk[(y * 4 + x) * 4], rgba + (std::size_t(sy) * w + sx) * 4, 4);
                }
            auto chan = [&](int c, std::uint8_t v[16]) { for (int i = 0; i < 16; ++i) v[i] = blk[i * 4 + c]; };
            std::uint8_t v[16];
            switch (f) {
                case BcFormat::BC1: EncodeBc1Block(blk, o, opt); break;
                case BcFormat::BC3: chan(3, v); EncodeBc4Block(v, o, opt); EncodeBc1Block(blk, o + 8, BcEncodeOptions{opt.quality, 0}); break;
                case BcFormat::BC4: chan(0, v); EncodeBc4Block(v, o, opt); break;
                case BcFormat::BC5: chan(0, v); EncodeBc4Block(v, o, opt); chan(1, v); EncodeBc4Block(v, o + 8, opt); break;
            }
            o += BcBlockBytes(f);
        }
    return out;
}

std::vector<std::uint8_t> DecodeBc(const std::uint8_t* data, std::size_t size, int w, int h, BcFormat f) {
    std::vector<std::uint8_t> out;
    const std::size_t need = BcImageBytes(f, w, h);
    if (!data || need == 0 || size < need) return out;
    if (std::size_t(w) * std::size_t(h) > (std::size_t(1) << 29)) return out;
    out.assign(std::size_t(w) * h * 4, 0);
    const int bw = (w + 3) / 4, bh = (h + 3) / 4;
    const std::uint8_t* in = data;
    for (int by = 0; by < bh; ++by)
        for (int bx = 0; bx < bw; ++bx) {
            std::uint8_t blk[64];
            std::uint8_t a[16], b[16];
            switch (f) {
                case BcFormat::BC1: DecodeBc1Block(in, blk); break;
                case BcFormat::BC3: DecodeBc1Block(in + 8, blk); DecodeBc4Block(in, a); for (int i = 0; i < 16; ++i) blk[i * 4 + 3] = a[i]; break;
                case BcFormat::BC4: DecodeBc4Block(in, a); for (int i = 0; i < 16; ++i) { blk[i*4] = a[i]; blk[i*4+1] = 0; blk[i*4+2] = 0; blk[i*4+3] = 255; } break;
                case BcFormat::BC5: DecodeBc4Block(in, a); DecodeBc4Block(in + 8, b);
                    for (int i = 0; i < 16; ++i) { blk[i*4] = a[i]; blk[i*4+1] = b[i]; blk[i*4+2] = 0; blk[i*4+3] = 255; } break;
            }
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    int px = bx * 4 + x, py = by * 4 + y;
                    if (px < w && py < h) std::memcpy(&out[(std::size_t(py) * w + px) * 4], &blk[(y * 4 + x) * 4], 4);
                }
            in += BcBlockBytes(f);
        }
    return out;
}

} // namespace okay::tex
