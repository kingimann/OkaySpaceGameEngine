#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/MatchResults.hpp"
#include "okay/Platform/GameIpc.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <thread>

using namespace okay;

// Match results: the report a server builds from its roster (places, winners,
// time played), its JSON, the match clock on NetworkManager, the stats rows the
// launcher reads back, and the local "Recently played with" log.
int main() {
    RUN_SUITE("match_results");

    // ---- A report from a roster ---------------------------------------------------
    {
        std::vector<NetworkManager::RosterEntry> roster = {
            {0, "host", "", 12, 300.0f},
            {1, "maya", "6f94da84-5a9b-48c6-a2d0-90059bd6bf79", 20, 290.0f},
            {2, "theo", "", 12, 45.0f},
            {3, "rin \"the\" fast", "b1a2c3d4-0000-4000-8000-000000000001", 3, 100.0f},
        };
        MatchReport r = MatchReport::FromRoster(roster, 0, 120.0f);
        CHECK(r.players.size() == 4);
        CHECK(r.players[0].name == "maya" && r.players[0].place == 1 && r.players[0].won);
        CHECK(r.players[1].place == 2 && r.players[2].place == 2);   // equal scores share a place
        CHECK(!r.players[1].won && !r.players[3].won && r.players[3].place == 4);
        bool hostSelf = false;
        for (const auto& p : r.players) if (p.name == "host") hostSelf = p.self;
        CHECK(hostSelf);
        CHECK(r.players[0].seconds == 120.0f);                        // capped at the match length
        bool theoTime = false;
        for (const auto& p : r.players) if (p.name == "theo") theoTime = p.seconds == 45.0f;
        CHECK(theoTime);

        // Explicit winners (a team): ids 2 and 3.
        MatchReport t = MatchReport::FromRoster(roster, ~0u, 0.0f, {2, 3});
        int wins = 0; bool anySelf = false;
        for (const auto& p : t.players) { if (p.won) ++wins; anySelf |= p.self; }
        CHECK(wins == 2 && !anySelf);                                  // dedicated: nobody is "self"

        // Alone: nobody "wins" by default.
        MatchReport solo = MatchReport::FromRoster(std::vector<NetworkManager::RosterEntry>{{0, "me", "", 5, 10.0f}}, 0, 10.0f);
        CHECK(solo.players.size() == 1 && !solo.players[0].won && solo.players[0].place == 1);

        r.game = "Star Arena"; r.map = "Docks"; r.server = "EU 1"; r.dedicated = false;
        const std::string js = r.ToJson();
        CHECK(js.find("\"game\":\"Star Arena\"") != std::string::npos);
        CHECK(js.find("\"user_id\":null") != std::string::npos);
        CHECK(js.find("\"user_id\":\"6f94da84-5a9b-48c6-a2d0-90059bd6bf79\"") != std::string::npos);
        CHECK(js.find("rin \\\"the\\\" fast") != std::string::npos);  // escaped
        CHECK(js.find("\"self\":true") != std::string::npos && js.find("\"duration\":120") != std::string::npos);
    }

    // ---- Reading rows the database sends back ----------------------------------------
    {
        const std::string body = R"([{"user_id":"u1","username":"maya","display_name":"Maya Chen","avatar_url":null,"matches":80,"wins":42,"total_score":9860,"best_score":102,"seconds":43200,"last_played":"2026-10-20T18:04:05.123+00:00"},
                                     {"user_id":"u2","username":"nova","display_name":null,"matches":3,"wins":1,"total_score":120,"best_score":61,"seconds":900,"last_played":null}])";
        auto rows = MatchResults::SplitObjects(body);
        CHECK(rows.size() == 2);
        CHECK(MatchResults::Field(rows[0], "display_name") == "Maya Chen" && MatchResults::Field(rows[1], "display_name").empty());
        CHECK(MatchResults::Field(rows[0], "wins") == "42" && MatchResults::Field(rows[0], "avatar_url").empty());
        CHECK(MatchResults::ParseIso(MatchResults::Field(rows[0], "last_played")) == 1792519445L);
        int n = 0; const char* const* k = MatchResults::Stats(&n);
        CHECK(n == 5 && std::string(k[0]) == "wins" && std::string(k[4]) == "time");
    }

    // ---- The match clock on a server --------------------------------------------------
    {
        Scene ss("Server");
        auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(srv->StartServer(0));
        ss.Start();
        srv->SetPeerScore(0, 7);
        for (int i = 0; i < 10; ++i) ss.Update(0.1f);
        srv->BeginMatch();
        CHECK(srv->InMatch() && srv->PeerScore(0) == 0);               // scores reset
        for (int i = 0; i < 20; ++i) ss.Update(0.1f);
        const float len = srv->EndMatch();
        CHECK(len > 1.9f && len < 2.1f);
        CHECK(!srv->InMatch());
        for (int i = 0; i < 10; ++i) ss.Update(0.1f);
        CHECK(srv->MatchSeconds() > 1.9f && srv->MatchSeconds() < 2.1f);   // frozen once ended
        srv->Stop();
        // Signed out: nothing is sent.
        CHECK(!SubmitMatchAsync(MatchReport{}));
    }

    // ---- "Recently played with" log ---------------------------------------------------
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / ("okay-played-test-" + std::to_string(GameIpc::CurrentPid()));
#if defined(_WIN32)
        _putenv_s("OKAY_IPC_DIR", dir.string().c_str());
#else
        setenv("OKAY_IPC_DIR", dir.string().c_str(), 1);
#endif
        CHECK(GameIpc::ReadPlayedWith().empty());
        PlayedWith a; a.userId = "u1"; a.name = "maya"; a.game = "Star Arena"; a.when = 1000;
        PlayedWith b; b.name = "Guest\t42"; b.game = "Kart Rush"; b.when = 2000;
        CHECK(GameIpc::RecordPlayedWith({a, b}));
        auto l = GameIpc::ReadPlayedWith();
        CHECK(l.size() == 2 && l[0].name == "Guest 42" && l[1].userId == "u1");   // newest first, tabs cleaned
        PlayedWith a2 = a; a2.game = "Kart Rush"; a2.when = 3000;                // same account, later
        PlayedWith b2; b2.name = "guest 42"; b2.game = "kart rush"; b2.when = 500; // same name+game, older
        CHECK(GameIpc::RecordPlayedWith({a2, b2}));
        l = GameIpc::ReadPlayedWith();
        CHECK(l.size() == 2 && l[0].userId == "u1" && l[0].game == "Kart Rush" && l[1].when == 2000);
        std::vector<PlayedWith> many;
        for (int i = 0; i < 130; ++i) { PlayedWith p; p.name = "p" + std::to_string(i); p.game = "G"; p.when = 5000 + i; many.push_back(p); }
        CHECK(GameIpc::RecordPlayedWith(many));
        l = GameIpc::ReadPlayedWith(1000);
        CHECK(l.size() == 100 && l[0].name == "p129");                // capped, newest kept
        std::error_code ec; fs::remove_all(dir, ec);
    }

    TEST_MAIN_RESULT();
}
