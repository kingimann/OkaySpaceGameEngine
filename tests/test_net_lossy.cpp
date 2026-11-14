#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/NetSim.hpp"
#include <chrono>
#include <map>
#include <string>
#include <thread>
#include <vector>

using namespace okay;

// A host and a client in one process, over a link that drops, delays, reorders and
// duplicates datagrams (NetSim). Reliable messages arrive once each, ordered ones in
// order, sequenced ones never go backwards, a join still completes and a synced object
// still converges. Two managers share the simulator here, which also checks that one's
// held datagrams are never sent through the other's socket (they were).

namespace {
struct Lossy {
    Scene ss{"Server"}, cs{"Client"};
    NetworkManager *srv = nullptr, *cli = nullptr;
    Lossy() {
        ss.physicsEnabled = false; cs.physicsEnabled = false;
        srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->interpolationRate = 0.0f; cli->interpolationRate = 0.0f;
    }
    void Step(float dt = 0.02f) {
        ss.Update(dt); cs.Update(dt);
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    bool Until(const std::function<bool()>& done, int steps) {
        for (int i = 0; i < steps && !done(); ++i) Step();
        return done();
    }
};
} // namespace

int main() {
    RUN_SUITE("net_lossy");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};
    auto& sim = net::Simulator();
    sim.enabled = true; sim.latencyMs = 30.0f; sim.jitterMs = 25.0f; sim.loss = 0.2f; sim.duplicate = 0.15f;

    // ---- Two managers, one simulator, no loss: nothing crosses sockets ----
    {
        sim.loss = 0.0f; sim.duplicate = 0.0f;
        Lossy w;
        CHECK(w.srv->StartServer(0)); w.ss.Start();
        CHECK(w.cli->StartClient("127.0.0.1", w.srv->ServerPort())); w.cs.Start();
        CHECK(w.Until([&] { return w.cli->Joined(); }, 400));
        w.cli->SendReliable("hello", "x");
        bool got = false;
        for (int i = 0; i < 200 && !got; ++i) {
            w.Step();
            NetworkManager::NetMessage m;
            while (w.srv->PopMessage(m)) got |= m.channel == "hello";
        }
        CHECK(got);
        sim.loss = 0.2f; sim.duplicate = 0.15f;
    }

    // ---- Join, reliable, ordered and sequenced delivery over a bad link ----
    {
        Lossy w;
        CHECK(w.srv->StartServer(0)); w.ss.Start();
        CHECK(w.cli->StartClient("127.0.0.1", w.srv->ServerPort())); w.cs.Start();
        CHECK(w.Until([&] { return w.cli->Joined(); }, 2000));
        CHECK(w.cli->Joined());
        w.Until([&] { return false; }, 20);

        std::map<std::string, int> relSeen;           // payload -> times delivered (host side)
        std::vector<int> ordered, sequenced;
        std::map<std::string, int> toClient;
        const int kRel = 60, kOrd = 150, kSeq = 200;
        for (int i = 0; i < kRel; ++i) w.cli->SendReliable("r", "m" + std::to_string(i));
        for (int i = 0; i < kOrd; ++i) w.cli->SendOrdered("o", std::to_string(i));
        for (int i = 0; i < kRel; ++i) w.srv->SendReliable("down", "d" + std::to_string(i));
        int seqSent = 0;
        auto drain = [&] {
            NetworkManager::NetMessage m;
            while (w.srv->PopMessage(m)) {
                if (m.channel == "r") ++relSeen[m.data];
                else if (m.channel == "o") ordered.push_back(std::atoi(m.data.c_str()));
                else if (m.channel == "s") sequenced.push_back(std::atoi(m.data.c_str()));
            }
            while (w.cli->PopMessage(m)) if (m.channel == "down") ++toClient[m.data];
        };
        for (int i = 0; i < 4000 && !((int)relSeen.size() == kRel && (int)ordered.size() == kOrd && (int)toClient.size() == kRel && seqSent >= kSeq); ++i) {
            if (seqSent < kSeq && i % 2 == 0) { w.cli->SendSequenced("s", std::to_string(seqSent)); ++seqSent; }
            w.Step();
            drain();
        }
        for (int i = 0; i < 100; ++i) { w.Step(); drain(); }   // late duplicates, if any
        CHECK((int)relSeen.size() == kRel);
        bool once = true;
        for (const auto& [k, n] : relSeen) once &= n == 1;
        CHECK(once);
        CHECK((int)toClient.size() == kRel);
        once = true;
        for (const auto& [k, n] : toClient) once &= n == 1;
        CHECK(once);
        CHECK((int)ordered.size() == kOrd);
        bool inOrder = true;
        for (std::size_t i = 0; i < ordered.size(); ++i) inOrder &= ordered[i] == (int)i;
        CHECK(inOrder);
        bool mono = !sequenced.empty();
        for (std::size_t i = 1; i < sequenced.size(); ++i) mono &= sequenced[i] > sequenced[i - 1];   // never old, never twice
        CHECK(mono);
        CHECK(sequenced.size() > 20);   // some arrive; loss may eat others
    }

    // ---- A synced object converges, and a dropped session resumes, on the same link ----
    {
        Lossy w;
        w.cli->autoReconnect = true;
        w.cli->reconnectDelaySeconds = 0.3f;
        CHECK(w.srv->StartServer(0));
        GameObject* hero = w.ss.CreateGameObject("Hero");
        auto* ns = hero->AddComponent<NetworkSync>();
        ns->netId = "hero"; ns->authority = NetworkSync::Authority::Host;
        w.ss.Start();
        CHECK(w.cli->StartClient("127.0.0.1", w.srv->ServerPort()));
        GameObject* proxy = w.cs.CreateGameObject("Hero");
        auto* cns = proxy->AddComponent<NetworkSync>();
        cns->netId = "hero"; cns->authority = NetworkSync::Authority::Host;
        w.cs.Start();
        CHECK(w.Until([&] { return w.cli->Joined(); }, 2000));
        hero->transform->SetPosition(Vec3(7.0f, 0.0f, 3.0f));
        CHECK(w.Until([&] { return std::fabs(proxy->transform->Position().x - 7.0f) < 0.1f; }, 1500));
        hero->transform->SetPosition(Vec3(-4.0f, 1.0f, 9.0f));
        CHECK(w.Until([&] { return std::fabs(proxy->transform->Position().x + 4.0f) < 0.1f &&
                                   std::fabs(proxy->transform->Position().z - 9.0f) < 0.1f; }, 1500));
        const std::uint32_t id = w.cli->LocalId();
        w.srv->SetPeerScore(id, 4);
        // The host hangs, comes back; the client resumes through the lossy link.
        for (int i = 0; i < 200 && w.cli->IsConnected(); ++i) { w.cs.Update(0.25f); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        CHECK(w.cli->ConnectionLost());
        CHECK(w.Until([&] { return w.cli->Joined(); }, 4000));
        CHECK(w.cli->Resumed() && w.cli->LocalId() == id);
        CHECK(w.srv->PeerScore(id) == 4);
    }

    sim.enabled = false; sim.loss = 0.0f; sim.duplicate = 0.0f;
    Log::sink = sink;
    TEST_MAIN_RESULT();
}
