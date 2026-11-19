// okay::tex DDS files and the streaming planner. DDS offsets come from the public layout; planner numbers are worked
// out by hand: pixels = worldSize / (dist * 2 * tan(fov / 2)) * screenHeight, mip = floor(log2(texels / pixels)).
#include "test_framework.hpp"
#include "okay/Texture/Dds.hpp"
#include "okay/Texture/Mip.hpp"
#include "okay/Texture/Stream.hpp"
#include <cmath>

using namespace okay::tex;
using Bytes = std::vector<std::uint8_t>;

static std::uint32_t U32(const Bytes& b, std::size_t o) { return b[o] | (b[o+1] << 8) | (b[o+2] << 16) | (std::uint32_t(b[o+3]) << 24); }

static DdsImage MakeImage(DdsFormat f, int w, int h, int levels) {
    DdsImage img; img.format = f; img.width = w; img.height = h;
    for (int l = 0; l < levels; ++l) {
        int lw, lh; MipSize(w, h, l, &lw, &lh);
        Bytes m(DdsLevelBytes(f, lw, lh));
        for (std::size_t i = 0; i < m.size(); ++i) m[i] = std::uint8_t(i * 31 + l * 7 + 3);
        img.mips.push_back(m);
    }
    return img;
}

static void TestDds() {
    RUN_SUITE("tex dds");
    DdsImage a = MakeImage(DdsFormat::BC1, 16, 8, 4);
    Bytes f = WriteDds(a);
    // sizes: levels 16x8 (4x2 blocks = 64), 8x4 (2x1 = 16), 4x2 (1 = 8), 2x1 (1 = 8)
    CHECK(f.size() == 128 + 64 + 16 + 8 + 8);
    CHECK(f[0] == 'D' && f[1] == 'D' && f[2] == 'S' && f[3] == ' ');
    CHECK(U32(f, 4) == 124 && U32(f, 12) == 8 && U32(f, 16) == 16 && U32(f, 28) == 4);
    CHECK(U32(f, 20) == 64);                                  // linear size of the top level
    CHECK(f[84] == 'D' && f[85] == 'X' && f[86] == 'T' && f[87] == '1');
    CHECK(U32(f, 8) == (0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000));
    CHECK(U32(f, 108) == (0x1000 | 0x8 | 0x400000));
    CHECK(U32(f, 76) == 32);                                  // pixel format size
    DdsImage back; std::string err;
    CHECK(ReadDds(f.data(), f.size(), &back, &err));
    CHECK(back.format == DdsFormat::BC1 && back.width == 16 && back.height == 8 && back.mips == a.mips);

    for (DdsFormat fm : {DdsFormat::RGBA8, DdsFormat::BC1, DdsFormat::BC3, DdsFormat::BC4, DdsFormat::BC5}) {
        DdsImage x = MakeImage(fm, 13, 7, MipCount(13, 7));
        Bytes file = WriteDds(x);
        CHECK(!file.empty());
        DdsImage y;
        CHECK(ReadDds(file.data(), file.size(), &y, &err));
        CHECK(y.format == fm && y.width == 13 && y.height == 7 && y.mips == x.mips);
        if (fm == DdsFormat::BC4 || fm == DdsFormat::BC5) {
            CHECK(file[84] == 'D' && file[85] == 'X' && file[86] == '1' && file[87] == '0');
            CHECK(U32(file, 128) == (fm == DdsFormat::BC4 ? 80u : 83u));
        }
        if (fm == DdsFormat::RGBA8) CHECK(U32(file, 88) == 32 && U32(file, 92) == 0xFF && U32(file, 100) == 0x00FF0000u && U32(file, 104) == 0xFF000000u);
    }
    // single level: no mip flag
    Bytes one = WriteDds(MakeImage(DdsFormat::RGBA8, 3, 3, 1));
    CHECK((U32(one, 8) & 0x20000) == 0 && U32(one, 20) == 12 && one.size() == 128 + 36);

    // writing refuses inconsistent images
    DdsImage bad = MakeImage(DdsFormat::BC1, 8, 8, 2);
    bad.mips[1].pop_back();
    CHECK(WriteDds(bad).empty());
    DdsImage zero = MakeImage(DdsFormat::BC1, 8, 8, 1); zero.width = 0;
    CHECK(WriteDds(zero).empty());
    DdsImage none; none.width = none.height = 4;
    CHECK(WriteDds(none).empty());

    // reading hostile files: every truncation either fails or returns whole levels; never crashes
    DdsImage full = MakeImage(DdsFormat::BC3, 16, 16, 5);
    Bytes ff = WriteDds(full);
    for (std::size_t cut = 0; cut < ff.size(); cut += 7) {
        DdsImage t; std::string e;
        bool ok = ReadDds(ff.data(), cut, &t, &e);
        if (ok) { CHECK(!t.mips.empty() && t.mips.size() <= 5); CHECK(t.mips[0].size() == full.mips[0].size()); }
        else CHECK(!e.empty());
    }
    // byte flips in the header
    unsigned seed = 99;
    for (int i = 0; i < 3000; ++i) {
        Bytes m = ff;
        seed = seed * 1664525u + 1013904223u;
        m[(seed >> 8) % 148] ^= std::uint8_t(1 << ((seed >> 4) & 7));
        DdsImage t; ReadDds(m.data(), m.size(), &t);
    }
    // specific rejections
    Bytes m = ff; m[0] = 'X';
    DdsImage t; CHECK(!ReadDds(m.data(), m.size(), &t, &err) && err.find("magic") != std::string::npos);
    m = ff; m[112 + 1] |= 0x02;   // caps2 cube map flag (0x200)
    CHECK(!ReadDds(m.data(), m.size(), &t, &err));
    m = ff; m[12] = 0; m[13] = 0; m[14] = 0; m[15] = 0;   // height 0
    CHECK(!ReadDds(m.data(), m.size(), &t, &err));
    m = ff; m[16] = 0xFF; m[17] = 0xFF; m[18] = 0xFF; m[19] = 0x7F;   // huge width
    CHECK(!ReadDds(m.data(), m.size(), &t, &err));
    m = ff; m[28] = 200;           // 200 mip levels
    CHECK(!ReadDds(m.data(), m.size(), &t, &err));
    CHECK(!ReadDds(nullptr, 0, &t, &err));

    // end to end with the encoder: image -> mips -> BC1 -> DDS -> read -> decode gives the same picture as direct decode
    Bytes px(32 * 32 * 4);
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) { std::uint8_t* p = &px[(y * 32 + x) * 4]; p[0] = std::uint8_t(x * 8); p[1] = std::uint8_t(y * 8); p[2] = 128; p[3] = 255; }
    auto chain = GenerateMipsRGBA8(px.data(), 32, 32);
    DdsImage e; e.format = DdsFormat::BC1; e.width = e.height = 32;
    for (std::size_t l = 0; l < chain.size(); ++l) {
        int w, h; MipSize(32, 32, int(l), &w, &h);
        e.mips.push_back(EncodeBc(chain[l].data(), w, h, BcFormat::BC1));
    }
    Bytes file = WriteDds(e);
    DdsImage r;
    CHECK(ReadDds(file.data(), file.size(), &r, &err) && r.mips.size() == 6);
    auto d0 = DecodeBc(r.mips[0].data(), r.mips[0].size(), 32, 32, BcFormat::BC1);
    std::cout << "  dds roundtrip psnr " << Psnr8(px.data(), d0.data(), px.size()) << "\n";
    CHECK(Psnr8(px.data(), d0.data(), px.size()) > 30.0);
}

static StreamTexture Tex(int id, int size, float bpp = 0.5f) { StreamTexture t; t.id = id; t.width = t.height = size; t.bytesPerPixel = bpp; return t; }

static void TestStream() {
    RUN_SUITE("tex stream");
    // fov 90 degrees: tan(45) = 1 -> pixels = worldSize / (2 * dist) * screenHeight
    CHECK_NEAR(ProjectedPixels(2.0f, 1.0f, 1.5707964f, 1000), 1000.0, 1.0);
    CHECK_NEAR(ProjectedPixels(2.0f, 4.0f, 1.5707964f, 1000), 250.0, 0.5);
    CHECK(ProjectedPixels(0.0f, 1.0f, 1.0f, 1000) == 0.0f);
    CHECK(ProjectedPixels(1.0f, 1.0f, 1.0f, 0) == 0.0f);
    CHECK(ProjectedPixels(1.0f, 0.0f, 1.0f, 100) > 0.0f);     // at distance 0: clamped, finite
    CHECK(std::isfinite(ProjectedPixels(1.0f, 0.0f, 1.0f, 100)));

    StreamCamera cam; cam.fovY = 1.5707964f; cam.screenHeight = 1000;
    std::vector<StreamTexture> tex = {Tex(1, 1024), Tex(2, 1024), Tex(3, 1024)};
    // 1 at distance 1 (1000 px -> mip 0), 2 at distance 4 (250 px -> log2(4.096) = 2.03 -> mip 2), 3 at distance 100 (10 px -> 6.68 -> mip 6)
    std::vector<StreamInstance> inst(3);
    inst[0].textureId = 1; inst[0].z = 1;   inst[0].worldSize = 2;
    inst[1].textureId = 2; inst[1].z = 4;   inst[1].worldSize = 2;
    inst[2].textureId = 3; inst[2].z = 100; inst[2].worldSize = 2;
    StreamPlanner sp;
    StreamPlan p = sp.Plan(cam, tex, inst);
    CHECK(p.requests.size() == 3);
    CHECK(p.requests[0].mip == 0 && p.requests[1].mip == 2 && p.requests[2].mip == 6);
    CHECK(p.loads == 3 && p.evictions == 0 && !p.overBudget);
    // whole chain of BC1 1024: (4^11 - 1) / 3 = 1398101 texels * 0.5 bytes = 699050.5 bytes
    CHECK_NEAR(p.requests[0].loadBytes, 699050.5, 2.0);
    CHECK(sp.ResidentMip(2) == 2);

    // same frame again: nothing to do
    StreamPlan same = sp.Plan(cam, tex, inst);
    CHECK(same.loads == 0 && same.evictions == 0 && same.loadBytes == 0.0f);

    // camera retreats from texture 1: it drops to a coarser mip, bytes are evicted, nothing loaded
    inst[0].z = 8;                                   // 125 px -> log2(8.19) = 3.03 -> mip 3
    StreamPlan away = sp.Plan(cam, tex, inst);
    CHECK(away.requests[0].mip == 3 && away.evictions == 1 && away.requests[0].evictBytes > 0 && away.loads == 0);
    // small wobble stays put (hysteresis): 2.94 would round down to mip 2, the old mip 3 is kept
    inst[0].z = 7.5f;                                // 133 px -> log2(7.68) = 2.94
    CHECK(sp.Plan(cam, tex, inst).requests[0].mip == 3);
    // hidden and unknown instances count as not visible
    inst[0].visible = false;
    StreamPlan hid = sp.Plan(cam, tex, inst);
    CHECK(hid.requests[0].mip > 3);
    inst.push_back(StreamInstance{99, 0, 0, 1, 2, 1, true});    // texture 99 is not in the list: ignored

    // tiling needs a coarser mip: 4 repeats across 250 px leave 62 px per repeat -> log2(4096 / 250) = 4.03 -> mip 4
    StreamPlanner sp2;
    std::vector<StreamInstance> one(1); one[0].textureId = 2; one[0].z = 4; one[0].worldSize = 2; one[0].uvScale = 4;
    CHECK(sp2.Plan(cam, {Tex(2, 1024)}, one).requests[0].mip == 4);

    // memory budget: three 1024 RGBA8 textures wanting mip 0 (5592405 bytes each = 16.8 MB); budget 12 MB demotes some
    StreamPlanner sp3;
    std::vector<StreamInstance> close(3);
    for (int i = 0; i < 3; ++i) { close[i].textureId = i + 1; close[i].z = 1; close[i].worldSize = 2; }
    std::vector<StreamTexture> rgba = {Tex(1, 1024, 4.0f), Tex(2, 1024, 4.0f), Tex(3, 1024, 4.0f)};
    StreamOptions bo; bo.budgetBytes = 12.0e6f;
    StreamPlan bp = sp3.Plan(cam, rgba, close, bo);
    CHECK(bp.residentBytes <= 12.0e6f && !bp.overBudget);
    int demoted = 0; for (auto& r : bp.requests) if (r.mip > r.desiredMip) ++demoted;
    CHECK(demoted >= 1);
    StreamOptions tiny; tiny.budgetBytes = 10.0f;
    CHECK(sp3.Plan(cam, rgba, close, tiny).overBudget);

    // upload cap: textures resident at mip 4 all want mip 0; a cap below two deltas lets only the most visible one up
    StreamPlanner sp4;
    std::vector<StreamInstance> far(3);
    for (int i = 0; i < 3; ++i) { far[i].textureId = i + 1; far[i].z = 16; far[i].worldSize = 2; }  // 62.5 px -> log2(16.4) = 4.03 -> mip 4
    StreamPlan f0 = sp4.Plan(cam, tex, far);
    CHECK(f0.requests[0].mip == 4);
    for (int i = 0; i < 3; ++i) { close[i].z = 1 + i * 0.1f; }
    StreamOptions co; co.uploadBytesPerFrame = 0.8e6f;
    StreamPlan f1 = sp4.Plan(cam, tex, close, co);
    CHECK(f1.loads == 1 && f1.deferred == 2);
    CHECK(f1.requests[0].mip == 0 && f1.requests[1].mip == 4 && f1.requests[2].mip == 4 && f1.requests[1].deferred);
    StreamPlan f2 = sp4.Plan(cam, tex, close, co);
    CHECK(f2.loads == 1 && f2.deferred == 1);
    StreamPlan f3 = sp4.Plan(cam, tex, close, co);
    CHECK(f3.loads == 1 && f3.deferred == 0);
    // pinned stays at mip 0, reset forgets
    StreamTexture pin = Tex(7, 512); pin.pinned = true;
    std::vector<StreamInstance> pi(1); pi[0].textureId = 7; pi[0].z = 500; pi[0].worldSize = 1;
    CHECK(sp4.Plan(cam, {pin}, pi).requests[0].mip == 0);
    sp4.Reset(); CHECK(sp4.ResidentMip(7) == -1);
    // empty input
    StreamPlan empty = sp4.Plan(cam, {}, {});
    CHECK(empty.requests.empty() && empty.residentBytes == 0.0f);
}

int main() {
    TestDds();
    TestStream();
    TEST_MAIN_RESULT();
}
