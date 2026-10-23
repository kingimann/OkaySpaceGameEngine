#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/JoinLink.hpp"
#include "okay/Net/ServerQuery.hpp"
#include "okay/Net/SessionPresence.hpp"
#include "okay/Platform/GameIpc.hpp"
#include "okay/Platform/Parties.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <thread>

using namespace okay;

// Steam-style launcher multiplayer: join links, server-browser queries (direct
// + LAN), rich presence from a live session, the game <-> launcher files, and
// a game launched to join someone actually getting in.
int main() {
    RUN_SUITE("launcher_join");

    // ---- Join links ------------------------------------------------------------
    {
        JoinLink l; l.game = "Space Arena"; l.addr = "203.0.113.7:45000"; l.room = "match 2";
        l.name = "EU #1"; l.needsPassword = true; l.host = "maya";
        const std::string s = l.Format();
        CHECK(s.rfind("okay://join?game=Space%20Arena", 0) == 0);
        CHECK(s.find("pw=1") != std::string::npos);
        JoinLink r;
        CHECK(JoinLink::Parse(s, r));
        CHECK(r.game == "Space Arena" && r.addr == "203.0.113.7:45000" && r.room == "match 2");
        CHECK(r.name == "EU #1" && r.needsPassword && r.password.empty() && r.host == "maya");
        CHECK(r.Valid() && !r.Relayed() && r.SameSession(l));

        JoinLink rl; rl.game = "Arena"; rl.relay = "relay.example.com:45100"; rl.code = "K7Q-29X"; rl.password = "s3cr&t";
        JoinLink rr;
        CHECK(JoinLink::Parse(rl.Format(), rr));
        CHECK(rr.Relayed() && rr.code == "K7Q-29X" && rr.password == "s3cr&t" && rr.needsPassword);
        CHECK(rr.Where().find("K7Q-29X") != std::string::npos);
        JoinLink bad;
        CHECK(!JoinLink::Parse("https://example.com", bad));
        CHECK(!JoinLink::Parse("okay://join?game=x", bad));   // nowhere to go

        std::string h; std::uint16_t p = 0;
        CHECK(SplitHostPort(" 10.0.0.5:47001 ", h, p) && h == "10.0.0.5" && p == 47001);
        CHECK(SplitHostPort("play.example.com", h, p) && h == "play.example.com" && p == 45000);
        CHECK(!SplitHostPort("host:99999", h, p));
        CHECK(!SplitHostPort("", h, p));
    }

    // ---- A server answers browser queries without anyone joining -------------------
    {
        Scene ss("Server");
        auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->serverName = "EU 1"; srv->gameId = "Arena"; srv->mapName = "Docks";
        srv->maxPlayers = 15; srv->password = "pw"; srv->dedicated = true;
        CHECK(srv->StartServer(0));
        ss.Start();
        const std::uint16_t port = srv->ServerPort();

        std::atomic<bool> stop{false};
        std::thread pump([&] { while (!stop) { ss.Update(0.01f); std::this_thread::sleep_for(std::chrono::milliseconds(2)); } });
        auto res = QueryServers({{"127.0.0.1", port}, {"127.0.0.1", 1}}, 600);
        CHECK(res.size() == 2);
        if (res.size() == 2) {
            const ServerInfo& s = res[0];
            CHECK(s.responded && s.pingMs >= 0);
            CHECK(s.name == "EU 1" && s.game == "Arena" && s.map == "Docks");
            CHECK(s.players == 0 && s.maxPlayers == 15);
            CHECK(s.password && s.dedicated && !s.auth);
            CHECK(!res[1].responded && res[1].pingMs < 0);    // nobody there
        }
        CHECK(srv->PlayerCount() == 0);                        // a query is not a join
        stop = true; pump.join();
    }

    // ---- LAN scan finds a server on the usual ports -----------------------------
    {
        Scene ls("Lan");
        auto* lan = ls.CreateGameObject("Net")->AddComponent<NetworkManager>();
        lan->serverName = "Living room"; lan->gameId = "Arena";
        if (lan->StartServer(45013)) {
            ls.Start();
            std::atomic<bool> stop{false};
            std::thread pump([&] { while (!stop) { ls.Update(0.01f); std::this_thread::sleep_for(std::chrono::milliseconds(2)); } });
            auto res = QueryServers({}, 500, /*lan=*/true);
            bool found = false;
            for (const auto& s : res) if (s.lan && s.port == 45013 && s.name == "Living room") { found = true; CHECK(s.players == 1); }
            CHECK(found);
            stop = true; pump.join();
        }
    }

    // ---- Rich presence from a live session + joining by link ----------------------
    {
        Scene hs("Host");
        auto* host = hs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        host->serverName = "Maya's game"; host->password = "letmein"; host->maxPlayers = 3;
        CHECK(host->StartServer(0));
        hs.Start();
        std::string inviteLink;
        GamePresence hp = DescribeSession(host, "Arena", "127.0.0.1", &inviteLink);
        CHECK(hp.hosting && hp.players == 1 && hp.maxPlayers == 4);
        CHECK(hp.status == "Hosting Maya's game");
        JoinLink pub, inv;
        CHECK(JoinLink::Parse(hp.join, pub) && JoinLink::Parse(inviteLink, inv));
        CHECK(pub.game == "Arena" && pub.needsPassword && pub.password.empty());   // public: no password
        CHECK(inv.password == "letmein" && inv.addr == "127.0.0.1:" + std::to_string(host->ServerPort()));
        CHECK(DescribeSession(nullptr, "Arena", "").status == "In the menus");

        // A game started to join (--join): its NetworkManager joins on Start.
        NetworkManager::SetLaunchJoin(inviteLink);
        CHECK(NetworkManager::LaunchedToJoin());
        Scene cs("Friend");
        auto* c = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        c->autoStart = NetworkManager::AutoStart::Host;   // the launch join wins over the game's own start
        cs.Start();
        CHECK(c->IsClient());
        CHECK(NetworkManager::TakeLaunchJoin().empty());  // consumed
        for (int i = 0; i < 300 && !c->Joined(); ++i) {
            hs.Update(0.02f); cs.Update(0.02f); std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        CHECK(c->Joined());                               // the password came with the invite
        GamePresence cp = DescribeSession(c, "Arena", "192.168.1.20");
        CHECK(!cp.hosting && cp.status == "Playing on Maya's game");
        JoinLink cl;
        CHECK(JoinLink::Parse(cp.join, cl) && cl.addr.rfind("192.168.1.20:", 0) == 0);   // friends get the LAN address, not 127.0.0.1
        for (int i = 0; i < 20; ++i) { hs.Update(0.02f); cs.Update(0.02f); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
        CHECK(host->PlayerCount() == 2);
    }

    // ---- Game <-> launcher files ----------------------------------------------------
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / ("okay-ipc-test-" + std::to_string(GameIpc::CurrentPid()));
#if defined(_WIN32)
        _putenv_s("OKAY_IPC_DIR", dir.string().c_str());
#else
        setenv("OKAY_IPC_DIR", dir.string().c_str(), 1);
#endif
        GamePresence g; g.title = "Arena"; g.status = "Hosting"; g.join = "okay://join?game=Arena&addr=1.2.3.4:45000";
        g.invite = g.join + "&pass=x"; g.players = 2; g.maxPlayers = 8; g.hosting = true;
        CHECK(GameIpc::WritePresence(g));
        GamePresence r;
        CHECK(GameIpc::ReadPresence(GameIpc::CurrentPid(), r));
        CHECK(r.title == "Arena" && r.join == g.join && r.invite == g.invite && r.players == 2 && r.maxPlayers == 8 && r.hosting);
        CHECK(!GameIpc::ReadPresence(GameIpc::CurrentPid() + 1, r));

        CHECK(GameIpc::RequestInvite());
        auto reqs = GameIpc::TakeInviteRequests();
        CHECK(reqs.size() == 1 && reqs[0] == GameIpc::CurrentPid());
        CHECK(GameIpc::TakeInviteRequests().empty());     // consumed

        std::string link;
        CHECK(!GameIpc::TakeJoin(link));
        CHECK(GameIpc::PostJoin(GameIpc::CurrentPid(), "okay://join?game=Arena&addr=5.6.7.8:45000"));
        CHECK(GameIpc::TakeJoin(link) && link == "okay://join?game=Arena&addr=5.6.7.8:45000");
        CHECK(!GameIpc::TakeJoin(link));

        GameIpc::ClearPresence();
        CHECK(!GameIpc::ReadPresence(GameIpc::CurrentPid(), r));
        std::error_code ec; fs::remove_all(dir, ec);
    }

    // ---- Party invites (chat messages) ----------------------------------------------------
    {
        const std::string pid = "0f6a2c1e-5b7d-4e8a-9c3f-2d1b0a9e8f7c";
        std::string id, from;
        CHECK(Parties::ParseInvite(Parties::InviteBody(pid, "maya"), id, from));
        CHECK(id == pid && from == "maya");
        CHECK(!Parties::ParseInvite("[Party] nope", id, from));          // not a party id
        CHECK(!Parties::ParseInvite("hello [Party] " + pid, id, from));  // only at the start
        Party p; p.id = pid; PartyMember m; m.uid = "u1"; m.username = "theo"; p.members.push_back(m);
        CHECK(p.Valid() && p.Find("u1") && p.Find("u1")->Name() == "theo" && !p.Find("u2"));
    }

    TEST_MAIN_RESULT();
}
