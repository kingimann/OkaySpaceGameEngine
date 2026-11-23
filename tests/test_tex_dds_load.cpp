// Loading .dds files: the reader's formats (DXT1/3/5, BC4/BC5 with ATI1/ATI2 or DX10, uncompressed RGB with masks),
// the CPU decoder of every block format, hostile files, and the texture cache the renderers share loading a .dds.
// Every expected value here is worked out by hand from the public DDS layout and the block layouts (Bc.hpp, Dds.hpp):
//   BC1 colour: two RGB565 endpoints, 2 bit indices; 4 colour mode (c0 > c1) palette a, b, (2a+b)/3, (a+2b)/3;
//               in BC2 and BC3 the colour half always uses the 4 colour palette, whatever the endpoint order.
//   BC2 alpha:  16 explicit 4 bit values, low nibble first, scaled by 17.
#include "test_framework.hpp"
#include "okay/Core/FileCopy.hpp"
#include "okay/Texture/Bc.hpp"
#include "okay/Texture/Dds.hpp"
#include "okay/Texture/Texture.hpp"
#include "okay/Render/SoftwareRenderer.hpp"   // GetCachedTexture: the cache every renderer loads pictures through
#include "okay/Render/GpuTexture.hpp"        // gputex: the GPU upload choice and the compressed import copy
#include "okay/Texture/Export.hpp"
#include <filesystem>
#include <thread>

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


void TestBc2RoundTrip() {
    RUN_SUITE("bc2 encoder and decoder round trip");
    // alpha: every 4 bit level survives exactly (v * 17 is a level); colour and alpha error bounds on a gradient
    Bytes px(16 * 16 * 4);
    for (int i = 0; i < 256; ++i) { px[i * 4] = std::uint8_t(i); px[i * 4 + 1] = std::uint8_t(255 - i); px[i * 4 + 2] = std::uint8_t((i * 7) & 255); px[i * 4 + 3] = std::uint8_t((i % 16) * 17); }
    Bytes enc = EncodeBc(px.data(), 16, 16, BcFormat::BC2);
    CHECK(enc.size() == 16 * 16);
    Bytes dec = DecodeBc(enc.data(), enc.size(), 16, 16, BcFormat::BC2);
    CHECK(dec.size() == px.size());
    int alphaWrong = 0;
    for (int i = 0; i < 256; ++i) if (dec[i * 4 + 3] != px[i * 4 + 3]) ++alphaWrong;
    CHECK(alphaWrong == 0);
    // arbitrary alpha: at most half a level off (17 / 2 rounded down = 8)
    for (int i = 0; i < 256; ++i) px[i * 4 + 3] = std::uint8_t(i);
    enc = EncodeBc(px.data(), 16, 16, BcFormat::BC2);
    dec = DecodeBc(enc.data(), enc.size(), 16, 16, BcFormat::BC2);
    int worstA = 0;
    for (int i = 0; i < 256; ++i) worstA = std::max(worstA, std::abs(int(dec[i * 4 + 3]) - int(px[i * 4 + 3])));
    CHECK(worstA <= 8);
    Bytes rgbA(px), rgbB(dec);
    for (std::size_t i = 3; i < rgbA.size(); i += 4) rgbA[i] = rgbB[i] = 255;
    const double psnr = Psnr8(rgbA.data(), rgbB.data(), rgbA.size());
    // the colour half is the opaque BC1 encoder: same quality as BC1 on the same pixels (the blue channel here is
    // noise inside each block, so the number itself is low)
    Bytes opaque(px); for (std::size_t i = 3; i < opaque.size(); i += 4) opaque[i] = 255;
    Bytes e1 = EncodeBc(opaque.data(), 16, 16, BcFormat::BC1), d1 = DecodeBc(e1.data(), e1.size(), 16, 16, BcFormat::BC1);
    const double psnr1 = Psnr8(rgbA.data(), d1.data(), rgbA.size());
    std::cout << "  bc2 colour psnr " << psnr << " dB (bc1 on the same pixels " << psnr1 << " dB), worst alpha error " << worstA << "\n";
    CHECK(std::fabs(psnr - psnr1) < 0.01 && psnr > 20.0);
    // same colour half as BC3 (both four colour): decode of BC2 colour == decode of BC3 colour for the same 8 bytes
    Bytes b3(16, 0); b3[0] = 255; b3[1] = 255;
    std::memcpy(&b3[8], &enc[8], 8);
    Bytes d3 = DecodeBc(b3.data(), 16, 4, 4, BcFormat::BC3);
    Bytes d2 = DecodeBc(enc.data(), 16, 4, 4, BcFormat::BC2);
    bool same = true;
    for (int i = 0; i < 16; ++i) for (int c = 0; c < 3; ++c) if (d3[i * 4 + c] != d2[i * 4 + c]) same = false;
    CHECK(same);
    // DDS with BC2 writes DXT3 and reads back
    DdsImage img; img.format = DdsFormat::BC2; img.width = img.height = 16; img.mips.push_back(enc);
    Bytes file = WriteDds(img);
    CHECK(file.size() == 128 + 256 && file[84] == 'D' && file[85] == 'X' && file[86] == 'T' && file[87] == '3');
    DdsImage back;
    CHECK(ReadDds(file.data(), file.size(), &back) && back.format == DdsFormat::BC2 && back.mips == img.mips);
    // BuildDds with a fixed BC2 format stores BC2 (not BC5)
    TextureBuildOptions bo; bo.compress = true; bo.autoFormat = false; bo.format = BcFormat::BC2; bo.mips = false;
    DdsImage built; std::string err;
    CHECK(BuildDds(px.data(), 16, 16, bo, &built, &err) && built.format == DdsFormat::BC2);
    BcFormat pf;
    CHECK(ParseBcFormat("BC2", &pf) && pf == BcFormat::BC2 && BcBlockBytes(BcFormat::BC2) == 16);
}

void TestBudgets() {
    RUN_SUITE("read budgets");
    DdsImage img; img.format = DdsFormat::BC1; img.width = 64; img.height = 64;
    for (int l = 0; l < MipCount(64, 64); ++l) { int w, h; MipSize(64, 64, l, &w, &h); img.mips.push_back(Bytes(DdsLevelBytes(DdsFormat::BC1, w, h), 0x55)); }
    Bytes file = WriteDds(img);
    DdsImage out; std::string err;
    DdsLimits small; small.maxBytes = 1024;            // 64x64 BC1 with mips is 2048 + 512 + ... bytes
    CHECK(!ReadDds(file.data(), file.size(), &out, &err, small) && err.find("budget") != std::string::npos);
    DdsLimits narrow; narrow.maxDimension = 32;
    CHECK(!ReadDds(file.data(), file.size(), &out, &err, narrow));
    CHECK(ReadDds(file.data(), file.size(), &out, &err, DdsLimits()));
    // 8 bit luminance expands four times: the budget counts the expanded bytes
    Bytes lum = Header(32, 32, 1, PF_LUMINANCE, nullptr, 8, 0xFF, 0, 0, 0);
    lum.resize(lum.size() + 32 * 32, 9);
    DdsLimits lb; lb.maxBytes = 32 * 32 * 2;
    CHECK(!ReadDds(lum.data(), lum.size(), &out, &err, lb));
    lb.maxBytes = 32 * 32 * 4;
    CHECK(ReadDds(lum.data(), lum.size(), &out, &err, lb) && out.mips[0].size() == 32 * 32 * 4);
    // a mip count past 1x1 is cut to the levels that can exist
    Bytes over = Header(4, 4, 9, PF_FOURCC, "DXT1", 0, 0, 0, 0, 0);
    over.resize(over.size() + 8 * 3, 0);
    CHECK(ReadDds(over.data(), over.size(), &out, &err) && out.mips.size() == 3);
    // ReadDdsFile refuses a file larger than the budget allows before reading it
    WriteTemp("tex_dds_load_big.dds", file);
    CHECK(!ReadDdsFile("tex_dds_load_big.dds", &out, &err, small) && !err.empty());
    CHECK(ReadDdsFile("tex_dds_load_big.dds", &out, &err));
    CHECK(!ReadDdsFile("tex_dds_load_missing.dds", &out, &err));
    std::remove("tex_dds_load_big.dds");
}

void TestUploadChoice() {
    RUN_SUITE("gpu upload choice (headless)");
    using namespace okay::gputex;
    // caps from the version string and the extension names
    Caps c = CapsFrom("4.5 (Core Profile) Mesa 23.2.1", {"GL_ARB_debug_output", "GL_EXT_texture_compression_s3tc"});
    CHECK(c.s3tc && c.rgtc);
    c = CapsFrom("2.1 Mesa", {});
    CHECK(!c.s3tc && !c.rgtc);
    c = CapsFrom("2.1 Mesa", {"GL_ARB_texture_compression_rgtc"});
    CHECK(!c.s3tc && c.rgtc);
    c = CapsFrom("OpenGL ES 3.2 Mesa", {});               // ES 3 has no RGTC in core
    CHECK(!c.s3tc && !c.rgtc);
    c = CapsFrom("OpenGL ES 3.0", {"GL_EXT_texture_compression_rgtc", "GL_WEBGL_compressed_texture_s3tc"});
    CHECK(c.s3tc && c.rgtc);
    c = CapsFrom(nullptr, {});
    CHECK(!c.s3tc && !c.rgtc);
    CHECK(GlCompressedFormat(DdsFormat::BC1) == 0x83F1 && GlCompressedFormat(DdsFormat::BC2) == 0x83F2 && GlCompressedFormat(DdsFormat::BC3) == 0x83F3);
    CHECK(GlCompressedFormat(DdsFormat::BC4) == 0x8DBB && GlCompressedFormat(DdsFormat::BC5) == 0x8DBD && GlCompressedFormat(DdsFormat::RGBA8) == 0);

    Bytes px(16 * 8 * 4);
    for (std::size_t i = 0; i < px.size(); ++i) px[i] = std::uint8_t(i * 5);
    auto make = [&](DdsFormat f, int w, int h) {
        TextureBuildOptions bo; bo.compress = f != DdsFormat::RGBA8; bo.autoFormat = false;
        BcFormat bc; if (DdsBcFormat(f, &bc)) bo.format = bc;
        Bytes p(std::size_t(w) * h * 4); for (std::size_t i = 0; i < p.size(); ++i) p[i] = std::uint8_t(i * 5 + 1);
        DdsImage img; std::string e; BuildDds(p.data(), w, h, bo, &img, &e); return img;
    };
    const Caps all{true, true}, none{false, false}, s3only{true, false};
    for (DdsFormat f : {DdsFormat::BC1, DdsFormat::BC2, DdsFormat::BC3, DdsFormat::BC4, DdsFormat::BC5}) {
        DdsImage img = make(f, 16, 8);
        const std::size_t levels = img.mips.size();
        Plan p;
        CHECK(PlanUpload(img, all, &p) && p.compressed && p.glFormat == GlCompressedFormat(f) && p.levels.size() == levels && p.note.empty());
        if (!p.levels.empty()) CHECK(p.levels[0].bytes == img.mips[0] && p.levels[0].width == 16 && p.levels[0].height == 8);
        CHECK(PlanUpload(img, none, &p) && !p.compressed && p.glFormat == kGlRgba8 && p.levels.size() == levels && !p.note.empty());
        // decoded levels are exactly our decoder's output
        Bytes want; DdsLevelToRGBA8(img, 1, &want);
        if (p.levels.size() > 1) CHECK(p.levels[1].bytes == want && p.levels[1].width == 8 && p.levels[1].height == 4);
        const bool s3 = f == DdsFormat::BC1 || f == DdsFormat::BC2 || f == DdsFormat::BC3;
        CHECK(PlanUpload(img, s3only, &p) && p.compressed == s3);
    }
    {   // a top level that is not a multiple of 4 is decoded even when the GPU has the format
        DdsImage img = make(DdsFormat::BC1, 6, 6);
        Plan p;
        CHECK(PlanUpload(img, all, &p) && !p.compressed && p.levels.size() == 3 && p.note.find("multiple of 4") != std::string::npos);
        CHECK(p.levels[0].bytes.size() == 6 * 6 * 4);
    }
    {   // RGBA8 goes up as RGBA8; empty images are refused
        DdsImage img = make(DdsFormat::RGBA8, 4, 4);
        Plan p;
        CHECK(PlanUpload(img, all, &p) && !p.compressed && p.glFormat == kGlRgba8 && p.note.empty());
        CHECK(!PlanUpload(DdsImage(), all, &p));
    }
}

void TestImportCache() {
    RUN_SUITE("import setting compress -> compressed copy in the import cache");
    namespace fs = std::filesystem;
    using namespace okay::gputex;
    fs::remove_all("tex_dds_load_assets");
    fs::create_directories("tex_dds_load_assets");
    const std::string png = "tex_dds_load_assets/brick.png";
    okay::Image im(32, 16);
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 32; ++x) { std::uint8_t* p = im.Data() + (y * 32 + x) * 4; p[0] = std::uint8_t(x * 8); p[1] = std::uint8_t(y * 16); p[2] = 60; p[3] = 255; }
    CHECK(im.SavePNG(png));
    CHECK(CompressedCachePath(png) == "tex_dds_load_assets/.okaycache/brick.png.dds");
    CHECK(CompressedCachePath("brick.png") == ".okaycache/brick.png.dds");
    DdsImage d; std::string err;
    // no settings, or compress off: not a compressed texture (the ordinary path, as before); nothing written
    CHECK(!Source(png, &d, &err));
    auto meta = [&](const char* body) { std::ofstream m(png + ".okaymeta"); m << "okaymeta 1\ntype texture\nimporter texture 2\n" << body; };
    meta("set compress 0\n");
    CHECK(!Source(png, &d, &err));
    CHECK(!fs::exists(CompressedCachePath(png)));
    CHECK(!Source("@registered", &d, &err) && !Source("proc:checker", &d, &err) && !Source("", &d, &err));
    // compress on: built, written to the cache, BC1 (opaque) with the full mip chain
    meta("set compress 1\n");
    bool rebuilt = false;
    CHECK(LoadCompressedCopy(png, &d, &err, &rebuilt) && rebuilt);
    CHECK(d.format == DdsFormat::BC1 && d.width == 32 && d.height == 16 && d.mips.size() == 6);
    CHECK(fs::exists(CompressedCachePath(png)));
    // the second time it comes from the cache
    DdsImage d2;
    CHECK(LoadCompressedCopy(png, &d2, &err, &rebuilt) && !rebuilt && d2.mips == d.mips);
    CHECK(Source(png, &d2, &err) && d2.format == DdsFormat::BC1);
    // a newer .okaymeta (settings changed) rebuilds: mipmaps off -> one level
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    meta("set compress 1\nset mipmaps 0\n");
    CHECK(LoadCompressedCopy(png, &d2, &err, &rebuilt) && rebuilt && d2.mips.size() == 1);
    // a picture with alpha becomes BC3; a newer picture rebuilds
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    im.Data()[3] = 10;
    CHECK(im.SavePNG(png));
    CHECK(LoadCompressedCopy(png, &d2, &err, &rebuilt) && rebuilt && d2.format == DdsFormat::BC3);
    // a picture replaced by a different one whose file time is OLDER than the cache (copied from a backup, unpacked
    // from an archive) is still noticed: the cache remembers what it was made from, not just when
    {
        okay::Image opaque(32, 16);
        for (int y = 0; y < 16; ++y) for (int x = 0; x < 32; ++x) { std::uint8_t* p = opaque.Data() + (y * 32 + x) * 4; p[0] = 200; p[1] = std::uint8_t(x * 4); p[2] = 10; p[3] = 255; }
        CHECK(opaque.SavePNG(png));
        std::error_code tec;
        const auto cacheTime = fs::last_write_time(CompressedCachePath(png), tec);
        CHECK(!tec);
        fs::last_write_time(png, cacheTime - std::chrono::hours(24), tec);
        CHECK(!tec);
        fs::last_write_time(png + ".okaymeta", cacheTime - std::chrono::hours(24), tec);
        CHECK(LoadCompressedCopy(png, &d2, &err, &rebuilt) && rebuilt && d2.format == DdsFormat::BC1);   // opaque now: BC1
        CHECK(LoadCompressedCopy(png, &d2, &err, &rebuilt) && !rebuilt);                                 // and then cached again
        CHECK(im.SavePNG(png));                                                                          // back to the picture with alpha
    }
    // a broken cache file is rebuilt, not trusted
    { std::ofstream junk(CompressedCachePath(png), std::ios::binary | std::ios::trunc); junk << "not a dds"; }
    CHECK(LoadCompressedCopy(png, &d2, &err, &rebuilt) && rebuilt && d2.format == DdsFormat::BC3);
    // a built game gets the compressed copy from the build: it never encodes at run time and never writes into its
    // install folder. The copy is made from what was shipped, so other file times after copying (an installer, a zip)
    // do not make the game rebuild it.
    {
        const std::string out = "tex_dds_load_assets/build/Assets/brick.png";
        fs::create_directories("tex_dds_load_assets/build/Assets");
        okay::CopyFileOver(png, out);
        okay::CopyFileOver(png + ".okaymeta", out + ".okaymeta");
        err.clear();
        CHECK(ShipCompressedCopy(png, out, &err) && err.empty());
        CHECK(fs::exists(CompressedCachePath(out)));
        std::error_code tec;
        fs::last_write_time(out, fs::file_time_type::clock::now() + std::chrono::hours(1), tec);   // unpacked later
        DdsImage shipped;
        CHECK(LoadCompressedCopy(out, &shipped, &err, &rebuilt) && !rebuilt && shipped.format == DdsFormat::BC3);
        // a picture without `compress` ships nothing extra, and that is not an error
        const std::string plain = "tex_dds_load_assets/build/Assets/plain.png";
        CHECK(im.SavePNG("tex_dds_load_assets/plain.png"));
        okay::CopyFileOver("tex_dds_load_assets/plain.png", plain);
        err.clear();
        CHECK(!ShipCompressedCopy("tex_dds_load_assets/plain.png", plain, &err) && err.empty());
        CHECK(!fs::exists(CompressedCachePath(plain)));
    }
    // a missing picture fails with a message
    err.clear();
    CHECK(!LoadCompressedCopy("tex_dds_load_assets/none.png", &d2, &err) && !err.empty());
    // a .dds name is read as it is
    const std::string dds = "tex_dds_load_assets/direct.dds";
    WriteTemp(dds.c_str(), WriteDds(d));
    CHECK(Source(dds, &d2, &err) && d2.format == DdsFormat::BC1 && d2.mips == d.mips);
    fs::remove_all("tex_dds_load_assets");
}

} // namespace

int main() {
    TestBc3FourColour();
    TestDxt3File();
    TestUncompressedMasks();
    TestDx10Variants();
    TestHostile();
    TestCacheLoadsDds();
    TestBc2RoundTrip();
    TestBudgets();
    TestUploadChoice();
    TestImportCache();
    TEST_MAIN_RESULT();
}
