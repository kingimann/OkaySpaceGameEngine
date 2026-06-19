#include "test_framework.hpp"
#include <Okay.hpp>

#include <chrono>
#include <cstdio>
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
    std::string joinedName; std::uint32_t joinedId = 0; int leftCount = 0;
    server->SetPeerJoinedHandler([&](std::uint32_t id, const std::string& nm) {
        joinedId = id; joinedName = nm;
    });
    server->SetPeerLeftHandler([&](std::uint32_t) { ++leftCount; });
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
    client->SetLocalName("Alice");
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

    // --- Custom message channel: client -> server, and server -> client ---
    {
        std::string fromClient, fromServer;
        std::uint32_t clientMsgSender = 999, serverMsgSender = 999;
        server->SetMessageHandler([&](const NetworkManager::NetMessage& m) {
            if (m.channel == "chat") { fromClient = m.data; clientMsgSender = m.from; }
        });
        client->SetMessageHandler([&](const NetworkManager::NetMessage& m) {
            if (m.channel == "evt") { fromServer = m.data; serverMsgSender = m.from; }
        });

        client->Send("chat", "hello server");
        server->Send("evt", "world tick");

        for (int i = 0; i < 100; ++i) {
            serverScene.Update(0.02f);
            clientScene.Update(0.02f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (!fromClient.empty() && !fromServer.empty()) break;
        }

        CHECK(fromClient == "hello server");
        CHECK(clientMsgSender == client->LocalId());  // stamped with the real sender
        CHECK(fromServer == "world tick");
        CHECK(serverMsgSender == 0);                  // server avatar id is 0
    }

    // --- Roster + join callback: the server knows the client by name ---
    CHECK(joinedId == client->LocalId());
    CHECK(joinedName == "Alice");
    {
        auto peers = server->Peers();
        CHECK(peers.size() == 1);
        CHECK(server->PeerName(client->LocalId()) == "Alice");
    }

    // --- Synced variables: server sets, client receives the same value ---
    {
        server->SetVar("phase", "play");
        server->SetVar("score", "10");
        std::string clientPhase;
        for (int i = 0; i < 100; ++i) {
            serverScene.Update(0.02f); clientScene.Update(0.02f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            clientPhase = client->GetVar("phase");
            if (clientPhase == "play" && client->GetVar("score") == "10") break;
        }
        CHECK(client->GetVar("phase") == "play");
        CHECK(client->GetVar("score") == "10");
        CHECK(server->GetVar("phase") == "play");

        // A client SetVar routes through the server and comes back to everyone.
        client->SetVar("ready", "1");
        for (int i = 0; i < 100; ++i) {
            serverScene.Update(0.02f); clientScene.Update(0.02f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (server->GetVar("ready") == "1") break;
        }
        CHECK(server->GetVar("ready") == "1");
    }

    // --- Ping: the client measures a round-trip time after ~1s of pinging ---
    {
        for (int i = 0; i < 150; ++i) {
            serverScene.Update(0.02f); clientScene.Update(0.02f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (client->RttMs() > 0.0f) break;
        }
        CHECK(client->RttMs() > 0.0f);
    }

    // --- Interpolation: the client's mirror of the host converges to its pos ---
    {
        host->transform->localPosition = {5.0f, 6.0f, 0.0f};
        for (int i = 0; i < 150; ++i) {
            serverScene.Update(0.02f); clientScene.Update(0.02f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        GameObject* mh = clientScene.Find("Remote0");
        CHECK(mh != nullptr);
        if (mh) {
            CHECK_NEAR(mh->transform->localPosition.x, 5.0f, 0.2f);
            CHECK_NEAR(mh->transform->localPosition.y, 6.0f, 0.2f);
        }
    }

    // --- Networked spawn: server spawns a prefab on every peer ---
    {
        // Write a small prefab file to instantiate.
        Scene tmp("p");
        GameObject* proto = tmp.CreateGameObject("Bullet");
        proto->AddComponent<SpriteRenderer>();
        CHECK(SceneSerializer::SaveObjectToFile(*proto, "bullet.okayprefab"));

        server->Spawn("bullet.okayprefab", {3, 0, 0});
        // The spawner has it immediately; the client gets it over the wire.
        CHECK(serverScene.Find("Bullet") != nullptr);
        bool onClient = false;
        for (int i = 0; i < 100; ++i) {
            serverScene.Update(0.02f); clientScene.Update(0.02f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (clientScene.Find("Bullet")) { onClient = true; break; }
        }
        CHECK(onClient);
        std::remove("bullet.okayprefab");
    }

    // --- Targeted send: server -> one client by id ---
    {
        std::string direct;
        client->SetMessageHandler([&](const NetworkManager::NetMessage& m) {
            if (m.channel == "dm") direct = m.data;
        });
        server->SendTo(client->LocalId(), "dm", "just for you");
        for (int i = 0; i < 100; ++i) {
            serverScene.Update(0.02f); clientScene.Update(0.02f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (!direct.empty()) break;
        }
        CHECK(direct == "just for you");
    }

    // --- Client leaving fires the server's peer-left callback ---
    client->Stop();
    for (int i = 0; i < 50; ++i) { serverScene.Update(0.02f); std::this_thread::sleep_for(std::chrono::milliseconds(2)); if (leftCount > 0) break; }
    CHECK(leftCount >= 1);

    server->Stop();
    TEST_MAIN_RESULT();
}
