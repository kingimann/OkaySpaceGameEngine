// Drives the engine's own engine/src/Texture/Mip.cpp and Texture.cpp, compiled unchanged by okay-cc
// (std::vector of okay::tex::Surface, std::pair, std::move, lambdas, our libm). Results are printed as bytes
// and scaled integers, so a last-bit difference from glibc in sin or pow does not show.
#include "okay/Texture/Mip.hpp"

#include <cstdint>
#include <vector>

extern "C" int printf(const char *fmt, ...);

static long q5(double v) { return (long)(v * 100000.0 + (v < 0 ? -0.5 : 0.5)); }

static unsigned long hashBytes(const std::vector<std::uint8_t> &b) {
    unsigned long h = 1469598103934665603ul;
    for (std::uint8_t c : b) h = (h ^ c) * 1099511628211ul;
    return h % 1000000007ul;
}

static int sameBytes(const std::vector<std::uint8_t> &a, const std::vector<std::uint8_t> &b) {
    if (a.size() != b.size()) return 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) return 0;
    return 1;
}

int main() {
    printf("count %d %d %d\n", okay::tex::MipCount(256, 64), okay::tex::MipCount(1, 1), okay::tex::MipCount(0, 5));
    int w = 0, h = 0;
    okay::tex::MipSize(100, 30, 3, &w, &h);
    printf("size %d %d\n", w, h);
    const okay::tex::MipFilter filters[3] = {okay::tex::MipFilter::Box, okay::tex::MipFilter::Lanczos3, okay::tex::MipFilter::Kaiser};
    for (int f = 0; f < 3; ++f) {
        std::vector<okay::tex::Tap> taps = okay::tex::ResampleTaps(9, 4, 1, filters[f], okay::tex::MipWrap::Clamp, 4.0f);
        printf("taps %d:", f);
        for (const okay::tex::Tap &t : taps) printf(" %d/%ld", t.index, q5(t.weight));
        printf("\n");
    }
    std::vector<okay::tex::Tap> wrapped = okay::tex::ResampleTaps(8, 4, 0, okay::tex::MipFilter::Lanczos3, okay::tex::MipWrap::Repeat);
    printf("wrap %d first %d\n", (int)wrapped.size(), wrapped.empty() ? -1 : wrapped[0].index);
    // An 8x6 RGBA8 image with a gradient, a checker and an alpha ramp.
    const int W = 8, H = 6;
    std::vector<std::uint8_t> img(W * H * 4);
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            std::uint8_t *p = &img[(y * W + x) * 4];
            p[0] = (std::uint8_t)(x * 32);
            p[1] = (std::uint8_t)(((x + y) & 1) ? 230 : 20);
            p[2] = (std::uint8_t)(y * 40);
            p[3] = (std::uint8_t)(x * 30 + y * 5);
        }
    for (int f = 0; f < 3; ++f) {
        okay::tex::MipOptions opt;
        opt.filter = filters[f];
        opt.srgb = f != 1;
        opt.wrap = f == 2 ? okay::tex::MipWrap::Repeat : okay::tex::MipWrap::Clamp;
        std::vector<std::vector<std::uint8_t>> chain = okay::tex::GenerateMipsRGBA8(img.data(), W, H, opt);
        printf("chain %d levels %d:", f, (int)chain.size());
        for (const auto &level : chain) printf(" %d/%ld", (int)level.size(), (long)hashBytes(level));
        printf("\n");
        const std::vector<std::uint8_t> &last = chain.back();
        printf("  last %d %d %d %d\n", last[0], last[1], last[2], last[3]);
    }
    okay::tex::Surface s = okay::tex::FromRGBA8(img.data(), W, H, true);
    printf("surface %d %d valid %d coverage %ld\n", s.w, s.h, s.Valid(), q5(okay::tex::AlphaCoverage(s, 0.5f)));
    okay::tex::MipOptions cov;
    cov.alphaCoverage = true;
    cov.alphaCutoff = 0.4f;
    std::vector<okay::tex::Surface> mips = okay::tex::GenerateMips(s, cov);
    printf("coverage levels %d:", (int)mips.size());
    for (const okay::tex::Surface &m : mips) printf(" %ld", q5(okay::tex::AlphaCoverage(m, 0.4f)));
    printf("\n");
    okay::tex::MipOptions nm;
    nm.normalMap = true;
    nm.filter = okay::tex::MipFilter::Kaiser;
    std::vector<okay::tex::Surface> normals = okay::tex::GenerateMips(okay::tex::FromRGBA8(img.data(), W, H, false), nm);
    const float *n0 = normals[1].At(0, 0);
    printf("normal %ld %ld %ld\n", q5(n0[0]), q5(n0[1]), q5(n0[2]));
    std::vector<std::uint8_t> back = okay::tex::ToRGBA8(s, true);
    printf("roundtrip %d same %d psnr %ld\n", (int)back.size(), sameBytes(back, img), q5(okay::tex::Psnr8(back.data(), img.data(), back.size())));
    std::vector<std::uint8_t> noisy = img;
    for (std::size_t i = 0; i < noisy.size(); i += 7) noisy[i] = (std::uint8_t)(noisy[i] ^ 3);
    printf("psnr %ld\n", (long)(okay::tex::Psnr8(noisy.data(), img.data(), img.size()) * 1000.0));
    return 0;
}
