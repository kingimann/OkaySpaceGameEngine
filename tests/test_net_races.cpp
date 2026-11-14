#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/UdpSocket.hpp"
#include "okay/Net/Matchmaker.hpp"
#include "okay/Net/ServerBrowser.hpp"
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace okay;
using namespace okay::net;

// Shared networking state touched from several threads. Meant to be run under
// ThreadSanitizer (it reports the races; the checks alone can pass by luck).

namespace {
struct SlowQueue : Matchmaker::Backend {
    std::atomic<int> polls{0};
    bool Join(const MatchRequest&, std::string& ticket, std::string&) override { ticket = "t"; return true; }
    bool Poll(const std::string&, Matchmaker::PollResult& out, std::string&) override {
        ++polls; out.status = "searching"; out.searching = 1; return true;
    }
    void Leave(const std::string&) override {}
    bool HostReady(const std::string&, const std::string&) override { return true; }
};
} // namespace

int main() {
    RUN_SUITE("net_races");

    // ---- net::Startup / Shutdown from many threads at once ----
    {
        std::atomic<int> failed{0};   // (CHECK itself isn't thread-safe)
        std::vector<std::thread> ts;
        for (int t = 0; t < 8; ++t)
            ts.emplace_back([&failed] { for (int i = 0; i < 2000; ++i) { if (!Startup()) ++failed; Shutdown(); } });
        for (auto& t : ts) t.join();
        CHECK(failed.load() == 0);
        for (int i = 0; i < 10; ++i) Shutdown();                               // extra Shutdowns are harmless
        CHECK(Startup());
        UdpSocket u; CHECK(u.Open() && u.Bind(0));
        Shutdown();
    }

    // ---- Matchmaker: restarting a search never races the old worker ----
    {
        auto q = std::make_shared<SlowQueue>();
        Matchmaker mm;
        mm.SetBackend(q);
        MatchRequest r; r.game = "Test";
        for (int i = 0; i < 30; ++i) {
            mm.pollInterval = 0.05f + 0.01f * (i % 5);   // written while the previous search ran
            mm.hostWaitSeconds = 1.0f + i;
            CHECK(mm.Start(r));
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        CHECK(mm.GetState() == Matchmaker::State::Searching);
        CHECK(q->polls.load() > 0);
        mm.Cancel();
    }   // ~Matchmaker joins the last worker

    // ---- ServerBrowser: Game() copies under the lock while SetGame rewrites it ----
    {
        ServerBrowser sb;
        std::atomic<bool> stop{false};
        std::atomic<int> bad{0};
        std::thread writer([&] {
            for (int i = 0; i < 20000; ++i) sb.SetGame(i % 2 ? std::string(200, 'a') : std::string("b"));
            stop = true;
        });
        std::vector<std::thread> readers;
        for (int t = 0; t < 3; ++t)
            readers.emplace_back([&] {
                while (!stop) {
                    const std::string g = sb.Game();   // value: safe to hold across the next SetGame
                    if (!(g.empty() || g == "b" || g == std::string(200, 'a'))) ++bad;
                }
            });
        writer.join();
        for (auto& t : readers) t.join();
        CHECK(bad.load() == 0);
        sb.SetGame("Space Arena");
        CHECK(sb.Game() == "Space Arena");
    }

    TEST_MAIN_RESULT();
}
