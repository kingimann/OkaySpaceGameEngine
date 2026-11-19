// okay::tex pixel conversion (okay/Texture/Convert.hpp). Half-float bit patterns are the well known binary16 values
// (1.0 = 0x3C00, 65504 = 0x7BFF, ...), worked out from the format, not from the code.
#include "test_framework.hpp"
#include "okay/Texture/Convert.hpp"
#include <cmath>
#include <limits>

using namespace okay::tex;
using Bytes = std::vector<std::uint8_t>;

static void TestHalf() {
    RUN_SUITE("tex convert half float");
    CHECK(FloatToHalf(0.0f) == 0x0000);
    CHECK(FloatToHalf(-0.0f) == 0x8000);
    CHECK(FloatToHalf(1.0f) == 0x3C00);
    CHECK(FloatToHalf(-2.0f) == 0xC000);
    CHECK(FloatToHalf(0.5f) == 0x3800);
    CHECK(FloatToHalf(65504.0f) == 0x7BFF);
    CHECK(FloatToHalf(65520.0f) == 0x7C00);       // rounds up to infinity
    CHECK(FloatToHalf(1e9f) == 0x7C00);
    CHECK(FloatToHalf(std::numeric_limits<float>::infinity()) == 0x7C00);
    CHECK((FloatToHalf(std::nanf("")) & 0x7C00) == 0x7C00 && (FloatToHalf(std::nanf("")) & 0x3FF) != 0);
    CHECK(FloatToHalf(std::ldexp(1.0f, -24)) == 0x0001);     // smallest subnormal
    CHECK(FloatToHalf(std::ldexp(1.0f, -14)) == 0x0400);     // smallest normal
    CHECK(FloatToHalf(std::ldexp(1.0f, -26)) == 0x0000);     // underflow
    CHECK(FloatToHalf(1.0f + 1.0f / 2048.0f) == 0x3C00);     // tie to even (down)
    CHECK(FloatToHalf(1.0f + 3.0f / 2048.0f) == 0x3C02);     // tie to even (up)
    CHECK_NEAR(HalfToFloat(0x3C00), 1.0, 0);
    CHECK_NEAR(HalfToFloat(0xC000), -2.0, 0);
    CHECK_NEAR(HalfToFloat(0x0001), std::ldexp(1.0, -24), 0);
    CHECK_NEAR(HalfToFloat(0x7BFF), 65504.0, 0);
    CHECK(std::isinf(HalfToFloat(0x7C00)) && std::isnan(HalfToFloat(0x7E00)));
    // every half converts to float and back to itself (NaNs only keep being NaN)
    for (unsigned h = 0; h < 65536; ++h) {
        float f = HalfToFloat(std::uint16_t(h));
        std::uint16_t back = FloatToHalf(f);
        if (std::isnan(f)) CHECK((back & 0x7C00) == 0x7C00 && (back & 0x3FF) != 0);
        else if (back != h) { CHECK(back == h); break; }
    }
}

static void TestFormats() {
    RUN_SUITE("tex convert formats");
    CHECK(BytesPerPixel(PixelFormat::RGBA16F) == 8 && BytesPerPixel(PixelFormat::RGB565) == 2 && BytesPerPixel(PixelFormat::R8) == 1);
    PixelFormat f;
    CHECK(ParseFormat("BGRA8", &f) && f == PixelFormat::BGRA8);
    CHECK(!ParseFormat("dxt9", &f));

    Bytes px = {10, 20, 30, 40,  255, 128, 0, 255};
    // BGRA swaps red and blue and back again
    Bytes bgra = ConvertPixels(px.data(), PixelFormat::RGBA8, 2, 1, PixelFormat::BGRA8);
    CHECK(bgra == (Bytes{30, 20, 10, 40,  0, 128, 255, 255}));
    CHECK(ConvertPixels(bgra.data(), PixelFormat::BGRA8, 2, 1, PixelFormat::RGBA8) == px);

    // RGB565: pure red -> 0xF800, pure green -> 0x07E0, white -> 0xFFFF; alpha reads back 255
    Bytes c = {255,0,0,255, 0,255,0,255, 255,255,255,255, 0,0,0,255};
    Bytes p565 = ConvertPixels(c.data(), PixelFormat::RGBA8, 4, 1, PixelFormat::RGB565);
    CHECK(p565.size() == 8);
    CHECK(p565[0] == 0x00 && p565[1] == 0xF8);
    CHECK(p565[2] == 0xE0 && p565[3] == 0x07);
    CHECK(p565[4] == 0xFF && p565[5] == 0xFF && p565[6] == 0 && p565[7] == 0);
    CHECK(ConvertPixels(p565.data(), PixelFormat::RGB565, 4, 1, PixelFormat::RGBA8) == c);
    // 565 error is at most half a step: 128 -> 5 bit 16/31 = 132 (within 4)
    Bytes mid = {128,128,128,255};
    Bytes m565 = ConvertPixels(mid.data(), PixelFormat::RGBA8, 1, 1, PixelFormat::RGB565);
    Bytes mback = ConvertPixels(m565.data(), PixelFormat::RGB565, 1, 1, PixelFormat::RGBA8);
    CHECK(std::abs(int(mback[0]) - 128) <= 4 && std::abs(int(mback[1]) - 128) <= 2);

    // RGBA16F: all 256 byte values survive RGBA8 -> 16F -> RGBA8
    Bytes all(256 * 4);
    for (int i = 0; i < 256; ++i) { all[i*4] = i; all[i*4+1] = 255 - i; all[i*4+2] = (i * 7) & 255; all[i*4+3] = i ^ 0x55; }
    Bytes h = ConvertPixels(all.data(), PixelFormat::RGBA8, 256, 1, PixelFormat::RGBA16F);
    CHECK(h.size() == 256 * 8);
    CHECK(h[0] == 0 && h[1] == 0);                                  // 0.0
    CHECK(ConvertPixels(h.data(), PixelFormat::RGBA16F, 256, 1, PixelFormat::RGBA8) == all);
    // 255 -> 1.0 = 0x3C00 little endian 00 3C
    Bytes one = {255,255,255,255};
    Bytes h1 = ConvertPixels(one.data(), PixelFormat::RGBA8, 1, 1, PixelFormat::RGBA16F);
    CHECK(h1[0] == 0x00 && h1[1] == 0x3C && h1[6] == 0x00 && h1[7] == 0x3C);
    // a value above 1 is kept in 16F (2.0 = 0x4000) but clamps going to bytes
    Bytes hdr = {0x00,0x40, 0x00,0x00, 0x00,0x00, 0x00,0x3C};
    Bytes back = ConvertPixels(hdr.data(), PixelFormat::RGBA16F, 1, 1, PixelFormat::RGBA8);
    CHECK(back == (Bytes{255, 0, 0, 255}));

    // R8: take red, or luma; reading back gives (v,0,0,255)
    Bytes rgb = {200, 100, 50, 255};
    Bytes r8 = ConvertPixels(rgb.data(), PixelFormat::RGBA8, 1, 1, PixelFormat::R8);
    CHECK(r8.size() == 1 && r8[0] == 200);
    ConvertOptions lo; lo.lumaToR8 = true;
    Bytes l8 = ConvertPixels(rgb.data(), PixelFormat::RGBA8, 1, 1, PixelFormat::R8, lo);
    // 0.2126*200 + 0.7152*100 + 0.0722*50 = 42.52 + 71.52 + 3.61 = 117.65 -> 118
    CHECK(l8[0] == 118);
    CHECK(ConvertPixels(r8.data(), PixelFormat::R8, 1, 1, PixelFormat::RGBA8) == (Bytes{200, 0, 0, 255}));

    // hostile
    CHECK(ConvertPixels(nullptr, PixelFormat::RGBA8, 1, 1, PixelFormat::R8).empty());
    CHECK(ConvertPixels(px.data(), PixelFormat::RGBA8, 0, 1, PixelFormat::R8).empty());
    CHECK(ConvertPixels(px.data(), PixelFormat::RGBA8, 100000, 100000, PixelFormat::R8).empty());
}

static void TestPremulSwizzle() {
    RUN_SUITE("tex convert premultiply / swizzle / pack");
    // (200, 100, 50, 128): 200*128/255 = 100.39 -> 100 ; 100*128/255 = 50.2 -> 50 ; 50*128/255 = 25.1 -> 25
    Bytes p = {200, 100, 50, 128,  255, 255, 255, 0};
    PremultiplyRGBA8(p.data(), 2);
    CHECK(p == (Bytes{100, 50, 25, 128,  0, 0, 0, 0}));
    UnpremultiplyRGBA8(p.data(), 2);
    // 100*255/128 = 199.2 -> 199 ; 50 -> 99.6 -> 100 ; 25 -> 49.8 -> 50
    CHECK(p[0] == 199 && p[1] == 100 && p[2] == 50 && p[3] == 128);
    CHECK(p[4] == 0 && p[7] == 0);
    // unpremultiplying over-range data clamps
    Bytes bad = {255, 0, 0, 10};
    UnpremultiplyRGBA8(bad.data(), 1);
    CHECK(bad[0] == 255);
    // float path premultiply agrees
    Bytes q = {200, 100, 50, 128};
    ConvertOptions po; po.premultiply = true;
    Bytes pq = ConvertPixels(q.data(), PixelFormat::RGBA8, 1, 1, PixelFormat::RGBA8, po);
    CHECK(pq == (Bytes{100, 50, 25, 128}));

    Bytes px = {1, 2, 3, 4,  5, 6, 7, 8};
    CHECK(SwizzleRGBA8(px.data(), 2, "bgra") == (Bytes{3, 2, 1, 4,  7, 6, 5, 8}));
    CHECK(SwizzleRGBA8(px.data(), 2, "rrr1") == (Bytes{1, 1, 1, 255,  5, 5, 5, 255}));
    CHECK(SwizzleRGBA8(px.data(), 2, "0aG0") == (Bytes{0, 4, 2, 0,  0, 8, 6, 0}));
    CHECK(SwizzleRGBA8(px.data(), 2, "rgb").empty());
    CHECK(SwizzleRGBA8(px.data(), 2, "rgbx").empty());
    CHECK(SwizzleRGBA8(nullptr, 2, "rgba").empty());

    Bytes R = {1, 2}, G = {3, 4}, B = {5, 6};
    const std::uint8_t fill[4] = {0, 0, 0, 255};
    CHECK(PackChannelsRGBA8(R.data(), G.data(), B.data(), nullptr, 2, fill) == (Bytes{1, 3, 5, 255,  2, 4, 6, 255}));
    CHECK(ExtractChannelRGBA8(px.data(), 2, 2) == (Bytes{3, 7}));
    CHECK(ExtractChannelRGBA8(px.data(), 2, 4).empty());
}

int main() {
    TestHalf();
    TestFormats();
    TestPremulSwizzle();
    TEST_MAIN_RESULT();
}
