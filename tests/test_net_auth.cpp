#include "test_framework.hpp"
#include <Okay.hpp>

#include <chrono>
#include <thread>

using namespace okay;

// A server with a token verifier accepts a client presenting a valid token (and
// binds it to a verified identity) and rejects one with a bad token.
int main() {
    RUN_SUITE("net_auth");

    // --- Server with a token verifier: "letmein:<uid>" accepts as <uid> --------
    Scene serverScene("Server");
    auto* server = serverScene.CreateGameObject("Net")->AddComponent<NetworkManager>();
    server->SetTokenVerifier([](const std::string& token, std::string& outUserId) {
        if (token.rfind("letmein:", 0) == 0) { outUserId = token.substr(8); return true; }
        return false;   // anything else (including "") is refused
    });
    CHECK(server->StartServer(0));
    serverScene.Start();
    std::uint16_t port = server->ServerPort();
    CHECK(port != 0);

    auto pump = [&](Scene& cs, NetworkManager* c, int frames) {
        for (int i = 0; i < frames; ++i) {
            serverScene.Update(0.02f); cs.Update(0.02f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (c->Joined() || c->JoinRejected()) break;
        }
    };

    // --- Good token: joins and is bound to its verified id ---------------------
    {
        Scene cs("GoodClient");
        auto* c = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        c->SetAuthToken("letmein:user-42");
        CHECK(c->StartClient("127.0.0.1", port));
        cs.Start();
        pump(cs, c, 250);
        CHECK(c->Joined());
        CHECK(!c->JoinRejected());
        CHECK(c->LocalUserId() == "user-42");           // client learned its identity
        CHECK(server->PeerUserId(c->LocalId()) == "user-42");  // server bound it
    }

    // --- Bad token: refused, never joins ---------------------------------------
    {
        Scene cs("BadClient");
        auto* c = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        c->SetAuthToken("forged");
        CHECK(c->StartClient("127.0.0.1", port));
        cs.Start();
        pump(cs, c, 250);
        CHECK(!c->Joined());
        CHECK(c->JoinRejected());
    }

    // --- No verifier set => open server, anonymous join still works ------------
    {
        Scene openScene("OpenServer");
        auto* open = openScene.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(open->StartServer(0));
        openScene.Start();
        std::uint16_t oport = open->ServerPort();

        Scene cs("AnonClient");
        auto* c = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(c->StartClient("127.0.0.1", oport));
        cs.Start();
        for (int i = 0; i < 250; ++i) {
            openScene.Update(0.02f); cs.Update(0.02f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (c->Joined()) break;
        }
        CHECK(c->Joined());                 // open server: no token required
        CHECK(c->LocalUserId().empty());    // anonymous
    }

    // --- Encrypted session: messages still round-trip; wire is sealed -----------
    {
        Scene es("EncServer");
        auto* esrv = es.CreateGameObject("Net")->AddComponent<NetworkManager>();
        esrv->encryption = true;
        std::string gotMsg, gotBig;
        esrv->SetMessageHandler([&](const NetworkManager::NetMessage& m){
            if (m.channel == "secret") gotMsg = m.data;
            if (m.channel == "big")    gotBig = m.data;
        });
        CHECK(esrv->StartServer(0));
        es.Start();
        std::uint16_t eport = esrv->ServerPort();

        Scene ec("EncClient");
        auto* ecli = ec.CreateGameObject("Net")->AddComponent<NetworkManager>();
        ecli->encryption = true;
        CHECK(ecli->StartClient("127.0.0.1", eport));
        ec.Start();
        for (int i = 0; i < 250; ++i) {
            es.Update(0.02f); ec.Update(0.02f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (ecli->Joined()) break;
        }
        CHECK(ecli->Joined());

        // A large payload exercises encrypt -> fragment -> reassemble -> decrypt.
        std::string big; big.reserve(8000);
        for (int i = 0; i < 8000; ++i) big.push_back((char)('a' + (i * 3 + 1) % 26));
        ecli->SendReliable("secret", "top secret payload");
        ecli->SendReliable("big", big);
        for (int i = 0; i < 250; ++i) {
            es.Update(0.02f); ec.Update(0.02f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (!gotMsg.empty() && !gotBig.empty()) break;
        }
        CHECK(gotMsg == "top secret payload");   // delivered through the encrypted channel
        CHECK(gotBig == big);                    // large encrypted+fragmented message intact

        if (okay::net::SecureChannel::Available()) {   // only when built with libsodium
            CHECK(ecli->Encrypted());
            CHECK(esrv->Encrypted());
        }
    }

    TEST_MAIN_RESULT();
}
