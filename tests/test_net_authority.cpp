#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/ServerQuery.hpp"

#include <atomic>
#include <chrono>
#include <thread>

using namespace okay;

// Server authority: too-fast moves are snapped back (and repeat offenders
// kicked), the server can teleport players, message spam is rate-limited,
// scores are server-owned and replicated, every peer gets the roster, and the
// server browser can ask who's playing.
int main() {
    RUN_SUITE("net_authority");
    auto nap = [] { std::this_thread::sleep_for(std::chrono::milliseconds(2)); };

    Scene ss("Server");
    auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
    srv->serverName = "Arena"; srv->SetLocalName("host");
    srv->maxMoveSpeed = 5.0f;          // units per second
    srv->maxMessagesPerSecond = 5;
    srv->kickAfterViolations = 4;
    int heard = 0;
    srv->SetMessageHandler([&](const NetworkManager::NetMessage& m) { if (m.channel == "spam") ++heard; });
    CHECK(srv->StartServer(0));
    ss.Start();
    const std::uint16_t port = srv->ServerPort();

    Scene cs("Client");
    GameObject* me = cs.CreateGameObject("Me");
    auto* cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
    cli->SetLocalAvatar(me->transform);
    cli->SetLocalName("maya");
    CHECK(cli->StartClient("127.0.0.1", port));
    cs.Start();
    auto pump = [&](int frames) { for (int i = 0; i < frames; ++i) { ss.Update(0.02f); cs.Update(0.02f); nap(); } };
    for (int i = 0; i < 300 && !cli->Joined(); ++i) pump(1);
    CHECK(cli->Joined());
    const std::uint32_t id = cli->LocalId();
    pump(10);

    // ---- Normal movement is accepted ------------------------------------------
    for (int i = 0; i < 20; ++i) { me->transform->localPosition.x += 0.05f; pump(1); }   // 2.5 u/s
    pump(5);
    CHECK(srv->PeerViolations(id) == 0);
    CHECK(cli->Corrections() == 0);

    // ---- A teleport-hack is rejected and snapped back ----------------------------
    const float before = me->transform->localPosition.x;
    me->transform->localPosition.x += 100.0f;
    for (int i = 0; i < 40 && cli->Corrections() == 0; ++i) pump(1);
    CHECK(cli->Corrections() > 0);
    CHECK(srv->PeerViolations(id) >= 1);
    CHECK(std::fabs(me->transform->localPosition.x - before) < 1.0f);   // back where the server says

    // ---- The server may move players (respawn) --------------------------------
    srv->ServerTeleport(id, Vec3(50.0f, 0.0f, 0.0f));
    for (int i = 0; i < 40 && std::fabs(me->transform->localPosition.x - 50.0f) > 0.01f; ++i) pump(1);
    CHECK(std::fabs(me->transform->localPosition.x - 50.0f) < 0.01f);
    const int v = srv->PeerViolations(id);
    for (int i = 0; i < 10; ++i) { me->transform->localPosition.x += 0.05f; pump(1); }
    CHECK(srv->PeerViolations(id) == v);   // moving on from the new spot is fine

    // ---- Message spam is rate-limited ---------------------------------------------
    pump(60);   // a fresh one-second window
    for (int i = 0; i < 20; ++i) cli->Send("spam", "x");
    pump(5);
    CHECK(heard == 5);

    // ---- Scores + roster on every peer ----------------------------------------------
    srv->SetPeerScore(id, 7);
    srv->SetPeerScore(0, 3);
    cli->SetPeerScore(id, 999);   // a client can't set scores
    pump(70);                     // the roster goes out once a second
    CHECK(srv->PeerScore(id) == 7);
    CHECK(cli->PeerScore(id) == 7 && cli->PeerScore(0) == 3);
    CHECK(cli->Roster().size() == 2);
    CHECK(cli->PeerName(0) == "host");
    CHECK(srv->Roster().size() == 2 && srv->Roster()[1].name == "maya");

    // ---- The server browser can ask who's playing -------------------------------------
    {
        std::atomic<bool> stop{false};
        std::thread t([&] { while (!stop) { ss.Update(0.01f); cs.Update(0.01f); nap(); } });
        std::vector<ServerPlayer> players;
        const bool ok = QueryPlayers("127.0.0.1", port, players, 1500);
        stop = true; t.join();
        CHECK(ok);
        CHECK(players.size() == 2);
        bool sawMaya = false;
        for (const auto& p : players) if (p.name == "maya") { sawMaya = true; CHECK(p.score == 7); CHECK(p.seconds > 1.0f); }
        CHECK(sawMaya);
    }
    std::vector<ServerPlayer> none;
    CHECK(!QueryPlayers("127.0.0.1", 1, none, 300));   // nobody there

    // ---- Repeat offenders are kicked ---------------------------------------------------
    for (int k = 0; k < 8 && !cli->WasKicked(); ++k) {
        me->transform->localPosition.x += 200.0f;
        pump(6);
    }
    for (int i = 0; i < 30 && !cli->WasKicked(); ++i) pump(1);
    CHECK(cli->WasKicked());

    TEST_MAIN_RESULT();
}
