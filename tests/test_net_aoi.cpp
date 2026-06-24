#include "test_framework.hpp"
#include <Okay.hpp>

#include <chrono>
#include <thread>

using namespace okay;

// Spatial interest management: with interestRadius set, a client is only sent
// peers near its own avatar. Two clients far apart never mirror each other; with
// no radius they do.
static NetworkManager* MakeClient(Scene& s, const char* name, Vec3 pos, std::uint16_t port) {
    auto* c = s.CreateGameObject("Net")->AddComponent<NetworkManager>();
    GameObject* av = s.CreateGameObject("Avatar");
    av->transform->localPosition = pos;
    c->SetLocalAvatar(av->transform);
    c->SetRoom("game");                 // keep the host (room "") out of the peer set
    c->SetLocalName(name);
    c->SetRemoteFactory([&s](std::uint32_t id, char) {
        return s.CreateGameObject("Remote" + std::to_string(id));
    });
    c->StartClient("127.0.0.1", port);
    s.Start();
    return c;
}

static void Pump(Scene& a, Scene& b, Scene& c, int frames) {
    for (int i = 0; i < frames; ++i) {
        a.Update(0.05f); b.Update(0.05f); c.Update(0.05f);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

int main() {
    RUN_SUITE("net_aoi");

    // --- With a small interest radius, far-apart clients don't see each other ---
    {
        Scene ss("Server");
        auto* server = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        server->interestRadius = 5.0f;     // only sync peers within 5 units
        CHECK(server->StartServer(0));
        ss.Start();
        std::uint16_t port = server->ServerPort();

        Scene csA("A"), csB("B");
        auto* A = MakeClient(csA, "A", {0, 0, 0},   port);
        auto* B = MakeClient(csB, "B", {100, 0, 0}, port);  // far away
        Pump(ss, csA, csB, 200);
        CHECK(A->Joined() && B->Joined());
        CHECK(A->PeerCount() == 0);        // B is out of A's interest radius
        CHECK(B->PeerCount() == 0);
    }

    // --- With no radius (0), the same far-apart clients DO see each other -------
    {
        Scene ss("Server2");
        auto* server = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        server->interestRadius = 0.0f;     // unlimited
        CHECK(server->StartServer(0));
        ss.Start();
        std::uint16_t port = server->ServerPort();

        Scene csA("A2"), csB("B2");
        auto* A = MakeClient(csA, "A", {0, 0, 0},   port);
        auto* B = MakeClient(csB, "B", {100, 0, 0}, port);
        Pump(ss, csA, csB, 200);
        CHECK(A->Joined() && B->Joined());
        CHECK(A->PeerCount() == 1);        // no culling -> they mirror each other
        CHECK(B->PeerCount() == 1);
    }

    TEST_MAIN_RESULT();
}
