#include "test_framework.hpp"
#include <Okay.hpp>
#include <chrono>
#include <thread>

using namespace okay;

// A server with two clients in *different* rooms: each client only sees peers
// (and the host) in its own room — the basis for lobbies / separate matches.
int main() {
    RUN_SUITE("net_rooms");

    // Server hosts in room "A".
    Scene serverScene("Server");
    auto* server = serverScene.CreateGameObject("Net")->AddComponent<NetworkManager>();
    GameObject* host = serverScene.CreateGameObject("Host");
    host->transform->localPosition = {1, 1, 0};
    server->SetLocalAvatar(host->transform, 'H');
    server->SetRemoteFactory([&](std::uint32_t id, char) {
        return serverScene.CreateGameObject("R" + std::to_string(id));
    });
    server->SetRoom("A");
    CHECK(server->StartServer(0));
    serverScene.Start();
    std::uint16_t port = server->ServerPort();

    auto makeClient = [&](Scene& sc, const char* room, const char* name) {
        auto* c = sc.CreateGameObject("Net")->AddComponent<NetworkManager>();
        GameObject* av = sc.CreateGameObject("Me");
        c->SetLocalAvatar(av->transform, 'P');
        c->SetRemoteFactory([&sc](std::uint32_t id, char) {
            return sc.CreateGameObject("Remote" + std::to_string(id));
        });
        c->SetRoom(room);
        c->SetLocalName(name);
        c->StartClient("127.0.0.1", port);
        sc.Start();
        return c;
    };

    Scene a("A"); NetworkManager* clientA = makeClient(a, "A", "Alice");  // same room as host
    Scene b("B"); NetworkManager* clientB = makeClient(b, "B", "Bob");    // different room

    for (int i = 0; i < 250; ++i) {
        serverScene.Update(0.02f); a.Update(0.02f); b.Update(0.02f);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if (clientA->Joined() && clientB->Joined() && server->PeerCount() >= 2) break;
    }
    CHECK(clientA->Joined());
    CHECK(clientB->Joined());

    // Let snapshots flow.
    for (int i = 0; i < 100; ++i) {
        serverScene.Update(0.02f); a.Update(0.02f); b.Update(0.02f);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    // Client A shares the host's room -> it mirrors the host (Remote0).
    CHECK(a.Find("Remote0") != nullptr);
    // Client B is in another room -> it must NOT see the host.
    CHECK(b.Find("Remote0") == nullptr);
    // A and B never see each other's avatars either.
    CHECK(a.Find("Remote" + std::to_string(clientB->LocalId())) == nullptr);
    CHECK(b.Find("Remote" + std::to_string(clientA->LocalId())) == nullptr);

    // A broadcast from A reaches the host (same room) but not B.
    std::string atHost, atB;
    server->SetMessageHandler([&](const NetworkManager::NetMessage& m){ if (m.channel=="hi") atHost = m.data; });
    clientB->SetMessageHandler([&](const NetworkManager::NetMessage& m){ if (m.channel=="hi") atB = m.data; });
    clientA->Send("hi", "roomA-only");
    for (int i = 0; i < 100; ++i) {
        serverScene.Update(0.02f); a.Update(0.02f); b.Update(0.02f);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if (!atHost.empty()) break;
    }
    CHECK(atHost == "roomA-only");
    CHECK(atB.empty());                 // B is in another room

    // --- Lobby: ready-up in room A, host starts the match for room A only ---
    {
        CHECK(!server->AllReady());          // nobody ready yet
        clientA->SetReady(true);
        for (int i = 0; i < 100; ++i) {
            serverScene.Update(0.02f); a.Update(0.02f); b.Update(0.02f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (server->ReadyCount() >= 1) break;
        }
        CHECK(server->ReadyCount() == 1);
        CHECK(server->AllReady());           // the one client in room A is ready

        server->StartMatch();                // begins the match for room A
        for (int i = 0; i < 100; ++i) {
            serverScene.Update(0.02f); a.Update(0.02f); b.Update(0.02f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (clientA->MatchStarted()) break;
        }
        CHECK(clientA->MatchStarted());      // room A got the start
        CHECK(!clientB->MatchStarted());     // room B is unaffected
    }

    // startRoom serializes on the no-code component.
    {
        Scene s("cfg");
        auto* nm = s.CreateGameObject("N")->AddComponent<NetworkManager>();
        nm->startRoom = "main";
        nm->startName = "Hero";
        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); SceneSerializer::Deserialize(s2, txt);
        auto* nm2 = s2.Find("N")->GetComponent<NetworkManager>();
        CHECK(nm2 && nm2->startRoom == "main");
        CHECK(nm2 && nm2->startName == "Hero");
    }

    clientA->Stop(); clientB->Stop(); server->Stop();
    TEST_MAIN_RESULT();
}
