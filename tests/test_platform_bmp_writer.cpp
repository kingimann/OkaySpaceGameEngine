// Pure tests for okay::platform::WriteBmp32Bgra / EncodeBmp32Bgra. Expected bytes are worked out by hand from the BMP file
// format (14-byte file header, 40-byte BITMAPINFOHEADER, bottom-up 32-bit BGRA rows), not by calling the code under test.
#include "okay/Platform/BmpWriter.hpp"
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static uint32_t Rd32(const std::vector<uint8_t>& b, size_t o) { return b[o] | (b[o+1] << 8) | (b[o+2] << 16) | ((uint32_t)b[o+3] << 24); }

int main() {
    const uint32_t px[4] = {0xFF112233u, 0x80445566u, 0x00778899u, 0xFFAABBCCu};
    std::vector<uint8_t> b;
    CHECK(okay::platform::EncodeBmp32Bgra(px, 2, 2, 8, b));
    CHECK(b.size() == 14 + 40 + 16);
    CHECK(b[0] == 'B' && b[1] == 'M');
    CHECK(Rd32(b, 2) == 70);
    CHECK(Rd32(b, 10) == 54);
    CHECK(Rd32(b, 14) == 40);
    CHECK(Rd32(b, 18) == 2 && Rd32(b, 22) == 2);
    CHECK(b[26] == 1 && b[28] == 32);
    CHECK(Rd32(b, 30) == 0);
    CHECK(Rd32(b, 34) == 16);
    // Bottom-up: the first stored row is the last source row.
    CHECK(b[54] == 0x99 && b[55] == 0x88 && b[56] == 0x77 && b[57] == 0x00);
    CHECK(b[58] == 0xCC && b[59] == 0xBB && b[60] == 0xAA && b[61] == 0xFF);
    CHECK(b[62] == 0x33 && b[63] == 0x22 && b[64] == 0x11 && b[65] == 0xFF);
    // Pitch larger than width*4: row padding is skipped.
    const uint32_t wide[8] = {1, 99, 99, 99, 2, 99, 99, 99};
    CHECK(okay::platform::EncodeBmp32Bgra(wide, 1, 2, 16, b));
    CHECK(b.size() == 14 + 40 + 8 && b[54] == 2 && b[58] == 1);
    CHECK(!okay::platform::EncodeBmp32Bgra(nullptr, 2, 2, 8, b));
    CHECK(!okay::platform::EncodeBmp32Bgra(px, 0, 2, 8, b));
    CHECK(!okay::platform::EncodeBmp32Bgra(px, 2, -1, 8, b));
    CHECK(!okay::platform::EncodeBmp32Bgra(px, 2, 2, 4, b));
    const char* path = "test_bmp_writer_out.bmp";
    CHECK(okay::platform::WriteBmp32Bgra(path, px, 2, 2, 8));
    std::ifstream f(path, std::ios::binary);
    std::vector<uint8_t> r((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    CHECK(r.size() == 70 && r[54] == 0x99);
    std::remove(path);
    CHECK(!okay::platform::WriteBmp32Bgra("/nonexistent_dir_xyz/a.bmp", px, 2, 2, 8));
    std::printf(g_fail ? "FAILED %d\n" : "OK\n", g_fail);
    return g_fail ? 1 : 0;
}
