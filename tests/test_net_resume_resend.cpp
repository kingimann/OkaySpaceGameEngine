#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/NetSim.hpp"
#include <chrono>
#include <map>
#include <string>
#include <thread>
#include <vector>

using namespace okay;

// Reliable messages in flight when a session drops (docs/multiplayer_audit.md):
//   * a message the peer never acked is kept and sent again once the rejoin is a RESUME,
//     in both directions, reliable and ordered, each arriving (and in order);
//   * what is kept is bounded (count and bytes) and the rest is counted, not hidden;
//   * a rejoin that is not a resume is a new peer and gets none of it;
//   * a rejoin that gives up, or a plain Stop, keeps nothing.
// The outage is a blackhole in NetSim (every datagram lost), so nothing in flight is
// delivered by a stale datagram and every arrival below is the re-send.

namespace {
void Nap() { std::this_thread::sleep_for(std::chrono::milliseconds(1)); }

struct Rig {
    Scene ss{"Server"}, cs{"Client"};
    NetworkManager* srv = nullptr;
    NetworkManager* cli = nullptr;
    Rig() {
        ss.physicsEnabled = false; cs.physicsEnabled = false;
        srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->interpolationRate = 0.0f; cli->interpolationRate = 0.0f;
        cli->autoReconnect = true;
        cli->reconnectDelaySeconds = 0.5f;
    }
    ~Rig() { Blackhole(false); }
    static void Blackhole(bool on) {
        auto& sim = net::Simulator();
        sim.enabled = on; sim.latencyMs = 0.0f; sim.jitterMs = 0.0f; sim.loss = on ? 1.0f : 0.0f; sim.duplicate = 0.0f;
        if (!on) net::ResetSimulator();
    }
    void Join() {
        CHECK(srv->StartServer(0));
        ss.Start();
        CHECK(cli->StartClient("127.0.0.1", srv->ServerPort()));
        cs.Start();
        for (int i = 0; i < 300 && !(cli->Joined() && srv->Peers().size() >= 1); ++i) Step(0.02f);
        CHECK(cli->Joined());
    }
    void Step(float dt, bool server = true, bool client = true) {
        if (server) ss.Update(dt);
        if (client) cs.Update(dt);
        Nap();
    }
    // Total outage until the client has given the session up and the host has dropped it.
    void Outage() {
        Blackhole(true);
        for (int i = 0; i < 200 && (cli->IsConnected() || srv->Peers().size() > 0); ++i) Step(0.1f);
        Blackhole(false);
    }
    bool Until(const std::function<bool()>& done, int steps, float dt = 0.05f) {
        for (int i = 0; i < steps && !done(); ++i) Step(dt);
        return done();
    }
};

struct Got {
    std::map<std::string, int> count;       // "channel|data" -> times delivered
    std::vector<std::string> order;
    void Drain(NetworkManager* n) {
        NetworkManager::NetMessage m;
        while (n->PopMessage(m)) { const std::string k = m.channel + "|" + m.data; ++count[k]; order.push_back(k); }
    }
    int Of(const std::string& k) const { auto it = count.find(k); return it == count.end() ? 0 : it->second; }
};
} // namespace

int main() {
    RUN_SUITE("net_resume_resend");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    // ---- Client to host: reliable and ordered messages unacked at the drop arrive after the resume ----
    {
        Rig r; r.Join();
        const std::uint32_t id = r.cli->LocalId();
        r.srv->SetPeerScore(id, 4);
        Rig::Blackhole(true);
        r.cli->SendReliable("rel", "r1");
        r.cli->SendReliable("rel", "r2");
        r.cli->SendOrdered("ord", "o1");
        r.cli->SendOrdered("ord", "o2");
        r.cli->SendOrdered("ord", "o3");
        for (int i = 0; i < 200 && (r.cli->IsConnected() || r.srv->Peers().size() > 0); ++i) r.Step(0.1f);
        Rig::Blackhole(false);
        CHECK(r.cli->Reconnecting());
        Got got;
        CHECK(r.Until([&] { got.Drain(r.srv); return r.cli->Joined() && got.Of("ord|o3") > 0; }, 600));
        got.Drain(r.srv);
        CHECK(r.cli->Resumed() && r.cli->LocalId() == id);
        for (const char* k : {"rel|r1", "rel|r2", "ord|o1", "ord|o2", "ord|o3"}) CHECK(got.Of(k) == 1);
        // Ordered messages stay in order.
        int p1 = -1, p2 = -1, p3 = -1;
        for (std::size_t i = 0; i < got.order.size(); ++i) {
            if (got.order[i] == "ord|o1") p1 = (int)i;
            if (got.order[i] == "ord|o2") p2 = (int)i;
            if (got.order[i] == "ord|o3") p3 = (int)i;
        }
        CHECK(p1 >= 0 && p1 < p2 && p2 < p3);
        CHECK(r.cli->ResentAfterResume() == 5 && r.cli->ResendDropped() == 0);
        // Sent once: nothing more arrives later.
        for (int i = 0; i < 60; ++i) r.Step(0.05f);
        got.Drain(r.srv);
        for (const char* k : {"rel|r1", "rel|r2", "ord|o1", "ord|o2", "ord|o3"}) CHECK(got.Of(k) == 1);
    }

    // ---- Host to client: what the host had not acked is kept with the parked state ----
    {
        Rig r; r.Join();
        const std::uint32_t id = r.cli->LocalId();
        Rig::Blackhole(true);
        r.srv->SendReliableTo(id, "srel", "s1");
        r.srv->SendOrderedTo(id, "sord", "t1");
        r.srv->SendOrderedTo(id, "sord", "t2");
        for (int i = 0; i < 200 && (r.cli->IsConnected() || r.srv->Peers().size() > 0); ++i) r.Step(0.1f);
        Rig::Blackhole(false);
        CHECK(r.srv->Peers().empty() && r.srv->ParkedCount() == 1);
        Got got;
        CHECK(r.Until([&] { got.Drain(r.cli); return got.Of("sord|t2") > 0; }, 600));
        got.Drain(r.cli);
        CHECK(r.cli->Resumed());
        CHECK(got.Of("srel|s1") == 1 && got.Of("sord|t1") == 1 && got.Of("sord|t2") == 1);
        CHECK(r.srv->ResentAfterResume() == 3);
    }

    // ---- Not a resume: a new peer is not sent the old peer's messages, and it is counted ----
    {
        Rig r;
        r.srv->resumeGraceSeconds = 0.0f;
        r.Join();
        Rig::Blackhole(true);
        r.cli->SendReliable("lost", "x");
        r.cli->SendOrdered("lost", "y");
        for (int i = 0; i < 200 && (r.cli->IsConnected() || r.srv->Peers().size() > 0); ++i) r.Step(0.1f);
        Rig::Blackhole(false);
        Got got;
        CHECK(r.Until([&] { got.Drain(r.srv); return r.cli->Joined(); }, 600));
        for (int i = 0; i < 60; ++i) r.Step(0.05f);
        got.Drain(r.srv);
        CHECK(!r.cli->Resumed());
        CHECK(got.Of("lost|x") == 0 && got.Of("lost|y") == 0);
        CHECK(r.cli->ResentAfterResume() == 0 && r.cli->ResendDropped() == 2);
    }

    // ---- Bounded by count: only kMaxResendMessages are kept; the rest is counted ----
    {
        Rig r; r.Join();
        Rig::Blackhole(true);
        const int n = (int)NetworkManager::kMaxResendMessages + 44;
        for (int i = 0; i < n; ++i) r.cli->SendReliable("bulk", "m" + std::to_string(i));
        for (int i = 0; i < 200 && (r.cli->IsConnected() || r.srv->Peers().size() > 0); ++i) r.Step(0.1f);
        Rig::Blackhole(false);
        Got got;
        CHECK(r.Until([&] { got.Drain(r.srv); return r.cli->Joined(); }, 600));
        for (int i = 0; i < 100; ++i) { r.Step(0.05f); got.Drain(r.srv); }
        CHECK(r.cli->Resumed());
        CHECK(r.cli->ResentAfterResume() == NetworkManager::kMaxResendMessages);
        CHECK(r.cli->ResendDropped() == 44);
        int total = 0; for (auto& [k, c] : got.count) { CHECK(c == 1); ++total; }
        CHECK(total == (int)NetworkManager::kMaxResendMessages);
        CHECK(got.Of("bulk|m0") == 1);   // the oldest are kept
    }

    // ---- Bounded by bytes ----
    {
        Rig r; r.Join();
        Rig::Blackhole(true);
        const std::string big(4000, 'z');
        for (int i = 0; i < 100; ++i) r.cli->SendReliable("big", std::to_string(1000 + i) + big);
        for (int i = 0; i < 200 && (r.cli->IsConnected() || r.srv->Peers().size() > 0); ++i) r.Step(0.1f);
        Rig::Blackhole(false);
        CHECK(r.Until([&] { return r.cli->Joined(); }, 600));
        const std::size_t kept = r.cli->ResentAfterResume();
        CHECK(kept > 0 && kept * 4004 <= NetworkManager::kMaxResendBytes);
        CHECK(kept + r.cli->ResendDropped() == 100);
    }

    // ---- A rejoin that gives up keeps nothing for a later session ----
    {
        Rig r;
        r.cli->reconnectMaxAttempts = 2;
        r.cli->reconnectDelaySeconds = 0.2f; r.cli->reconnectMaxDelaySeconds = 0.4f;
        r.Join();
        Rig::Blackhole(true);
        r.cli->SendReliable("gone", "g");
        for (int i = 0; i < 200 && r.cli->IsConnected(); ++i) r.Step(0.25f, false, true);
        r.srv->Stop();   // the host never comes back
        for (int i = 0; i < 400 && !r.cli->ReconnectGaveUp(); ++i) r.Step(0.25f, false, true);
        Rig::Blackhole(false);
        CHECK(r.cli->ReconnectGaveUp());
        CHECK(r.cli->ResentAfterResume() == 0 && r.cli->ResendDropped() == 1);
    }

    Log::sink = sink;
    TEST_MAIN_RESULT();
}
