#include "test_framework.hpp"
#include <Okay.hpp>

#include <chrono>
#include <thread>

using namespace okay;

// First-class RPC + chat over UDP loopback: an RPC dispatches by name to a
// registered handler on the other peer, and chat fans out + logs on everyone.
int main() {
    RUN_SUITE("net_rpc");

    Scene serverScene("Server");
    auto* server = serverScene.CreateGameObject("Net")->AddComponent<NetworkManager>();
    server->SetLocalName("Host");
    CHECK(server->StartServer(0));
    serverScene.Start();
    std::uint16_t port = server->ServerPort();
    CHECK(port != 0);

    Scene clientScene("Client");
    auto* client = clientScene.CreateGameObject("Net")->AddComponent<NetworkManager>();
    client->SetLocalName("Alice");
    CHECK(client->StartClient("127.0.0.1", port));
    clientScene.Start();

    // RPC handlers: record what each side receives.
    std::string srvGot, cliGot; std::uint32_t srvFrom = 999;
    server->OnRpc("hello", [&](std::uint32_t from, const std::string& data) { srvGot = data; srvFrom = from; });
    client->OnRpc("welcome", [&](std::uint32_t, const std::string& data) { cliGot = data; });

    // Chat handlers/logs.
    int srvChat = 0, cliChat = 0;
    server->OnChat([&](const NetworkManager::ChatEntry&) { ++srvChat; });
    client->OnChat([&](const NetworkManager::ChatEntry&) { ++cliChat; });

    for (int i = 0; i < 150 && !(client->Joined() && server->PeerCount() >= 1); ++i) {
        serverScene.Update(0.02f); clientScene.Update(0.02f);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(client->Joined());

    // Client RPC -> server handler "hello".
    client->Rpc("hello", "hi-from-alice");
    // Server RPC -> client handler "welcome".
    server->Rpc("welcome", "greetings");
    // Chat both ways.
    client->Chat("hello everyone");   // logs locally on the client immediately
    server->Chat("hi back");

    for (int i = 0; i < 120; ++i) {
        serverScene.Update(0.02f); clientScene.Update(0.02f);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    CHECK(srvGot == "hi-from-alice");
    CHECK(srvFrom == client->LocalId());
    CHECK(cliGot == "greetings");

    // The client logged its own line immediately and then the host's line.
    CHECK(client->ChatLog().size() >= 2);
    CHECK(server->ChatLog().size() >= 2);
    bool sawAlice = false, sawHost = false;
    for (const auto& e : server->ChatLog()) {
        if (e.text == "hello everyone") sawAlice = true;
        if (e.text == "hi back") sawHost = true;
    }
    CHECK(sawAlice);     // host received the client's chat
    CHECK(sawHost);      // host logged its own chat
    CHECK(cliChat >= 1); // the client's OnChat fired for the host's line

    server->Stop(); client->Stop();
    TEST_MAIN_RESULT();
}
