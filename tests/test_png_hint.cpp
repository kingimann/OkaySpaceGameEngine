// A PNG header can claim a big image while the image data is a few bytes. The decoder must refuse it WITHOUT allocating (and
// zero-filling) the claimed size first (the first inflate allocation is capped by what the input could produce, 1032:1).
// Standalone: ImageDecode.cpp + ImageDecodeJpeg.cpp only; a counting operator new measures the peak heap of each decode.
// Written from the PNG specification (chunk layout, IHDR fields, CRC-32) and RFC 1950/1951 (a one-block fixed-Huffman stream).
#include "okay/Core/ImageDecode.hpp"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>

static std::atomic<size_t> g_live{0}, g_peak{0};
static void* CountedAlloc(size_t n) {
    void* raw = std::malloc(n + 16);
    if (!raw) throw std::bad_alloc();
    *static_cast<size_t*>(raw) = n;
    size_t now = g_live.fetch_add(n) + n, pk = g_peak.load();
    while (now > pk && !g_peak.compare_exchange_weak(pk, now)) {}
    return static_cast<char*>(raw) + 16;
}
static void CountedFree(void* p) {
    if (!p) return;
    void* raw = static_cast<char*>(p) - 16;
    g_live.fetch_sub(*static_cast<size_t*>(raw));
    std::free(raw);
}
void* operator new(size_t n) { return CountedAlloc(n); }
void* operator new[](size_t n) { return CountedAlloc(n); }
void operator delete(void* p) noexcept { CountedFree(p); }
void operator delete[](void* p) noexcept { CountedFree(p); }
void operator delete(void* p, size_t) noexcept { CountedFree(p); }
void operator delete[](void* p, size_t) noexcept { CountedFree(p); }

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { ++g_checks; if (!(c)) { ++g_fail; std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static uint32_t Crc(const uint8_t* p, size_t n) {   // PNG CRC-32 (same polynomial as gzip), bit by bit
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; ++i) { c ^= p[i]; for (int k = 0; k < 8; ++k) c = (c & 1) ? (c >> 1) ^ 0xEDB88320u : (c >> 1); }
    return ~c;
}
static void U32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 3; i >= 0; --i) v.push_back(uint8_t(x >> (8 * i))); }
static void Chunk(std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& body) {
    U32(png, uint32_t(body.size()));
    std::vector<uint8_t> tb(type, type + 4);
    tb.insert(tb.end(), body.begin(), body.end());
    png.insert(png.end(), tb.begin(), tb.end());
    U32(png, Crc(tb.data(), tb.size()));
}
// A 10-byte zlib stream holding the two bytes 00 80 (made by Python's zlib at level 9): enough data for a 1 x 1 grey image
// (one filter byte 0 and one pixel byte 0x80), far too little for anything larger.
static std::vector<uint8_t> TinyZlib() {
    return {0x78, 0xda, 0x63, 0x68, 0x00, 0x00, 0x00, 0x82, 0x00, 0x81};
}
static std::vector<uint8_t> MakePng(uint32_t w, uint32_t h, int depth, int ctype) {
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    std::vector<uint8_t> ihdr;
    U32(ihdr, w); U32(ihdr, h);
    ihdr.push_back(uint8_t(depth)); ihdr.push_back(uint8_t(ctype)); ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    Chunk(png, "IHDR", ihdr);
    Chunk(png, "IDAT", TinyZlib());
    Chunk(png, "IEND", {});
    return png;
}

int main() {
    // sanity: the tiny zlib stream really is valid
    std::vector<uint8_t> raw;
    CHECK(okay::Inflate(TinyZlib().data(), TinyZlib().size(), true, raw) && raw.size() == 2 && raw[0] == 0 && raw[1] == 0x80);
    struct Case { uint32_t w, h; int depth, ctype; const char* what; };
    const Case cases[] = {
        {4096, 4096, 16, 6, "4096 x 4096 16-bit RGBA: 134 MB of pixel bytes claimed (under the 64 MB RGBA plausibility guard)"},
        {4000, 4000, 8, 6, "4000 x 4000 8-bit RGBA: 64 MB claimed"},
        {16384, 16384, 16, 6, "16384 x 16384 16-bit RGBA: 2 GiB claimed"},
    };
    for (const Case& c : cases) {
        std::vector<uint8_t> png = MakePng(c.w, c.h, c.depth, c.ctype);
        okay::DecodedImage img; std::string err;
        const size_t before = g_live.load(); g_peak = before;
        bool ok = okay::DecodeImage(png.data(), png.size(), img, &err);
        const size_t peak = g_peak.load() - before;
        CHECK(!ok);
        CHECK(!err.empty());
        CHECK(peak < (size_t(4) << 20));   // refused with next to no allocation (the claim is 64 MB to 2 GiB)
        std::printf("%s: %zu-byte file refused (%s), peak extra heap %zu bytes\n", c.what, png.size(), err.c_str(), peak);
    }
    // a normal image made of the same pieces still decodes: 1 x 1 8-bit grey, pixel 0x80
    {
        std::vector<uint8_t> png = MakePng(1, 1, 8, 0);
        okay::DecodedImage img; std::string err;
        CHECK(okay::DecodeImage(png.data(), png.size(), img, &err));
        CHECK(img.width == 1 && img.height == 1 && img.rgba.size() == 4);
        CHECK(img.rgba.size() == 4 && img.rgba[0] == 0x80 && img.rgba[1] == 0x80 && img.rgba[2] == 0x80 && img.rgba[3] == 255);
    }
    if (g_fail) { std::printf("FAILED: %d of %d checks\n", g_fail, g_checks); return 1; }
    std::printf("ok: %d checks\n", g_checks);
    return 0;
}
