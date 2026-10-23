#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/DedicatedServer.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace okay;

// The dedicated server: argument parsing, non-blocking token checks, the
// "dedicated" NetworkManager mode (no host avatar, hears every room) and the
// headless run loop players can join.
int main() {
    RUN_SUITE("dedicated_server");

    // ---- Arguments -------------------------------------------------------------
    {
        DedicatedServerOptions o; std::string err;
        const char* a1[] = {"okay-player", "level.okayscene"};
        CHECK(!ParseDedicatedServerArgs(2, (char**)a1, o, &err));   // no --server: a normal game
        const char* a2[] = {"okay-player", "--server", "--port", "47001", "--max-players", "32", "--tick", "60",
                            "--name", "EU 1", "--auth", "--no-encryption", "--scene", "arena.okayscene", "--run-for", "2"};
        DedicatedServerOptions p;
        CHECK(ParseDedicatedServerArgs(16, (char**)a2, p, &err));
        CHECK(err.empty());
        CHECK(p.port == 47001 && p.maxPlayers == 32 && p.tickRate == 60.0f && p.name == "EU 1");
        CHECK(p.requireAuth && !p.encryption && p.scenePath == "arena.okayscene" && p.maxRuntime == 2.0f);
        const char* a3[] = {"okay-player", "--server", "--port", "99999"};
        DedicatedServerOptions q; std::string e3;
        CHECK(ParseDedicatedServerArgs(4, (char**)a3, q, &e3));
        CHECK(!e3.empty());                                         // out of range -> an error
        const char* a4[] = {"okay-player", "--server", "--bogus"};
        std::string e4; DedicatedServerOptions r;
        CHECK(ParseDedicatedServerArgs(3, (char**)a4, r, &e4) && !e4.empty());
    }

    // ---- AsyncTokenCheck: pending, then a cached answer -------------------------
    {
        std::atomic<int> calls{0};
        AsyncTokenCheck check([&](const std::string& token, std::string& uid) {
            ++calls;
            std::this_thread::sleep_for(std::chrono::milliseconds(30));   // a slow web request
            if (token == "good") { uid = "user-7"; return true; }
            return false;
        });
        std::string uid;
        CHECK(check.Check("", uid) == 0);                     // anonymous: refused at once
        CHECK(check.Check("good", uid) == -1);                // first ask: pending, never blocks
        int r = -1;
        for (int i = 0; i < 200 && r < 0; ++i) { std::this_thread::sleep_for(std::chrono::milliseconds(5)); r = check.Check("good", uid); }
        CHECK(r == 1);
        CHECK(uid == "user-7");
        int b = -1; std::string u2;
        for (int i = 0; i < 200 && b < 0; ++i) { b = check.Check("forged", u2); if (b < 0) std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        CHECK(b == 0);
        const int before = calls;
        CHECK(check.Check("good", uid) == 1);                 // cached: no second web request
        CHECK(calls == before);
    }

    // ---- Dedicated mode + async join check, in-process ---------------------------
    {
        Scene ss("Server");
        auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->dedicated = true;
        AsyncTokenCheck auth([](const std::string& token, std::string& uid) {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            if (token.rfind("ok:", 0) == 0) { uid = token.substr(3); return true; }
            return false;
        });
        srv->SetJoinCheck([&](const std::string& t, std::string& uid) { return auth.Check(t, uid); });
        std::vector<std::string> heard;
        srv->SetMessageHandler([&](const NetworkManager::NetMessage& m) { heard.push_back(m.data); });
        CHECK(srv->StartServer(0));
        ss.Start();
        const std::uint16_t port = srv->ServerPort();

        auto makeClient = [&](Scene& cs, const char* token, const char* room) {
            auto* c = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
            c->SetAuthToken(token); c->SetRoom(room);
            CHECK(c->StartClient("127.0.0.1", port));
            cs.Start();
            return c;
        };
        Scene ca("A"), cb("B"), cx("Forged");
        auto* a = makeClient(ca, "ok:alice", "match-1");
        auto* b = makeClient(cb, "ok:bob", "match-2");
        auto* x = makeClient(cx, "nope", "match-1");
        for (int i = 0; i < 400; ++i) {
            ss.Update(0.02f); ca.Update(0.02f); cb.Update(0.02f); cx.Update(0.02f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (a->Joined() && b->Joined() && x->JoinRejected()) break;
        }
        CHECK(a->Joined() && b->Joined());                  // waited for the check, then got in
        CHECK(x->JoinRejected() && !x->Joined());           // forged token refused
        CHECK(srv->PeerUserId(a->LocalId()) == "alice");
        CHECK(a->LocalUserId() == "alice");
        // A dedicated server hears every room (one process, many matches).
        a->SendReliable("chat", "from match 1");
        b->SendReliable("chat", "from match 2");
        for (int i = 0; i < 200 && heard.size() < 2; ++i) {
            ss.Update(0.02f); ca.Update(0.02f); cb.Update(0.02f);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        CHECK(heard.size() == 2);
        // And it has no avatar of its own: alone in its room, a client sees nobody.
        for (int i = 0; i < 30; ++i) { ss.Update(0.02f); ca.Update(0.02f); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        CHECK(a->PeerCount() == 0);
    }

    // ---- The headless run loop: a player can join it, then it stops ------------------
    {
        Scene server("Arena");
        DedicatedServerOptions o;
        o.port = 47391; o.tickRate = 50.0f; o.maxRuntime = 3.0f; o.statusEvery = 0.0f; o.name = "Test Arena";
        std::atomic<int> rc{-1};
        std::thread run([&] { rc = RunDedicatedServer(server, o); });   // adds a NetworkManager itself

        Scene cs("Player");
        auto* c = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        c->SetLocalName("tester");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));   // let it bind
        CHECK(c->StartClient("127.0.0.1", o.port));
        cs.Start();
        for (int i = 0; i < 300 && !c->Joined(); ++i) { cs.Update(0.02f); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        CHECK(c->Joined());
        CHECK(c->ServerName() == "Test Arena");
        RequestDedicatedServerStop();
        run.join();
        CHECK(rc == 0);
    }

    TEST_MAIN_RESULT();
}
