#include "test_framework.hpp"
#include <Okay.hpp>

#include <chrono>
#include <thread>
#include <cmath>

using namespace okay;

// Networked transform replication: a host-owned object's position + rotation
// drive the matching (same-id) object on a client. The client's copy is NOT
// owned, so it follows the host; the host ignores anything the client sends.
int main() {
    RUN_SUITE("net_sync");

    Scene serverScene("Server");
    auto* server = serverScene.CreateGameObject("Net")->AddComponent<NetworkManager>();
    server->interpolationRate = 0.0f;   // snap, so the test doesn't depend on easing
    CHECK(server->StartServer(0));
    // The object the host drives.
    auto* srvBox = serverScene.CreateGameObject("Box");
    auto* sNs = srvBox->AddComponent<NetworkSync>();
    sNs->netId = "box1"; sNs->authority = NetworkSync::Authority::Host;
    serverScene.Start();
    std::uint16_t port = server->ServerPort();
    CHECK(port != 0);

    Scene clientScene("Client");
    auto* client = clientScene.CreateGameObject("Net")->AddComponent<NetworkManager>();
    client->interpolationRate = 0.0f;
    CHECK(client->StartClient("127.0.0.1", port));
    // The client's copy of the same object — same id, not the authority.
    auto* cliBox = clientScene.CreateGameObject("Box");
    auto* cNs = cliBox->AddComponent<NetworkSync>();
    cNs->netId = "box1"; cNs->authority = NetworkSync::Authority::Host;
    clientScene.Start();

    for (int i = 0; i < 150 && !(client->Joined() && server->PeerCount() >= 1); ++i) {
        serverScene.Update(0.02f); clientScene.Update(0.02f);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(client->Joined());

    // Move + rotate the host's box, then pump frames so it replicates.
    srvBox->transform->localPosition = {3.0f, 5.0f, -2.0f};
    srvBox->transform->localRotation = Quat::Euler(0.0f, 90.0f, 0.0f);

    for (int i = 0; i < 120; ++i) {
        serverScene.Update(0.02f); clientScene.Update(0.02f);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    Vec3 cp = cliBox->transform->localPosition;
    CHECK(std::fabs(cp.x - 3.0f) < 0.01f);
    CHECK(std::fabs(cp.y - 5.0f) < 0.01f);
    CHECK(std::fabs(cp.z + 2.0f) < 0.01f);
    // Rotation replicated too (compare against the host's quaternion).
    Quat sq = srvBox->transform->localRotation, cq = cliBox->transform->localRotation;
    float dot = sq.x*cq.x + sq.y*cq.y + sq.z*cq.z + sq.w*cq.w;
    CHECK(std::fabs(std::fabs(dot) - 1.0f) < 0.01f);

    // The client is NOT the authority: moving its copy must not push back to the host.
    cliBox->transform->localPosition = {99.0f, 99.0f, 99.0f};
    for (int i = 0; i < 60; ++i) {
        serverScene.Update(0.02f); clientScene.Update(0.02f);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    Vec3 hp = srvBox->transform->localPosition;
    CHECK(std::fabs(hp.x - 3.0f) < 0.01f);   // host unchanged by the client

    server->Stop(); client->Stop();
    TEST_MAIN_RESULT();
}
