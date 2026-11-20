// okay::tex mip chains (okay/Texture/Mip.hpp). Expected numbers were worked out by hand (box averages, the sRGB
// curve at 0.5 linear = 187.5 -> 188), not copied from the code's output.
#include "test_framework.hpp"
#include "okay/Texture/Mip.hpp"
#include <cmath>

using namespace okay::tex;
using Bytes = std::vector<std::uint8_t>;

static Bytes Px(std::initializer_list<int> v) { Bytes b; for (int x : v) b.push_back(std::uint8_t(x)); return b; }

static void TestBasics() {
    RUN_SUITE("tex mip basics");
    CHECK(MipCount(1, 1) == 1);
    CHECK(MipCount(256, 64) == 9);
    CHECK(MipCount(5, 3) == 3);
    CHECK(MipCount(0, 3) == 0);
    int w, h; MipSize(5, 3, 1, &w, &h); CHECK(w == 2 && h == 1);
    MipSize(5, 3, 2, &w, &h); CHECK(w == 1 && h == 1);
}

static void TestBox() {
    RUN_SUITE("tex mip box");
    // 2x2 red channel 0,255,255,0 -> (0+255+255+0)/4 = 127.5 -> 128; alpha 255 everywhere
    Bytes in = Px({0,0,0,255,  255,0,0,255,  255,0,0,255,  0,0,0,255});
    auto ch = GenerateMipsRGBA8(in.data(), 2, 2);
    CHECK(ch.size() == 2);
    CHECK(ch[0] == in);                       // level 0 untouched
    CHECK(ch[1].size() == 4);
    CHECK(ch[1][0] == 128 && ch[1][1] == 0 && ch[1][3] == 255);

    // 3x1 -> 1x1 (non power of two): mean of 30, 60, 90 = 60 in every channel
    Bytes t = Px({30,30,30,255,  60,60,60,255,  90,90,90,255});
    auto c3 = GenerateMipsRGBA8(t.data(), 3, 1);
    CHECK(c3.size() == 2);
    CHECK(c3[1][0] == 60 && c3[1][1] == 60 && c3[1][2] == 60 && c3[1][3] == 255);

    // 4x1 -> 2x1 -> 1x1 on values 0,100,200,100: level 1 = 50,150, level 2 = 100
    Bytes r = Px({0,0,0,255, 100,100,100,255, 200,200,200,255, 100,100,100,255});
    auto c4 = GenerateMipsRGBA8(r.data(), 4, 1);
    CHECK(c4.size() == 3);
    CHECK(c4[1][0] == 50 && c4[1][4] == 150);
    CHECK(c4[2][0] == 100);

    // 5x3: sizes 5x3, 2x1, 1x1
    Bytes big(5 * 3 * 4, 77);
    auto c5 = GenerateMipsRGBA8(big.data(), 5, 3);
    CHECK(c5.size() == 3 && c5[1].size() == 2 * 1 * 4 && c5[2].size() == 4);
    for (auto& lvl : c5) for (auto v : lvl) CHECK(v == 77);   // constant stays constant

    // 1x1 and hostile input
    Bytes one = Px({1,2,3,4});
    auto c1 = GenerateMipsRGBA8(one.data(), 1, 1);
    CHECK(c1.size() == 1 && c1[0] == one);
    CHECK(GenerateMipsRGBA8(nullptr, 4, 4).empty());
    CHECK(GenerateMipsRGBA8(one.data(), 0, 4).empty());
    CHECK(GenerateMipsRGBA8(one.data(), 4, -1).empty());
    MipOptions mo; mo.maxLevels = 2;
    auto cm = GenerateMipsRGBA8(big.data(), 5, 3, mo);
    CHECK(cm.size() == 2);

    // 1xN: width stays 1
    Bytes col(1 * 8 * 4, 10);
    auto cc = GenerateMipsRGBA8(col.data(), 1, 8);
    CHECK(cc.size() == 4 && cc[3].size() == 4);
}

static void TestSrgbAndPremul() {
    RUN_SUITE("tex mip srgb / premultiplied");
    // black + white averaged in linear light = 0.5 linear = sRGB 0.7354 = 187.5 -> 188
    Bytes in = Px({0,0,0,255, 255,255,255,255});
    MipOptions lin;                      // wrong way: bytes averaged (127.5 -> 128)
    auto a = GenerateMipsRGBA8(in.data(), 2, 1, lin);
    CHECK(a[1][0] == 128);
    MipOptions so; so.srgb = true;
    auto b = GenerateMipsRGBA8(in.data(), 2, 1, so);
    CHECK(b[1][0] == 188 && b[1][1] == 188 && b[1][2] == 188 && b[1][3] == 255);
    CHECK_NEAR(SrgbToLinear(0.0f), 0.0, 1e-9);
    CHECK_NEAR(SrgbToLinear(1.0f), 1.0, 1e-9);
    CHECK_NEAR(SrgbToLinear(0.04045f), 0.04045 / 12.92, 1e-7);   // end of the toe
    CHECK_NEAR(LinearToSrgb(SrgbToLinear(0.5f)), 0.5, 1e-5);

    // straight alpha: opaque red + fully transparent green -> red at alpha 128 (green must not leak in)
    Bytes p = Px({255,0,0,255,  0,255,0,0});
    auto c = GenerateMipsRGBA8(p.data(), 2, 1);
    CHECK(c[1][0] == 255 && c[1][1] == 0 && c[1][2] == 0 && c[1][3] == 128);

    // premultiplied input stays premultiplied: (127,0,0,128) + (0,0,0,0) -> red 64 (=127/2 rounded) alpha 64
    Bytes q = Px({128,0,0,128,  0,0,0,0});
    MipOptions po; po.premultipliedInput = true;
    auto d = GenerateMipsRGBA8(q.data(), 2, 1, po);
    CHECK(d[1][3] == 64 && d[1][0] == 64 && d[1][1] == 0);
}

static void TestFilters() {
    RUN_SUITE("tex mip filters");
    // weights always sum to 1, any size pair, any filter, both wraps
    for (MipFilter f : {MipFilter::Box, MipFilter::Lanczos3, MipFilter::Kaiser})
        for (MipWrap wr : {MipWrap::Clamp, MipWrap::Repeat})
            for (int s = 1; s <= 37; ++s)
                for (int d = 1; d <= s; ++d)
                    for (int x = 0; x < d; ++x) {
                        auto taps = ResampleTaps(s, d, x, f, wr);
                        double sum = 0; bool ok = !taps.empty();
                        for (auto& t : taps) { sum += t.weight; if (t.index < 0 || t.index >= s) ok = false; }
                        CHECK(ok);
                        CHECK_NEAR(sum, 1.0, 1e-5);
                    }
    // box 4 -> 2: each destination texel is two texels at 1/2
    auto bt = ResampleTaps(4, 2, 1, MipFilter::Box, MipWrap::Clamp);
    CHECK(bt.size() == 2 && bt[0].index == 2 && bt[1].index == 3);
    CHECK_NEAR(bt[0].weight, 0.5, 1e-6);
    // box 3 -> 2 (scale 1.5): texel 0 covers [0,1.5): weights 2/3, 1/3
    auto b32 = ResampleTaps(3, 2, 0, MipFilter::Box, MipWrap::Clamp);
    CHECK(b32.size() == 2);
    CHECK_NEAR(b32[0].weight, 2.0 / 3.0, 1e-6);
    CHECK_NEAR(b32[1].weight, 1.0 / 3.0, 1e-6);
    // Lanczos is symmetric around the middle
    auto l = ResampleTaps(8, 4, 1, MipFilter::Lanczos3, MipWrap::Clamp);
    double mid = 0, sumW = 0;
    for (auto& t : l) { mid += t.index * t.weight; sumW += t.weight; }
    CHECK_NEAR(mid / sumW, 2.5 - 0.5 + 0.0 + 0.0, 1.0);   // loose (edges clamp); the strict check is below
    auto l2 = ResampleTaps(64, 8, 4, MipFilter::Lanczos3, MipWrap::Clamp);   // interior: centre = (4.5*8) - 0.5 = 35.5
    mid = 0; for (auto& t : l2) mid += t.index * t.weight;
    CHECK_NEAR(mid, 35.5, 1e-4);
    // a constant image stays constant under every filter, odd sizes included
    Bytes flat(7 * 5 * 4, 200);
    for (MipFilter f : {MipFilter::Lanczos3, MipFilter::Kaiser}) {
        MipOptions o; o.filter = f;
        auto ch = GenerateMipsRGBA8(flat.data(), 7, 5, o);
        CHECK(ch.size() == 3);
        for (auto& lvl : ch) for (auto v : lvl) CHECK(v == 200);
    }
    // a linear ramp 0..255 over 8 texels keeps its middle value under Lanczos (mean preserved within 2)
    Bytes ramp;
    for (int i = 0; i < 8; ++i) { int v = i * 255 / 7; ramp.push_back(v); ramp.push_back(v); ramp.push_back(v); ramp.push_back(255); }
    MipOptions lo; lo.filter = MipFilter::Lanczos3;
    auto lc = GenerateMipsRGBA8(ramp.data(), 8, 1, lo);
    CHECK(lc.back()[0] >= 120 && lc.back()[0] <= 135);
    // determinism
    CHECK(GenerateMipsRGBA8(ramp.data(), 8, 1, lo) == lc);
    // repeat wrap: a 2-texel image 0,255 shrunk to 1 under Kaiser is the mean either way
    Bytes pair = Px({0,0,0,255, 255,255,255,255});
    MipOptions ko; ko.filter = MipFilter::Kaiser; ko.wrap = MipWrap::Repeat;
    auto kc = GenerateMipsRGBA8(pair.data(), 2, 1, ko);
    CHECK(kc[1][0] >= 126 && kc[1][0] <= 129);
}

static void TestCoverage() {
    RUN_SUITE("tex mip alpha coverage");
    // 16x16 cut-out with mostly low alpha (about a third of the texels are at or above 0.5)
    Bytes img(16 * 16 * 4, 255);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            int r = (x * 37 + y * 91 + 13) % 256;
            img[(std::size_t(y) * 16 + x) * 4 + 3] = std::uint8_t(r * r / 255);
        }
    Surface base = FromRGBA8(img.data(), 16, 16, false);
    const float c0 = AlphaCoverage(base, 0.5f);
    CHECK(c0 > 0.2f && c0 < 0.45f);
    MipOptions plain;
    auto p = GenerateMips(base, plain);
    MipOptions cov; cov.alphaCoverage = true;
    auto q = GenerateMips(base, cov);
    CHECK(p.size() == 5 && q.size() == 5);
    // plain filtering averages alpha towards ~0.33, so the share above 0.5 collapses; with coverage it is kept
    CHECK(AlphaCoverage(p[2], 0.5f) < c0 - 0.15f);
    for (std::size_t l = 1; l <= 2; ++l) {
        double n = double(q[l].w) * q[l].h;
        CHECK_NEAR(AlphaCoverage(q[l], 0.5f), c0, 2.0 / n);
        CHECK(AlphaCoverage(q[l], 0.5f) >= AlphaCoverage(p[l], 0.5f));
    }
    // a fully opaque image is untouched by coverage scaling
    Bytes solid(4 * 4 * 4, 255);
    auto so = GenerateMipsRGBA8(solid.data(), 4, 4, cov);
    for (auto v : so.back()) CHECK(v == 255);
}

static void TestNormals() {
    RUN_SUITE("tex mip normal map");
    // two normals tilted to +x and -x (about 45 degrees) average to a shorter vector pointing up; renormalised -> straight up
    // encode (0.7071, 0, 0.7071) -> bytes (218, 128, 218) ; (-0.7071, 0, 0.7071) -> (37, 128, 218)
    Bytes n = Px({218,128,218,255,  37,128,218,255});
    MipOptions o; o.normalMap = true;
    auto c = GenerateMipsRGBA8(n.data(), 2, 1, o);
    CHECK(c.size() == 2);
    CHECK(c[1][0] >= 127 && c[1][0] <= 128);          // x about 0
    CHECK(c[1][2] >= 254);                             // z about 1 (255 after renormalising)
    // every level of a bumpy map is unit length (within byte quantisation)
    Bytes bump;
    for (int i = 0; i < 16; ++i) {
        double ax = std::sin(i * 0.9) * 0.6, ay = std::cos(i * 1.3) * 0.5, az = std::sqrt(1 - ax * ax - ay * ay);
        bump.push_back(std::uint8_t(std::floor((ax * 0.5 + 0.5) * 255 + 0.5)));
        bump.push_back(std::uint8_t(std::floor((ay * 0.5 + 0.5) * 255 + 0.5)));
        bump.push_back(std::uint8_t(std::floor((az * 0.5 + 0.5) * 255 + 0.5)));
        bump.push_back(255);
    }
    MipOptions bo; bo.normalMap = true; bo.srgb = true;   // srgb must be ignored for normals
    auto bc = GenerateMipsRGBA8(bump.data(), 4, 4, bo);
    CHECK(bc.size() == 3);
    for (std::size_t l = 1; l < bc.size(); ++l)
        for (std::size_t i = 0; i + 3 < bc[l].size(); i += 4) {
            double x = bc[l][i] / 255.0 * 2 - 1, y = bc[l][i + 1] / 255.0 * 2 - 1, z = bc[l][i + 2] / 255.0 * 2 - 1;
            CHECK_NEAR(std::sqrt(x * x + y * y + z * z), 1.0, 0.02);
        }
    // opposite normals cancel to the zero vector (0.5, 0.5, 0.5 encoded): fall back to straight up, never NaN
    Surface zs(2, 1);
    float* p0 = zs.At(0, 0); p0[0] = 1; p0[1] = 0.5f; p0[2] = 0.5f; p0[3] = 1;
    float* p1 = zs.At(1, 0); p1[0] = 0; p1[1] = 0.5f; p1[2] = 0.5f; p1[3] = 1;
    auto zc = GenerateMips(zs, o);
    CHECK(zc.size() == 2);
    CHECK_NEAR(zc[1].px[0], 0.5, 1e-5); CHECK_NEAR(zc[1].px[1], 0.5, 1e-5); CHECK_NEAR(zc[1].px[2], 1.0, 1e-5);
}

int main() {
    TestBasics();
    TestBox();
    TestSrgbAndPremul();
    TestFilters();
    TestCoverage();
    TestNormals();
    TEST_MAIN_RESULT();
}
