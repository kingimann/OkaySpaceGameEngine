// Hostile-input tests and a deterministic mutation fuzz for our own image decoders (engine/src/Core/ImageDecode*.cpp: PNG, JPEG, BMP, TGA,
// GIF, HDR, PSD, PNM, and the inflater). Written from the public file-format descriptions (PNG with its CRC-32 definition, JFIF/JPEG, BMP, TGA,
// GIF89a, Radiance RGBE, PSD, RFC 1950/1951) and nothing else. test_image_decoder already truncates and flips bytes in the small fixtures;
// this adds what it lacks: every fixture of any size, a "lying header field" sweep (every 1, 2 and 4 byte field in the first 128 bytes set to
// extreme values, both byte orders), PNG files with valid CRCs and extreme headers, decompression bombs, huge claimed sizes with tiny data,
// random garbage with and without each magic, GIF/JPEG structure spam, a live-memory budget (counting operator new) and a time limit per
// input. OKAY_READER_FUZZ_ITERS=200000 for a long run.
#include "test_framework.hpp"
#include "okay/Core/ImageDecode.hpp"
#include "okay/Core/ImageWrite.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <new>
#include <sstream>
#include <string>
#include <vector>

using namespace okay;
typedef std::vector<std::uint8_t> Bytes;

// ---- counting allocator: live bytes and the peak since the last reset ----
static std::atomic<std::size_t> g_live{0}, g_peak{0};
static void* CountedAlloc(std::size_t n) {
    void* raw = std::malloc(n + 16);
    if (!raw) throw std::bad_alloc();
    *static_cast<std::size_t*>(raw) = n;
    const std::size_t now = g_live.fetch_add(n) + n;
    std::size_t p = g_peak.load();
    while (now > p && !g_peak.compare_exchange_weak(p, now)) {}
    return static_cast<char*>(raw) + 16;
}
static void CountedFree(void* q) noexcept { if (!q) return; void* raw = static_cast<char*>(q) - 16; g_live.fetch_sub(*static_cast<std::size_t*>(raw)); std::free(raw); }
void* operator new(std::size_t n) { return CountedAlloc(n); }
void* operator new[](std::size_t n) { return CountedAlloc(n); }
void operator delete(void* p) noexcept { CountedFree(p); }
void operator delete[](void* p) noexcept { CountedFree(p); }
void operator delete(void* p, std::size_t) noexcept { CountedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { CountedFree(p); }

static std::uint64_t g_rs = 0x9E3779B97F4A7C15ull;
static std::uint64_t Rnd() { g_rs ^= g_rs << 13; g_rs ^= g_rs >> 7; g_rs ^= g_rs << 17; return g_rs; }
static double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static std::size_t g_worstPeak = 0;
static double g_worstTime = 0;

// A 16384 x 16384 claim would cost 1 GiB of RGBA; hostile input may not make a decoder hold more than this on top of the input.
static constexpr std::size_t kMemCap = 600u * 1024u * 1024u;
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_UNDEFINED__)
static constexpr double kMaxSeconds = 30.0;
#else
static constexpr double kMaxSeconds = 5.0;
#endif

static bool Consistent(const DecodedImage& img) {
    return img.width >= 1 && img.height >= 1 && img.width <= kMaxImageDimension && img.height <= kMaxImageDimension &&
           img.rgba.size() == static_cast<std::size_t>(img.width) * (std::size_t)img.height * 4;
}
// One input through every entry point; checks the contract.
static void Try(const Bytes& v) {
    g_peak.store(g_live.load());
    const std::size_t base = g_live.load();
    const double t0 = Now();
    DecodedImage img; std::string err;
    const bool ok = DecodeImage(v.data(), v.size(), img, &err);
    if (ok) { CHECK(Consistent(img)); } else { CHECK(img.rgba.empty() && !err.empty()); }
    int w = 0, h = 0; ImageFormat f = ImageFormat::Unknown;
    if (GetImageInfo(v.data(), v.size(), &w, &h, &f)) { CHECK(w >= 1 && h >= 1 && w <= kMaxImageDimension && h <= kMaxImageDimension); }
    DecodedAnimation an; std::string e2;
    if (DecodeGifFrames(v.data(), v.size(), an, &e2)) {
        CHECK(an.FrameCount() >= 1 && an.frames.size() == (std::size_t)an.FrameCount() * (std::size_t)an.width * (std::size_t)an.height * 4);
    } else { CHECK(!e2.empty() && an.frames.empty()); }
    const double dt = Now() - t0;
    const std::size_t used = g_peak.load() > base ? g_peak.load() - base : 0;
    if (dt > g_worstTime) g_worstTime = dt;
    if (used > g_worstPeak) g_worstPeak = used;
    CHECK(dt < kMaxSeconds);
    if (used > kMemCap) { std::fprintf(stderr, "  over budget: %zu KiB, input %zu bytes, first bytes:", used / 1024, v.size()); for (std::size_t i = 0; i < v.size() && i < 48; ++i) std::fprintf(stderr, " %02x", v[i]); std::fprintf(stderr, "\n"); }
    CHECK(used <= kMemCap);
}

static void Add(Bytes& b, std::initializer_list<int> l) { for (int x : l) b.push_back((std::uint8_t)x); }
static Bytes ReadFile(const std::string& p) { std::ifstream f(p, std::ios::binary); return Bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }
static void Put(Bytes& b, std::size_t at, std::uint64_t v, int n) { for (int i = 0; i < n && at + (std::size_t)i < b.size(); ++i) b[at + (std::size_t)i] = (std::uint8_t)((v >> (8 * i)) & 255); }
static void PutBE(Bytes& b, std::size_t at, std::uint64_t v, int n) { for (int i = 0; i < n && at + (std::size_t)i < b.size(); ++i) b[at + (std::size_t)i] = (std::uint8_t)((v >> (8 * (n - 1 - i))) & 255); }

static std::uint32_t Crc32(const std::uint8_t* d, std::size_t n) {
    static std::uint32_t t[256]; static bool init = false;
    if (!init) { for (std::uint32_t i = 0; i < 256; ++i) { std::uint32_t c = i; for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; t[i] = c; } init = true; }
    std::uint32_t c = 0xFFFFFFFFu; for (std::size_t i = 0; i < n; ++i) c = t[(c ^ d[i]) & 255] ^ (c >> 8); return c ^ 0xFFFFFFFFu;
}
static void PngChunk(Bytes& out, const char* type, const Bytes& body, bool goodCrc = true) {
    const std::size_t at = out.size(); out.resize(at + 8); PutBE(out, at, body.size(), 4); std::memcpy(&out[at + 4], type, 4);
    out.insert(out.end(), body.begin(), body.end());
    Bytes tb(type, type + 4); tb.insert(tb.end(), body.begin(), body.end());
    std::uint32_t c = Crc32(tb.data(), tb.size()); if (!goodCrc) c ^= 1;
    const std::size_t e = out.size(); out.resize(e + 4); PutBE(out, e, c, 4);
}
static const std::uint8_t kPngSig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
static Bytes Ihdr(std::uint32_t w, std::uint32_t h, int depth, int ctype, int interlace, int comp = 0, int filt = 0) {
    Bytes b(13); PutBE(b, 0, w, 4); PutBE(b, 4, h, 4); b[8] = (std::uint8_t)depth; b[9] = (std::uint8_t)ctype; b[10] = (std::uint8_t)comp; b[11] = (std::uint8_t)filt; b[12] = (std::uint8_t)interlace; return b;
}
static Bytes MakePng(const Bytes& ihdr, const Bytes& idat, const Bytes& extra = Bytes()) {
    Bytes o(kPngSig, kPngSig + 8); PngChunk(o, "IHDR", ihdr); if (!extra.empty()) o.insert(o.end(), extra.begin(), extra.end());
    PngChunk(o, "IDAT", idat); PngChunk(o, "IEND", Bytes()); return o;
}
// A zlib stream of `n` zero bytes: one fixed-Huffman block, a literal 0 and then matches of length 258 at distance 1 (RFC 1951 3.2.6).
static Bytes ZlibZeros(std::size_t n) {
    struct W {
        Bytes b; int bit = 0;
        void Bit(unsigned v) { if (bit == 0) b.push_back(0); b.back() |= (std::uint8_t)((v & 1) << bit); bit = (bit + 1) & 7; }
        void Bits(std::uint32_t v, int c) { for (int i = 0; i < c; ++i) Bit(v >> i); }
        void Huff(std::uint32_t code, int len) { for (int i = len - 1; i >= 0; --i) Bit(code >> i); }
    } w;
    w.b = {0x78, 0x01};
    w.Bits(1, 1); w.Bits(1, 2);
    w.Huff(0x30, 8);                                          // literal 0
    std::size_t left = n > 0 ? n - 1 : 0;
    while (left >= 258) { w.Huff(0xC5, 8); w.Huff(0, 5); left -= 258; }   // length code 285 (258), distance code 0 (1)
    while (left > 0) { w.Huff(0x30, 8); --left; }
    w.Huff(0, 7);                                             // end of block
    return w.b;
}

int main() {
    RUN_SUITE("image_hostile");
    const std::string dir = std::string(OKAY_SOURCE_DIR) + "/tests/data/images/";
    std::vector<std::pair<std::string, Bytes>> corpus;
    {
        std::ifstream man(dir + "manifest.txt"); std::string line;
        while (std::getline(man, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream ss(line); std::string tok;
            while (ss >> tok) {
                if (tok.size() < 5 || tok[tok.size() - 4] != '.') continue;
                Bytes b = ReadFile(dir + tok);
                if (!b.empty()) { corpus.emplace_back(tok, std::move(b)); break; }
            }
        }
    }
    std::printf("  corpus: %zu fixtures\n", corpus.size());
    CHECK(corpus.size() > 100);

    // ---- 1. empty, tiny, magic only, garbage with and without each magic ----
    Try(Bytes());
    for (std::size_t n = 1; n <= 40; ++n) { Try(Bytes(n, 0)); Try(Bytes(n, 0xFF)); }
    {
        const Bytes magics[] = {Bytes(kPngSig, kPngSig + 8), Bytes{0xFF, 0xD8, 0xFF}, Bytes{'B', 'M'}, Bytes{'G', 'I', 'F', '8', '9', 'a'}, Bytes{'G', 'I', 'F', '8', '7', 'a'},
                                Bytes{'#', '?', 'R', 'A', 'D', 'I', 'A', 'N', 'C', 'E', '\n'}, Bytes{'8', 'B', 'P', 'S', 0, 1}, Bytes{}};
        for (const Bytes& m : magics) {
            Try(m);
            for (int round = 0; round < 60; ++round) {
                Bytes b = m; const std::size_t len = Rnd() % 700; for (std::size_t i = 0; i < len; ++i) b.push_back((std::uint8_t)Rnd());
                Try(b);
                for (std::size_t i = m.size(); i < b.size(); ++i) if (Rnd() % 3) b[i] = 0;   // mostly zero: header fields parse as small numbers
                Try(b);
            }
        }
    }

    // ---- 2. truncation of every fixture, plus trailing junk ----
    for (const auto& item : corpus) {
        const Bytes& o = item.second;
        const std::size_t step = o.size() <= 400 ? 1 : o.size() / 150;
        for (std::size_t cut = 0; cut < o.size(); cut += step) Try(Bytes(o.begin(), o.begin() + (std::ptrdiff_t)cut));
        Bytes more = o; more.insert(more.end(), 300, 0xAB); Try(more);
    }

    // ---- 3. lying header fields: 1, 2 and 4 byte fields at every offset of the first 128 bytes, both byte orders ----
    {
        const std::uint64_t vals[] = {0, 1, 0xFF, 0x7FFF, 0x8000, 0xFFFF, 0x10000, 0x7FFFFFFFull, 0x80000000ull, 0xFFFFFFFFull, 0x4001, 0x3FFF};
        long n = 0;
        for (const auto& item : corpus) {
            const Bytes& o = item.second; const std::size_t lim = o.size() < 128 ? o.size() : 128;
            for (std::size_t at = 0; at < lim; ++at)
                for (int width : {1, 2, 4})
                    for (std::uint64_t v : vals) {
                        if ((width == 1 && v > 255) || (width == 2 && v > 0xFFFF)) continue;
                        Bytes a = o; Put(a, at, v, width); Try(a); ++n;
                        if (width > 1) { Bytes b = o; PutBE(b, at, v, width); Try(b); ++n; }
                    }
        }
        std::printf("  header-field sweep: %ld inputs\n", n);
    }

    // ---- 4. PNG with valid CRCs and extreme headers ----
    {
        const Bytes idatSmall = ZlibZeros(1000);
        const std::uint32_t dims[][2] = {{0, 1}, {1, 0}, {1, 1}, {16384, 16384}, {16385, 1}, {1, 16385}, {0x7FFFFFFFu, 1}, {1, 0x7FFFFFFFu}, {0xFFFFFFFFu, 0xFFFFFFFFu},
                                         {0x80000000u, 0x80000000u}, {16384, 1}, {1, 16384}, {65536, 65536}, {46341, 46341}};
        const int depths[] = {0, 1, 2, 4, 8, 16, 3, 255}; const int ctypes[] = {0, 2, 3, 4, 6, 1, 5, 255};
        long n = 0;
        for (auto& d : dims) for (int dep : depths) for (int ct : ctypes) for (int il : {0, 1, 2}) { Try(MakePng(Ihdr(d[0], d[1], dep, ct, il), idatSmall)); ++n; }
        for (int comp = 0; comp < 3; ++comp) for (int filt = 0; filt < 3; ++filt) Try(MakePng(Ihdr(4, 4, 8, 6, 0, comp, filt), ZlibZeros(4 * 4 * 4 + 4)));
        std::printf("  png header grid: %ld inputs\n", n);
        for (std::size_t mb : {1u, 64u, 512u}) Try(MakePng(Ihdr(8, 8, 8, 6, 0), ZlibZeros(mb * 1024u * 1024u)));   // decompression bombs
        Try(MakePng(Ihdr(16384, 16384, 8, 6, 0), ZlibZeros(100)));                                              // big claim, tiny data
        Try(MakePng(Ihdr(16384, 16384, 16, 6, 1), ZlibZeros(100)));
        for (int fb = 0; fb < 256; ++fb) {   // every filter byte value on every scanline (stored deflate block)
            Bytes raw(4 * 13, 0); for (int r = 0; r < 4; ++r) raw[(std::size_t)r * 13] = (std::uint8_t)fb;
            const std::size_t L = raw.size(); Bytes z = {0x78, 0x01, 0x01, (std::uint8_t)(L & 255), (std::uint8_t)(L >> 8), (std::uint8_t)(~L & 255), (std::uint8_t)((~L >> 8) & 255)};
            z.insert(z.end(), raw.begin(), raw.end()); z.insert(z.end(), 4, 0);
            Try(MakePng(Ihdr(3, 4, 8, 6, 0), z));
        }
        { Bytes o(kPngSig, kPngSig + 8); PngChunk(o, "IHDR", Ihdr(2, 2, 8, 6, 0), false); PngChunk(o, "IDAT", ZlibZeros(20)); PngChunk(o, "IEND", Bytes()); Try(o); }   // bad CRC
        { Bytes o(kPngSig, kPngSig + 8); PngChunk(o, "IDAT", ZlibZeros(20)); PngChunk(o, "IHDR", Ihdr(2, 2, 8, 6, 0)); Try(o); }                                          // IDAT before IHDR
        { Bytes o(kPngSig, kPngSig + 8); PngChunk(o, "IHDR", Ihdr(2, 2, 8, 6, 0)); Try(o); }                                                                              // no IDAT
        { Bytes o(kPngSig, kPngSig + 8); PngChunk(o, "IHDR", Ihdr(2, 2, 8, 6, 0)); PngChunk(o, "IHDR", Ihdr(60000, 60000, 8, 6, 0)); PngChunk(o, "IDAT", ZlibZeros(20)); Try(o); }
        for (std::uint64_t len : {0xFFFFFFFFull, 0x80000000ull, 0x7FFFFFFFull, 0x1000000ull}) {   // chunk length lies, in the IHDR and the IDAT
            Bytes o = MakePng(Ihdr(2, 2, 8, 6, 0), ZlibZeros(20)); PutBE(o, 8, len, 4); Try(o);
            Bytes p = MakePng(Ihdr(2, 2, 8, 6, 0), ZlibZeros(20)); PutBE(p, 8 + 12 + 13, len, 4); Try(p);
        }
        for (std::size_t pl : {0u, 1u, 2u, 3u, 255u, 256u * 3u, 257u * 3u, 1000u}) {   // palette sizes, tRNS longer than the palette
            Bytes ex; PngChunk(ex, "PLTE", Bytes(pl, 0x55)); PngChunk(ex, "tRNS", Bytes(pl + 5, 0x80));
            Try(MakePng(Ihdr(4, 4, 8, 3, 0), ZlibZeros(4 * 5), ex)); Try(MakePng(Ihdr(4, 4, 2, 3, 0), ZlibZeros(4 * 5), ex));
        }
        { Bytes o(kPngSig, kPngSig + 8); PngChunk(o, "IHDR", Ihdr(2, 2, 8, 6, 0)); for (int i = 0; i < 20000; ++i) PngChunk(o, "IDAT", Bytes()); PngChunk(o, "IEND", Bytes()); Try(o); }
        { Bytes o(kPngSig, kPngSig + 8); PngChunk(o, "IHDR", Ihdr(2, 2, 8, 6, 0)); for (int i = 0; i < 20000; ++i) PngChunk(o, "tEXt", Bytes(3, 'a')); PngChunk(o, "IDAT", ZlibZeros(20)); PngChunk(o, "IEND", Bytes()); Try(o); }
    }

    // ---- 5. the inflater on its own ----
    {
        for (std::size_t cap : {0u, 1u, 100u, 1u << 20})
            for (int round = 0; round < 400; ++round) {
                Bytes z; const std::size_t len = Rnd() % 200; for (std::size_t i = 0; i < len; ++i) z.push_back((std::uint8_t)Rnd());
                Bytes out; const bool ok = Inflate(z.data(), z.size(), (round & 1) != 0, out, cap);
                if (ok) { CHECK(out.size() <= cap); } else { CHECK(out.empty()); }
            }
        const Bytes bomb = ZlibZeros(300u * 1024u * 1024u); Bytes out;
        g_peak.store(g_live.load()); const std::size_t before = g_live.load();
        CHECK(!Inflate(bomb.data(), bomb.size(), true, out, 1000) && out.empty());
        CHECK(g_peak.load() < before + 64u * 1024u * 1024u);
        Bytes e; CHECK(!Inflate(nullptr, 0, true, e, 1000)); CHECK(!Inflate(nullptr, 0, false, e, 1000));
    }

    // ---- 6. hand-built BMP / TGA / HDR / PSD / GIF with lying sizes ----
    {
        auto bmp = [](std::int32_t w, std::int32_t h, int bpp, std::uint32_t comp, std::uint32_t clr, std::uint32_t off, std::size_t pix, std::uint32_t hdrSize) {
            Bytes b(14 + 40 + pix, 0x7F); b[0] = 'B'; b[1] = 'M'; Put(b, 2, b.size(), 4); Put(b, 10, off, 4); Put(b, 14, hdrSize, 4);
            Put(b, 18, (std::uint32_t)w, 4); Put(b, 22, (std::uint32_t)h, 4); Put(b, 26, 1, 2); Put(b, 28, (std::uint32_t)bpp, 2); Put(b, 30, comp, 4); Put(b, 46, clr, 4); return b; };
        const std::int32_t ds[] = {0, 1, 3, -1, -3, 16384, 16385, 0x7FFFFFFF, (std::int32_t)0x80000000, 65536, 46341};
        for (std::int32_t w : ds) for (std::int32_t h : ds) for (int bpp : {1, 4, 8, 16, 24, 32, 0, 7, 64}) Try(bmp(w, h, bpp, 0, 0, 54, 64, 40));
        for (std::uint32_t off : {0u, 13u, 14u, 53u, 54u, 55u, 1000u, 0x7FFFFFFFu, 0xFFFFFFFFu}) Try(bmp(4, 4, 24, 0, 0, off, 64, 40));
        for (std::uint32_t clr : {0u, 1u, 256u, 257u, 0x7FFFFFFFu, 0xFFFFFFFFu}) for (int bpp : {1, 4, 8}) Try(bmp(4, 4, bpp, 0, clr, 54, 64, 40));
        for (std::uint32_t hs : {0u, 12u, 16u, 39u, 40u, 52u, 56u, 108u, 124u, 125u, 0xFFFFFFFFu}) Try(bmp(4, 4, 24, 0, 0, 54, 64, hs));
        for (std::uint32_t comp = 0; comp < 8; ++comp) for (int bpp : {16, 32}) Try(bmp(4, 4, bpp, comp, 0, 54, 64, 40));

        auto tga = [](int type, int w, int h, int bpp, int cmType, int cmLen, int cmDepth, std::size_t pix) {
            Bytes b(18 + pix, 0x81); b[0] = 0; b[1] = (std::uint8_t)cmType; b[2] = (std::uint8_t)type; Put(b, 5, (std::uint32_t)cmLen, 2); b[7] = (std::uint8_t)cmDepth;
            Put(b, 12, (std::uint32_t)w, 2); Put(b, 14, (std::uint32_t)h, 2); b[16] = (std::uint8_t)bpp; b[17] = 0; return b; };
        for (int type : {0, 1, 2, 3, 9, 10, 11, 32, 33, 255}) for (int w : {0, 1, 5, 16384, 16385, 65535}) for (int h : {0, 1, 5, 16384, 65535}) for (int bpp : {0, 8, 15, 16, 24, 32, 255})
            Try(tga(type, w, h, bpp, (type == 1 || type == 9) ? 1 : 0, 256, 24, 50));
        for (int cmLen : {0, 1, 255, 256, 65535}) for (int cmDepth : {0, 8, 15, 16, 24, 32, 255}) Try(tga(1, 4, 4, 8, 1, cmLen, cmDepth, 30));
        { Bytes b = tga(10, 4, 4, 32, 0, 0, 0, 20); for (std::size_t i = 18; i < b.size(); ++i) b[i] = 0xFF; Try(b); }       // RLE runs of 128 pixels in a 16 pixel image
        { Bytes b = tga(10, 16384, 16384, 32, 0, 0, 0, 20); for (std::size_t i = 18; i < b.size(); ++i) b[i] = 0xFF; Try(b); }

        const char* hdrs[] = {"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 4 +X 4\n", "#?RADIANCE\n\n-Y 0 +X 0\n", "#?RADIANCE\n\n-Y 16384 +X 16384\n", "#?RADIANCE\n\n-Y 99999999999 +X 1\n",
                              "#?RADIANCE\n\n-Y -5 +X 5\n", "#?RADIANCE\n\n+Y 4 +X 4\n", "#?RADIANCE\n\n-X 4 +Y 4\n", "#?RADIANCE\n\n-Y 4 +X 8\n", "#?RADIANCE\n\n-Y 4 +X 32768\n", "#?RADIANCE\n\n",
                              "#?RADIANCE\n", "#?RADIANCE\nEXPOSURE=1e999\n\n-Y 2 +X 2\n", "#?RGBE\n\n-Y 2 +X 2\n", "#?RADIANCE\n\n-Y 2 +X 2"};
        for (const char* hd : hdrs) for (int tail : {0, 3, 16, 40, 4096}) for (int fill : {0x00, 0x02, 0xFF, 0x80}) { Bytes b(hd, hd + std::strlen(hd)); b.insert(b.end(), (std::size_t)tail, (std::uint8_t)fill); Try(b); }
        { Bytes b(hdrs[0], hdrs[0] + std::strlen(hdrs[0])); for (int r = 0; r < 4; ++r) { b.push_back(2); b.push_back(2); b.push_back(0); b.push_back(4); for (int i = 0; i < 4; ++i) { b.push_back(130); b.push_back(1); b.push_back(1); b.push_back(1); } } Try(b); }
        { Bytes b(hdrs[0], hdrs[0] + std::strlen(hdrs[0])); for (int r = 0; r < 4; ++r) { b.push_back(2); b.push_back(2); b.push_back(0); b.push_back(4); for (int i = 0; i < 200; ++i) b.push_back(0xFF); } Try(b); }
        { std::string h(100000, 'x'); Bytes b(h.begin(), h.end()); Try(b); const char* m = "#?RADIANCE\n"; b.insert(b.begin(), m, m + 11); Try(b); }   // a header line that never ends

        auto psd = [](std::uint32_t w, std::uint32_t h, int ch, int depth, int mode, int comp, std::size_t body) {
            Bytes b(42 + body, 0x80); std::memcpy(&b[0], "8BPS", 4); PutBE(b, 4, 1, 2); PutBE(b, 12, (std::uint32_t)ch, 2); PutBE(b, 14, h, 4); PutBE(b, 18, w, 4); PutBE(b, 22, (std::uint32_t)depth, 2);
            PutBE(b, 24, (std::uint32_t)mode, 2); PutBE(b, 26, 0, 4); PutBE(b, 30, 0, 4); PutBE(b, 34, 0, 4); PutBE(b, 38, (std::uint32_t)comp, 2); return b; };
        for (std::uint32_t w : {0u, 1u, 4u, 16384u, 16385u, 30000u, 0xFFFFFFFFu}) for (std::uint32_t h : {0u, 4u, 16384u, 0xFFFFFFFFu}) for (int ch : {0, 1, 3, 4, 5, 56, 57, 65535}) for (int dep : {1, 8, 16, 32}) for (int comp : {0, 1, 2, 3})
            Try(psd(w, h, ch, dep, 3, comp, 40));
        for (int mode : {0, 1, 2, 3, 4, 7, 8, 9, 65535}) Try(psd(4, 4, 3, 8, mode, 0, 100));
        for (std::uint32_t sect : {1u, 100u, 0x7FFFFFFFu, 0xFFFFFFFFu}) for (int at : {26, 30, 34}) { Bytes b = psd(4, 4, 3, 8, 3, 1, 100); PutBE(b, (std::size_t)at, sect, 4); Try(b); }

        auto gif = [](int w, int h, int flags, const Bytes& body) { Bytes b = {'G', 'I', 'F', '8', '9', 'a', 0, 0, 0, 0, (std::uint8_t)flags, 0, 0}; Put(b, 6, (std::uint32_t)w, 2); Put(b, 8, (std::uint32_t)h, 2); b.insert(b.end(), body.begin(), body.end()); return b; };
        for (int w : {0, 1, 8, 16384, 16385, 65535}) for (int h : {0, 1, 8, 16384, 65535}) for (int fl : {0x00, 0x80, 0xF7, 0x87}) Try(gif(w, h, fl, Bytes(40, 0x2C)));
        for (int mc : {0, 1, 2, 7, 8, 9, 12, 13, 255}) {
            const Bytes body = {0x2C, 0, 0, 0, 0, 4, 0, 4, 0, 0, (std::uint8_t)mc, 5, 0x84, 0x1F, 0x00, 0x55, 0x66, 0, 0x3B};
            Try(gif(4, 4, 0x00, body)); Bytes t(6, 0x10); t.insert(t.end(), body.begin(), body.end()); Try(gif(4, 4, 0x80, t));
        }
        for (int ox : {0, 3, 4, 65535}) for (int fw : {0, 1, 5, 65535}) {
            Bytes t(6, 0x20); const Bytes body = {0x2C, (std::uint8_t)(ox & 255), (std::uint8_t)(ox >> 8), 0, 0, (std::uint8_t)(fw & 255), (std::uint8_t)(fw >> 8), 4, 0, 0, 2, 2, 0x44, 0x01, 0, 0x3B};
            t.insert(t.end(), body.begin(), body.end()); Try(gif(4, 4, 0x80, t));
        }
        {   // thousands of frames on a large canvas: the animation budget must hold
            Bytes t(6, 0x20);
            for (int f = 0; f < 3000; ++f) { const Bytes fr = {0x21, 0xF9, 4, 0, 1, 0, 0, 0, 0x2C, 0, 0, 0, 0, 1, 0, 1, 0, 0, 2, 2, 0x44, 0x01, 0}; t.insert(t.end(), fr.begin(), fr.end()); }
            t.push_back(0x3B); Try(gif(4000, 4000, 0x80, t)); Try(gif(1, 1, 0x80, t));
        }
        {   // a few one-pixel frames on a huge canvas, padded with comments so the header passes the size check: the frames
            // vector must not grow past the budget (old + new buffer while it grows), and ~52 KB must not become 600+ MiB.
            for (int side : {4000, 4608, 4700}) {
                Bytes t(6, 0x20);
                for (int i = 0; i < 200; ++i) { t.push_back(0x21); t.push_back(0xFE); t.push_back(255); t.insert(t.end(), 255, 'x'); t.push_back(0); }
                for (int f = 0; f < 10; ++f) { const Bytes fr = {0x21, 0xF9, 4, 0, 1, 0, 0, 0, 0x2C, 0, 0, 0, 0, 1, 0, 1, 0, 0, 2, 2, 0x44, 0x01, 0}; t.insert(t.end(), fr.begin(), fr.end()); }
                t.push_back(0x3B); Try(gif(side, side, 0x80, t));
            }
        }
        { Try(gif(4, 4, 0, Bytes(70000, 0x21))); Bytes b2; for (int i = 0; i < 20000; ++i) { b2.push_back(0x21); b2.push_back(0xFE); b2.push_back(0); } Try(gif(4, 4, 0, b2)); }
        { Bytes body = {0x2C, 0, 0, 0, 0, 4, 0, 4, 0, 0, 8}; for (int i = 0; i < 4000; ++i) { body.push_back(255); body.insert(body.end(), 255, 0xFF); } body.push_back(0); body.push_back(0x3B); Try(gif(4, 4, 0, body)); }   // LZW data far larger than the image
    }

    // ---- 7. JPEG: lying marker lengths and segment bodies, SOF dimensions, component counts, structure edits ----
    {
        for (const auto& item : corpus) {
            if (item.first.size() < 4 || item.first.compare(item.first.size() - 4, 4, ".jpg") != 0) continue;
            const Bytes& o = item.second;
            for (std::size_t i = 2; i + 4 < o.size(); ++i) {
                if (o[i] != 0xFF || o[i + 1] == 0 || o[i + 1] == 0xFF || (o[i + 1] >= 0xD0 && o[i + 1] <= 0xD9)) continue;
                for (std::uint32_t len : {0u, 1u, 2u, 3u, 0xFFFFu, 0x7FFFu}) { Bytes a = o; PutBE(a, i + 2, len, 2); Try(a); }
                for (int v : {0, 1, 0xFF, 0x80}) for (std::size_t k = 4; k < 14 && i + k < o.size(); ++k) { Bytes a = o; a[i + k] = (std::uint8_t)v; Try(a); }
            }
            for (std::size_t i = 2; i + 10 < o.size(); ++i) if (o[i] == 0xFF && (o[i + 1] == 0xC0 || o[i + 1] == 0xC1 || o[i + 1] == 0xC2)) {
                for (std::uint32_t w : {0u, 1u, 16384u, 16385u, 0xFFFFu}) for (std::uint32_t h : {0u, 1u, 16384u, 0xFFFFu}) { Bytes a = o; PutBE(a, i + 5, h, 2); PutBE(a, i + 7, w, 2); Try(a); }
                for (int nc : {0, 1, 2, 3, 4, 255}) { Bytes a = o; a[i + 9] = (std::uint8_t)nc; Try(a); }
                break;
            }
            for (int round = 0; round < 40; ++round) {
                Bytes a = o; const std::size_t at = Rnd() % a.size(); const std::size_t len = 1 + Rnd() % 64; const std::size_t end = std::min(a.size(), at + len);
                if (round & 1) a.erase(a.begin() + (std::ptrdiff_t)at, a.begin() + (std::ptrdiff_t)end);
                else { const Bytes piece(a.begin() + (std::ptrdiff_t)at, a.begin() + (std::ptrdiff_t)end); a.insert(a.begin() + (std::ptrdiff_t)at, piece.begin(), piece.end()); }
                Try(a);
            }
        }
        { Bytes b = {0xFF, 0xD8}; for (int i = 0; i < 100000; ++i) { b.push_back(0xFF); b.push_back((std::uint8_t)(0xD0 + (i & 7))); } Try(b); }
        { Bytes b = {0xFF, 0xD8}; b.insert(b.end(), 100000, 0xFF); Try(b); }
        { Bytes b = {0xFF, 0xD8}; for (int i = 0; i < 3000; ++i) { b.push_back(0xFF); b.push_back(0xFE); b.push_back(0); b.push_back(2); } Try(b); }
    }

    // ---- 8. random mutation fuzz of every fixture (PNG chunk CRCs repaired half of the time), plus encoder output ----
    {
        std::vector<Bytes> seeds; for (const auto& c : corpus) seeds.push_back(c.second);
        for (int ch = 1; ch <= 4; ++ch) { Bytes px((std::size_t)9 * 7 * (std::size_t)ch); for (auto& p : px) p = (std::uint8_t)Rnd(); seeds.push_back(ImageWrite::EncodePNG(9, 7, ch, px.data())); }
        const char* e = std::getenv("OKAY_READER_FUZZ_ITERS"); const long iters = e ? std::atol(e) : 12000L;
        for (long it = 0; it < iters; ++it) {
            Bytes s = seeds[Rnd() % seeds.size()]; const int ops = 1 + (int)(Rnd() % 5);
            for (int o = 0; o < ops && !s.empty(); ++o) {
                const std::size_t at = Rnd() % s.size();
                switch (Rnd() % 7) {
                case 0: s[at] = (std::uint8_t)Rnd(); break;
                case 1: s[at] ^= (std::uint8_t)(1u << (Rnd() % 8)); break;
                case 2: Put(s, at, Rnd() % 3 == 0 ? 0xFFFFFFFFull : Rnd() % 70000, 4); break;
                case 3: s.insert(s.begin() + (std::ptrdiff_t)at, 1 + Rnd() % 8, (std::uint8_t)Rnd()); break;
                case 4: s.erase(s.begin() + (std::ptrdiff_t)at, s.begin() + (std::ptrdiff_t)std::min(s.size(), at + 1 + Rnd() % 16)); break;
                case 5: s.resize(at + 1); break;
                default: PutBE(s, at, Rnd() % 4 == 0 ? 0xFFFF : Rnd() % 100, 2); break;
                }
            }
            if (s.size() > 20 && std::memcmp(s.data(), kPngSig, 8) == 0 && (Rnd() & 1)) {
                std::size_t p = 8;
                while (p + 12 <= s.size()) {
                    const std::uint32_t len = ((std::uint32_t)s[p] << 24) | ((std::uint32_t)s[p + 1] << 16) | ((std::uint32_t)s[p + 2] << 8) | s[p + 3];
                    if ((std::size_t)len > s.size() - p - 12) break;
                    PutBE(s, p + 8 + len, Crc32(&s[p + 4], 4 + (std::size_t)len), 4); p += 12 + (std::size_t)len;
                }
            }
            Try(s);
        }
        std::printf("  fuzz: %ld iterations\n", iters);
    }
    // ---- 9. PNM (P1..P6, our own decoder, OKAY_OWN_PNM=1): truncations, lying headers, comment storms, garbage ----
    {
        const char* oldEnv = std::getenv("OKAY_OWN_PNM"); const std::string saved = oldEnv ? oldEnv : ""; const bool had = oldEnv != nullptr;
        setenv("OKAY_OWN_PNM", "1", 1);
        long n = 0;
        // One input through DecodeImage / GetImageInfo (Try), and through DecodePnm directly; both must agree on success.
        // lowMem: the header lies about the size, so the decoder must fail before it allocates anything big.
        auto TryPnm = [&](const Bytes& v, int expect /* -1 any, 0 must fail, 1 must succeed */, bool lowMem) {
            ++n;
            Try(v);
            g_peak.store(g_live.load()); const std::size_t base = g_live.load(); const double t0 = Now();
            DecodedImage img; std::string err;
            const bool ok = DecodePnm(v.data(), v.size(), img, &err);
            const double dt = Now() - t0; const std::size_t used = g_peak.load() > base ? g_peak.load() - base : 0;
            if (ok) { CHECK(Consistent(img)); } else { CHECK(img.rgba.empty() && !err.empty()); }
            if (expect == 0) CHECK(!ok);
            if (expect == 1) CHECK(ok);
            CHECK(dt < kMaxSeconds);
            CHECK(used <= kMemCap);
            if (lowMem) {
                if (used >= (1u << 20)) std::fprintf(stderr, "  PNM lowMem violated: %zu bytes, header '%.24s'\n", used, std::string(v.begin(), v.begin() + (std::ptrdiff_t)std::min<std::size_t>(v.size(), 24)).c_str());
                CHECK(used < (1u << 20));
            }
            DecodedImage via; std::string e2;
            const bool ok2 = DecodeImage(v.data(), v.size(), via, &e2);
            if (ok2) { CHECK(ok); }            // the routed path may never accept what the direct decoder rejects
            if (ok && v.size() >= 3 && v[0] == 'P' && v[1] >= '1' && v[1] <= '6' && (v[2] == ' ' || v[2] == '\n' || v[2] == '\t' || v[2] == '\r')) { CHECK(ok2 && via.width == img.width && via.rgba == img.rgba); }
        };
        auto S = [](const std::string& s) { return Bytes(s.begin(), s.end()); };
        auto Cat = [](Bytes a, const Bytes& b) { a.insert(a.end(), b.begin(), b.end()); return a; };
        auto Rand = [](std::size_t k) { Bytes b(k); for (auto& x : b) x = (std::uint8_t)Rnd(); return b; };

        // valid fixtures P1..P6 (P4 with padded rows, 16-bit P5/P6, ASCII P2/P3 with comments)
        std::vector<Bytes> fx;
        fx.push_back(S("P1\n# c\n5 3\n10101 01010\n11111\n"));
        fx.push_back(S("P2\n3 2\n255\n0 10 20\n30 40 255\n"));
        fx.push_back(S("P2\n2 2\n65535\n0 65535 1000 40000\n"));
        fx.push_back(S("P3\n# c\n2 2\n255\n255 0 0  0 255 0\n0 0 255  9 9 9\n"));
        { Bytes b = S("P4\n10 3\n"); Add(b, {0xAA, 0x80, 0x55, 0x40, 0xFF, 0xC0}); fx.push_back(b); }
        { Bytes b = S("P5\n3 2\n255\n"); Add(b, {0, 50, 100, 150, 200, 255}); fx.push_back(b); }
        { Bytes b = S("P5\n2 2\n65535\n"); Add(b, {0, 0, 0x80, 0, 0xFF, 0xFF, 0x12, 0x34}); fx.push_back(b); }
        { Bytes b = S("P6\n2 2\n255\n"); b.insert(b.end(), 12, 0x77); fx.push_back(b); }
        { Bytes b = S("P6\n2 1\n1000\n"); Add(b, {0, 0, 1, 0, 3, 0xE8, 0, 1, 0, 2, 0, 3}); fx.push_back(b); }
        for (const Bytes& f : fx) {
            TryPnm(f, 1, false);
            for (std::size_t cut = 0; cut < f.size(); ++cut) TryPnm(Bytes(f.begin(), f.begin() + (std::ptrdiff_t)cut), -1, false);
            for (std::size_t cut = 0; cut + 1 < f.size(); ++cut) { Bytes g = f; g.insert(g.begin() + (std::ptrdiff_t)cut, '#'); TryPnm(g, -1, false); }   // a comment opened anywhere
            TryPnm(Cat(f, Bytes(300, 0xAB)), -1, false);                                                         // extra trailing bytes
            TryPnm(Cat(f, S("\n# trailing comment, no newline")), -1, false);
        }
        // binary kinds must fail when a single byte of body is missing, and must not read past the end
        for (int k = 4; k <= 6; ++k) {
            Bytes full = S("P" + std::to_string(k) + "\n9 3\n" + (k == 4 ? "" : "255\n")); const std::size_t need = (k == 4 ? 2 * 3 : k == 5 ? 27 : 81);
            TryPnm(Cat(full, Bytes(need, 0)), 1, false); TryPnm(Cat(full, Bytes(need - 1, 0)), 0, true);
        }
        { Bytes b = S("P5\n3 3\n65535\n"); TryPnm(Cat(b, Bytes(18, 0)), 1, false); TryPnm(Cat(b, Bytes(17, 0)), 0, true); }

        // lying header fields: every kind, width x height over a list of extremes, tiny body
        const char* vals[] = {"0", "1", "2", "65535", "65536", "2147483647", "2147483648", "4294967295", "4294967296", "18446744073709551615", "18446744073709551616", "100000000", "100000001", "-1", "-0", "+5", "abc", "1.5", "0x10",
                              "123456789012345678901234567890", "000000000000000000000000000000002", "16384", "16385", "32768", "99999999999", ""};
        for (int k = 1; k <= 6; ++k) {
            const std::string kk = "P" + std::to_string(k);
            for (const char* w : vals) for (const char* h : vals) {
                const std::string mv = (k == 1 || k == 4) ? "" : " 255";
                TryPnm(Cat(S(kk + " " + w + " " + h + mv + "\n"), Bytes(12, '1')), -1, true);
            }
            if (k == 1 || k == 4) continue;
            for (const char* m : vals) {
                TryPnm(Cat(S(kk + " 2 2 " + m + "\n"), Bytes(24, '1')), -1, true);
                TryPnm(Cat(S(kk + " 2 2 " + m + "\n"), Bytes(24, 0)), -1, true);
            }
            for (const char* m : {"1", "255", "256", "65535", "65536", "0"})   // maxval x oversized dimensions
                for (const char* d : {"16384", "16385", "46341", "65536", "2147483647", "4294967295", "100000000", "100000001"}) {
                    TryPnm(S(kk + " " + d + " " + d + " " + m + "\n"), 0, true);
                    TryPnm(S(kk + " " + d + " 1 " + m + "\n" + std::string(16, 'x')), 0, true);
                    TryPnm(S(kk + " 1 " + d + " " + m + "\n" + std::string(16, 'x')), 0, true);
                }
        }
        // huge claimed dimensions with tiny data
        for (int k = 1; k <= 6; ++k) for (const char* d : {"16384 16384", "100000 100000", "16384 1", "1 16384", "8192 8192", "46341 46341"}) {
            const std::string h = std::string("P") + char('0' + k) + " " + d + ((k == 1 || k == 4) ? "" : " 255") + "\n";
            TryPnm(Cat(S(h), Bytes(100, '0')), 0, true); TryPnm(S(h), 0, true); TryPnm(Cat(S(h), Rand(64)), 0, true);
        }
        // a 16384 x 16384 P4 with just one byte too few is refused at once; the P5 16-bit twin as well
        { Bytes b = S("P4 16384 16384\n"); b.insert(b.end(), (16384 / 8) * 16384 - 1, 0); TryPnm(b, 0, true); }
        { Bytes b = S("P5 16384 16384 65535\n"); b.insert(b.end(), 16384u * 16384u * 2u - 1u, 0); TryPnm(b, 0, true); }

        // comment storms
        { Bytes b = S("P6\n#"); b.insert(b.end(), 10u * 1024u * 1024u, 'c'); Add(b, {'\n'}); Bytes t = S("1 1\n255\n"); b.insert(b.end(), t.begin(), t.end()); Add(b, {1, 2, 3}); TryPnm(b, 1, false); }
        { Bytes b = S("P6\n#"); b.insert(b.end(), 10u * 1024u * 1024u, 'c'); TryPnm(b, 0, false); }                                   // 10 MB comment, nothing after it
        { Bytes b = S("P3 # a\n#b\n\n#\n2#c\n1 # d\n#\n255#e\n 1 2 3#f\n 4 5 6 #g"); TryPnm(b, 1, false); }                            // comments between every token, none newline-terminated at the end
        { Bytes b = S("P2\n1 1\n255\n7 #"); TryPnm(b, 1, false); }
        { Bytes b = S("P1\n1 1\n#"); TryPnm(b, 0, false); }
        { Bytes b = S("P5\n1 1\n255#c\n"); Add(b, {9}); TryPnm(b, -1, false); }                                                        // comment right before the data: the newline is the one separator
        { Bytes b = S("P3\n"); for (int i = 0; i < 200000; ++i) b.insert(b.end(), {'#', '\n'}); Bytes t = S("1 1 255 1 2 3"); b.insert(b.end(), t.begin(), t.end()); TryPnm(b, 1, false); }
        { Bytes b = S("P1\n64 64\n"); for (int i = 0; i < 4096; ++i) { b.push_back('#'); b.push_back('\r'); b.push_back('1'); } TryPnm(b, -1, false); }
        { Bytes b = S("P1\n2 2\n1#x\n0#y\n1#z\n0"); TryPnm(b, 1, false); }

        // whitespace after the header: none, one, two, odd bytes, NUL
        for (int k = 4; k <= 6; ++k) {
            const std::string h = "P" + std::to_string(k) + " 2 2" + ((k == 4) ? "" : " 255");
            Bytes body((k == 4) ? 2 : k == 5 ? 4 : 12, 0x55);
            TryPnm(Cat(S(h), body), 0, false);                                           // no separator at all
            for (char sp : {' ', '\n', '\t', '\r', '\v', '\f'}) TryPnm(Cat(S(h + sp), body), 1, false);
            TryPnm(Cat(S(h + "\r\n"), body), -1, false);                                 // two white space bytes: the second is data, so the body is one byte short
            TryPnm(Cat(S(h + "  "), body), -1, false);
            TryPnm(Cat(S(h + std::string(1, '\0')), body), -1, false);
            TryPnm(Cat(S(h + "\n\n\n\n"), body), -1, false);
        }
        for (const char* h : {"P3 2 1 255 1 2 3 4 5 6", "P3\t2\r\n1\f255\v1 2 3 4 5 6", "P3  2   1  255   1   2   3   4   5   6   ", "P3 2 1 255\n1 2 3 4 5 6\n\n\n", "P2 2 1 255 1 2", "P2 2 1 255 1", "P2 2 1 255 1 2 3"}) TryPnm(S(h), -1, false);
        for (const char* h : {"P", "P ", "P1", "P1 ", "P1 1", "P1 1 1", "P2 1 1", "P2 1 1 255", "P3 1 1 255 1 2", "P7\n1 1\n255\n123", "P0 1 1 1", "P9 1 1", "Q1 1 1", "p1 1 1 1", "P11 1 1 1", "P6", "P6\n", "P6\n1", "P6\n1 1", "P6\n1 1\n255"}) TryPnm(S(h), -1, false);
        { Bytes b = {'P', '1', '\0', '1', ' ', '1', ' ', '0'}; TryPnm(b, -1, false); }
        { Bytes b = {'P', '1', ' ', '1', ' ', '1', ' ', '0'}; TryPnm(b, 1, false); }
        { Bytes b = S("P7\n1 1\n255\n"); b.insert(b.end(), 3, 7); TryPnm(b, 0, false); }                                                 // P7 (PAM) is not handled; the router must say so
        { Bytes b = S("P7\nWIDTH 1\nHEIGHT 1\nDEPTH 3\nMAXVAL 255\nTUPLTYPE RGB\nENDHDR\n"); b.insert(b.end(), 3, 7); TryPnm(b, 0, false); }
        TryPnm(Bytes{'P'}, 0, false); TryPnm(Bytes{'P', '7'}, 0, false); TryPnm(Bytes{'P', '3'}, 0, false); TryPnm(Bytes{'P', '3', '\n'}, 0, false);

        // maxval and samples above it, 16-bit edge cases
        TryPnm(S("P2 1 1 15 16"), 0, false); TryPnm(S("P2 1 1 15 15"), 1, false); TryPnm(S("P3 1 1 0 0 0 0"), 0, false);
        { Bytes b = S("P5 1 1 300\n"); Add(b, {0x01, 0x2C}); TryPnm(b, 1, false); b = S("P5 1 1 300\n"); Add(b, {0x01, 0x2D}); TryPnm(b, 0, false); }
        { Bytes b = S("P6 1 1 65535\n"); Add(b, {0xFF, 0xFF, 0, 0, 0x80, 0}); TryPnm(b, 1, false); }
        { Bytes b = S("P6 1 1 256\n"); Add(b, {1, 0, 0, 0, 0, 0}); TryPnm(b, 1, false); b = S("P6 1 1 256\n"); Add(b, {1, 1, 0, 0, 0, 0}); TryPnm(b, 0, false); }
        TryPnm(S("P1 3 1 0 1 2"), 0, false); TryPnm(S("P1 3 1 011"), 1, false); TryPnm(S("P1 3 1 0 1 x"), 0, false);

        // random garbage with and without a magic, binary and text-like
        for (int round = 0; round < 400; ++round) {
            const std::size_t len = Rnd() % 300;
            TryPnm(Rand(len), -1, false);
            for (int k = 1; k <= 7; ++k) {
                Bytes b = S("P" + std::to_string(k)); b.push_back(" \n\t\r"[Rnd() % 4]);
                const int mode = round % 4;
                if (mode == 0) { Bytes r = Rand(len); b.insert(b.end(), r.begin(), r.end()); }
                else { for (std::size_t i = 0; i < len; ++i) b.push_back("0123456789 \n#\r\t-x"[Rnd() % (mode == 1 ? 12 : 17)]); }
                TryPnm(b, -1, false);
            }
        }
        // random mutations of the valid fixtures
        for (int round = 0; round < 3000; ++round) {
            Bytes s = fx[Rnd() % fx.size()];
            for (int o = 0, ops = 1 + (int)(Rnd() % 4); o < ops && !s.empty(); ++o) {
                const std::size_t at = Rnd() % s.size();
                switch (Rnd() % 4) {
                case 0: s[at] = (std::uint8_t)Rnd(); break;
                case 1: s[at] = (std::uint8_t)"0123456789 #\n-"[Rnd() % 14]; break;
                case 2: s.insert(s.begin() + (std::ptrdiff_t)at, 1 + Rnd() % 5, (std::uint8_t)"0129 #\n"[Rnd() % 7]); break;
                default: s.erase(s.begin() + (std::ptrdiff_t)at); break;
                }
            }
            TryPnm(s, -1, false);
        }
        std::printf("  pnm: %ld inputs\n", n);
        if (had) setenv("OKAY_OWN_PNM", saved.c_str(), 1); else unsetenv("OKAY_OWN_PNM");
    }

    std::printf("  worst: peak %zu KiB above the input, %.2f s for one input\n", g_worstPeak / 1024, g_worstTime);
    TEST_MAIN_RESULT();
}
