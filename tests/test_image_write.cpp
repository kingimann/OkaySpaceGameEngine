// Our own PNG writer: checksums against known vectors, PNG structure (signature, chunk CRCs, IHDR),
// and round trips through the loader (RGB / RGBA / grey / grey+alpha, odd sizes, 1x1, large,
// noise, flat and patterned content).
#include "test_framework.hpp"
#include "okay/Core/ImageWrite.hpp"
#include "okay/Graphics/Image.hpp"

#include <cstdint>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace okay;
using Bytes = std::vector<std::uint8_t>;

static std::uint32_t g_seed = 12345;
static std::uint8_t Rnd() { g_seed = g_seed * 1664525u + 1013904223u; return static_cast<std::uint8_t>(g_seed >> 24); }

enum class Content { Noise, Flat, Gradient, Pattern };

static Bytes Make(int w, int h, int ch, Content c) {
    Bytes px(static_cast<std::size_t>(w) * h * ch);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int k = 0; k < ch; ++k) {
                std::uint8_t v = 0;
                switch (c) {
                    case Content::Noise: v = Rnd(); break;
                    case Content::Flat: v = static_cast<std::uint8_t>(40 + k * 30); break;
                    case Content::Gradient: v = static_cast<std::uint8_t>((x * 3 + y * 5 + k * 17) & 0xFF); break;
                    case Content::Pattern: v = static_cast<std::uint8_t>(((x / 3 + y / 5) & 1) ? 200 - k * 9 : 10 + k * 3); break;
                }
                px[(static_cast<std::size_t>(y) * w + x) * ch + k] = v;
            }
    return px;
}

// Expected RGBA the loader should hand back.
static Bytes ToRGBA(const Bytes& px, int w, int h, int ch) {
    Bytes out(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < static_cast<std::size_t>(w) * h; ++i) {
        const std::uint8_t* s = &px[i * ch];
        std::uint8_t* d = &out[i * 4];
        if (ch == 1) { d[0] = d[1] = d[2] = s[0]; d[3] = 255; }
        else if (ch == 2) { d[0] = d[1] = d[2] = s[0]; d[3] = s[1]; }
        else if (ch == 3) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255; }
        else { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; }
    }
    return out;
}

static std::uint32_t BE32(const std::uint8_t* p) { return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3]; }

// Walk the chunks, verifying every CRC. Returns the chunk type list joined, e.g. "IHDR IDAT IEND".
static std::string CheckChunks(const Bytes& png, bool* crcOk) {
    std::string types;
    *crcOk = true;
    std::size_t pos = 8;
    while (pos + 12 <= png.size()) {
        const std::uint32_t len = BE32(&png[pos]);
        if (pos + 12 + len > png.size()) { *crcOk = false; break; }
        if (!types.empty()) types += ' ';
        types.append(reinterpret_cast<const char*>(&png[pos + 4]), 4);
        const std::uint32_t crc = BE32(&png[pos + 8 + len]);
        if (ImageWrite::Crc32(&png[pos + 4], 4 + len) != crc) *crcOk = false;
        pos += 12 + len;
    }
    if (pos != png.size()) *crcOk = false;
    return types;
}

static bool RoundTrip(int w, int h, int ch, Content c, const std::filesystem::path& dir) {
    const Bytes px = Make(w, h, ch, c);
    const Bytes png = ImageWrite::EncodePNG(w, h, ch, px.data());
    if (png.size() < 33) return false;
    const std::string path = (dir / "rt.png").string();
    if (!ImageWrite::WritePNG(path, w, h, ch, px.data())) return false;
    Image img;
    std::string err;
    if (!img.Load(path, &err)) { std::cerr << "  load failed " << w << "x" << h << "x" << ch << ": " << err << "\n"; return false; }
    if (img.Width() != w || img.Height() != h) return false;
    const Bytes want = ToRGBA(px, w, h, ch);
    return img.Pixels().size() == want.size() && std::equal(want.begin(), want.end(), img.Pixels().begin());
}

int main() {
    RUN_SUITE("image_write");

    // Known checksum vectors.
    {
        const char* s = "123456789";
        const std::uint8_t* p = reinterpret_cast<const std::uint8_t*>(s);
        CHECK(ImageWrite::Crc32(p, 9) == 0xCBF43926u);
        CHECK(ImageWrite::Adler32(p, 9) == 0x091E01DEu);
        CHECK(ImageWrite::Crc32(nullptr, 0) == 0u);
        CHECK(ImageWrite::Adler32(nullptr, 0) == 1u);
        // Running checksums continue correctly.
        CHECK(ImageWrite::Crc32(p + 4, 5, ImageWrite::Crc32(p, 4)) == 0xCBF43926u);
        CHECK(ImageWrite::Adler32(p + 4, 5, ImageWrite::Adler32(p, 4)) == 0x091E01DEu);
        // Adler over a long run (exercises the modulo batching).
        Bytes ff(100000, 0xFF);
        std::uint32_t a = 1, b = 0;
        for (std::uint8_t v : ff) { a = (a + v) % 65521u; b = (b + a) % 65521u; }
        CHECK(ImageWrite::Adler32(ff.data(), ff.size()) == ((b << 16) | a));
    }

    // PNG structure: signature, chunk order, every CRC, IHDR fields, zlib header.
    {
        const Bytes px = Make(7, 5, 4, Content::Gradient);
        const Bytes png = ImageWrite::EncodePNG(7, 5, 4, px.data());
        static const std::uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        CHECK(png.size() > 45 && std::equal(sig, sig + 8, png.begin()));
        bool crcOk = false;
        CHECK(CheckChunks(png, &crcOk) == "IHDR IDAT IEND");
        CHECK(crcOk);
        CHECK(BE32(&png[8]) == 13u);
        CHECK(BE32(&png[16]) == 7u && BE32(&png[20]) == 5u);
        CHECK(png[24] == 8 && png[25] == 6 && png[26] == 0 && png[27] == 0 && png[28] == 0);
        CHECK(png[41] == 0x78 && png[42] == 0x9C);
        const Bytes iend(png.end() - 12, png.end());
        CHECK(BE32(&iend[0]) == 0u && iend[4] == 'I' && iend[5] == 'E' && iend[6] == 'N' && iend[7] == 'D' && BE32(&iend[8]) == 0xAE426082u);
        const int types[5] = {0, 0, 4, 2, 6};
        for (int ch = 1; ch <= 4; ++ch) {
            const Bytes p2 = Make(3, 3, ch, Content::Noise);
            const Bytes e = ImageWrite::EncodePNG(3, 3, ch, p2.data());
            CHECK(e.size() > 33 && e[25] == types[ch]);
        }
    }

    // Invalid arguments.
    {
        const Bytes px(16, 0);
        CHECK(ImageWrite::EncodePNG(0, 1, 4, px.data()).empty());
        CHECK(ImageWrite::EncodePNG(1, 0, 4, px.data()).empty());
        CHECK(ImageWrite::EncodePNG(1, 1, 0, px.data()).empty());
        CHECK(ImageWrite::EncodePNG(1, 1, 5, px.data()).empty());
        CHECK(ImageWrite::EncodePNG(1, 1, 4, nullptr).empty());
        CHECK(!ImageWrite::WritePNG("/nonexistent-dir-okay/x.png", 1, 1, 4, px.data()));
    }

    // Raw DEFLATE / zlib streams of edge inputs decode through the loader as part of the round trips
    // below; also check sizes behave: flat data must compress hard, noise must not blow up.
    {
        const Bytes flat(200000, 7);
        CHECK(ImageWrite::ZlibCompress(flat.data(), flat.size()).size() < 2000);
        const Bytes noise = Make(300, 300, 1, Content::Noise);
        const std::size_t z = ImageWrite::ZlibCompress(noise.data(), noise.size()).size();
        CHECK(z < noise.size() + noise.size() / 100 + 64);
        CHECK(ImageWrite::ZlibCompress(nullptr, 0).size() == 2 + 5 + 4);
    }

    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "okay_image_write_test";
    std::filesystem::create_directories(dir);

    // Round trips: every channel count, odd sizes, 1x1, thin strips, every content type.
    {
        const int sizes[][2] = {{1, 1}, {1, 9}, {9, 1}, {2, 3}, {7, 5}, {31, 33}, {64, 64}, {101, 77}, {255, 3}, {3, 255}};
        const Content cs[] = {Content::Noise, Content::Flat, Content::Gradient, Content::Pattern};
        int failed = 0;
        for (int ch = 1; ch <= 4; ++ch)
            for (const auto& s : sizes)
                for (Content c : cs)
                    if (!RoundTrip(s[0], s[1], ch, c, dir)) { ++failed; std::cerr << "  roundtrip " << s[0] << "x" << s[1] << " ch" << ch << " c" << int(c) << "\n"; }
        CHECK(failed == 0);
    }

    // Large images (several DEFLATE blocks, window wraparound, long matches).
    {
        CHECK(RoundTrip(1024, 1024, 4, Content::Gradient, dir));
        CHECK(RoundTrip(1024, 1024, 4, Content::Pattern, dir));
        CHECK(RoundTrip(1500, 900, 3, Content::Noise, dir));
        CHECK(RoundTrip(2048, 2048, 4, Content::Flat, dir));
        CHECK(RoundTrip(4000, 50, 1, Content::Gradient, dir));
    }

    // Mixed content: a noisy block inside a flat image, so one block is incompressible and others not.
    {
        const int w = 600, h = 600;
        Bytes px = Make(w, h, 4, Content::Flat);
        for (int y = 200; y < 400; ++y) for (int x = 0; x < w * 4; ++x) px[static_cast<std::size_t>(y) * w * 4 + x] = Rnd();
        const std::string path = (dir / "mixed.png").string();
        CHECK(ImageWrite::WritePNG(path, w, h, 4, px.data()));
        Image img;
        CHECK(img.Load(path, nullptr));
        CHECK(img.Pixels().size() == px.size() && std::equal(px.begin(), px.end(), img.Pixels().begin()));
    }

    // Image::SavePNG goes through the new writer.
    {
        Image img;
        img = Image(13, 9);
        for (int y = 0; y < 9; ++y) for (int x = 0; x < 13; ++x) img.SetPixel(x, y, Color(x / 12.0f, y / 8.0f, 0.5f, 1.0f));
        const std::string path = (dir / "save.png").string();
        CHECK(img.SavePNG(path));
        Image back;
        CHECK(back.Load(path, nullptr));
        CHECK(back.Width() == 13 && back.Height() == 9 && back.Pixels() == img.Pixels());
        Image empty;
        CHECK(!empty.SavePNG(path));
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
