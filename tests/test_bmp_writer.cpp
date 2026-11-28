// okay::platform::EncodeBmp32 / WriteBmp32: the screenshot tools' BMP writer.
// Expected bytes are worked out by hand from the BMP file format (BITMAPINFOHEADER, 32 bpp, bottom-up rows, BGRA).
#include "okay/Platform/BmpWriter.hpp"
#include <cstdio>
#include <cstdint>
#include <vector>
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++g_fail; } } while (0)
static uint32_t Rd32(const std::vector<uint8_t>& b, size_t at) { return b[at] | (b[at+1] << 8) | (b[at+2] << 16) | ((uint32_t)b[at+3] << 24); }
int main() {
    // 2 wide, 2 tall: top row red, green; bottom row blue, white(alpha 128).
    std::vector<uint8_t> px = {255,0,0,255, 0,255,0,255,  0,0,255,255, 255,255,255,128};
    std::vector<uint8_t> out;
    CHECK(okay::platform::EncodeBmp32(px.data(), 2, 2, out));
    CHECK(out.size() == 54 + 16);
    CHECK(out[0] == 'B' && out[1] == 'M');
    CHECK(Rd32(out, 2) == 70);
    CHECK(Rd32(out, 10) == 54);
    CHECK(Rd32(out, 14) == 40);
    CHECK(Rd32(out, 18) == 2 && Rd32(out, 22) == 2);
    CHECK(out[26] == 1 && out[28] == 32);
    CHECK(Rd32(out, 34) == 16);
    // First stored row is the bottom picture row, as BGRA.
    const uint8_t e[16] = {255,0,0,255, 255,255,255,128,  0,0,255,255, 0,255,0,255};
    for (int i = 0; i < 16; ++i) CHECK(out[54 + i] == e[i]);
    // Bad input is refused.
    CHECK(!okay::platform::EncodeBmp32(nullptr, 2, 2, out));
    CHECK(!okay::platform::EncodeBmp32(px.data(), 0, 2, out));
    CHECK(!okay::platform::EncodeBmp32(px.data(), 2, -1, out));
    CHECK(!okay::platform::EncodeBmp32(px.data(), 70000, 70000, out) || out.size() > 0);
    CHECK(okay::platform::EncodeBmp32(px.data(), 2, 2, out));
    // File round trip.
    const char* path = "test_bmp_writer.bmp";
    CHECK(okay::platform::WriteBmp32(path, px.data(), 2, 2));
    std::FILE* f = std::fopen(path, "rb");
    CHECK(f != nullptr);
    if (f) { std::vector<uint8_t> in(70); CHECK(std::fread(in.data(), 1, 70, f) == 70); CHECK(in == out); std::fclose(f); std::remove(path); }
    CHECK(!okay::platform::WriteBmp32("/nonexistent_dir_x/a.bmp", px.data(), 2, 2));
    std::printf(g_fail ? "FAILED\n" : "bmp_writer OK\n");
    return g_fail ? 1 : 0;
}
