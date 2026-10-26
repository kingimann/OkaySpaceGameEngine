#include "test_framework.hpp"
#include <Okay.hpp>

#include <chrono>
#include <cmath>
#include <thread>

using namespace okay;

// Remote players' gait: a proxy's Character gets the speed it really moves at
// (so its feet don't skate) and the owner's walk / run, and only one component
// writes its anim.
int main() {
    RUN_SUITE("net_proxy_gait");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    Scene ss("Server");
    auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
    CHECK(srv->StartServer(0));
    GameObject* hero = ss.CreateGameObject("Hero");
    auto* sChar = hero->AddComponent<Character>();
    auto* sNs = hero->AddComponent<NetworkSync>();
    sNs->netId = "hero"; sNs->authority = NetworkSync::Authority::Host;
    ss.Start();

    Scene cs("Client");
    auto* cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();   // default easing on
    CHECK(cli->StartClient("127.0.0.1", srv->ServerPort()));
    GameObject* proxy = cs.CreateGameObject("Hero");
    auto* cChar = proxy->AddComponent<Character>();
    auto* cNs = proxy->AddComponent<NetworkSync>();
    cNs->netId = "hero"; cNs->authority = NetworkSync::Authority::Host;
    proxy->AddComponent<NetworkAvatarMotion>();                // a spawner proxy has both
    cs.Start();
    auto pump = [&](int n) { for (int i = 0; i < n; ++i) { ss.Update(0.02f); cs.Update(0.02f); std::this_thread::sleep_for(std::chrono::milliseconds(2)); } };
    for (int i = 0; i < 300 && !cli->Joined(); ++i) pump(1);
    CHECK(cli->Joined());
    pump(20);

    // The owner walks forward at 2 m/s, sidestepping a bit (its controller
    // reports moveSpeed / moveAngle and picks walk).
    float lastSpeed = -1.0f, lastAngle = 0.0f; int lastAnim = -1;
    for (int i = 0; i < 100; ++i) {
        hero->transform->localPosition.z += 2.0f * 0.02f;
        sChar->anim = 2; sChar->moveSpeed = 2.0f; sChar->moveAngle = 15.0f;
        pump(1);
        lastSpeed = cChar->moveSpeed; lastAngle = cChar->moveAngle; lastAnim = cChar->anim;
    }
    CHECK(std::fabs(lastSpeed - 2.0f) < 0.4f);    // was -1: the proxy's cycle ran at its nominal pace
    CHECK(std::fabs(lastAngle - 15.0f) < 0.01f);  // the owner's travel direction
    CHECK(lastAnim == 2);                          // the owner's walk; NetworkAvatarMotion (runSpeed 3.2) doesn't overwrite it

    // The owner's game calls 3 m/s a run (its own thresholds): the proxy runs too,
    // rather than NetworkAvatarMotion's 3.2 m/s rule turning it back into a walk.
    int walks = 0;
    for (int i = 0; i < 100; ++i) {
        hero->transform->localPosition.z += 3.0f * 0.02f;
        sChar->anim = 3; sChar->moveSpeed = 3.0f; sChar->moveAngle = 0.0f;
        pump(1);
        if (i > 30 && cChar->anim == 2) ++walks;
    }
    CHECK(walks == 0);
    CHECK(cChar->anim == 3);

    // Running at 5 m/s: the proxy runs, at that speed.
    for (int i = 0; i < 100; ++i) {
        hero->transform->localPosition.z += 5.0f * 0.02f;
        sChar->anim = 3; sChar->moveSpeed = 5.0f; sChar->moveAngle = 0.0f;
        pump(1);
    }
    CHECK(std::fabs(cChar->moveSpeed - 5.0f) < 0.8f);
    CHECK(cChar->anim == 3);

    // Stopping: speed settles to 0, idle.
    for (int i = 0; i < 60; ++i) { sChar->anim = 1; sChar->moveSpeed = 0.0f; pump(1); }
    CHECK(cChar->moveSpeed < 0.1f);
    CHECK(cChar->anim == 1);

    // A NetworkPlayerSpawner-style proxy (no NetworkSync): NetworkAvatarMotion
    // reports the speed it sees and picks walk / run from it.
    {
        Scene t("T");
        GameObject* g = t.CreateGameObject("Avatar");
        auto* ch = g->AddComponent<Character>();
        g->AddComponent<NetworkAvatarMotion>();
        t.Start();
        for (int i = 0; i < 60; ++i) { g->transform->localPosition.x += 4.0f * 0.02f; t.Update(0.02f); }
        CHECK(std::fabs(ch->moveSpeed - 4.0f) < 0.2f);
        CHECK(ch->anim == 3);
        CHECK(std::fabs(ch->moveAngle) < 5.0f);     // it turned to face where it goes
    }

    Log::sink = sink;
    TEST_MAIN_RESULT();
}
