// Several threads hammer one AssetManager: Get / Find / handle release / Trim / EvictUnused / Stats / SetBudget, while the main
// thread pumps. Checks every Get returns the right bytes and that the accounting (entries, bytes) is back to zero at the end.
// Run under ThreadSanitizer when the engine library is built with it (see docs/asset_manager.md).
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

namespace { struct Blob { std::string text; }; }

int main() {
    RUN_SUITE("asset_stress");
    const fs::path dir = fs::temp_directory_path() / ("okay_astress_" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);
    const int kFiles = 6;
    for (int i = 0; i < kFiles; ++i) { std::ofstream f(dir / ("f" + std::to_string(i) + ".txt"), std::ios::binary); f << std::string(40 + i, char('a' + i)); }
    {
        AssetManager m;
        m.RegisterLoader<Blob>("Blob", [](const std::string& p, std::string& err) -> std::shared_ptr<Blob> {
            std::ifstream f(p, std::ios::binary);
            if (!f) { err = "cannot open"; return nullptr; }
            auto b = std::make_shared<Blob>();
            b->text.assign(std::istreambuf_iterator<char>(f), {});
            return b;
        }, [](const Blob& b) { return b.text.size(); });
        m.RegisterExternalCache({"fake", [] { return std::size_t(10); }, [] { return std::size_t(1); }, [](std::size_t) {}, [] {}});
        std::atomic<bool> stop{false};
        std::atomic<int> bad{0}, gets{0};
        std::vector<std::thread> ts;
        for (int t = 0; t < 4; ++t)
            ts.emplace_back([&, t] {
                std::mt19937 rng(100 + t);
                for (int i = 0; i < 600; ++i) {
                    const int k = static_cast<int>(rng() % kFiles);
                    const std::string p = (dir / ("f" + std::to_string(k) + ".txt")).generic_string();
                    switch (rng() % 6) {
                    case 0: case 1: case 2: {
                        auto h = m.Get<Blob>(p);
                        if (!h.Ready() || h->text != std::string(40 + k, char('a' + k))) ++bad;
                        ++gets;
                        break;
                    }
                    case 3: m.Find<Blob>(p); break;
                    case 4: (void)m.Stats().ToString(); break;
                    default: if (rng() % 2) m.Trim(); else m.EvictUnused(); break;
                    }
                }
            });
        std::thread budget([&] {
            std::mt19937 rng(7);
            while (!stop.load()) { m.SetBudget(rng() % 3 == 0 ? 0 : 50 + rng() % 200); std::this_thread::sleep_for(std::chrono::microseconds(200)); }
        });
        for (int i = 0; i < 200; ++i) { m.Pump(); std::this_thread::sleep_for(std::chrono::microseconds(300)); }
        for (auto& t : ts) t.join();
        stop = true;
        budget.join();
        CHECK(bad == 0);
        CHECK(gets > 0);
        m.SetBudget(0);
        m.Purge();
        const AssetStats s = m.Stats();
        CHECK(s.entries == 0 && s.residentBytes == 0 && s.pinnedCount == 0);   // nothing leaked or double-counted
        CHECK(m.CachedCount() == 0);
        CHECK(s.hits + s.misses >= static_cast<std::uint64_t>(gets.load()));
    }
    std::error_code ec; fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
