#include "test_framework.hpp"
#include <Okay.hpp>

#include <chrono>
#include <thread>

using namespace okay;

// Runs a server and a client in one process over UDP loopback and checks that
// each peer's avatar position propagates to the other.
int main() {
    RUN_SUITE("net");

    // --- Server scene ---
    Scene serverScene("Server");
    GameObject* srvMgrObj = serverScene.CreateGameObject("NetManager");
    auto* server = srvMgrObj->AddComponent<NetworkManager>();
    GameObject* host = serverScene.CreateGameObject("HostAvatar");
    host->transform->localPosition = {1.0f, 2.0f, 0.0f};
    server->SetLocalAvatar(host->transform, 'H');
    server->SetRemoteFactory([&](std::uint32_t id, char) {
        return serverScene.CreateGameObject("Remote" + std::to_string(id));
    });
    CHECK(server->StartServer(0)); // ephemeral port
    serverScene.Start();
    std::uint16_t port = server->ServerPort();
    CHECK(port != 0);

    // --- Client scene ---
    Scene clientScene("Client");
    GameObject* cliMgrObj = clientScene.CreateGameObject("NetManager");
    auto* client = cliMgrObj->AddComponent<NetworkManager>();
    GameObject* player = clientScene.CreateGameObject("PlayerAvatar");
    player->transform->localPosition = {7.0f, -3.0f, 0.0f};
    client->SetLocalAvatar(player->transform, 'P');
    client->SetRemoteFactory([&](std::uint32_t id, char) {
        return clientScene.CreateGameObject("Remote" + std::to_string(id));
    });
    CHECK(client->StartClient("127.0.0.1", port));
    clientScene.Start();

    // Pump both peers for a while so the handshake and snapshots flow.
    for (int i = 0; i < 200; ++i) {
        serverScene.Update(0.02f);
        clientScene.Update(0.02f);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if (client->Joined() && server->PeerCount() >= 1 && client->PeerCount() >= 1)
            break;
    }

    CHECK(client->Joined());
    CHECK(client->LocalId() != 0);

    // Server should have mirrored the client's avatar.
    CHECK(server->PeerCount() >= 1);
    GameObject* mirroredClient = serverScene.Find("Remote" + std::to_string(client->LocalId()));
    CHECK(mirroredClient != nullptr);
    if (mirroredClient) {
        Vec3 p = mirroredClient->transform->localPosition;
        CHECK_NEAR(p.x, 7.0f, 0.01f);
        CHECK_NEAR(p.y, -3.0f, 0.01f);
    }

    // Client should have mirrored the host avatar (peer id 0).
    CHECK(client->PeerCount() >= 1);
    GameObject* mirroredHost = clientScene.Find("Remote0");
    CHECK(mirroredHost != nullptr);
    if (mirroredHost) {
        Vec3 p = mirroredHost->transform->localPosition;
        CHECK_NEAR(p.x, 1.0f, 0.01f);
        CHECK_NEAR(p.y, 2.0f, 0.01f);
    }

    server->Stop();
    client->Stop();
    TEST_MAIN_RESULT();
}
