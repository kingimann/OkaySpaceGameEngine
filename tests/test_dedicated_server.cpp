#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/DedicatedServer.hpp"

#include <atomic>
#include <cstdio>
#include <fstream>
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
        // Server browser listing needs the address players reach.
        const char* a5[] = {"okay-player", "--server", "--advertise"};
        std::string e5; DedicatedServerOptions s5;
        CHECK(ParseDedicatedServerArgs(3, (char**)a5, s5, &e5) && !e5.empty());
        const char* a6[] = {"okay-player", "--server", "--advertise", "--public-addr", "play.example.com", "--region", "EU"};
        std::string e6; DedicatedServerOptions s6;
        CHECK(ParseDedicatedServerArgs(7, (char**)a6, s6, &e6) && e6.empty());
        CHECK(s6.advertise && s6.publicAddr == "play.example.com" && s6.region == "EU");
    }

    // ---- server.cfg: the exported server folder's settings file ----------------
    {
        const std::string path = "test_server.cfg";
        { std::ofstream f(path);
          f << "# OkaySpace server\nname = \"EU 1\"\nport = 47002\nmax_players = 24\npassword = hunter2\n"
               "auth = yes\nadvertise = 1\npublic_addr = play.example.com\nregion = EU\nmax_speed = 12.5\n"
               "rate_limit = 30\nkick_after = 6\n"; }
        DedicatedServerOptions o; std::string e;
        CHECK(LoadDedicatedServerConfig(path, o, &e) && e.empty());
        CHECK(o.name == "EU 1" && o.port == 47002 && o.maxPlayers == 24 && o.password == "hunter2");
        CHECK(o.requireAuth && o.advertise && o.publicAddr == "play.example.com" && o.region == "EU");
        CHECK(o.maxSpeed == 12.5f && o.rateLimit == 30 && o.kickAfter == 6);
        // --config loads it; flags still win.
        const char* a[] = {"okay-player", "--server", "--config", "test_server.cfg", "--port", "47003"};
        DedicatedServerOptions p; std::string pe;
        CHECK(ParseDedicatedServerArgs(6, (char**)a, p, &pe) && pe.empty());
        CHECK(p.port == 47003 && p.name == "EU 1" && DedicatedServerConfigPath() == "test_server.cfg");
        // Comments after values; # inside quotes or a bare word stays.
        { std::ofstream f(path); f << "name = \"Room #1\"   # shown in the browser\nmax_speed = 12   # units/s\npassword = ab#c\nregion = \"\"  ; none\n"; }
        DedicatedServerOptions c; std::string ce;
        CHECK(LoadDedicatedServerConfig(path, c, &ce) && ce.empty());
        CHECK(c.name == "Room #1" && c.maxSpeed == 12.0f && c.password == "ab#c" && c.region.empty());
        { std::ofstream f(path); f << "port = 99999\n"; }
        DedicatedServerOptions q; std::string qe;
        CHECK(!LoadDedicatedServerConfig(path, q, &qe) && qe.find("line 1") != std::string::npos);
        { std::ofstream f(path); f << "colour = blue\n"; }
        CHECK(!LoadDedicatedServerConfig(path, q, &qe) && qe.find("unknown setting") != std::string::npos);
        std::remove(path.c_str());
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
        o.encryption = okay::net::SecureChannel::Available();   // without libsodium the server won't start encrypted
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
