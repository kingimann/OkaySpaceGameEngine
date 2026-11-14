#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/AssetManager.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>

using namespace okay;
namespace fs = std::filesystem;

namespace {
struct Blob { std::string text; int pad = 0; };

void WriteText(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << s;
}
// Make a rewrite visible to the mtime check even on coarse clocks.
void Bump(const fs::path& p, int seconds) {
    fs::last_write_time(p, fs::last_write_time(p) + std::chrono::seconds(seconds));
}
template <class F> bool PumpUntil(AssetManager& m, F done, int ms = 5000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        m.Pump();
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return false;
}
std::atomic<int> g_loads{0};
void RegisterBlob(AssetManager& m, int delayMs = 0) {
    m.RegisterLoader<Blob>("Blob",
        [delayMs](const std::string& p, std::string& err) -> std::shared_ptr<Blob> {
            ++g_loads;
            if (delayMs) std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            std::ifstream f(p, std::ios::binary);
            if (!f) { err = "cannot open " + p; return nullptr; }
            auto b = std::make_shared<Blob>();
            b->text.assign(std::istreambuf_iterator<char>(f), {});
            return b;
        },
        [](const Blob& b) { return b.text.size(); });
}
}

int main() {
    RUN_SUITE("assetmanager");
    const fs::path dir = fs::temp_directory_path() / ("okay_assets_" + std::to_string(std::random_device{}()));
    fs::create_directories(dir / "sub");
    const std::string d = dir.generic_string();

    // --- Path normalization
    CHECK(AssetManager::Normalize("a\\b//c/./d/../e.png", false) == "a/b/c/e.png");
    CHECK(AssetManager::Normalize("../x/../../y", false) == "../../y");
    CHECK(AssetManager::Normalize("/a/../../b/", false) == "/b");
    CHECK(AssetManager::Normalize("C:\\Dir\\..\\File.PNG", true) == "c:/file.png");
    CHECK(AssetManager::Normalize("C:\\Dir\\File.PNG", false) == "C:/Dir/File.PNG");
    CHECK(AssetManager::Normalize("./", false) == ".");
    CHECK(AssetManager::Normalize("", false).empty());

    // --- Cache, refcount, release on last handle
    {
        AssetManager m;
        RegisterBlob(m);
        WriteText(dir / "a.txt", "hello");
        g_loads = 0;
        auto h1 = m.Get<Blob>(d + "/a.txt");
        auto h2 = m.Get<Blob>(d + "/sub/../a.txt");           // same file, other spelling
        CHECK(h1.Ready() && h2.Ready());
        CHECK(h1 == h2);
        CHECK(g_loads == 1);
        CHECK(h1->text == "hello");
        CHECK(m.CachedCount() == 1);
        CHECK(h1.UseCount() == 2);
        CHECK(m.Find<Blob>(d + "/a.txt").Ready());
        { auto h3 = m.Get<Blob>(d + "/a.txt"); CHECK(h3.UseCount() == 3); }
        h1.Reset();
        CHECK(m.CachedCount() == 1);
        h2.Reset();
        CHECK(m.CachedCount() == 0);                           // freed with the last handle
        CHECK(!m.Find<Blob>(d + "/a.txt").Ready());
        m.Get<Blob>(d + "/a.txt");
        CHECK(g_loads == 2);                                   // reloaded from disk
        // Different type, same file: separate entries.
        m.RegisterLoader<int>("Int", [](const std::string&, std::string&) { return std::make_shared<int>(7); });
        auto hb = m.Get<Blob>(d + "/a.txt");
        auto hi = m.Get<int>(d + "/a.txt");
        CHECK(*hi == 7 && hb.Ready() && m.CachedCount() == 2);
        // Unregistered type, missing file: empty handle with an error, never cached.
        std::string err;
        auto hf = m.Get<double>(d + "/a.txt", &err);
        CHECK(!hf.Ready() && !err.empty());
        err.clear();
        auto hm = m.Get<Blob>(d + "/missing.txt", &err);
        CHECK(!hm.Ready() && !err.empty());
        int before = g_loads;
        m.Get<Blob>(d + "/missing.txt");
        CHECK(g_loads == before + 1);                          // failure retried, not cached
        WriteText(dir / "missing.txt", "now here");
        CHECK(m.Get<Blob>(d + "/missing.txt")->text == "now here");
        CHECK(m.Report().ToString().find("Blob") != std::string::npos);
    }

    // --- Report
    {
        AssetManager m;
        RegisterBlob(m);
        WriteText(dir / "r1.txt", std::string(100, 'x'));
        WriteText(dir / "r2.txt", std::string(50, 'y'));
        auto a = m.Get<Blob>(d + "/r1.txt");
        auto b = m.Get<Blob>(d + "/r2.txt");
        auto b2 = b;
        AssetReport r = m.Report();
        CHECK(r.totalBytes == 150 && r.loadedCount == 2 && r.items.size() == 2);
        CHECK(r.items[0].bytes == 100 && r.items[0].refs == 1 && r.items[1].refs == 2);
        CHECK(r.types.size() == 1 && r.types[0].type == "Blob" && r.types[0].count == 2);
    }

    // --- Async load: progress, callbacks on Pump, dedupe, failure
    {
        AssetManager m;
        RegisterBlob(m, 20);
        for (int i = 0; i < 4; ++i) WriteText(dir / ("as" + std::to_string(i) + ".txt"), "n" + std::to_string(i));
        g_loads = 0;
        CHECK(m.Progress() == 1.0f);
        std::vector<AssetHandle<Blob>> hs;
        std::atomic<int> called{0};
        const std::thread::id mainId = std::this_thread::get_id();
        bool onMain = true;
        for (int i = 0; i < 4; ++i)
            hs.push_back(m.LoadAsync<Blob>(d + "/as" + std::to_string(i) + ".txt",
                [&](AssetHandle<Blob> h, const std::string& e) {
                    if (std::this_thread::get_id() != mainId) onMain = false;
                    if (h.Ready() && e.empty()) ++called;
                }));
        auto dup = m.LoadAsync<Blob>(d + "/as0.txt", [&](AssetHandle<Blob> h, const std::string&) { if (h.Ready()) ++called; });
        CHECK(dup == hs[0]);
        CHECK(hs[0].Loading() && !hs[0].Ready() && hs[0].Get() == nullptr);
        CHECK(m.PendingCount() == 4);
        float last = 0;
        bool monotonic = true;
        CHECK(PumpUntil(m, [&] { float p = m.Progress(); if (p < last) monotonic = false; last = p; return called == 5; }));
        CHECK(monotonic && onMain);
        CHECK(g_loads == 4);
        CHECK(hs[3]->text == "n3" && m.PendingCount() == 0 && m.Progress() == 1.0f);
        // Cached already: callback still arrives through Pump.
        bool got = false;
        m.LoadAsync<Blob>(d + "/as1.txt", [&](AssetHandle<Blob> h, const std::string&) { got = h.Ready(); });
        CHECK(PumpUntil(m, [&] { return got; }));
        // Failure: empty handle plus error, not cached.
        std::string failErr; bool failed = false;
        auto hf = m.LoadAsync<Blob>(d + "/nope.txt", [&](AssetHandle<Blob> h, const std::string& e) { failed = !h.Ready(); failErr = e; });
        CHECK(PumpUntil(m, [&] { return failed; }));
        CHECK(!failErr.empty() && !hf.Ready() && !hf.Loading());
        CHECK(m.CachedCount() == 4);
        // Sync Get while the async decode is in flight wins and callbacks still fire once.
        WriteText(dir / "race.txt", "race");
        int fired = 0;
        auto ha = m.LoadAsync<Blob>(d + "/race.txt", [&](AssetHandle<Blob>, const std::string&) { ++fired; });
        auto hsx = m.Get<Blob>(d + "/race.txt");
        CHECK(hsx.Ready() && hsx == ha);
        CHECK(PumpUntil(m, [&] { return fired > 0; }));
        for (int i = 0; i < 20; ++i) { m.Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        CHECK(fired == 1);
    }

    // --- Hot reload in place
    {
        AssetManager m;
        RegisterBlob(m);
        WriteText(dir / "hot.txt", "v1");
        auto h = m.Get<Blob>(d + "/hot.txt");
        Blob* addr = h.Get();
        m.SetHotReload(true, 0.0);
        std::string reloadedKey, reloadedType; int cbs = 0;
        m.SetReloadCallback([&](const std::string& k, const std::string& t) { reloadedKey = k; reloadedType = t; ++cbs; });
        for (int i = 0; i < 5; ++i) m.Pump();
        CHECK(cbs == 0 && h.Version() == 0);                    // unchanged file: no reload
        WriteText(dir / "hot.txt", "version two");
        Bump(dir / "hot.txt", 10);
        CHECK(PumpUntil(m, [&] { return cbs == 1; }));
        CHECK(h->text == "version two" && h.Get() == addr && h.Version() == 1);
        CHECK(reloadedKey == AssetManager::Normalize(d + "/hot.txt") && reloadedType == "Blob");
        CHECK(m.Report().items[0].bytes == 11);
        for (int i = 0; i < 20; ++i) m.Pump();
        CHECK(cbs == 1);                                        // no repeat without another change
        // Broken rewrite (file removed): keep the old data; returns when it reappears.
        fs::remove(dir / "hot.txt");
        for (int i = 0; i < 5; ++i) m.Pump();
        CHECK(h->text == "version two" && cbs == 1);
        WriteText(dir / "hot.txt", "v3");
        Bump(dir / "hot.txt", 20);
        CHECK(PumpUntil(m, [&] { return cbs == 2; }));
        CHECK(h->text == "v3");
        // Reload disabled: edits are ignored.
        m.SetHotReload(false);
        WriteText(dir / "hot.txt", "v4 ignored");
        Bump(dir / "hot.txt", 30);
        for (int i = 0; i < 10; ++i) { m.Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        CHECK(h->text == "v3");
    }

    // --- Concurrency: many threads Get the same files; one load per file
    {
        AssetManager m;
        RegisterBlob(m, 5);
        for (int i = 0; i < 6; ++i) WriteText(dir / ("c" + std::to_string(i) + ".txt"), "c" + std::to_string(i));
        g_loads = 0;
        std::atomic<int> bad{0};
        // Held for the whole phase so no entry is released and re-decoded.
        std::vector<AssetHandle<Blob>> keep;
        for (int i = 0; i < 6; ++i) keep.push_back(m.LoadAsync<Blob>(d + "/c" + std::to_string(i) + ".txt"));
        std::vector<std::thread> ts;
        for (int t = 0; t < 8; ++t)
            ts.emplace_back([&, t] {
                for (int r = 0; r < 30; ++r) {
                    int i = (t + r) % 6;
                    auto h = m.Get<Blob>(d + (r % 2 ? "/c" : "/sub/../c") + std::to_string(i) + ".txt");
                    if (!h.Ready() || h->text != "c" + std::to_string(i)) ++bad;
                    if (r % 7 == 0) (void)m.Report();
                }
            });
        // Meanwhile the main thread pumps and starts async loads of the same files.
        for (int i = 0; i < 50; ++i) { m.Pump(); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        for (auto& t : ts) t.join();
        CHECK(bad == 0);
        CHECK(PumpUntil(m, [&] { return m.PendingCount() == 0; }));
        for (auto& k : keep) CHECK(k.Ready());
        CHECK(g_loads == 6);                                   // handles held: each file decoded once
        // Same file requested by the same threads when nobody holds it: still consistent.
        keep.clear();
        std::vector<std::thread> ts2;
        for (int t = 0; t < 6; ++t)
            ts2.emplace_back([&] { for (int r = 0; r < 20; ++r) { auto h = m.Get<Blob>(d + "/c0.txt"); if (!h.Ready()) ++bad; } });
        for (auto& t : ts2) t.join();
        CHECK(bad == 0);
    }

    // --- Default loaders: DataAsset and Material from real files, Image/Audio failures reported
    {
        AssetManager m;
        m.RegisterDefaultLoaders();
        DataAsset da; da.Set("hp", "42");
        CHECK(da.Save(d + "/item.okaydata"));
        auto hd = m.Get<DataAsset>(d + "/item.okaydata");
        CHECK(hd.Ready() && hd->GetNumber("hp") == 42.0);
        Material mat; CHECK(mat.SaveToFile(d + "/m.okaymat"));
        CHECK(m.Get<Material>(d + "/m.okaymat").Ready());
        std::string err;
        CHECK(!m.Get<Image>(d + "/none.png", &err).Ready() && !err.empty());
        CHECK(!m.Get<AudioClip>(d + "/none.wav").Ready());
        CHECK(!m.Get<Mesh>(d + "/none.obj").Ready());
        // Hot reload of a DataAsset keeps the handle.
        m.SetHotReload(true, 0.0);
        da.Set("hp", "99"); da.Set("mp", "5");
        CHECK(da.Save(d + "/item.okaydata"));
        Bump(dir / "item.okaydata", 15);
        CHECK(PumpUntil(m, [&] { return hd->GetNumber("hp") == 99.0; }));
        CHECK(hd->Has("mp"));
    }

    std::error_code ec; fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
