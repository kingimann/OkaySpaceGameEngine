// Asset manager memory budget (LRU), hash-checked hot reload, stats, and the loaders that now report into it
// (DataAsset::Cached, AudioClip::LoadCached, the picture cache). Deterministic: no timing beyond file times set by hand.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/AssetManager.hpp"
#include "okay/Core/AssetCacheBridge.hpp"
#include "okay/Render/SoftwareRenderer.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>

using namespace okay;
namespace fs = std::filesystem;

namespace {
struct Blob { std::string text; };
std::atomic<int> g_loads{0};

void WriteText(const fs::path& p, const std::string& s) { std::ofstream f(p, std::ios::binary | std::ios::trunc); f << s; }
void Bump(const fs::path& p, int seconds) { fs::last_write_time(p, fs::last_write_time(p) + std::chrono::seconds(seconds)); }
void RegisterBlob(AssetManager& m) {
    m.RegisterLoader<Blob>("Blob", [](const std::string& p, std::string& err) -> std::shared_ptr<Blob> {
        ++g_loads;
        std::ifstream f(p, std::ios::binary);
        if (!f) { err = "cannot open " + p; return nullptr; }
        auto b = std::make_shared<Blob>();
        b->text.assign(std::istreambuf_iterator<char>(f), {});
        return b;
    }, [](const Blob& b) { return b.text.size(); });
}
void WriteWav(const fs::path& p) {   // 16-bit mono 8000 Hz, 100 samples
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    auto u32 = [&](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4); u32(36 + 200); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(1); u32(8000); u32(16000); u16(2); u16(16);
    f.write("data", 4); u32(200);
    for (int i = 0; i < 100; ++i) u16(static_cast<std::uint16_t>(i * 100));
}
}

int main() {
    RUN_SUITE("asset_cache");
    const fs::path dir = fs::temp_directory_path() / ("okay_acache_" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);
    const std::string d = dir.generic_string();
    for (char c : std::string("abc")) WriteText(dir / (std::string(1, c) + ".txt"), std::string(60, c));

    // --- Unlimited budget (default): identical to before. An asset is freed with its last handle, nothing is pinned.
    {
        AssetManager m; RegisterBlob(m);
        CHECK(m.Budget() == 0);
        { auto h = m.Get<Blob>(d + "/a.txt"); CHECK(h.Ready()); CHECK(m.CachedCount() == 1); }
        CHECK(m.CachedCount() == 0);
        CHECK(m.Stats().pinnedCount == 0);
        CHECK(m.Trim() == 0);
        const AssetStats s = m.Stats();
        CHECK(s.misses == 1 && s.loads == 1 && s.evictions == 0);
    }

    // --- Budget with LRU eviction
    {
        AssetManager m; RegisterBlob(m); g_loads = 0;
        m.SetBudget(100);
        CHECK(m.Budget() == 100);
        { m.Get<Blob>(d + "/a.txt"); m.Get<Blob>(d + "/b.txt"); m.Get<Blob>(d + "/c.txt"); }   // every handle dropped at once
        CHECK(m.CachedCount() == 3);                     // kept alive by the cache
        { AssetStats s = m.Stats(); CHECK(s.pinnedCount == 3 && s.residentBytes == 180 && s.budget == 100); }
        { auto a = m.Get<Blob>(d + "/a.txt"); CHECK(g_loads == 3); }   // a hit: no new decode; a is now the most recent
        CHECK(m.Trim() == 2);                            // order of use b, c, a: 180 -> drop b (120) -> drop c (60)
        CHECK(m.CachedCount() == 1);
    }
    {
        AssetManager m; RegisterBlob(m); g_loads = 0;
        m.SetBudget(100);
        { m.Get<Blob>(d + "/a.txt"); m.Get<Blob>(d + "/b.txt"); m.Get<Blob>(d + "/c.txt"); }
        { m.Get<Blob>(d + "/a.txt"); }                   // order of use now: b, c, a
        CHECK(m.Trim() == 2);                            // 180 -> drop b -> 120 -> drop c -> 60
        CHECK(m.Find<Blob>(d + "/a.txt").Ready());
        CHECK(!m.Find<Blob>(d + "/b.txt").Ready());
        CHECK(!m.Find<Blob>(d + "/c.txt").Ready());
        CHECK(m.Stats().evictions == 2);
        auto b = m.Get<Blob>(d + "/b.txt");              // an evicted asset loads again
        CHECK(b.Ready() && g_loads == 4);
    }
    // A held handle is never evicted, even over budget.
    {
        AssetManager m; RegisterBlob(m);
        m.SetBudget(50);
        auto a = m.Get<Blob>(d + "/a.txt");
        { m.Get<Blob>(d + "/b.txt"); }
        CHECK(m.Trim() == 1);                            // b goes, a is held
        CHECK(a.Ready() && m.Find<Blob>(d + "/a.txt").Ready());
        CHECK(m.Stats().residentBytes == 60);            // still over budget, nothing else can go
    }
    // Pump never evicts an asset used since the previous Pump; the next Pump does.
    {
        AssetManager m; RegisterBlob(m);
        m.SetBudget(100);
        m.Pump();
        { m.Get<Blob>(d + "/a.txt"); m.Get<Blob>(d + "/b.txt"); }
        m.Pump();                                        // both used this frame: kept
        CHECK(m.Stats().evictions == 0 && m.CachedCount() == 2);
        m.Pump();                                        // a frame later: the least recently used goes
        CHECK(m.Stats().evictions == 1 && m.CachedCount() == 1);
        CHECK(m.Find<Blob>(d + "/b.txt").Ready());
    }
    // Turning the budget off returns to weak caching.
    {
        AssetManager m; RegisterBlob(m);
        m.SetBudget(1000);
        { m.Get<Blob>(d + "/a.txt"); }
        CHECK(m.CachedCount() == 1);
        m.SetBudget(0);
        CHECK(m.CachedCount() == 0);
    }
    // EvictUnused drops what only the cache holds.
    {
        AssetManager m; RegisterBlob(m);
        m.SetBudget(1000000);
        auto a = m.Get<Blob>(d + "/a.txt");
        { m.Get<Blob>(d + "/b.txt"); }
        CHECK(m.EvictUnused() == 1 && m.CachedCount() == 1);
    }
    // Report marks cached assets and does not count the manager's own reference as a user.
    {
        AssetManager m; RegisterBlob(m);
        m.SetBudget(1000);
        auto a = m.Get<Blob>(d + "/a.txt");
        AssetReport r = m.Report();
        CHECK(r.items.size() == 1 && r.items[0].refs == 1 && r.items[0].pinned);
        CHECK(m.Stats().ToString().find("budget 1000") != std::string::npos);
    }

    // --- External caches are counted and trimmed by the budget
    {
        AssetManager m;
        std::size_t extBytes = 500;
        AssetExternalCache ex;
        ex.name = "fake";
        ex.bytes = [&] { return extBytes; };
        ex.count = [] { return std::size_t(5); };
        ex.trim = [&](std::size_t target) { if (extBytes > target) extBytes = target; };
        ex.clear = [&] { extBytes = 0; };
        m.RegisterExternalCache(ex);
        AssetStats s = m.Stats();
        CHECK(s.externals.size() == 1 && s.externalBytes == 500 && s.TotalBytes() == 500);
        m.SetBudget(200);
        m.Trim();
        CHECK(extBytes == 200);
    }

    // --- Hot reload hashes the file: touching it (new time, same bytes) does not decode again
    {
        AssetManager m; RegisterBlob(m);
        m.SetHotReload(true, 0.0);
        WriteText(dir / "h.txt", "one");
        auto h = m.Get<Blob>(d + "/h.txt");
        CHECK(h.Ready() && h->text == "one");
        g_loads = 0;
        Bump(dir / "h.txt", 20);                          // new time, same bytes
        for (int i = 0; i < 400 && m.Stats().reloadsSkippedSameHash == 0; ++i) { m.Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        CHECK(m.Stats().reloadsSkippedSameHash == 1);
        CHECK(g_loads == 0 && h.Version() == 0);
        for (int i = 0; i < 20; ++i) { m.Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        CHECK(m.Stats().reloadsSkippedSameHash == 1);     // the new time was remembered: the scan stays quiet
        WriteText(dir / "h.txt", "two!");                 // different bytes: reloaded in place
        Bump(dir / "h.txt", 40);
        for (int i = 0; i < 400 && h.Version() == 0; ++i) { m.Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        CHECK(h.Version() == 1 && h->text == "two!" && g_loads == 1);
        CHECK(m.Stats().reloads == 1);
    }

    // --- DataAsset::Cached is registered with the process-wide manager and hot reloads through it
    {
        const std::string p = d + "/cfg.okaydata";
        DataAsset w; w.Set("hp", "10"); CHECK(w.Save(p));
        DataAsset& a = DataAsset::Cached(p);
        CHECK(a.GetNumber("hp") == 10.0);
        DataAsset& again = DataAsset::Cached(p);
        CHECK(&again == &a);                              // same object as before
        auto h = AssetManager::Instance().Find<DataAsset>("data:" + p);
        CHECK(h.Ready() && h.Get() == &a);
        CHECK(AssetManager::Instance().Report().ToString().find("cfg.okaydata") != std::string::npos);
        AssetManager::Instance().SetHotReload(true, 0.0);
        w.Set("hp", "77"); CHECK(w.Save(p)); Bump(dir / "cfg.okaydata", 30);
        for (int i = 0; i < 500 && a.GetNumber("hp") != 77.0; ++i) { AssetManager::Instance().Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        CHECK(a.GetNumber("hp") == 77.0);                 // the reference a script holds saw the new file
        CHECK(&DataAsset::Cached(p) == &a);
        AssetManager::Instance().SetHotReload(false);
        // A file that appears later is still picked up (unchanged behaviour), and a budget never evicts it.
        const std::string late = d + "/late.okaydata";
        DataAsset& l = DataAsset::Cached(late);
        CHECK(!l.Has("x"));
        DataAsset w2; w2.Set("x", "1"); CHECK(w2.Save(late));
        CHECK(&DataAsset::Cached(late) == &l && l.Has("x"));
        AssetManager::Instance().SetBudget(1);
        AssetManager::Instance().Trim();
        CHECK(l.Has("x") && a.GetNumber("hp") == 77.0);
        AssetManager::Instance().SetBudget(0);
    }

    // --- AudioClip::LoadCached and the picture cache report into the manager
    {
        WriteWav(dir / "s.wav");
        std::string err;
        auto clip = AudioClip::LoadCached(d + "/s.wav", 0, &err);
        CHECK(clip != nullptr);
        bool found = false;
        for (const auto& x : AssetManager::Instance().Stats().externals) if (x.name == "audio clips") found = x.count >= 1 && x.bytes > 0;
        CHECK(found);
        // A budget of 1 byte trims the decode cache but not the clip a voice still holds.
        AssetManager::Instance().SetBudget(1);
        AssetManager::Instance().Trim();
        CHECK(clip && !clip->samples.empty());
        AssetManager::Instance().SetBudget(0);
        AudioClip::ClearCache();

        Image img(4, 4);
        img.SavePNG(d + "/t.png");
        GetCachedTexture(d + "/t.png");
        bool tex = false;
        for (const auto& x : AssetManager::Instance().Stats().externals) if (x.name == "textures") tex = x.count >= 1 && x.bytes >= 64;
        CHECK(tex);
        RegisterTexture("@keep", Image(2, 2));
        AssetManager::Instance().SetBudget(1);
        AssetManager::Instance().Trim();
        CHECK(TextureCacheMap().count(d + "/t.png") == 0);   // read from a file: dropped, read again when next drawn
        CHECK(TextureCacheMap().count("@keep") == 1);        // registered by name: kept
        CHECK(GetCachedTexture(d + "/t.png") != nullptr);
        AssetManager::Instance().SetBudget(0);
    }

    std::error_code ec; fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
