// OkayBackend round 3: golden request tests for the Database part (docs/platform/okay_backend.md, sections 6, 10.1 and 16).
//
// Every table read and write and every database function call that moved onto okay::backend::Database in round 3 is run
// against a fake server through the programs' own public functions: AccountService's username check, cloud saves and
// leaderboards; Matchmaking (game_sessions); MatchResults (report_match, game_leaderboard, my_game_stats, recent_players);
// and Matchmaker's Supabase queue (queue_join, queue_poll, queue_leave, queue_host_ready). The exact requests (method, URL
// with its query in order, header lines in order, body) and what the program made of each answer must equal the GOLDEN
// texts in test_okay_backend_golden_db.inc, which were recorded from the code BEFORE the move (run with
// OKAY_GOLDEN_PRINT=1 to print them again). Then the same requests are sent through the Supabase provider directly
// (okay::backend::supabase::Database over a RecordingTransport) and must be the same bytes.
//
// One part of one address changes with the clock: Matchmaking's browse asks for rows newer than "now - 150 s". The fake
// server writes that time down as <FRESH> and the test checks it separately (within 5 seconds of what it should be).
//
// No request ever leaves the process: every call goes to the test transport. The address, key, names and tokens are made up.
#include "test_framework.hpp"
#include "okay_backend_golden.hpp"   // the fake server, made-up tokens, RequestsOf
#include <Okay.hpp>
#include "okay/Net/Matchmaker.hpp"
#include "okay/Net/MatchResults.hpp"
#include "okay/Net/Matchmaking.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "okay/Platform/Account/Account.hpp"
#include "okay/Platform/Account/AccountService.hpp"
#include "okay/Backend/Backend.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
namespace acc = okay::account;
using golden::kUrl; using golden::kKey; using golden::Tok; using golden::Fake; using golden::Http; using golden::NoAnswer;
using golden::SaveSession; using golden::RequestsOf;

// ---- Helpers -------------------------------------------------------------------------------------------------------------
static const int kFresh = 150;      // Matchmaking::Browse's default window
static long g_badFresh = 0;         // browse times that were not "now - window" (checked at the end)

/// Writes Matchmaking's "updated_at=gt.<time>" as <FRESH> and checks the time is now - `window` (give or take 5 s).
static std::string RewriteFresh(const std::string& url, int window) {
    const std::string tag = "updated_at=gt.";
    const std::size_t p = url.find(tag);
    if (p == std::string::npos) return url;
    const std::size_t v = p + tag.size();
    const std::size_t e = url.find('&', v);
    const std::string when = url.substr(v, e == std::string::npos ? std::string::npos : e - v);
    const long t = okay::MatchResults::ParseIso(when);
    const long want = (long)std::time(nullptr) - window;
    if (when.size() != 20 || when.back() != 'Z' || t < want - 5 || t > want + 5) ++g_badFresh;
    return url.substr(0, v) + "<FRESH>" + (e == std::string::npos ? std::string() : url.substr(e));
}

static void Hook(acc::AccountService& a, Fake& f) {
    a.SetTransportForTest([&f](const std::string& m, const std::string& u, const std::vector<std::string>& h, const std::string& b) {
        return f.Answer(m, u, h, b);
    });
}
static std::unique_ptr<acc::AccountService> Make(const fs::path& dir, Fake& f) {
    auto a = std::make_unique<acc::AccountService>(dir, kUrl, kKey);
    Hook(*a, f);
    return a;
}
/// The shared account (okay::Account, what Matchmaking and Matchmaker use), signed in as `user` with token `tok`.
static void UseShared(const fs::path& dir, Fake& f, int tok) {
    SaveSession(dir, "alex", Tok(tok), "r" + std::to_string(tok));
    okay::Account::Configure(dir.string(), kUrl, kKey);
    Hook(okay::Account::Get(), f);
}
static std::string B(bool v) { return v ? "1" : "0"; }
static std::string List(const std::vector<std::string>& v) {
    std::string s = "[";
    for (std::size_t i = 0; i < v.size(); ++i) s += (i ? "|" : "") + v[i];
    return s + "]";
}
static std::string Sessions(const std::vector<okay::GameSession>& v) {
    std::string s;
    for (const auto& g : v)
        s += "  session id=[" + g.id + "] name=[" + g.name + "] room=[" + g.room + "] addr=[" + g.hostAddr + ":" + std::to_string(g.port) +
             "] region=[" + g.region + "] players=" + std::to_string(g.players) + "/" + std::to_string(g.maxPlayers) + " game=[" + g.game +
             "] map=[" + g.map + "] pw=" + B(g.hasPassword) + " dedicated=" + B(g.dedicated) + "\n";
    return s;
}
static std::string Stats(const okay::PlayerStats& s) {
    return "  stats user=[" + s.userId + "] name=[" + s.Name() + "] avatar=[" + s.avatar + "] matches=" + std::to_string(s.matches) +
           " wins=" + std::to_string(s.wins) + " total=" + std::to_string(s.totalScore) + " best=" + std::to_string(s.bestScore) +
           " seconds=" + std::to_string(s.seconds) + " last=" + std::to_string(s.lastPlayed) + " rank=" + std::to_string(s.rank) + "\n";
}
template <class F> static bool Within(int ms, F done) {
    for (int i = 0; i < ms / 5; ++i) { if (done()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    return done();
}
static std::string MatchState(const okay::Matchmaker& mm) {
    const okay::MatchAssignment a = mm.Assignment();
    return "=> state=" + std::string(okay::Matchmaker::StateName(mm.GetState())) + " error=[" + mm.Error() + "] match=[" + a.id +
           "] server=[" + a.serverAddr + "] name=[" + a.serverName + "] room=[" + a.room + "] host=" + B(a.isHost) + " players=" +
           std::to_string(a.players) + "\n";
}
static const char* kTicket = "\"7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7\"";

// ---- The scenarios, run through the programs' public functions --------------------------------------------------------------
struct Scenario { const char* name; std::function<std::string(const fs::path& dir)> run; };

static std::vector<Scenario> Scenarios() {
    std::vector<Scenario> v;

    // AccountService: profiles (the username check), cloud_saves, leaderboards.
    v.push_back({"username_available", [](const fs::path& d) {
        Fake f; f.queue = {Http(200, "[]"), Http(200, "[{\"id\":\"u-1\"}]"), Http(500, "{}"), NoAnswer("connect: winhttp error 12029")};
        auto a = Make(d, f);   // signed out: the check needs no session (publishable key only)
        std::string out = "=> " + B(a->UsernameAvailable("alex")) + "\n";
        out += "=> " + B(a->UsernameAvailable("taken_name")) + "\n";
        out += "=> " + B(a->UsernameAvailable("al ex+\xc3\xa9/1&x=y")) + "\n";
        out += "=> " + B(a->UsernameAvailable("a.b-c~d")) + "\n";
        out += "=> " + B(a->UsernameAvailable("")) + "\n";
        return out + f.log;
    }});
    v.push_back({"cloud_save", [](const fs::path& d) {
        SaveSession(d, "alex", Tok(1), "r1");
        Fake f; f.queue = {Http(201, ""), Http(409, "{\"message\":\"conflict\"}"), NoAnswer("x")};
        auto a = Make(d, f);
        std::string out = "=> " + B(a->CloudSave("save1", "{\"lvl\":3,\"name\":\"a\\\\b\"}\nline2\t\r")) + "\n";
        out += "=> " + B(a->CloudSave("slot-2.bak", "x")) + "\n";
        out += "=> " + B(a->CloudSave("Save_3", "")) + "\n";
        out += "=> " + B(a->CloudSave("bad/key", "x")) + "\n";
        out += "=> " + B(a->CloudSave(std::string(65, 'k'), "x")) + "\n";
        return out + f.log;
    }});
    v.push_back({"cloud_load", [](const fs::path& d) {
        SaveSession(d, "alex", Tok(1), "r1");
        Fake f; f.queue = {Http(200, "[{\"data\":\"hello \\\"you\\\"\\n\"}]"), Http(200, "[]"), Http(404, "{}"), NoAnswer("x")};
        auto a = Make(d, f);
        std::string out = "=> [" + a->CloudLoad("save1") + "]\n";
        out += "=> [" + a->CloudLoad("save2") + "]\n";
        out += "=> [" + a->CloudLoad("a.b") + "]\n";
        out += "=> [" + a->CloudLoad("c-d") + "]\n";
        out += "=> [" + a->CloudLoad("e f") + "]\n";
        return out + f.log;
    }});
    v.push_back({"cloud_has_delete_list", [](const fs::path& d) {
        SaveSession(d, "alex", Tok(1), "r1");
        Fake f; f.queue = {Http(200, "[{\"key\":\"save1\"}]"), Http(200, "[]"), Http(500, "{}"), Http(204, ""), Http(404, "{}"),
                           Http(200, "[{\"key\":\"a\"},{\"key\":\"b.c\"}]"), Http(500, "{}")};
        auto a = Make(d, f);
        std::string out = "=> " + B(a->CloudHas("save1")) + "\n";
        out += "=> " + B(a->CloudHas("save2")) + "\n";
        out += "=> " + B(a->CloudHas("save3")) + "\n";
        out += "=> " + B(a->CloudDelete("save1")) + "\n";
        out += "=> " + B(a->CloudDelete("save9")) + "\n";
        out += "=> " + B(a->CloudDelete("")) + "\n";
        out += "=> " + List(a->CloudList()) + "\n";
        out += "=> " + List(a->CloudList()) + "\n";
        return out + f.log;
    }});
    v.push_back({"leaderboard_submit_top", [](const fs::path& d) {
        SaveSession(d, "al\"ex", Tok(1), "r1");
        Fake f; f.queue = {Http(201, ""), Http(400, "{}"), Http(200, "[{\"name\":\"alice\",\"score\":500},{\"name\":\"bob\",\"score\":300}]"),
                           Http(200, "[]"), Http(500, "{}")};
        auto a = Make(d, f);
        std::string out = "=> " + B(a->LeaderboardSubmit("arcade", 1250)) + "\n";
        out += "=> " + B(a->LeaderboardSubmit("arcade.hard", -5)) + "\n";
        out += "=> " + B(a->LeaderboardSubmit("bad board", 1)) + "\n";
        for (const auto& e : a->LeaderboardTop("arcade")) out += "  #" + std::to_string(e.rank) + " " + e.name + " " + std::to_string(e.score) + "\n";
        out += "=> " + std::to_string(a->LeaderboardTop("arcade", 3).size()) + "\n";
        out += "=> " + std::to_string(a->LeaderboardTop("arcade", -1).size()) + "\n";
        return out + f.log;
    }});
    v.push_back({"signed_out_sends_nothing", [](const fs::path& d) {
        Fake f;
        auto a = Make(d, f);
        std::string out = B(a->CloudSave("s", "x")) + "[" + a->CloudLoad("s") + "]" + B(a->CloudHas("s")) + B(a->CloudDelete("s")) +
                          List(a->CloudList()) + B(a->LeaderboardSubmit("b", 1)) + std::to_string(a->LeaderboardTop("b").size());
        okay::MatchResults mr(*a);
        okay::PlayerStats st;
        std::string err;
        const bool reported = mr.Report(okay::MatchReport{}, nullptr, &err);   // (before err is read)
        out += B(reported) + "[" + err + "]" + std::to_string(mr.Leaderboard("g", "wins").size()) +
               B(mr.MyStats("g", st)) + std::to_string(mr.RecentPlayers().size());
        return "=> " + out + "\n" + f.log;
    }});
    v.push_back({"newer_token_after_401", [](const fs::path& d) {
        SaveSession(d, "alex", Tok(1), "r1");
        Fake f; f.queue = {Http(401, "{}"), Http(200, "[{\"data\":\"d\"}]")};
        bool once = false;
        f.before = [&](const std::string&) { if (!once) { once = true; SaveSession(d, "alex", Tok(2), "r2"); } };
        auto a = Make(d, f);
        std::string out = "=> [" + a->CloudLoad("save1") + "]\n";
        return out + f.log;
    }});

    // Matchmaking: game_sessions, through the shared account.
    v.push_back({"mm_host_advertise", [](const fs::path& d) {
        Fake f; f.queue = {Http(201, "[{\"id\":\"s-1\",\"name\":\"x\"}]"), Http(500, "{}"),
                           Http(201, "[{\"id\":\"s-2\"}]"), Http(400, "{\"message\":\"column game does not exist\"}"), Http(201, "[{\"id\":\"s-3\"}]"),
                           Http(400, "{}"), Http(400, "{}")};
        UseShared(d, f, 3);
        std::string out = "=> [" + okay::Matchmaking::Host("Alex's \"Arena\"", "203.0.113.5", 7777, 8, "room-1", "eu") + "]\n";
        out += "=> [" + okay::Matchmaking::Host("Plain", "203.0.113.5", 7778) + "]\n";
        okay::GameSession s;
        s.name = "Duel \\ night"; s.hostAddr = "198.51.100.7"; s.port = 45000; s.maxPlayers = 4; s.players = 1; s.room = "r"; s.region = "us";
        s.game = "Arena Game"; s.map = "Dust"; s.hasPassword = true; s.dedicated = false;
        out += "=> [" + okay::Matchmaking::Advertise(s) + "]\n";
        out += "=> [" + okay::Matchmaking::Advertise(s) + "]\n";
        out += "=> [" + okay::Matchmaking::Advertise(s) + "]\n";
        okay::Account::Shutdown();
        return out + f.log;
    }});
    v.push_back({"mm_browse", [](const fs::path& d) {
        const std::string rows =
            "[{\"id\":\"s-1\",\"name\":\"One \\\"q\\\"\",\"room\":\"\",\"host_addr\":\"203.0.113.5\",\"port\":7777,\"players\":2,\"max_players\":8,"
            "\"region\":\"eu\",\"game\":\"Arena Game\",\"map\":\"Dust\",\"has_password\":true,\"dedicated\":false},"
            "{\"id\":\"s-2\",\"name\":\"No port\",\"room\":\"\",\"host_addr\":\"203.0.113.6\",\"port\":0,\"players\":0,\"max_players\":8,"
            "\"region\":null,\"game\":\"Arena Game\",\"map\":null,\"has_password\":false,\"dedicated\":true}]";
        Fake f; f.queue = {Http(200, rows), Http(400, "{}"), Http(200, "[{\"id\":\"s-9\",\"name\":\"Old\",\"room\":\"r\",\"host_addr\":\"h\",\"port\":1,"
                                                       "\"players\":1,\"max_players\":2,\"region\":\"\"}]"),
                           Http(200, "[]"), NoAnswer("x")};
        f.rewriteUrl = [](const std::string& u) { return RewriteFresh(u, kFresh); };
        UseShared(d, f, 3);
        std::string out = Sessions(okay::Matchmaking::BrowseWith(okay::Account::Get(), "Arena Game"));
        out += Sessions(okay::Matchmaking::Browse());
        out += "=> " + std::to_string(okay::Matchmaking::Browse("A&B=c d").size()) + "\n";
        f.rewriteUrl = [](const std::string& u) { return RewriteFresh(u, 30); };
        out += "=> " + std::to_string(okay::Matchmaking::BrowseWith(okay::Account::Get(), "", 30).size()) + "\n";
        okay::Account::Shutdown();
        return out + f.log;
    }});
    v.push_back({"mm_heartbeat_unregister_list", [](const fs::path& d) {
        const std::string rows = "[{\"id\":\"s-1\",\"name\":\"One\",\"room\":\"r1\",\"host_addr\":\"203.0.113.5\",\"region\":\"eu\",\"port\":7777,"
                                 "\"players\":2,\"max_players\":8},{\"id\":\"s-2\",\"name\":\"Two\",\"room\":\"r1\",\"host_addr\":\"h2\","
                                 "\"region\":\"\",\"port\":1,\"players\":0,\"max_players\":4}]";
        Fake f; f.queue = {Http(204, ""), Http(404, "{}"), Http(204, ""), Http(200, rows), Http(200, "[]"), Http(500, "{}")};
        UseShared(d, f, 3);
        std::string out = "=> " + B(okay::Matchmaking::Heartbeat("7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7", 3)) + "\n";
        out += "=> " + B(okay::Matchmaking::Heartbeat("s-2", -1)) + "\n";
        out += "=> " + B(okay::Matchmaking::Heartbeat("", 3)) + "\n";
        out += "=> " + B(okay::Matchmaking::Unregister("7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7")) + "\n";
        out += "=> " + B(okay::Matchmaking::Unregister("")) + "\n";
        out += Sessions(okay::Matchmaking::List());
        out += "=> " + std::to_string(okay::Matchmaking::List("room-1").size()) + "\n";
        out += "=> " + std::to_string(okay::Matchmaking::List("Lobby A").size()) + "\n";
        okay::Account::Shutdown();
        return out + f.log;
    }});

    // MatchResults: the four database functions.
    v.push_back({"mr_report", [](const fs::path& d) {
        SaveSession(d, "alex", Tok(4), "r4");
        Fake f; f.queue = {Http(200, "\"m-1\"\n"), Http(404, "{}"), Http(400, "{\"message\":\"Not a member of this match\"}"), NoAnswer("x")};
        auto a = Make(d, f);
        okay::MatchResults mr(*a);
        okay::MatchReport m;
        m.game = "Arena \"Game\""; m.map = "Dust\n2"; m.server = "EU 1"; m.mode = "duel"; m.dedicated = true; m.duration = 312.7f;
        okay::MatchPlayer p1; p1.userId = "u-1"; p1.name = "alex"; p1.score = 30; p1.place = 1; p1.won = true; p1.self = true; p1.seconds = 300.4f;
        okay::MatchPlayer p2; p2.name = "guest\\1"; p2.score = 10; p2.place = 2; p2.seconds = 120.0f;
        m.players = {p1, p2};
        std::string out;
        for (int i = 0; i < 4; ++i) {
            std::string id, err;
            out += "=> " + B(mr.Report(m, &id, &err)) + " id=[" + id + "] err=[" + err + "]\n";
        }
        return out + f.log;
    }});
    v.push_back({"mr_reads", [](const fs::path& d) {
        SaveSession(d, "alex", Tok(4), "r4");
        const std::string board = "[{\"user_id\":\"u-1\",\"username\":\"alex\",\"display_name\":\"Alex\",\"avatar_url\":\"https://a.example.test/1.png\","
                                  "\"matches\":12,\"wins\":7,\"total_score\":340,\"best_score\":60,\"seconds\":3600,\"last_played\":\"2026-11-20T10:00:00+00:00\"},"
                                  "{\"user_id\":\"u-2\",\"username\":\"bo\",\"display_name\":null,\"avatar_url\":null,\"matches\":3,\"wins\":0,"
                                  "\"total_score\":20,\"best_score\":9,\"seconds\":400,\"last_played\":null}]";
        const std::string mine = "[{\"user_id\":\"u-1\",\"username\":\"alex\",\"display_name\":\"\",\"matches\":12,\"wins\":7,\"total_score\":340,"
                                 "\"best_score\":60,\"seconds\":3600,\"last_played\":\"2026-11-20T10:00:00Z\",\"rank\":4}]";
        const std::string recent = "[{\"user_id\":\"u-2\",\"username\":\"bo\",\"display_name\":\"Bo\",\"avatar_url\":\"\",\"game\":\"Arena\","
                                   "\"last_played\":\"2026-11-21T08:30:00+00:00\",\"matches\":2},{\"user_id\":null,\"username\":\"x\"}]";
        Fake f; f.queue = {Http(200, board), Http(404, "{}"), Http(200, mine), Http(200, "[]"), Http(200, recent), Http(404, "{}")};
        auto a = Make(d, f);
        okay::MatchResults mr(*a);
        bool sup = false;
        std::string out;
        for (const auto& s : mr.Leaderboard("Arena \"Game\"", "wins", 5, &sup)) out += Stats(s);
        out += "supported=" + B(sup) + "\n";
        // (one call per statement: the operands of a chain of + on strings may be evaluated in any order)
        const std::size_t none = mr.Leaderboard("Arena", "time", 50, &sup).size();
        out += "=> " + std::to_string(none) + " supported=" + B(sup) + "\n";
        okay::PlayerStats st;
        const bool haveMine = mr.MyStats("Arena", st);
        out += "=> " + B(haveMine) + "\n" + Stats(st);
        const bool again = mr.MyStats("Arena\n2", st);
        out += "=> " + B(again) + "\n";
        for (const auto& p : mr.RecentPlayers(10, &sup))
            out += "  recent user=[" + p.userId + "] name=[" + p.Name() + "] game=[" + p.game + "] when=" + std::to_string(p.when) +
                   " matches=" + std::to_string(p.matches) + "\n";
        out += "supported=" + B(sup) + "\n";
        const std::size_t recent404 = mr.RecentPlayers(-3, &sup).size();
        out += "=> " + std::to_string(recent404) + " supported=" + B(sup) + "\n";
        return out + f.log;
    }});

    // Matchmaker's Supabase queue (the default backend), through the shared account, on its worker thread.
    okay::MatchRequest req; req.game = "Arena \"Game\""; req.mode = "duel"; req.region = "eu"; req.players = 4; req.minPlayers = 2; req.withParty = false;
    v.push_back({"mq_join_refused", [req](const fs::path& d) {
        Fake f; f.queue = {Http(404, "{}"), Http(400, "{\"message\":\"Only your party leader can start matchmaking.\"}"), NoAnswer("x"),
                           Http(200, "\"short\"")};
        UseShared(d, f, 5);
        std::string out;
        for (int i = 0; i < 4; ++i) {
            okay::Matchmaker mm;
            mm.pollInterval = 0.05f;
            out += "start=" + B(mm.Start(req)) + "\n";
            Within(3000, [&] { return mm.GetState() == okay::Matchmaker::State::Failed; });
            out += MatchState(mm);
        }
        okay::Account::Shutdown();
        return out + f.log;
    }});
    v.push_back({"mq_search_then_gone", [req](const fs::path& d) {
        Fake f; f.queue = {Http(200, kTicket), Http(200, "[{\"status\":\"searching\",\"waited\":1,\"searching\":3}]"),
                           Http(500, "{\"message\":\"temporary\"}"), Http(200, "[{\"status\":\"gone\"}]"), Http(204, "")};
        UseShared(d, f, 5);
        std::string out;
        {
            okay::Matchmaker mm;
            mm.pollInterval = 0.05f;
            out += "start=" + B(mm.Start(req)) + "\n";
            Within(5000, [&] { return mm.GetState() == okay::Matchmaker::State::Failed; });
            out += MatchState(mm);
        }   // (the destructor waits for the search thread: it has left the queue by now)
        okay::Account::Shutdown();
        return out + f.log;
    }});
    v.push_back({"mq_matched_server", [req](const fs::path& d) {
        Fake f; f.queue = {Http(200, kTicket),
                           Http(200, "[{\"status\":\"matched\",\"waited\":4,\"searching\":0,\"assignment\":\"as-1\",\"players\":4,"
                                     "\"server_addr\":\"203.0.113.9:7777\",\"server_name\":\"EU 1\",\"room\":\"match-1a2b\",\"is_host\":false}]"),
                           Http(204, "")};
        UseShared(d, f, 5);
        std::string out;
        {
            okay::Matchmaker mm;
            mm.pollInterval = 0.05f;
            out += "start=" + B(mm.Start(req)) + "\n";
            Within(5000, [&] { return mm.GetState() == okay::Matchmaker::State::Found; });
            out += MatchState(mm);
        }
        okay::Account::Shutdown();
        return out + f.log;
    }});
    v.push_back({"mq_host_posts_link", [req](const fs::path& d) {
        Fake f; f.queue = {Http(200, kTicket),
                           Http(200, "[{\"status\":\"matched\",\"assignment\":\"as-2\",\"players\":2,\"is_host\":true}]"),
                           Http(200, "true"), Http(204, "")};
        std::atomic<int> answered{0};   // (the search thread answers; this thread only waits for the count)
        f.before = [&answered](const std::string&) { ++answered; };
        UseShared(d, f, 5);
        std::string out;
        {
            okay::Scene hs("Host");
            auto* nh = hs.CreateGameObject("Net")->AddComponent<okay::NetworkManager>();
            hs.Start();
            okay::Matchmaker mm;
            mm.pollInterval = 0.05f; mm.hostPort = 45293; mm.hostPublicAddress = "127.0.0.1";
            out += "start=" + B(mm.Start(req)) + "\n";
            Within(5000, [&] { return mm.GetState() == okay::Matchmaker::State::Found; });
            const bool connected = mm.Connect(nh);
            out += "connect=" + B(connected) + " port=" + std::to_string(nh->ServerPort()) + "\n";
            out += MatchState(mm);
            Within(5000, [&] { return answered.load() >= 3; });   // joined, polled, posted the link
        }   // (the Matchmaker goes first: its destructor waits for the search thread, which leaves the queue)
        okay::Account::Shutdown();
        return out + f.log;
    }});
    return v;
}

// ---- The same requests through the Supabase provider directly (okay::backend::supabase::Database, RecordingTransport) ------
struct ProviderRun {
    okay::backend::RecordingTransport rec;
    Fake fake;
    std::string token = Tok(1);
    okay::backend::supabase::Database Db() { return okay::backend::supabase::Database(kUrl, kKey, rec, token); }
    ProviderRun() {
        rec.answer = [this](const okay::backend::Request& r) {
            return okay::backend::FromHttp(fake.Answer(r.method, r.url, r.headers, r.bodyFile.empty() ? r.body : std::string()));
        };
    }
};
using ProviderFlow = std::function<void(ProviderRun&)>;
static std::vector<std::pair<const char*, ProviderFlow>> ProviderFlows() {
    namespace ob = okay::backend;
    std::vector<std::pair<const char*, ProviderFlow>> v;
    v.push_back({"username_available", [](ProviderRun& p) {
        p.token.clear();   // the publishable key only
        for (const char* n : {"alex", "taken_name", "al ex+\xc3\xa9/1&x=y", "a.b-c~d"}) p.Db().From("profiles").Select("id").Eq("username", n).Get();
    }});
    v.push_back({"cloud_save", [](ProviderRun& p) {
        for (const auto& kv : std::vector<std::pair<std::string, std::string>>{{"save1", "{\"lvl\":3,\"name\":\"a\\\\b\"}\nline2\t\r"}, {"slot-2.bak", "x"}, {"Save_3", ""}})
            p.Db().From("cloud_saves").Returning(ob::Return::Minimal)
                .Upsert("[{\"key\":\"" + okay::account::detail::JsonEscape(kv.first) + "\",\"data\":\"" + okay::account::detail::JsonEscape(kv.second) + "\"}]");
    }});
    v.push_back({"cloud_load", [](ProviderRun& p) {
        for (const char* k : {"save1", "save2", "a.b", "c-d"}) p.Db().From("cloud_saves").Select("data").Eq("key", k).Get();
    }});
    v.push_back({"cloud_has_delete_list", [](ProviderRun& p) {
        for (const char* k : {"save1", "save2", "save3"}) p.Db().From("cloud_saves").Select("key").Eq("key", k).Get();
        for (const char* k : {"save1", "save9"}) p.Db().From("cloud_saves").Eq("key", k).Delete();
        for (int i = 0; i < 2; ++i) p.Db().From("cloud_saves").Select("key").Get();
    }});
    v.push_back({"leaderboard_submit_top", [](ProviderRun& p) {
        p.Db().From("leaderboards").Returning(ob::Return::Minimal).Upsert("[{\"board\":\"arcade\",\"name\":\"al\\\"ex\",\"score\":1250}]");
        p.Db().From("leaderboards").Returning(ob::Return::Minimal).Upsert("[{\"board\":\"arcade.hard\",\"name\":\"al\\\"ex\",\"score\":-5}]");
        for (int n : {10, 3, -1}) p.Db().From("leaderboards").Select("name,score").Eq("board", "arcade").Order("score", true).Limit(n).Get();
    }});
    v.push_back({"mr_report", [](ProviderRun& p) {
        p.token = Tok(4);
        okay::MatchReport m;
        m.game = "Arena \"Game\""; m.map = "Dust\n2"; m.server = "EU 1"; m.mode = "duel"; m.dedicated = true; m.duration = 312.7f;
        okay::MatchPlayer p1; p1.userId = "u-1"; p1.name = "alex"; p1.score = 30; p1.place = 1; p1.won = true; p1.self = true; p1.seconds = 300.4f;
        okay::MatchPlayer p2; p2.name = "guest\\1"; p2.score = 10; p2.place = 2; p2.seconds = 120.0f;
        m.players = {p1, p2};
        for (int i = 0; i < 4; ++i) p.Db().Rpc("report_match", "{\"p\":" + m.ToJson() + "}");
    }});
    v.push_back({"mm_heartbeat_unregister_list", [](ProviderRun& p) {
        p.token = Tok(3);
        p.Db().From("game_sessions").Eq("id", "7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7", ob::ValueText::AsIs).Update("{\"players\":3}");
        p.Db().From("game_sessions").Eq("id", "s-2", ob::ValueText::AsIs).Update("{\"players\":-1}");
        p.Db().From("game_sessions").Eq("id", "7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7", ob::ValueText::AsIs).Delete();
        for (const char* room : {"", "room-1", "Lobby A"}) {
            auto q = p.Db();
            ob::Query l = q.From("game_sessions").Select("id,name,room,host_addr,port,players,max_players,region").Order("updated_at", true);
            if (*room) l.Eq("room", room, ob::ValueText::AsIs);
            l.Get();
        }
    }});
    return v;
}

#include "test_okay_backend_golden_db.inc"   // GOLDEN: the texts recorded from the code before the move

int main() {
    RUN_SUITE("okay_backend_golden_db");
    const fs::path root = fs::temp_directory_path() / ("okay_backend_golden_db_" + acc::detail::RandomHex(8));
    const bool print = std::getenv("OKAY_GOLDEN_PRINT") != nullptr;
    const std::vector<Scenario> all = Scenarios();

    // 1. The programs' own functions send exactly the golden requests and make the same of the answers.
    if (print) std::cout << "// Recorded with OKAY_GOLDEN_PRINT=1 (tests/test_okay_backend_golden_db.cpp).\n"
                            "static const char* const kGolden[][2] = {\n";
    std::size_t matched = 0;
    for (const Scenario& s : all) {
        const std::string got = s.run(root / s.name);
        if (print) { std::cout << "    {\"" << s.name << "\", R\"GOLD(" << got << ")GOLD\"},\n"; continue; }
        const char* want = nullptr;
        for (const auto& g : kGolden) if (std::string(g[0]) == s.name) want = g[1];
        CHECK(want != nullptr);
        if (!want) continue;
        const bool same = got == want;
        if (!same) std::cout << "  MISMATCH in " << s.name << "\n--- golden\n" << want << "--- now\n" << got << "\n";
        CHECK(same);
        matched += same;
    }
    if (print) { std::cout << "};\n"; return 0; }
    CHECK(matched == all.size());
    CHECK(all.size() == sizeof(kGolden) / sizeof(kGolden[0]));
    CHECK(g_badFresh == 0);   // every browse asked for rows newer than now - its window

    // 2. The Supabase provider called directly sends the same bytes.
    std::size_t sameRequests = 0;
    const auto flows = ProviderFlows();
    for (const auto& flow : flows) {
        const char* want = nullptr;
        for (const auto& g : kGolden) if (std::string(g[0]) == flow.first) want = g[1];
        CHECK(want != nullptr);
        if (!want) continue;
        ProviderRun p;
        flow.second(p);
        const bool same = p.fake.log == RequestsOf(want);
        if (!same) std::cout << "  PROVIDER MISMATCH in " << flow.first << "\n--- golden requests\n" << RequestsOf(want) << "--- provider\n" << p.fake.log << "\n";
        CHECK(same);
        sameRequests += same;
        CHECK(!p.rec.sent.empty());
    }
    CHECK(sameRequests == flows.size());
    CHECK(flows.size() >= 7);

    std::error_code ec;
    fs::remove_all(root, ec);
    TEST_MAIN_RESULT();
}
