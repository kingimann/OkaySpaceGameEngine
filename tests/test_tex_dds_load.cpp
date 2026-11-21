// Loading .dds files: the reader's formats (DXT1/3/5, BC4/BC5 with ATI1/ATI2 or DX10, uncompressed RGB with masks),
// the CPU decoder of every block format, hostile files, and the texture cache the renderers share loading a .dds.
// Every expected value here is worked out by hand from the public DDS layout and the block layouts (Bc.hpp, Dds.hpp):
//   BC1 colour: two RGB565 endpoints, 2 bit indices; 4 colour mode (c0 > c1) palette a, b, (2a+b)/3, (a+2b)/3;
//               in BC2 and BC3 the colour half always uses the 4 colour palette, whatever the endpoint order.
//   BC2 alpha:  16 explicit 4 bit values, low nibble first, scaled by 17.
#include "test_framework.hpp"
#include "okay/Texture/Bc.hpp"
#include "okay/Texture/Dds.hpp"
#include "okay/Texture/Texture.hpp"
#include "okay/Render/SoftwareRenderer.hpp"   // GetCachedTexture: the cache every renderer loads pictures through

#include <cstdio>
#include <cstring>
#include <fstream>

using namespace okay::tex;
using Bytes = std::vector<std::uint8_t>;

namespace {

void P32(Bytes& b, std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back(std::uint8_t(v >> (8 * i))); }
std::uint32_t FourCC(const char* s) { return std::uint32_t(std::uint8_t(s[0])) | (std::uint32_t(std::uint8_t(s[1])) << 8) | (std::uint32_t(std::uint8_t(s[2])) << 16) | (std::uint32_t(std::uint8_t(s[3])) << 24); }

const std::uint32_t PF_ALPHAPIXELS = 0x1, PF_ALPHA = 0x2, PF_FOURCC = 0x4, PF_RGB = 0x40, PF_LUMINANCE = 0x20000;

// A 128 byte header by hand: magic, 124 byte header, 32 byte pixel format.
Bytes Header(int w, int h, int mips, std::uint32_t pfFlags, const char* fourcc, std::uint32_t bits,
             std::uint32_t rm, std::uint32_t gm, std::uint32_t bm, std::uint32_t am, std::uint32_t caps2 = 0, std::uint32_t extraFlags = 0) {
    Bytes b;
    P32(b, FourCC("DDS "));
    P32(b, 124);
    P32(b, 0x1 | 0x2 | 0x4 | 0x1000 | (mips > 1 ? 0x20000u : 0u) | extraFlags);
    P32(b, std::uint32_t(h)); P32(b, std::uint32_t(w));
    P32(b, 0); P32(b, 0); P32(b, std::uint32_t(mips));
    for (int i = 0; i < 11; ++i) P32(b, 0);
    P32(b, 32); P32(b, pfFlags); P32(b, fourcc ? FourCC(fourcc) : 0u); P32(b, bits);
    P32(b, rm); P32(b, gm); P32(b, bm); P32(b, am);
    P32(b, 0x1000); P32(b, caps2); P32(b, 0); P32(b, 0); P32(b, 0);
    return b;
}
void Dx10(Bytes& b, std::uint32_t dxgi, std::uint32_t misc = 0, std::uint32_t arraySize = 1) {
    P32(b, dxgi); P32(b, 3); P32(b, misc); P32(b, arraySize); P32(b, 0);
}
void Append(Bytes& b, std::initializer_list<int> v) { for (int x : v) b.push_back(std::uint8_t(x)); }

std::string WriteTemp(const char* name, const Bytes& b) {
    std::ofstream f(name, std::ios::binary);
    f.write(reinterpret_cast<const char*>(b.data()), std::streamsize(b.size()));
    return name;
}

bool Px(const okay::Image* im, int x, int y, int r, int g, int bl, int a) {
    if (!im || x >= im->Width() || y >= im->Height()) return false;
    const std::uint8_t* p = im->Data() + (std::size_t(y) * im->Width() + x) * 4;
    const bool ok = p[0] == r && p[1] == g && p[2] == bl && p[3] == a;
    if (!ok) std::cerr << "    pixel (" << x << "," << y << ") = " << int(p[0]) << "," << int(p[1]) << "," << int(p[2]) << "," << int(p[3])
                       << " want " << r << "," << g << "," << bl << "," << a << "\n";
    return ok;
}

// A hand-made DXT3 (BC2) block: alpha nibbles 0, 15, 4, 8 then 15; colour c0 = pure blue (0x001F) < c1 = pure red
// (0xF800), indices 0 1 2 3 in the first row, 0 elsewhere.
Bytes Bc2Block() { Bytes b; Append(b, {0xF0, 0x84, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x1F, 0x00, 0x00, 0xF8, 0xE4, 0, 0, 0}); return b; }

// Truncate a file at every length: the reader fails with a message, or returns only whole levels. Never crashes.
void EveryTruncation(const Bytes& f, const char* what) {
    int fails = 0, oks = 0;
    for (std::size_t cut = 0; cut <= f.size(); ++cut) {
        DdsImage t; std::string e;
        if (ReadDds(f.data(), cut, &t, &e)) {
            ++oks;
            CHECK(!t.mips.empty());
            if (!t.mips.empty()) CHECK(t.mips[0].size() == DdsLevelBytes(t.format, t.width, t.height));
        } else { ++fails; CHECK(!e.empty()); }
    }
    CHECK(oks >= 1);                       // at least the whole file reads
    std::cout << "  " << what << ": " << f.size() + 1 << " cuts, " << fails << " refused, " << oks << " read\n";
}

void TestBc3FourColour() {
    RUN_SUITE("bc3 colour half is always four colour");
    // alpha: a0 = a1 = 255, indices 0 -> 255. colour: c0 = black 0x0000 <= c1 = white 0xFFFF; row 0 indices 0 1 2 3.
    Bytes blk; Append(blk, {255, 255, 0, 0, 0, 0, 0, 0, 0x00, 0x00, 0xFF, 0xFF, 0xE4, 0xE4, 0xE4, 0xE4});
    Bytes d = DecodeBc(blk.data(), blk.size(), 4, 4, BcFormat::BC3);
    CHECK(d.size() == 64);
    if (d.size() == 64) {
        // index 2 = (2*0 + 255) / 3 = 85, index 3 = (0 + 2*255) / 3 = 170 (three colour mode would give 127 and black)
        const int want[4] = {0, 255, 85, 170};
        for (int x = 0; x < 4; ++x) {
            CHECK(d[x * 4] == want[x] && d[x * 4 + 1] == want[x] && d[x * 4 + 2] == want[x] && d[x * 4 + 3] == 255);
            if (d[x * 4] != want[x]) std::cerr << "    texel " << x << " red " << int(d[x * 4]) << " want " << want[x] << "\n";
        }
    }
}

void TestDxt3File() {
    RUN_SUITE("dxt3 (bc2) file");
    Bytes f = Header(4, 4, 1, PF_FOURCC, "DXT3", 0, 0, 0, 0, 0);
    Bytes blk = Bc2Block(); f.insert(f.end(), blk.begin(), blk.end());
    DdsImage img; std::string err;
    CHECK(ReadDds(f.data(), f.size(), &img, &err));
    if (!err.empty()) std::cout << "  read: " << err << "\n";
    CHECK(img.width == 4 && img.height == 4 && img.mips.size() == 1);
    EveryTruncation(f, "dxt3 4x4");

    okay::InvalidateTextureCache("tex_dds_load_dxt3.dds");
    const okay::Image* im = okay::GetCachedTexture(WriteTemp("tex_dds_load_dxt3.dds", f));
    CHECK(im && im->Width() == 4 && im->Height() == 4);
    // row 0: index 0 = blue, 1 = red, 2 = (2*blue + red)/3 = (85,0,170), 3 = (blue + 2*red)/3 = (170,0,85)
    // alpha: 0*17, 15*17, 4*17, 8*17, then 255
    CHECK(Px(im, 0, 0, 0, 0, 255, 0));
    CHECK(Px(im, 1, 0, 255, 0, 0, 255));
    CHECK(Px(im, 2, 0, 85, 0, 170, 68));
    CHECK(Px(im, 3, 0, 170, 0, 85, 136));
    CHECK(Px(im, 3, 3, 0, 0, 255, 255));
    std::remove("tex_dds_load_dxt3.dds");
}

void TestUncompressedMasks() {
    RUN_SUITE("uncompressed with masks");
    std::string err;
    {   // 32 bit BGRA (the layout most tools write): bytes B G R A
        Bytes f = Header(2, 1, 1, PF_RGB | PF_ALPHAPIXELS, nullptr, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
        Append(f, {10, 20, 30, 40, 50, 60, 70, 80});
        DdsImage img;
        CHECK(ReadDds(f.data(), f.size(), &img, &err));
        CHECK(img.format == DdsFormat::RGBA8 && img.mips.size() == 1);
        if (img.mips.size() == 1) CHECK(img.mips[0] == Bytes({30, 20, 10, 40, 70, 60, 50, 80}));
        EveryTruncation(f, "bgra8 2x1");
    }
    {   // 24 bit BGR, no alpha -> 255
        Bytes f = Header(2, 1, 1, PF_RGB, nullptr, 24, 0x00FF0000, 0x0000FF00, 0x000000FF, 0);
        Append(f, {1, 2, 3, 4, 5, 6});
        DdsImage img;
        CHECK(ReadDds(f.data(), f.size(), &img, &err));
        if (img.mips.size() == 1) CHECK(img.mips[0] == Bytes({3, 2, 1, 255, 6, 5, 4, 255}));
    }
    {   // 16 bit R5G6B5: 0xF800 red, 0x07E0 green, 0x8410 = r16 g32 b16 -> (16*255+15)/31 = 132, (32*255+31)/63 = 130
        Bytes f = Header(3, 1, 1, PF_RGB, nullptr, 16, 0xF800, 0x07E0, 0x001F, 0);
        Append(f, {0x00, 0xF8, 0xE0, 0x07, 0x10, 0x84});
        DdsImage img;
        CHECK(ReadDds(f.data(), f.size(), &img, &err));
        if (img.mips.size() == 1) CHECK(img.mips[0] == Bytes({255, 0, 0, 255, 0, 255, 0, 255, 132, 130, 132, 255}));
    }
    {   // 8 bit luminance -> grey; 8 bit alpha only -> (0,0,0,a)
        Bytes f = Header(2, 1, 1, PF_LUMINANCE, nullptr, 8, 0xFF, 0, 0, 0);
        Append(f, {7, 200});
        DdsImage img;
        CHECK(ReadDds(f.data(), f.size(), &img, &err));
        if (img.mips.size() == 1) CHECK(img.mips[0] == Bytes({7, 7, 7, 255, 200, 200, 200, 255}));
        Bytes a = Header(1, 1, 1, PF_ALPHA, nullptr, 8, 0, 0, 0, 0xFF);
        Append(a, {99});
        CHECK(ReadDds(a.data(), a.size(), &img, &err));
        if (img.mips.size() == 1) CHECK(img.mips[0] == Bytes({0, 0, 0, 99}));
    }
    {   // mip chain of BGRA 4x2: levels 4x2, 2x1, 1x1
        Bytes f = Header(4, 2, 3, PF_RGB | PF_ALPHAPIXELS, nullptr, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
        for (int i = 0; i < (8 + 2 + 1) * 4; ++i) f.push_back(std::uint8_t(i));
        DdsImage img;
        CHECK(ReadDds(f.data(), f.size(), &img, &err));
        CHECK(img.mips.size() == 3);
        if (img.mips.size() == 3) CHECK(img.mips[2] == Bytes({42, 41, 40, 43}));
        EveryTruncation(f, "bgra8 4x2 with 3 levels");
    }
}

void TestDx10Variants() {
    RUN_SUITE("dx10 header variants");
    std::string err;
    struct V { std::uint32_t dxgi; DdsFormat want; std::size_t block; };
    for (V v : {V{71, DdsFormat::BC1, 8}, V{72, DdsFormat::BC1, 8}, V{77, DdsFormat::BC3, 16}, V{78, DdsFormat::BC3, 16},
                V{80, DdsFormat::BC4, 8}, V{83, DdsFormat::BC5, 16}}) {
        Bytes f = Header(4, 4, 1, PF_FOURCC, "DX10", 0, 0, 0, 0, 0);
        Dx10(f, v.dxgi);
        for (std::size_t i = 0; i < v.block; ++i) f.push_back(std::uint8_t(i * 13));
        DdsImage img;
        CHECK(ReadDds(f.data(), f.size(), &img, &err) && img.format == v.want);
        if (v.dxgi == 72) EveryTruncation(f, "dx10 bc1 srgb 4x4");
    }
    {   // DXGI 74 / 75: BC2
        Bytes f = Header(4, 4, 1, PF_FOURCC, "DX10", 0, 0, 0, 0, 0);
        Dx10(f, 75);
        Bytes blk = Bc2Block(); f.insert(f.end(), blk.begin(), blk.end());
        DdsImage img;
        CHECK(ReadDds(f.data(), f.size(), &img, &err));
        if (!err.empty()) std::cout << "  dx10 bc2: " << err << "\n";
    }
    {   // DXGI 87: BGRA8
        Bytes f = Header(1, 1, 1, PF_FOURCC, "DX10", 0, 0, 0, 0, 0);
        Dx10(f, 87);
        Append(f, {1, 2, 3, 4});
        DdsImage img;
        CHECK(ReadDds(f.data(), f.size(), &img, &err) && img.format == DdsFormat::RGBA8);
        if (img.mips.size() == 1) CHECK(img.mips[0] == Bytes({3, 2, 1, 4}));
    }
    {   // ATI1 / ATI2 FourCC
        for (const char* cc : {"ATI1", "ATI2"}) {
            Bytes f = Header(4, 4, 1, PF_FOURCC, cc, 0, 0, 0, 0, 0);
            for (int i = 0; i < (cc[3] == '1' ? 8 : 16); ++i) f.push_back(std::uint8_t(i));
            DdsImage img;
            CHECK(ReadDds(f.data(), f.size(), &img, &err) && img.format == (cc[3] == '1' ? DdsFormat::BC4 : DdsFormat::BC5));
        }
    }
}

void TestHostile() {
    RUN_SUITE("hostile headers are refused");
    std::string err; DdsImage img;
    Bytes body(64, 0);
    auto with = [&](Bytes h) { h.insert(h.end(), body.begin(), body.end()); return h; };
    // volume texture (caps2 0x200000, flag DDSD_DEPTH 0x800000)
    Bytes v = with(Header(4, 4, 1, PF_FOURCC, "DXT1", 0, 0, 0, 0, 0, 0x200000, 0x800000));
    err.clear(); CHECK(!ReadDds(v.data(), v.size(), &img, &err) && !err.empty());
    // DX10 cube map (misc flag 0x4), array of 6, signed BC4 / BC5 (81, 84), unknown DXGI
    for (std::uint32_t dxgi : {81u, 84u, 999u}) {
        Bytes f = Header(4, 4, 1, PF_FOURCC, "DX10", 0, 0, 0, 0, 0); Dx10(f, dxgi); f = with(f);
        err.clear(); CHECK(!ReadDds(f.data(), f.size(), &img, &err) && !err.empty());
    }
    Bytes cube = Header(4, 4, 1, PF_FOURCC, "DX10", 0, 0, 0, 0, 0); Dx10(cube, 71, 0x4); cube = with(cube);
    err.clear(); CHECK(!ReadDds(cube.data(), cube.size(), &img, &err) && !err.empty());
    Bytes arr = Header(4, 4, 1, PF_FOURCC, "DX10", 0, 0, 0, 0, 0); Dx10(arr, 71, 0, 6); arr = with(arr);
    err.clear(); CHECK(!ReadDds(arr.data(), arr.size(), &img, &err) && !err.empty());
    // premultiplied DXT2 / DXT4 would draw too dark: refused with a message
    for (const char* cc : {"DXT2", "DXT4"}) {
        Bytes f = with(Header(4, 4, 1, PF_FOURCC, cc, 0, 0, 0, 0, 0));
        err.clear(); CHECK(!ReadDds(f.data(), f.size(), &img, &err) && !err.empty());
    }
    // masks that overlap or bit counts that are not 8/16/24/32
    Bytes odd = with(Header(4, 4, 1, PF_RGB, nullptr, 12, 0xF00, 0x0F0, 0x00F, 0));
    err.clear(); CHECK(!ReadDds(odd.data(), odd.size(), &img, &err) && !err.empty());
    // a 16384 x 16384 BC1 header with 64 bytes of data: refused at once, nothing big allocated
    Bytes big = with(Header(16384, 16384, 15, PF_FOURCC, "DXT1", 0, 0, 0, 0, 0));
    err.clear(); CHECK(!ReadDds(big.data(), big.size(), &img, &err) && !err.empty());
}

void TestCacheLoadsDds() {
    RUN_SUITE("texture cache loads .dds");
    // a gradient, encoded by our own encoder, written by our own writer, loaded through the cache every renderer uses
    const int W = 16, H = 8;
    Bytes px(std::size_t(W) * H * 4);
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) { std::uint8_t* p = &px[(y * W + x) * 4]; p[0] = std::uint8_t(x * 16); p[1] = std::uint8_t(y * 32); p[2] = 90; p[3] = 255; }
    for (DdsFormat fm : {DdsFormat::BC1, DdsFormat::BC3, DdsFormat::BC4, DdsFormat::BC5, DdsFormat::RGBA8}) {
        DdsImage img; img.format = fm; img.width = W; img.height = H;
        Bytes lvl;
        if (fm == DdsFormat::RGBA8) lvl = px;
        else lvl = EncodeBc(px.data(), W, H, fm == DdsFormat::BC1 ? BcFormat::BC1 : fm == DdsFormat::BC3 ? BcFormat::BC3 : fm == DdsFormat::BC4 ? BcFormat::BC4 : BcFormat::BC5);
        img.mips.push_back(lvl);
        Bytes file = WriteDds(img);
        const std::string path = WriteTemp("tex_dds_load_cache.dds", file);
        okay::InvalidateTextureCache(path);
        const okay::Image* im = okay::GetCachedTexture(path);
        CHECK(im && im->Width() == W && im->Height() == H);
        if (im && im->Width() == W && im->Height() == H) {
            Bytes want = fm == DdsFormat::RGBA8 ? px : DecodeBc(lvl.data(), lvl.size(), W, H, fm == DdsFormat::BC1 ? BcFormat::BC1 : fm == DdsFormat::BC3 ? BcFormat::BC3 : fm == DdsFormat::BC4 ? BcFormat::BC4 : BcFormat::BC5);
            CHECK(std::memcmp(im->Data(), want.data(), want.size()) == 0);
        }
        std::remove(path.c_str());
    }
    okay::InvalidateTextureCache("tex_dds_load_cache.dds");
}

} // namespace

int main() {
    TestBc3FourColour();
    TestDxt3File();
    TestUncompressedMasks();
    TestDx10Variants();
    TestHostile();
    TestCacheLoadsDds();
    TEST_MAIN_RESULT();
}
