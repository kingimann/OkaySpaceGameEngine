// One shared texture asset for every renderer (okay/Render/TextureAsset.hpp): handles through AssetManager, reference
// counting, hot reload, per-texture settings from the .okaymeta, gamma-correct mip chains, the MemoryTracker producer
// and the budget. Also checks that the software renderer's GetCachedTexture / GetCachedMips go through it.
#include "test_framework.hpp"
#include "okay/Render/TextureAsset.hpp"
#include "okay/Render/SoftwareRenderer.hpp"
#include "okay/Debug/MemoryTracker.hpp"
#include "okay/Render/GpuTexture.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>

using namespace okay;
namespace fs = std::filesystem;

namespace {
Image Solid(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a = 255) {
    Image im(w, h);
    for (int i = 0; i < w * h; ++i) {
        im.Data()[i * 4 + 0] = r; im.Data()[i * 4 + 1] = g; im.Data()[i * 4 + 2] = b; im.Data()[i * 4 + 3] = a;
    }
    return im;
}
Image Checker(int n) {   // black and white single-pixel checker
    Image im(n, n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x) {
            const std::uint8_t v = ((x + y) & 1) ? 255 : 0;
            std::uint8_t* p = im.Data() + (y * n + x) * 4;
            p[0] = p[1] = p[2] = v; p[3] = 255;
        }
    return im;
}
void WriteMeta(const std::string& png, const std::string& sets) {
    std::ofstream f(png + ".okaymeta", std::ios::binary | std::ios::trunc);
    f << "okaymeta 1\nimporter texture 3\n" << sets;
}
void Bump(const std::string& p, int seconds) {
    fs::last_write_time(fs::path(p), fs::last_write_time(fs::path(p)) + std::chrono::seconds(seconds));
}
template <class F> bool PumpUntil(F done, int ms = 5000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        textures::Pump();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}
int Red(const Image* im) { return im && im->Valid() ? im->Data()[0] : -1; }
} // namespace

int main() {
    RUN_SUITE("texture_asset");
    const fs::path dir = fs::temp_directory_path() / ("okay_texasset_" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);
    const std::string d = dir.generic_string();
    MemoryTracker& mem = MemoryTracker::Global();
    textures::ResetForTests();
    const std::int64_t baseCount = mem.Count(MemoryTracker::Textures), baseBytes = mem.Bytes(MemoryTracker::Textures);

    // --- A handle loads once, is shared, and is freed with the last handle.
    const std::string red = d + "/red.png";
    CHECK(Solid(8, 4, 200, 10, 10).SavePNG(red));
    {
        std::string err;
        TextureHandle a = textures::Load(red, &err);
        CHECK(a.Valid());
        CHECK(err.empty());
        CHECK(a->Width() == 8 && a->Height() == 4);
        CHECK(Red(a.GetImage()) == 200);
        CHECK(a.Serial() != 0);
        TextureHandle b = textures::Load(red);
        CHECK(a == b);                                       // one asset per file
        CHECK(textures::Find(red) == a);
        CHECK(mem.Count(MemoryTracker::Textures) == baseCount + 1);
        CHECK(mem.Bytes(MemoryTracker::Textures) == baseBytes + 8 * 4 * 4);
        CHECK(textures::GetStats().count == 1);
        CHECK(textures::GetStats().bytes == 8 * 4 * 4);
        // The renderers' cache shares the same asset.
        CHECK(textures::ForRender(red) == a.Get());
        CHECK(textures::IsHeld(red));
        CHECK(GetCachedTexture(red) == &a->image);           // the software renderer's lookup goes through it
        const std::vector<textures::Row> rows = textures::Report();
        CHECK(rows.size() == 1 && rows[0].width == 8 && rows[0].refs >= 3);
        CHECK(textures::OverlayLine().find("textures 1") != std::string::npos);
    }
    // The library still holds it for the renderers; dropping that frees it.
    CHECK(textures::Find(red).Valid());
    const unsigned gen = textures::Generation();
    textures::Drop(red);
    CHECK(textures::Generation() != gen);
    CHECK(!textures::IsHeld(red));
    CHECK(!textures::Find(red).Valid());
    CHECK(mem.Count(MemoryTracker::Textures) == baseCount);  // counts return to zero
    CHECK(mem.Bytes(MemoryTracker::Textures) == baseBytes);
    CHECK(textures::GetStats().count == 0);

    // --- A missing file: an empty handle with a reason; the renderers get null.
    {
        std::string err;
        CHECK(!textures::Load(d + "/missing.png", &err).Valid());
        CHECK(!err.empty());
        CHECK(textures::ForRender(d + "/missing.png") == nullptr);
        CHECK(GetCachedTexture(d + "/missing.png") == nullptr);
    }

    // --- Hot reload: rewrite the PNG on disk, see the new pixels through the same handle.
    {
        const std::string hot = d + "/hot.png";
        CHECK(Solid(4, 4, 10, 20, 30).SavePNG(hot));
        TextureHandle h = textures::Load(hot);
        const TextureAsset* live = h.Get();
        const std::uint64_t s0 = h.Serial();
        CHECK(Red(GetCachedTexture(hot)) == 10);
        textures::SetHotReload(true, 0.0);
        CHECK(textures::HotReload());
        CHECK(Solid(4, 4, 240, 20, 30).SavePNG(hot));
        Bump(hot, 15);
        CHECK(PumpUntil([&] { return Red(h.GetImage()) == 240; }));
        CHECK(h.Get() == live);                              // same object: handles stay valid
        CHECK(h.Serial() != s0);                             // renderers see a new serial and upload again
        CHECK(Red(GetCachedTexture(hot)) == 240);            // the software renderer's cache sees it too
        CHECK(textures::GetStats().reloads >= 1);
        // A new size moves the memory count with it.
        CHECK(Solid(16, 16, 1, 2, 3).SavePNG(hot));
        Bump(hot, 30);
        CHECK(PumpUntil([&] { return h->Width() == 16; }));
        CHECK(mem.Bytes(MemoryTracker::Textures) == baseBytes + 16 * 16 * 4);
        textures::SetHotReload(false);
        h.Reset();
        textures::Drop(hot);
        CHECK(mem.Count(MemoryTracker::Textures) == baseCount);
    }

    // --- Per-texture settings from the .okaymeta, texture-only keys included.
    {
        const std::string png = d + "/normal.png";
        CHECK(Solid(4, 4, 128, 128, 255).SavePNG(png));
        WriteMeta(png, "set srgb 0\nset filter bilinear\nset wrap mirror\nset mipmaps 1\nset maxSize 0\nset compress 1\n"
                       "set anisotropy 4\nset normalMap 1\nset mipFilter kaiser\nset compression bc5\n");
        const TextureSettings s = ReadTextureSettings(png);
        CHECK(!s.sRGB);
        CHECK(s.filter == TextureSettings::Bilinear);
        CHECK(s.wrap == TextureSettings::Mirror);
        CHECK(s.compress);
        CHECK(s.anisotropy == 4);
        CHECK(s.normalMap);
        CHECK(!s.IsColour());
        CHECK(s.mipFilter == TextureSettings::MipKaiser);
        CHECK(s.compression == TextureSettings::CompressBC5);
        TextureSettings back;
        ApplyTextureExtraKeys(back, TextureExtraKeys(s));
        CHECK(back.anisotropy == 4 && back.normalMap && back.mipFilter == TextureSettings::MipKaiser &&
              back.compression == TextureSettings::CompressBC5);
        // Out of range is pulled in; defaults without a meta.
        WriteMeta(png, "set anisotropy 99\nset compression nonsense\n");
        CHECK(ReadTextureSettings(png).anisotropy == 16);
        CHECK(ReadTextureSettings(png).compression == TextureSettings::CompressAuto);
        CHECK(ReadTextureSettings(d + "/none.png") == TextureSettings());
        CHECK(ReadTextureSettings("proc:checker") == TextureSettings());
        // The sampling the renderers read carries the anisotropy too.
        WriteMeta(png, "set filter trilinear\nset anisotropy 2\n");
        CHECK(TextureImportSampling(png).anisotropy == 2);
        WriteMeta(png, "set filter point\nset anisotropy 16\n");
        CHECK(TextureImportSampling(png).anisotropy == 1);   // point filtering never asks for anisotropy
    }

    // --- The compressed GPU copy follows the picture's compression format and normal-map settings.
    {
        const std::string png = d + "/bc.png";
        CHECK(Solid(8, 8, 90, 120, 200).SavePNG(png));   // opaque: auto picks BC1
        WriteMeta(png, "set compress 1\n");
        tex::DdsImage dds;
        CHECK(gputex::Source(png, &dds));
        CHECK(dds.format == tex::DdsFormat::BC1);
        WriteMeta(png, "set compress 1\nset compression bc3\n");   // asked for BC3: BC3 even though it is opaque
        CHECK(gputex::Source(png, &dds));
        CHECK(dds.format == tex::DdsFormat::BC3);
        WriteMeta(png, "set compress 1\nset srgb 0\nset normalMap 1\n");   // a normal map: auto picks BC5
        CHECK(gputex::Source(png, &dds));
        CHECK(dds.format == tex::DdsFormat::BC5);
        WriteMeta(png, "set compress 1\nset compression bc4\n");
        CHECK(gputex::Source(png, &dds));
        CHECK(dds.format == tex::DdsFormat::BC4);
        WriteMeta(png, "set compress 1\nset mipmaps 0\n");
        CHECK(gputex::Source(png, &dds));
        CHECK(dds.mips.size() == 1);
    }

    // --- Reload after an import setting changed: the same asset, the new size.
    {
        const std::string big = d + "/big.png";
        CHECK(Solid(256, 128, 50, 60, 70).SavePNG(big));
        TextureHandle h = textures::Load(big);
        CHECK(h->Width() == 256);
        const std::uint64_t s0 = h.Serial();
        WriteMeta(big, "set maxSize 64\n");
        CHECK(textures::Reload(big));
        CHECK(h->Width() == 64 && h->Height() == 32);
        CHECK(h.Serial() != s0);
        CHECK(!textures::Reload(d + "/never_loaded.png"));
        h.Reset();
        textures::Drop(big);
    }

    // --- Mip chains are filtered in linear light for colour pictures: a black / white checker averages to sRGB 188,
    //     not 128 (which reads as a darker grey than the picture). Linear (data) pictures average to 128.
    {
        const std::string col = d + "/checker.png", lin = d + "/checker_linear.png";
        CHECK(Checker(8).SavePNG(col));
        CHECK(Checker(8).SavePNG(lin));
        WriteMeta(lin, "set srgb 0\n");
        TextureHandle c = textures::Load(col), l = textures::Load(lin);
        CHECK(c.Valid() && l.Valid());
        const std::vector<Image>& cm = c->Mips();
        CHECK(cm.size() == 4);                               // 8, 4, 2, 1
        CHECK(cm.size() == 4 && cm[1].Width() == 4 && cm[3].Width() == 1);
        const int cv = cm.size() > 1 ? cm[1].Data()[0] : -1;
        CHECK(cv >= 186 && cv <= 190);
        const std::vector<Image>& lm = l->Mips();
        const int lv = lm.size() > 1 ? lm[1].Data()[0] : -1;
        CHECK(lv >= 127 && lv <= 128);
        CHECK(c->Bytes() > std::size_t(8 * 8 * 4));          // the chain is counted once built
        // The software renderer samples the same chain.
        const MipChain* sw = GetCachedMips(col);
        CHECK(sw && sw->size() == 4);
        const int swv = sw && sw->size() > 1 ? (*sw)[1].Data()[0] : -1;
        CHECK(swv >= 186 && swv <= 190);
        // A normal map stays unit length down the chain.
        const std::string nm = d + "/nm.png";
        Image n(2, 2);
        const std::uint8_t px[4][3] = {{255, 128, 128}, {0, 128, 128}, {128, 255, 128}, {128, 0, 128}};
        for (int i = 0; i < 4; ++i) { n.Data()[i * 4] = px[i][0]; n.Data()[i * 4 + 1] = px[i][1]; n.Data()[i * 4 + 2] = px[i][2]; n.Data()[i * 4 + 3] = 255; }
        CHECK(n.SavePNG(nm));
        WriteMeta(nm, "set srgb 0\nset normalMap 1\n");
        TextureHandle nh = textures::Load(nm);
        const std::vector<Image>& nmips = nh->Mips();
        CHECK(nmips.size() == 2);
        if (nmips.size() == 2) {
            const std::uint8_t* p = nmips[1].Data();
            const float x = p[0] / 127.5f - 1, y = p[1] / 127.5f - 1, z = p[2] / 127.5f - 1;
            CHECK_NEAR(std::sqrt(x * x + y * y + z * z), 1.0f, 0.05f);
        }
        // Mipmaps off: one level.
        WriteMeta(col, "set mipmaps 0\n");
        CHECK(textures::Reload(col));
        CHECK(c->Mips().size() == 1);
        c.Reset(); l.Reset(); nh.Reset();
        textures::Drop(col); textures::Drop(lin); textures::Drop(nm);
    }

    // --- Registered (in-memory) pictures: held until unregistered, changed in place with a new serial.
    {
        TextureHandle r = textures::Register("@tex_asset_test", Solid(2, 2, 9, 9, 9));
        CHECK(r.Valid());
        CHECK(textures::ForRender("@tex_asset_test") == r.Get());
        const std::uint64_t s0 = r.Serial();
        Image& im = textures::RegisteredImage("@tex_asset_test");
        im.Data()[0] = 77;
        textures::TouchRegistered("@tex_asset_test");
        CHECK(r.Serial() != s0);
        CHECK(Red(r.GetImage()) == 77);
        textures::Register("@tex_asset_test", Solid(4, 4, 5, 5, 5));   // same asset, new pixels
        CHECK(r->Width() == 4 && Red(r.GetImage()) == 5);
        textures::InvalidateAll();                                       // keeps registered pictures
        CHECK(textures::ForRender("@tex_asset_test") == r.Get());
        r.Reset();
        textures::Unregister("@tex_asset_test");
        CHECK(textures::ForRender("@tex_asset_test") == nullptr);
        // The old free function still works and lands in the library.
        RegisterTexture("@tex_asset_old", Solid(2, 2, 33, 0, 0));
        CHECK(Red(GetCachedTexture("@tex_asset_old")) == 33);
        textures::Unregister("@tex_asset_old");
        CHECK(GetCachedTexture("@tex_asset_old") == nullptr);
    }

    // --- Procedural names load through the same path.
    {
        const TextureAsset* p = textures::ForRender("proc:checker");
        CHECK(p && p->Width() == 256);
        textures::Drop("proc:checker");
    }

    // --- Budget: crossing it is reported, and idle renderer textures are let go of while over it.
    {
        CHECK(mem.Count(MemoryTracker::Textures) == baseCount);
        const std::string a = d + "/budget_a.png", b = d + "/budget_b.png";
        CHECK(Solid(64, 64, 1, 1, 1).SavePNG(a));
        CHECK(Solid(64, 64, 2, 2, 2).SavePNG(b));
        textures::SetBudgetBytes(64 * 64 * 4 + 100);      // room for one
        textures::SetIdleFrames(5);
        CHECK(textures::ForRender(a) != nullptr);
        textures::Pump();
        CHECK(!textures::GetStats().overBudget);
        CHECK(textures::ForRender(b) != nullptr);
        textures::Pump();
        CHECK(textures::GetStats().overBudget);
        CHECK(textures::OverlayLine().find("OVER BUDGET") != std::string::npos);
        for (int i = 0; i < 10; ++i) { textures::ForRender(b); textures::Pump(); }   // b drawn every frame, a idle
        CHECK(!textures::IsHeld(a));
        CHECK(textures::IsHeld(b));
        CHECK(textures::GetStats().trimmed >= 1);
        CHECK(!textures::GetStats().overBudget);
        textures::SetBudgetBytes(0);
        textures::Drop(b);
        CHECK(mem.Count(MemoryTracker::Textures) == baseCount);
    }

    // --- Background loading: the renderers get null (draw untextured) until the decode finishes, then the picture.
    {
        const std::string p = d + "/async.png";
        CHECK(Solid(32, 32, 66, 0, 0).SavePNG(p));
        textures::SetAsyncLoading(true);
        CHECK(textures::AsyncLoading());
        const TextureAsset* first = textures::ForRender(p);
        CHECK(first == nullptr || Red(&first->image) == 66);   // a fast worker may already be done; never wrong pixels
        CHECK(PumpUntil([&] { return textures::ForRender(p) != nullptr; }));
        CHECK(Red(GetCachedTexture(p)) == 66);
        CHECK(textures::GetStats().loading == 0);
        // A missing file under async loading: null, and no endless retry.
        CHECK(textures::ForRender(d + "/async_missing.png") == nullptr);
        CHECK(PumpUntil([&] { return textures::GetStats().loading == 0; }));
        CHECK(textures::ForRender(d + "/async_missing.png") == nullptr);
        textures::SetAsyncLoading(false);
        textures::Drop(p);
        textures::Drop(d + "/async_missing.png");
        CHECK(mem.Count(MemoryTracker::Textures) == baseCount);
    }

    // --- InvalidateTextureCache (the editor's Apply) re-decodes a picture still held elsewhere.
    {
        const std::string p = d + "/inv.png";
        CHECK(Solid(4, 4, 11, 0, 0).SavePNG(p));
        TextureHandle h = textures::Load(p);
        CHECK(GetCachedTexture(p) == &h->image);
        CHECK(Solid(4, 4, 99, 0, 0).SavePNG(p));
        InvalidateTextureCache(p);
        CHECK(Red(GetCachedTexture(p)) == 99);
        CHECK(Red(h.GetImage()) == 99);
        h.Reset();
        InvalidateAllTextureCaches();
        CHECK(!textures::Find(p).Valid());
    }

    textures::ResetForTests();
    CHECK(mem.Count(MemoryTracker::Textures) == baseCount);
    std::error_code ec; fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
