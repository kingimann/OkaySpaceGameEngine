#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/RelayServer.hpp"
#include "okay/Net/SecureChannel.hpp"
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace okay;

// Automatic reconnect and state resume after ConnectionLost() (docs/multiplayer_audit.md):
//   * off by default; opt-in autoReconnect rejoins with bounded, backing-off tries
//   * the host hands back the peer id, score, ready flag and position
//     (from a live connection it had not noticed was dead, or from parked state)
//   * a client that gets no resume (grace 0) rejoins as a new peer
//   * tries stop after reconnectMaxAttempts, and at once on a refusal that cannot change
//   * SpawnOwned objects are announced again after a resume
//   * the same through a relay

namespace {
void Nap() { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }

struct Rig {
    Scene ss{"Server"}, cs{"Client"};
    NetworkManager* srv = nullptr;
    NetworkManager* cli = nullptr;
    double clientTime = 0.0;
    Rig() {
        ss.physicsEnabled = false; cs.physicsEnabled = false;
        srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->interpolationRate = 0.0f; cli->interpolationRate = 0.0f;
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
        if (client) { cs.Update(dt); clientTime += dt; }
        Nap();
    }
    // The host hangs: only the client ticks until it gives the session up.
    void HostHangs() {
        for (int i = 0; i < 200 && cli->IsConnected(); ++i) Step(0.25f, false, true);
    }
    bool Until(const std::function<bool()>& done, int steps, float dt = 0.05f) {
        for (int i = 0; i < steps && !done(); ++i) Step(dt);
        return done();
    }
};
} // namespace

int main() {
    RUN_SUITE("net_reconnect");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    // ---- Off by default: a lost session stays lost ----
    {
        Rig r; r.Join();
        CHECK(!r.cli->autoReconnect);
        r.HostHangs();
        CHECK(r.cli->ConnectionLost() && !r.cli->Reconnecting());
        for (int i = 0; i < 40; ++i) r.Step(0.25f);
        CHECK(!r.cli->IsConnected() && !r.cli->Joined());
    }

    // ---- Resume: the host still holds the dead connection ----
    {
        Rig r;
        r.cli->autoReconnect = true;
        r.cli->reconnectDelaySeconds = 0.5f;
        std::vector<std::pair<NetworkManager::ReconnectEvent, int>> ev;
        r.cli->SetReconnectHandler([&](NetworkManager::ReconnectEvent e, int n) { ev.push_back({e, n}); });
        std::vector<NetworkManager::ResumedPeer> resumedOnHost;
        r.srv->SetPeerResumedHandler([&](const NetworkManager::ResumedPeer& p) { resumedOnHost.push_back(p); });
        r.cli->SetLocalName("Rejoiner");
        r.Join();
        const std::uint32_t id = r.cli->LocalId();
        CHECK(id != 0);
        // A local avatar that the host tracks.
        GameObject* av = r.cs.CreateGameObject("Avatar");
        av->transform->localPosition = Vec3(4.0f, 0.0f, -2.0f);
        r.cli->SetLocalAvatar(av->transform);
        r.cli->SetReady(true);
        for (int i = 0; i < 40; ++i) r.Step(0.05f);
        r.srv->SetPeerScore(id, 7);
        CHECK(r.srv->PeerScore(id) == 7);
        CHECK(r.srv->ReadyCount() >= 1);

        r.HostHangs();
        CHECK(r.cli->ConnectionLost() && r.cli->Reconnecting());
        CHECK(!r.cli->ReconnectGaveUp());
        CHECK(!ev.empty() && ev[0].first == NetworkManager::ReconnectEvent::Lost);
        // The host comes back, still holding the old connection (it never saw a Leave).
        CHECK(r.Until([&] { return r.cli->Joined(); }, 400));
        CHECK(r.cli->Resumed());
        CHECK(r.cli->LocalId() == id);
        CHECK(!r.cli->ConnectionLost() && !r.cli->Reconnecting());
        CHECK(r.cli->Reconnects() == 1);
        CHECK(r.srv->Peers().size() == 1);
        CHECK(r.srv->PeerScore(id) == 7);
        CHECK(r.srv->ReadyCount() >= 1);                   // ready again
        CHECK(r.cli->LocalName() == "Rejoiner" && r.srv->PeerName(id) == "Rejoiner");
        CHECK(resumedOnHost.size() == 1 && resumedOnHost[0].id == id && resumedOnHost[0].score == 7);
        CHECK(resumedOnHost[0].hasPosition && std::fabs(resumedOnHost[0].position.x - 4.0f) < 0.01f);
        bool lost = false, attempt = false, restored = false;
        for (auto& e : ev) {
            lost |= e.first == NetworkManager::ReconnectEvent::Lost;
            attempt |= e.first == NetworkManager::ReconnectEvent::Attempt && e.second == 1;
            restored |= e.first == NetworkManager::ReconnectEvent::Restored;
        }
        CHECK(lost && attempt && restored);
        // The new session works: a message reaches the host.
        r.cli->Send("hello", "again");
        bool got = false;
        for (int i = 0; i < 100 && !got; ++i) {
            r.Step(0.02f);
            NetworkManager::NetMessage m;
            while (r.srv->PopMessage(m)) got |= m.channel == "hello" && m.data == "again";
        }
        CHECK(got);
    }

    // ---- Resume over an encrypted session (new keys each time; the token rides the sealed join) ----
    if (net::SecureChannel::Available()) {
        Rig r;
        r.srv->encryption = true; r.cli->encryption = true;
        r.cli->autoReconnect = true;
        r.cli->reconnectDelaySeconds = 0.5f;
        r.Join();
        CHECK(r.cli->Encrypted() && r.cli->SealedJoin());
        const std::uint32_t id = r.cli->LocalId();
        r.srv->SetPeerScore(id, 21);
        r.HostHangs();
        CHECK(r.cli->Reconnecting());
        CHECK(r.Until([&] { return r.cli->Joined(); }, 600));
        CHECK(r.cli->Resumed() && r.cli->LocalId() == id);
        CHECK(r.cli->Encrypted());
        CHECK(r.srv->PeerScore(id) == 21);
        r.cli->Send("sealed", "after");
        bool got = false;
        for (int i = 0; i < 150 && !got; ++i) {
            r.Step(0.02f);
            NetworkManager::NetMessage m;
            while (r.srv->PopMessage(m)) got |= m.channel == "sealed" && m.data == "after";
        }
        CHECK(got);
    }

    // ---- Resume from parked state: the host already dropped the client ----
    {
        Rig r;
        r.cli->autoReconnect = true;
        r.cli->reconnectDelaySeconds = 0.5f;
        r.Join();
        const std::uint32_t id = r.cli->LocalId();
        r.srv->SetPeerScore(id, 11);
        r.HostHangs();
        CHECK(r.cli->Reconnecting());
        for (int i = 0; i < 80 && r.srv->Peers().size() > 0; ++i) r.Step(0.1f, true, false);   // the host times it out
        CHECK(r.srv->Peers().size() == 0);
        CHECK(r.Until([&] { return r.cli->Joined(); }, 400));
        CHECK(r.cli->Resumed() && r.cli->LocalId() == id);
        CHECK(r.srv->PeerScore(id) == 11);
    }

    // ---- No grace: a new peer, honestly reported ----
    {
        Rig r;
        r.cli->autoReconnect = true;
        r.cli->reconnectDelaySeconds = 0.5f;
        r.srv->resumeGraceSeconds = 0.0f;
        r.Join();
        const std::uint32_t id = r.cli->LocalId();
        r.srv->SetPeerScore(id, 3);
        r.HostHangs();
        for (int i = 0; i < 80 && r.srv->Peers().size() > 0; ++i) r.Step(0.1f, true, false);
        CHECK(r.Until([&] { return r.cli->Joined(); }, 400));
        CHECK(!r.cli->Resumed());
        CHECK(r.cli->LocalId() != id);
        CHECK(r.srv->PeerScore(r.cli->LocalId()) == 0);
        CHECK(r.cli->Reconnects() == 1);
    }

    // ---- Parked state expires; a stolen or stale token is no use ----
    {
        Rig r;
        r.cli->autoReconnect = true;
        r.cli->reconnectDelaySeconds = 0.5f;
        r.srv->resumeGraceSeconds = 2.0f;
        r.Join();
        const std::uint32_t id = r.cli->LocalId();
        r.srv->SetPeerScore(id, 5);
        r.HostHangs();
        for (int i = 0; i < 100 && r.srv->Peers().size() > 0; ++i) r.Step(0.1f, true, false);
        for (int i = 0; i < 60; ++i) r.Step(0.1f, true, false);   // 6 s more: past the grace
        CHECK(r.Until([&] { return r.cli->Joined(); }, 400));
        CHECK(!r.cli->Resumed());
        CHECK(r.srv->PeerScore(r.cli->LocalId()) == 0);
    }

    // ---- Bounded tries with backoff, then give up ----
    {
        Rig r;
        r.cli->autoReconnect = true;
        r.cli->reconnectDelaySeconds = 0.2f;
        r.cli->reconnectMaxDelaySeconds = 0.8f;
        r.cli->reconnectMaxAttempts = 3;
        r.cli->joinTimeoutSeconds = 1.0f;
        std::vector<double> attemptAt;
        std::vector<NetworkManager::ReconnectEvent> events;
        r.cli->SetReconnectHandler([&](NetworkManager::ReconnectEvent e, int) {
            events.push_back(e);
            if (e == NetworkManager::ReconnectEvent::Attempt) attemptAt.push_back(r.clientTime);
        });
        r.Join();
        r.HostHangs();
        CHECK(r.cli->Reconnecting());
        r.srv->Stop();                                        // the host is gone for good
        for (int i = 0; i < 400 && !r.cli->ReconnectGaveUp(); ++i) r.Step(0.05f, false, true);
        CHECK(r.cli->ReconnectGaveUp());
        CHECK(!r.cli->Reconnecting() && !r.cli->Joined());
        CHECK(r.cli->ConnectionLost());
        CHECK(attemptAt.size() == 3);                          // bounded
        CHECK(events.back() == NetworkManager::ReconnectEvent::GaveUp);
        if (attemptAt.size() == 3) {
            const double g1 = attemptAt[1] - attemptAt[0], g2 = attemptAt[2] - attemptAt[1];
            CHECK(g1 >= 1.0 + 0.3 && g2 >= 1.0 + 0.7 && g2 > g1 + 0.2);   // join wait + a doubling delay
        }
        const int before = (int)attemptAt.size();
        for (int i = 0; i < 100; ++i) r.Step(0.05f, false, true);
        CHECK((int)attemptAt.size() == before);                // no more after giving up
    }

    // ---- A refusal that cannot change ends it at once ----
    {
        Rig r;
        r.cli->autoReconnect = true;
        r.cli->reconnectDelaySeconds = 0.3f;
        r.cli->reconnectMaxAttempts = 6;
        r.srv->password = "secret";
        r.cli->password = "secret";
        r.Join();
        r.HostHangs();
        r.srv->password = "changed";   // the host came back with another password
        int attempts = 0;
        r.cli->SetReconnectHandler([&](NetworkManager::ReconnectEvent e, int) {
            if (e == NetworkManager::ReconnectEvent::Attempt) ++attempts;
        });
        for (int i = 0; i < 300 && !r.cli->ReconnectGaveUp(); ++i) r.Step(0.05f);
        CHECK(r.cli->ReconnectGaveUp());
        CHECK(attempts <= 2);
        CHECK(!r.cli->Joined());
    }

    // ---- A deliberate Stop() cancels reconnecting ----
    {
        Rig r;
        r.cli->autoReconnect = true;
        r.cli->reconnectDelaySeconds = 5.0f;
        r.Join();
        r.HostHangs();
        CHECK(r.cli->Reconnecting());
        r.cli->Stop();
        CHECK(!r.cli->Reconnecting());
        for (int i = 0; i < 200; ++i) r.Step(0.1f);
        CHECK(!r.cli->Joined());
    }

    // ---- SpawnOwned objects are announced again after a resume ----
    {
        {
            Scene a("A");
            GameObject* p = a.CreateGameObject("Crate");
            p->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
            CHECK(SceneSerializer::SaveObjectToFile(*p, "okay_reconnect_crate.okayprefab"));
        }
        const std::string prefab = "okay_reconnect_crate.okayprefab";
        Rig r;
        r.cli->autoReconnect = true;
        r.cli->reconnectDelaySeconds = 0.5f;
        r.srv->spawnablePrefabs = {prefab};
        r.Join();
        GameObject* mine = r.cli->SpawnOwned(prefab, Vec3(1.0f, 2.0f, 3.0f));
        CHECK(mine != nullptr);
        CHECK(r.Until([&] { return r.ss.Find("Crate") != nullptr; }, 200));
        r.HostHangs();
        CHECK(r.cli->Reconnecting());
        CHECK(r.Until([&] { return r.cli->Joined(); }, 400));
        CHECK(r.cli->Resumed());
        // The host dropped the old copy with the old connection and has one again now.
        for (int i = 0; i < 100; ++i) r.Step(0.05f);
        int crates = 0;
        for (const auto& o : r.ss.Objects()) if (o && o->name == "Crate") ++crates;
        CHECK(crates == 1);
        // And still just one on the client.
        int mineCount = 0;
        for (const auto& o : r.cs.Objects()) if (o && o->name == "Crate") ++mineCount;
        CHECK(mineCount == 1);
        std::remove(prefab.c_str());
    }

    // ---- Through a relay ----
    {
        net::RelayServer relay;
        CHECK(relay.Start(0));
        Scene host("Host"); host.physicsEnabled = false;
        auto* hn = host.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(hn->HostViaRelay("127.0.0.1", relay.Port(), "RECON-1"));
        Scene client("Client"); client.physicsEnabled = false;
        auto* cn = client.CreateGameObject("Net")->AddComponent<NetworkManager>();
        cn->autoReconnect = true;
        cn->reconnectDelaySeconds = 0.5f;
        CHECK(cn->JoinViaRelay("127.0.0.1", relay.Port(), "RECON-1"));
        host.Start(); client.Start();
        auto step = [&](float dt, bool h, bool c) {
            relay.Poll(dt); if (h) host.Update(dt); if (c) client.Update(dt); Nap();
        };
        for (int i = 0; i < 600 && !(cn->Joined() && hn->Peers().size() >= 1); ++i) step(0.02f, true, true);
        CHECK(cn->Joined());
        const std::uint32_t id = cn->LocalId();
        hn->SetPeerScore(id, 9);
        for (int i = 0; i < 200 && cn->IsConnected(); ++i) step(0.25f, false, true);
        CHECK(cn->ConnectionLost() && cn->Reconnecting());
        bool back = false;
        for (int i = 0; i < 1500 && !back; ++i) { step(0.05f, true, true); back = cn->Joined(); }
        CHECK(back);
        CHECK(cn->Reconnects() == 1);
        CHECK(cn->Resumed() && cn->LocalId() == id);
        CHECK(hn->PeerScore(id) == 9);
    }

    Log::sink = sink;
    TEST_MAIN_RESULT();
}
