// okay::tex mip chains. Written from the filter definitions (box area coverage, Lanczos a=3, Kaiser-windowed sinc
// with a Bessel I0 series) and the idea of preserving alpha-test coverage; see docs/rendering/own_texture_library.md.
#include "okay/Texture/Mip.hpp"
#include <algorithm>
#include <cmath>

namespace okay::tex {

namespace {

const double kPi = 3.14159265358979323846;

double Sinc(double x) {
    if (std::fabs(x) < 1e-9) return 1.0;
    double px = kPi * x;
    return std::sin(px) / px;
}

// Modified Bessel function of the first kind, order 0, by its power series.
double BesselI0(double x) {
    double sum = 1.0, term = 1.0, q = x * x * 0.25;
    for (int k = 1; k < 60; ++k) {
        term *= q / (double(k) * double(k));
        sum += term;
        if (term < sum * 1e-16) break;
    }
    return sum;
}

double Kernel(MipFilter f, double x, double beta) {
    const double R = 3.0;
    double ax = std::fabs(x);
    if (ax >= R) return 0.0;
    if (f == MipFilter::Lanczos3) return Sinc(ax) * Sinc(ax / R);
    double t = ax / R;
    return Sinc(ax) * BesselI0(beta * std::sqrt(std::max(0.0, 1.0 - t * t))) / BesselI0(beta);
}

int WrapIndex(int i, int n, MipWrap w) {
    if (w == MipWrap::Repeat) { i %= n; if (i < 0) i += n; return i; }
    return i < 0 ? 0 : (i >= n ? n - 1 : i);
}

void AddTap(std::vector<Tap>& v, int idx, double w) {
    for (auto& t : v) if (t.index == idx) { t.weight = float(double(t.weight) + w); return; }
    v.push_back({idx, float(w)});
}

} // namespace

std::vector<Tap> ResampleTaps(int srcSize, int dstSize, int dst, MipFilter f, MipWrap wrap, float kaiserBeta) {
    std::vector<Tap> taps;
    if (srcSize < 1 || dstSize < 1 || dst < 0 || dst >= dstSize) return taps;
    if (srcSize == dstSize) { taps.push_back({dst, 1.0f}); return taps; }
    const double scale = double(srcSize) / double(dstSize);
    std::vector<std::pair<int, double>> raw;
    double sum = 0.0;
    if (f == MipFilter::Box) {
        double lo = dst * scale, hi = (dst + 1) * scale;
        for (int i = int(std::floor(lo)); i < int(std::ceil(hi)) && i < srcSize; ++i) {
            double ov = std::min(hi, double(i + 1)) - std::max(lo, double(i));
            if (ov > 1e-12) { raw.push_back({i, ov}); sum += ov; }
        }
    } else {
        const double c = (dst + 0.5) * scale;      // centre in source edge coordinates
        const double s = std::max(scale, 1.0);     // stretch the kernel when shrinking
        const double support = 3.0 * s;
        for (int i = int(std::ceil(c - support - 0.5)); i <= int(std::floor(c + support - 0.5)); ++i) {
            double w = Kernel(f, (i + 0.5 - c) / s, kaiserBeta);
            if (w != 0.0) { raw.push_back({i, w}); sum += w; }
        }
    }
    if (sum == 0.0) { taps.push_back({std::min(dst, srcSize - 1), 1.0f}); return taps; }
    for (auto& r : raw) AddTap(taps, WrapIndex(r.first, srcSize, wrap), r.second / sum);
    return taps;
}

Surface Resample(const Surface& src, int dstW, int dstH, const MipOptions& opt) {
    if (!src.Valid() || dstW < 1 || dstH < 1) return Surface();
    const bool premul = !opt.normalMap;
    // working copy: premultiplied by alpha so transparent texels carry no colour weight
    Surface a = src;
    if (premul)
        for (std::size_t i = 0; i < std::size_t(a.w) * a.h; ++i) {
            float al = a.px[i * 4 + 3];
            for (int c = 0; c < 3; ++c) a.px[i * 4 + c] *= al;
        }
    // horizontal
    Surface b(dstW, a.h);
    std::vector<std::vector<Tap>> tx(dstW);
    for (int x = 0; x < dstW; ++x) tx[x] = ResampleTaps(a.w, dstW, x, opt.filter, opt.wrap, opt.kaiserBeta);
    for (int y = 0; y < a.h; ++y)
        for (int x = 0; x < dstW; ++x) {
            double acc[4] = {0, 0, 0, 0};
            for (const Tap& t : tx[x]) {
                const float* p = a.At(t.index, y);
                for (int c = 0; c < 4; ++c) acc[c] += double(p[c]) * t.weight;
            }
            float* o = b.At(x, y);
            for (int c = 0; c < 4; ++c) o[c] = float(acc[c]);
        }
    // vertical
    Surface d(dstW, dstH);
    std::vector<std::vector<Tap>> ty(dstH);
    for (int y = 0; y < dstH; ++y) ty[y] = ResampleTaps(b.h, dstH, y, opt.filter, opt.wrap, opt.kaiserBeta);
    for (int y = 0; y < dstH; ++y)
        for (int x = 0; x < dstW; ++x) {
            double acc[4] = {0, 0, 0, 0};
            for (const Tap& t : ty[y]) {
                const float* p = b.At(x, t.index);
                for (int c = 0; c < 4; ++c) acc[c] += double(p[c]) * t.weight;
            }
            float* o = d.At(x, y);
            for (int c = 0; c < 4; ++c) o[c] = float(acc[c]);
        }
    // back to straight alpha, clamp
    for (std::size_t i = 0; i < std::size_t(dstW) * dstH; ++i) {
        float* p = &d.px[i * 4];
        float al = std::min(1.0f, std::max(0.0f, p[3]));
        p[3] = al;
        for (int c = 0; c < 3; ++c) {
            float v = p[c];
            if (premul) v = al > 1e-6f ? v / al : 0.0f;
            if (v < 0.0f) v = 0.0f;
            if (!opt.hdr && v > 1.0f) v = 1.0f;
            p[c] = v;
        }
    }
    return d;
}

float AlphaCoverage(const Surface& s, float cutoff) {
    if (!s.Valid()) return 0.0f;
    std::size_t n = std::size_t(s.w) * s.h, k = 0;
    for (std::size_t i = 0; i < n; ++i) if (s.px[i * 4 + 3] >= cutoff) ++k;
    return float(double(k) / double(n));
}

namespace {

std::size_t CountAbove(const Surface& s, float scale, float cutoff) {
    std::size_t n = std::size_t(s.w) * s.h, k = 0;
    for (std::size_t i = 0; i < n; ++i) if (s.px[i * 4 + 3] * scale >= cutoff) ++k;
    return k;
}

void ScaleAlphaToCoverage(Surface& s, float cutoff, double targetFraction) {
    if (targetFraction <= 0.0) return;
    const std::size_t n = std::size_t(s.w) * s.h;
    const std::size_t want = std::size_t(std::ceil(targetFraction * double(n) - 1e-9));
    if (want >= n && CountAbove(s, 1.0f, cutoff) >= n) return;   // everything already solid: nothing to restore
    double lo = 0.0, hi = 32.0;
    if (CountAbove(s, float(hi), cutoff) < want) lo = hi;     // cannot reach it: use the largest scale
    else for (int it = 0; it < 30; ++it) {
        double mid = 0.5 * (lo + hi);
        if (CountAbove(s, float(mid), cutoff) >= want) hi = mid; else lo = mid;
    }
    const float scale = float(lo == 32.0 ? 32.0 : hi);
    for (std::size_t i = 0; i < n; ++i) s.px[i * 4 + 3] = std::min(1.0f, s.px[i * 4 + 3] * scale);
}

void Renormalise(Surface& s) {
    for (std::size_t i = 0; i < std::size_t(s.w) * s.h; ++i) {
        float* p = &s.px[i * 4];
        double x = p[0] * 2.0 - 1.0, y = p[1] * 2.0 - 1.0, z = p[2] * 2.0 - 1.0;
        double len = std::sqrt(x * x + y * y + z * z);
        if (len < 1e-6) { x = 0; y = 0; z = 1; len = 1; }
        p[0] = float(x / len * 0.5 + 0.5);
        p[1] = float(y / len * 0.5 + 0.5);
        p[2] = float(z / len * 0.5 + 0.5);
    }
}

void Premultiply(Surface& s, bool undo) {
    for (std::size_t i = 0; i < std::size_t(s.w) * s.h; ++i) {
        float* p = &s.px[i * 4];
        if (!undo) for (int c = 0; c < 3; ++c) p[c] *= p[3];
        else for (int c = 0; c < 3; ++c) p[c] = p[3] > 1e-6f ? p[c] / p[3] : 0.0f;
    }
}

} // namespace

std::vector<Surface> GenerateMips(const Surface& base, const MipOptions& opt) {
    std::vector<Surface> chain;
    if (!base.Valid()) return chain;
    chain.push_back(base);
    int levels = MipCount(base.w, base.h);
    if (opt.maxLevels > 0) levels = std::min(levels, opt.maxLevels);
    const bool pre = opt.premultipliedInput && !opt.normalMap;
    Surface cur = base;
    if (pre) Premultiply(cur, true);
    const double target = opt.alphaCoverage ? double(AlphaCoverage(cur, opt.alphaCutoff)) : 0.0;
    for (int l = 1; l < levels; ++l) {
        int w, h;
        MipSize(base.w, base.h, l, &w, &h);
        cur = Resample(cur, w, h, opt);
        if (opt.normalMap) Renormalise(cur);
        if (opt.alphaCoverage && !opt.normalMap) ScaleAlphaToCoverage(cur, opt.alphaCutoff, target);
        Surface out = cur;
        if (pre) Premultiply(out, false);
        chain.push_back(std::move(out));
    }
    return chain;
}

std::vector<std::vector<std::uint8_t>> GenerateMipsRGBA8(const std::uint8_t* rgba, int w, int h, const MipOptions& opt) {
    std::vector<std::vector<std::uint8_t>> out;
    if (!rgba || w < 1 || h < 1) return out;
    const bool srgb = opt.srgb && !opt.normalMap;
    Surface s = FromRGBA8(rgba, w, h, srgb);
    std::vector<Surface> chain = GenerateMips(s, opt);
    out.reserve(chain.size());
    out.emplace_back(rgba, rgba + std::size_t(w) * h * 4);
    for (std::size_t i = 1; i < chain.size(); ++i) out.push_back(ToRGBA8(chain[i], srgb));
    return out;
}

} // namespace okay::tex
