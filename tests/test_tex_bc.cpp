// okay::tex block compression (okay/Texture/Bc.hpp). Decoder values are worked out by hand from the block layouts;
// encoder quality is measured as decode(encode(x)) PSNR on synthetic images with fixed thresholds.
#include "test_framework.hpp"
#include "okay/Texture/Bc.hpp"
#include "okay/Texture/Texture.hpp"
#include <cmath>

using namespace okay::tex;
using Bytes = std::vector<std::uint8_t>;

static unsigned g_seed = 777;
static int Rnd(int m) { g_seed = g_seed * 1664525u + 1013904223u; return int((g_seed >> 8) % unsigned(m)); }

static Bytes Make(int w, int h, int kind) {
    Bytes b(std::size_t(w) * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = &b[(std::size_t(y) * w + x) * 4];
            switch (kind) {
                case 0:  // smooth colour gradient
                    p[0] = std::uint8_t(x * 255 / std::max(1, w - 1)); p[1] = std::uint8_t(y * 255 / std::max(1, h - 1));
                    p[2] = std::uint8_t((x + y) * 255 / std::max(1, w + h - 2)); p[3] = std::uint8_t(255 - x * 255 / std::max(1, w - 1)); break;
                case 1:  // two exact 565 colours in a checkerboard of 2x2 cells
                    if (((x / 2) + (y / 2)) & 1) { p[0] = 255; p[1] = 0; p[2] = 0; } else { p[0] = 0; p[1] = 0; p[2] = 255; }
                    p[3] = 255; break;
                case 2:  // noise
                    p[0] = std::uint8_t(Rnd(256)); p[1] = std::uint8_t(Rnd(256)); p[2] = std::uint8_t(Rnd(256)); p[3] = std::uint8_t(Rnd(256)); break;
                case 3: { // soft ball (radial) in grey with alpha
                    double dx = x - w / 2.0, dy = y - h / 2.0, d = std::sqrt(dx * dx + dy * dy) / (w / 2.0);
                    int v = int(std::max(0.0, 1.0 - d) * 255);
                    p[0] = p[1] = p[2] = std::uint8_t(v); p[3] = std::uint8_t(v); break; }
            }
        }
    return b;
}

static double Psnr(const Bytes& a, const Bytes& b, int channels) {
    double se = 0; std::size_t n = 0;
    for (std::size_t i = 0; i + 3 < a.size(); i += 4)
        for (int c = 0; c < channels; ++c) { double d = double(a[i + c]) - b[i + c]; se += d * d; ++n; }
    if (se == 0) return 99;
    return 10 * std::log10(255.0 * 255.0 / (se / double(n)));
}

static void TestDecoderByHand() {
    RUN_SUITE("tex bc decoder by hand");
    // BC1, 4 colour mode: c0 = red 0xF800, c1 = blue 0x001F, texel indices 0,1,2,3 then all 0
    std::uint8_t b1[8] = {0x00, 0xF8, 0x1F, 0x00, 0xE4, 0x00, 0x00, 0x00};
    std::uint8_t px[64];
    DecodeBc1Block(b1, px);
    CHECK(px[0] == 255 && px[1] == 0 && px[2] == 0 && px[3] == 255);          // red
    CHECK(px[4] == 0 && px[5] == 0 && px[6] == 255);                          // blue
    CHECK(px[8] == 170 && px[9] == 0 && px[10] == 85);                        // (2*red+blue)/3
    CHECK(px[12] == 85 && px[13] == 0 && px[14] == 170);                      // (red+2*blue)/3
    CHECK(px[16] == 255);                                                     // second row all index 0 = red
    // 3 colour mode: c0 = blue < c1 = red; index 2 = midpoint (127,0,127), index 3 = transparent
    std::uint8_t b3[8] = {0x1F, 0x00, 0x00, 0xF8, 0xE4, 0x00, 0x00, 0x00};
    DecodeBc1Block(b3, px);
    CHECK(px[0] == 0 && px[2] == 255 && px[3] == 255);
    CHECK(px[8] == 127 && px[9] == 0 && px[10] == 127 && px[11] == 255);
    CHECK(px[12] == 0 && px[15] == 0);
    // 565 expansion of green 0x07E0 -> 255 (6 bit 63 -> 255) and the mid grey 0x8410 -> (132,130,132)
    std::uint8_t bg[8] = {0xE0, 0x07, 0xE0, 0x07, 0, 0, 0, 0};
    DecodeBc1Block(bg, px); CHECK(px[0] == 0 && px[1] == 255 && px[2] == 0);
    std::uint8_t bm[8] = {0x10, 0x84, 0x10, 0x84, 0, 0, 0, 0};
    DecodeBc1Block(bm, px); CHECK(px[0] == 132 && px[1] == 130 && px[2] == 132);

    // BC4, 8 value mode a0 = 255 a1 = 0: codes 2..7 = 218 182 145 109 72 36 (truncating /7)
    // indices 0..7 then 0..7 again (3 bits each): bits = texel i has code (i & 7)
    std::uint64_t bits = 0;
    for (int i = 0; i < 16; ++i) bits |= std::uint64_t(i & 7) << (3 * i);
    std::uint8_t b4[8] = {255, 0};
    for (int i = 0; i < 6; ++i) b4[2 + i] = std::uint8_t(bits >> (8 * i));
    std::uint8_t v[16];
    DecodeBc4Block(b4, v);
    const int want8[8] = {255, 0, 218, 182, 145, 109, 72, 36};
    for (int i = 0; i < 16; ++i) CHECK(v[i] == want8[i & 7]);
    // 6 value mode a0 = 0 a1 = 255: codes 2..5 = 51 102 153 204, 6 = 0, 7 = 255
    b4[0] = 0; b4[1] = 255;
    DecodeBc4Block(b4, v);
    const int want6[8] = {0, 255, 51, 102, 153, 204, 0, 255};
    for (int i = 0; i < 16; ++i) CHECK(v[i] == want6[i & 7]);

    // sizes
    CHECK(BcImageBytes(BcFormat::BC1, 1, 1) == 8);
    CHECK(BcImageBytes(BcFormat::BC3, 5, 3) == 2 * 1 * 16);
    CHECK(BcImageBytes(BcFormat::BC5, 8, 8) == 4 * 16);
    CHECK(BcImageBytes(BcFormat::BC4, 0, 8) == 0);
    BcFormat f; CHECK(ParseBcFormat("BC3", &f) && f == BcFormat::BC3 && !ParseBcFormat("bc7", &f));
}

static void TestExactCases() {
    RUN_SUITE("tex bc exact cases");
    // two exactly representable colours: encoded and decoded losslessly
    Bytes chk = Make(16, 16, 1);
    for (int q = 0; q <= 2; ++q) {
        BcEncodeOptions o; o.quality = q;
        auto enc = EncodeBc(chk.data(), 16, 16, BcFormat::BC1, o);
        CHECK(enc.size() == BcImageBytes(BcFormat::BC1, 16, 16));
        auto dec = DecodeBc(enc.data(), enc.size(), 16, 16, BcFormat::BC1);
        CHECK(dec == chk);
    }
    // solid colours of every grey level stay within the 565 grid error (<= 4)
    for (int v = 0; v < 256; v += 5) {
        Bytes s(4 * 4 * 4, 0);
        for (int i = 0; i < 16; ++i) { s[i*4] = std::uint8_t(v); s[i*4+1] = std::uint8_t(v); s[i*4+2] = std::uint8_t(v); s[i*4+3] = 255; }
        auto d = DecodeBc(EncodeBc(s.data(), 4, 4, BcFormat::BC1).data(), 8, 4, 4, BcFormat::BC1);
        for (int i = 0; i < 16; ++i) CHECK(std::abs(int(d[i*4]) - v) <= 4 && std::abs(int(d[i*4+1]) - v) <= 2);
    }
    // BC4: a solid channel and a block holding only 0 and 255 are exact
    Bytes four(16 * 4, 0);
    for (int i = 0; i < 16; ++i) { four[i*4] = (i % 3 == 0) ? 255 : 0; four[i*4+3] = 255; }
    auto e4 = EncodeBc(four.data(), 4, 4, BcFormat::BC4);
    CHECK(e4.size() == 8);
    auto d4 = DecodeBc(e4.data(), e4.size(), 4, 4, BcFormat::BC4);
    for (int i = 0; i < 16; ++i) CHECK(d4[i*4] == four[i*4] && d4[i*4+3] == 255);
    Bytes sol(16 * 4, 0);
    for (int i = 0; i < 16; ++i) { sol[i*4] = 123; sol[i*4+3] = 255; }
    auto ds = DecodeBc(EncodeBc(sol.data(), 4, 4, BcFormat::BC4).data(), 8, 4, 4, BcFormat::BC4);
    for (int i = 0; i < 16; ++i) CHECK(ds[i*4] == 123);
    // BC1 1-bit alpha: a half transparent block keeps the transparent texels transparent and the others close
    Bytes ha(16 * 4, 0);
    for (int i = 0; i < 16; ++i) { ha[i*4] = 255; ha[i*4+1] = 0; ha[i*4+2] = 0; ha[i*4+3] = (i < 8) ? 0 : 255; }
    BcEncodeOptions ao; ao.bc1AlphaCutoff = 128;
    auto da = DecodeBc(EncodeBc(ha.data(), 4, 4, BcFormat::BC1, ao).data(), 8, 4, 4, BcFormat::BC1);
    for (int i = 0; i < 16; ++i) {
        if (i < 8) CHECK(da[i*4+3] == 0);
        else CHECK(da[i*4+3] == 255 && da[i*4] >= 250 && da[i*4+1] <= 4);
    }
    // an entirely transparent block
    Bytes allT(16 * 4, 0);
    auto dt = DecodeBc(EncodeBc(allT.data(), 4, 4, BcFormat::BC1, ao).data(), 8, 4, 4, BcFormat::BC1);
    for (int i = 0; i < 16; ++i) CHECK(dt[i*4+3] == 0);
}

static void TestQuality() {
    RUN_SUITE("tex bc psnr thresholds");
    Bytes grad = Make(64, 64, 0), noise = Make(64, 64, 2), ball = Make(64, 64, 3);
    struct Case { const Bytes* img; BcFormat f; int ch; double minPsnr; const char* name; };
    const Case cases[] = {
        {&grad, BcFormat::BC1, 3, 36.0, "gradient bc1"},
        {&grad, BcFormat::BC3, 3, 36.0, "gradient bc3 rgb"},
        {&ball, BcFormat::BC1, 3, 40.0, "ball bc1"},
        {&noise, BcFormat::BC1, 3, 12.0, "noise bc1"},
    };
    for (const Case& c : cases) {
        auto enc = EncodeBc(c.img->data(), 64, 64, c.f);
        auto dec = DecodeBc(enc.data(), enc.size(), 64, 64, c.f);
        double p = Psnr(*c.img, dec, c.ch);
        std::cout << "  psnr " << c.name << " " << p << " dB (min " << c.minPsnr << ")\n";
        CHECK(p >= c.minPsnr);
    }
    // alpha of BC3 and single channels of BC4 / BC5
    {
        auto enc = EncodeBc(grad.data(), 64, 64, BcFormat::BC3);
        auto dec = DecodeBc(enc.data(), enc.size(), 64, 64, BcFormat::BC3);
        double se = 0; for (std::size_t i = 3; i < grad.size(); i += 4) { double d = double(grad[i]) - dec[i]; se += d * d; }
        double p = 10 * std::log10(255.0 * 255.0 / (se / (64.0 * 64.0)));
        CHECK(p >= 45.0);
        auto e4 = EncodeBc(grad.data(), 64, 64, BcFormat::BC4);
        auto d4 = DecodeBc(e4.data(), e4.size(), 64, 64, BcFormat::BC4);
        CHECK(Psnr(grad, d4, 1) >= 45.0);
        auto e5 = EncodeBc(grad.data(), 64, 64, BcFormat::BC5);
        auto d5 = DecodeBc(e5.data(), e5.size(), 64, 64, BcFormat::BC5);
        Bytes g2 = grad; for (std::size_t i = 0; i < g2.size(); i += 4) { g2[i+2] = 0; g2[i+3] = 255; }
        CHECK(Psnr(g2, d5, 3) >= 40.0);
        auto en = EncodeBc(noise.data(), 64, 64, BcFormat::BC4);
        auto dn = DecodeBc(en.data(), en.size(), 64, 64, BcFormat::BC4);
        CHECK(Psnr(noise, dn, 1) >= 28.0);     // 3 bits per texel on pure noise
    }
    // higher quality is never worse
    for (const Bytes* img : {&grad, &noise, &ball}) {
        double prev = 0;
        for (int q = 0; q <= 2; ++q) {
            BcEncodeOptions o; o.quality = q;
            auto enc = EncodeBc(img->data(), 64, 64, BcFormat::BC1, o);
            double p = Psnr(*img, DecodeBc(enc.data(), enc.size(), 64, 64, BcFormat::BC1), 3);
            CHECK(p >= prev - 1e-9);
            prev = p;
        }
    }
    // deterministic
    CHECK(EncodeBc(noise.data(), 64, 64, BcFormat::BC3) == EncodeBc(noise.data(), 64, 64, BcFormat::BC3));
}

static void TestOddSizes() {
    RUN_SUITE("tex bc odd sizes / hostile");
    const int sizes[][2] = {{1,1},{2,2},{3,5},{4,4},{5,3},{7,1},{1,9},{13,11},{17,4}};
    for (auto& s : sizes)
        for (BcFormat f : {BcFormat::BC1, BcFormat::BC3, BcFormat::BC4, BcFormat::BC5}) {
            Bytes img = Make(s[0], s[1], 0);
            for (auto& v : img) v = std::uint8_t(100 + v / 8);    // gentle ramp: BC1 can follow it in tiny images too
            auto enc = EncodeBc(img.data(), s[0], s[1], f);
            CHECK(enc.size() == BcImageBytes(f, s[0], s[1]));
            auto dec = DecodeBc(enc.data(), enc.size(), s[0], s[1], f);
            CHECK(dec.size() == img.size());
            if (f == BcFormat::BC1 || f == BcFormat::BC3) { double pp = Psnr(img, dec, 3); if (pp < 28.0) std::cerr << "  " << s[0] << "x" << s[1] << " " << BcName(f) << " " << pp << "\n"; CHECK(pp >= 28.0); }
        }
    Bytes img = Make(8, 8, 0);
    auto enc = EncodeBc(img.data(), 8, 8, BcFormat::BC1);
    CHECK(DecodeBc(enc.data(), enc.size() - 1, 8, 8, BcFormat::BC1).empty());     // truncated
    CHECK(DecodeBc(nullptr, 0, 8, 8, BcFormat::BC1).empty());
    CHECK(DecodeBc(enc.data(), enc.size(), 0, 8, BcFormat::BC1).empty());
    CHECK(EncodeBc(nullptr, 8, 8, BcFormat::BC1).empty());
    CHECK(EncodeBc(img.data(), -4, 8, BcFormat::BC1).empty());
    // any 16 random bytes decode without trouble and re-encode to something that decodes the same size
    for (int t = 0; t < 200; ++t) {
        std::uint8_t blk[16]; for (auto& b : blk) b = std::uint8_t(Rnd(256));
        for (BcFormat f : {BcFormat::BC1, BcFormat::BC3, BcFormat::BC4, BcFormat::BC5}) {
            auto d = DecodeBc(blk, 16, 4, 4, f);
            CHECK(d.size() == 64);
            // decoded data re-encodes to the same picture or better than 99 dB for 565/8 bit exact blocks is not guaranteed: only size
            CHECK(EncodeBc(d.data(), 4, 4, f).size() == BcImageBytes(f, 4, 4));
        }
    }
}

int main() {
    TestDecoderByHand();
    TestExactCases();
    TestQuality();
    TestOddSizes();
    TEST_MAIN_RESULT();
}
