#include "test_framework.hpp"
#include <Okay.hpp>

#include <chrono>
#include <thread>
#include <cmath>

using namespace okay;

// SpawnOwned + Despawn: the host spawns a prefab that is replicated to the client
// with a follower NetworkSync, the host drives its movement (the client follows),
// and Despawn removes it on both peers.
int main() {
    RUN_SUITE("net_spawn");

    // A tiny prefab on disk for both peers to instantiate.
    {
        Scene a("A");
        GameObject* p = a.CreateGameObject("Bullet");
        p->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
        CHECK(SceneSerializer::SaveObjectToFile(*p, "/tmp/okay_bullet.okayprefab"));
    }
    const std::string prefab = "/tmp/okay_bullet.okayprefab";

    Scene serverScene("Server");
    auto* server = serverScene.CreateGameObject("Net")->AddComponent<NetworkManager>();
    server->interpolationRate = 0.0f;   // snap
    CHECK(server->StartServer(0));
    serverScene.Start();
    std::uint16_t port = server->ServerPort();
    CHECK(port != 0);

    Scene clientScene("Client");
    auto* client = clientScene.CreateGameObject("Net")->AddComponent<NetworkManager>();
    client->interpolationRate = 0.0f;
    CHECK(client->StartClient("127.0.0.1", port));
    clientScene.Start();

    for (int i = 0; i < 150 && !(client->Joined() && server->PeerCount() >= 1); ++i) {
        serverScene.Update(0.02f); clientScene.Update(0.02f);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(client->Joined());

    // Host spawns a bullet it owns at (1,2,3).
    GameObject* hostBullet = server->SpawnOwned(prefab, {1.0f, 2.0f, 3.0f});
    CHECK(hostBullet != nullptr);
    std::string id = hostBullet && hostBullet->GetComponent<NetworkSync>()
                   ? hostBullet->GetComponent<NetworkSync>()->netId : "";
    CHECK(!id.empty());

    for (int i = 0; i < 80; ++i) {
        serverScene.Update(0.02f); clientScene.Update(0.02f);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // The client received the spawn and placed it.
    GameObject* cliBullet = clientScene.Find("Bullet");
    CHECK(cliBullet != nullptr);
    if (cliBullet) {
        Vec3 p = cliBullet->transform->localPosition;
        CHECK(std::fabs(p.x - 1.0f) < 0.05f);
        CHECK(std::fabs(p.y - 2.0f) < 0.05f);
        CHECK(std::fabs(p.z - 3.0f) < 0.05f);
    }

    // The host moves its bullet; the client's copy follows.
    if (hostBullet) hostBullet->transform->localPosition = {7.0f, 0.0f, -1.0f};
    for (int i = 0; i < 80; ++i) {
        serverScene.Update(0.02f); clientScene.Update(0.02f);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (cliBullet) {
        Vec3 p = cliBullet->transform->localPosition;
        CHECK(std::fabs(p.x - 7.0f) < 0.05f);
        CHECK(std::fabs(p.z + 1.0f) < 0.05f);
    }

    // Despawn removes it on both peers.
    server->Despawn(id);
    for (int i = 0; i < 80; ++i) {
        serverScene.Update(0.02f); clientScene.Update(0.02f);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(serverScene.Find("Bullet") == nullptr);
    CHECK(clientScene.Find("Bullet") == nullptr);

    server->Stop(); client->Stop();
    TEST_MAIN_RESULT();
}
