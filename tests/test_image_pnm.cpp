// Tests for our own PNM decoder (P1..P6). Fixtures are hand-written bytes with pixels worked out
// by hand from the Netpbm format description (header, comments, maxval, ASCII or binary rows).
// Scaling to 8 bits is v*255/maxval, rounded to nearest (half up).
#include "okay/Core/ImageDecode.hpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <vector>

static int g_checks = 0, g_fail = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

using Bytes = std::vector<std::uint8_t>;
static Bytes S(const std::string& s) { return Bytes(s.begin(), s.end()); }
static void Add(Bytes& b, std::initializer_list<int> v) { for (int x : v) b.push_back(static_cast<std::uint8_t>(x)); }

static bool Dec(const Bytes& b, okay::DecodedImage& out, std::string* e = nullptr) {
    return okay::DecodePnm(b.data(), b.size(), out, e);
}
static bool Px(const okay::DecodedImage& d, int x, int y, int r, int g, int bl, int a = 255) {
    const std::uint8_t* p = &d.rgba[(static_cast<size_t>(y) * d.width + x) * 4];
    return p[0] == r && p[1] == g && p[2] == bl && p[3] == a;
}

int main() {
    okay::DecodedImage d; std::string e;
    // P1: 3x2 bits, 1 = black, no spaces needed between digits. "101" / "010"
    CHECK(Dec(S("P1\n3 2\n101\n010\n"), d) && d.width == 3 && d.height == 2);
    CHECK(Px(d,0,0,0,0,0) && Px(d,1,0,255,255,255) && Px(d,2,0,0,0,0));
    CHECK(Px(d,0,1,255,255,255) && Px(d,1,1,0,0,0) && Px(d,2,1,255,255,255));
    // P1 with spaces and a comment inside the header
    CHECK(Dec(S("P1 # c\n2 1\n1 0"), d) && Px(d,0,0,0,0,0) && Px(d,1,0,255,255,255));
    // P2: maxval 15, values 0 15 5 -> 0, 255, 85
    CHECK(Dec(S("P2\n3 1\n15\n0 15 5\n"), d) && Px(d,0,0,0,0,0) && Px(d,1,0,255,255,255) && Px(d,2,0,85,85,85));
    // P2 maxval 2: 1 -> (255+1)/2 = 128
    CHECK(Dec(S("P2 1 1 2 1"), d) && Px(d,0,0,128,128,128));
    // P3: maxval 255 two pixels
    CHECK(Dec(S("P3\n2 1\n255\n255 0 0  0 128 255\n"), d) && Px(d,0,0,255,0,0) && Px(d,1,0,0,128,255));
    // P3 maxval 100: 50 -> (50*255+50)/100 = 128 ; 100 -> 255 ; 1 -> (255+50)/100 = 3
    CHECK(Dec(S("P3 1 1 100 50 100 1"), d) && Px(d,0,0,128,255,3));
    // P4: width 10 -> 2 bytes per row; row0 bits 1010101010 -> 0xAA 0x80 ; row1 all 0 -> 0 0
    { Bytes b = S("P4\n10 2\n"); Add(b, {0xAA, 0x80, 0x00, 0x00});
      CHECK(Dec(b, d) && d.width == 10 && d.height == 2);
      CHECK(Px(d,0,0,0,0,0) && Px(d,1,0,255,255,255) && Px(d,8,0,0,0,0) && Px(d,9,0,255,255,255));
      CHECK(Px(d,0,1,255,255,255) && Px(d,9,1,255,255,255)); }
    // P5 8-bit
    { Bytes b = S("P5\n# note\n2 2\n255\n"); Add(b, {0, 64, 128, 255});
      CHECK(Dec(b, d) && Px(d,1,0,64,64,64) && Px(d,0,1,128,128,128) && Px(d,1,1,255,255,255)); }
    // P5 16-bit big endian, maxval 65535: 0x0000 -> 0, 0x8000 (32768) -> (32768*255+32767)/65535 = 128, 0xFFFF -> 255
    { Bytes b = S("P5 3 1 65535\n"); Add(b, {0,0, 0x80,0x00, 0xFF,0xFF});
      CHECK(Dec(b, d) && Px(d,0,0,0,0,0) && Px(d,1,0,128,128,128) && Px(d,2,0,255,255,255)); }
    // P6 8-bit
    { Bytes b = S("P6 2 1 255\n"); Add(b, {10,20,30, 40,50,60});
      CHECK(Dec(b, d) && Px(d,0,0,10,20,30) && Px(d,1,0,40,50,60)); }
    // P6 16-bit maxval 1000: sample 500 (0x01F4) -> (500*255+500)/1000 = 128
    { Bytes b = S("P6 1 1 1000\n"); Add(b, {0x01,0xF4, 0x00,0x00, 0x03,0xE8});
      CHECK(Dec(b, d) && Px(d,0,0,128,0,255)); }
    // Exactly one whitespace byte follows maxval; a following 0x20 is pixel data
    { Bytes b = S("P5 1 1 255 "); Add(b, {0x20}); CHECK(Dec(b, d) && Px(d,0,0,32,32,32)); }
    // Rejections
    CHECK(!Dec(S("P7 1 1 255 0"), d, &e) && !e.empty() && d.rgba.empty());
    CHECK(!Dec(S("P5 0 1 255 "), d));
    CHECK(!Dec(S("P5 1 0 255 "), d));
    CHECK(!Dec(S("P5 99999 1 255 "), d));
    CHECK(!Dec(S("P5 1 1 0 "), d));
    CHECK(!Dec(S("P5 1 1 65536 "), d));
    CHECK(!Dec(S("P2 1 1 255 256"), d));      // sample above maxval
    CHECK(!Dec(S("P3 1 1 255 1 2"), d));      // too few samples
    CHECK(!Dec(S("P1 2 1 1 2"), d));          // bad bit
    { Bytes b = S("P6 2 1 255\n"); Add(b, {1,2,3,4,5}); CHECK(!Dec(b, d)); }   // truncated binary
    { Bytes b = S("P4 9 2\n"); Add(b, {0xFF, 0x80, 0xFF}); CHECK(!Dec(b, d)); } // one byte short
    CHECK(!Dec(S("P"), d)); CHECK(!Dec(S(""), d)); CHECK(!Dec(S("P6"), d));
    CHECK(!Dec(S("P6 9999999999999999999 1 255\n"), d));                        // overflow in the number
    CHECK(!Dec(S("P6 16384 16384 255\nxx"), d));   // huge header, tiny body: fail, do not allocate
    // Every truncation of a valid file fails cleanly
    { Bytes b = S("P6 3 2 255\n"); for (int i = 0; i < 18; ++i) b.push_back(static_cast<std::uint8_t>(i * 7));
      for (size_t n = 0; n < b.size(); ++n) { Bytes t(b.begin(), b.begin() + n); CHECK(!Dec(t, d)); }
      CHECK(Dec(b, d)); }
    // Routing: off by default, on with OKAY_OWN_PNM=1
    { Bytes b = S("P5 1 1 255 "); Add(b, {7});
      unsetenv("OKAY_OWN_PNM");
      CHECK(okay::DetectImageFormat(b.data(), b.size()) == okay::ImageFormat::Unknown);
      CHECK(!okay::DecodeImage(b.data(), b.size(), d));
      setenv("OKAY_OWN_PNM", "1", 1);
      CHECK(okay::DetectImageFormat(b.data(), b.size()) == okay::ImageFormat::PNM);
      CHECK(okay::DecodeImage(b.data(), b.size(), d) && Px(d,0,0,7,7,7));
      int w = 0, h = 0; CHECK(okay::GetImageInfo(b.data(), b.size(), &w, &h) && w == 1 && h == 1);
      unsetenv("OKAY_OWN_PNM"); }
    std::printf("test_image_pnm: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
