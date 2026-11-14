#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/SessionTicket.hpp"
#include "TicketClient.hpp"
#include "TicketJoin.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#if !defined(_WIN32)
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

using namespace okay;
namespace tk = okayac::ticket;
namespace fs = std::filesystem;
using net::SessionTicket;
using net::SessionTicketClaims;

// The launcher / game side of the session-ticket flow (shared/TicketClient.hpp, shared/TicketJoin.hpp, the NetworkManager join gate):
//  A. the client logic with a scripted transport and a fake clock (retries, 401, banned, skew, expiry, cache, config);
//  B. the whole chain with a REAL local HTTP server standing in for the ticket service: AccountService -> curl -> the fake service,
//     a real ticket signed with a throwaway key, a real server NetworkManager that requires tickets and a client that joins through the gate.
// Every key and token here is made at test time and thrown away; nothing leaves 127.0.0.1.

namespace {

tk::Reply Ok(const std::string& ticket, long exp, long iat) {
    tk::Reply r; r.reached = true; r.status = 200;
    r.body = "{\"ticket\":\"" + ticket + "\",\"exp\":" + std::to_string(exp) + (iat ? ",\"iat\":" + std::to_string(iat) : std::string()) + "}";
    return r;
}
tk::Reply Status(long status, const std::string& body = "{}") { tk::Reply r; r.reached = true; r.status = status; r.body = body; return r; }
tk::Reply Down() { return tk::Reply(); }
const std::string kBanned = "{\"error\":\"banned\"}";

// A transport that plays back replies and counts calls.
struct Script {
    std::vector<tk::Reply> replies;
    std::size_t next = 0;
    int calls = 0;
    std::vector<std::string> bodies;
    tk::Transport Fn() {
        return [this](const okayac::backend::Request& r) {
            ++calls;
            bodies.push_back(r.body);
            return replies.empty() ? Down() : replies[std::min(next++, replies.size() - 1)];
        };
    }
};
struct Clock {
    long now = 1800000000;
    double mono = 0;
    std::vector<double> sleeps;
    tk::Options Opt() {
        tk::Options o;
        o.now = [this] { return now; };
        o.mono = [this] { return mono; };
        o.sleep = [this](double s) { sleeps.push_back(s); mono += s; };
        return o;
    }
};
const std::string kBind = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";   // 43 base64url characters

#if !defined(_WIN32)
// ---- a tiny HTTP server on 127.0.0.1 playing the ticket service ------------------------------------------------------------------
struct FakeService {
    std::mutex m;
    std::string expectToken = "tok-1";
    enum Mode { Normal, Banned, Error500, Slow, NoFunction } mode = Normal;
    int failFirst = 0;           // answer 500 this many times, then Normal
    long clockOffset = 0;        // the service's clock minus the real one
    int ttl = 600;
    std::string user = "acct-1";
    std::vector<unsigned char> sk;
    int hits = 0, unauthorized = 0;
    std::string lastBody, lastAuth;
    int port = 0, fd = -1;
    std::atomic<bool> stop{false};
    std::thread th;

    bool Start() {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return false;
        int one = 1; ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
        sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
        if (::bind(fd, (sockaddr*)&a, sizeof(a)) != 0 || ::listen(fd, 16) != 0) return false;
        socklen_t len = sizeof(a);
        ::getsockname(fd, (sockaddr*)&a, &len);
        port = ntohs(a.sin_port);
        th = std::thread([this] { Loop(); });
        return true;
    }
    void Shutdown() {
        stop = true;
        if (fd >= 0) { ::shutdown(fd, SHUT_RDWR); ::close(fd); fd = -1; }
        if (th.joinable()) th.join();
    }
    std::string Url() const { return "http://127.0.0.1:" + std::to_string(port); }
    static std::string Field(const std::string& body, const char* key) {   // "key":"value" from the one-line JSON the client sends
        const std::string k = std::string("\"") + key + "\":\"";
        const std::size_t p = body.find(k);
        if (p == std::string::npos) return {};
        const std::size_t e = body.find('"', p + k.size());
        return body.substr(p + k.size(), e - p - k.size());
    }
    void Loop() {
        while (!stop) {
            sockaddr_in ca{}; socklen_t cl = sizeof(ca);
            const int c = ::accept(fd, (sockaddr*)&ca, &cl);
            if (c < 0) break;
            timeval tv{2, 0}; ::setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            std::string req;
            char buf[4096];
            std::size_t headEnd = std::string::npos, need = 0;
            while (true) {
                const ssize_t n = ::recv(c, buf, sizeof(buf), 0);
                if (n <= 0) break;
                req.append(buf, (std::size_t)n);
                if (headEnd == std::string::npos && (headEnd = req.find("\r\n\r\n")) != std::string::npos) {
                    const std::size_t cl2 = req.find("Content-Length:");
                    const std::size_t cl3 = req.find("content-length:");
                    const std::size_t p = cl2 != std::string::npos ? cl2 : cl3;
                    need = headEnd + 4 + (p == std::string::npos ? 0 : (std::size_t)std::atol(req.c_str() + p + 15));
                }
                if (headEnd != std::string::npos && req.size() >= need) break;
            }
            std::string status = "200 OK", out;
            {
                std::lock_guard<std::mutex> lk(m);
                ++hits;
                const std::string body = headEnd == std::string::npos ? std::string() : req.substr(headEnd + 4);
                lastBody = body;
                std::string auth;
                for (const char* h : {"Authorization: Bearer ", "authorization: Bearer "})
                    if (const std::size_t p = req.find(h); p != std::string::npos) auth = req.substr(p + std::strlen(h), req.find("\r\n", p) - p - std::strlen(h));
                lastAuth = auth;
                if (req.rfind("POST /functions/v1/session-ticket", 0) != 0 || mode == NoFunction) { status = "404 Not Found"; out = "{\"error\":\"no function\"}"; }
                else if (auth != expectToken) { ++unauthorized; status = "401 Unauthorized"; out = "{\"error\":\"sign in first\"}"; }
                else if (mode == Banned) { status = "403 Forbidden"; out = "{\"error\":\"banned\"}"; }
                else if (mode == Error500 || failFirst > 0) { if (failFirst > 0) --failFirst; status = "500 Internal Server Error"; out = "{\"error\":\"boom\"}"; }
                else {
                    SessionTicketClaims cl4;
                    const long iat = (long)std::time(nullptr) + clockOffset;
                    cl4.userId = user; cl4.audience = Field(body, "aud"); cl4.issuedAt = iat; cl4.expiresAt = iat + ttl;
                    cl4.keyBinding = Field(body, "cnf");
                    out = "{\"ticket\":\"" + SessionTicket::Sign(cl4, sk) + "\",\"exp\":" + std::to_string(cl4.expiresAt) + ",\"iat\":" + std::to_string(iat) + "}";
                }
            }
            if (mode == Slow) std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            const std::string resp = "HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nContent-Length: " + std::to_string(out.size()) +
                                     "\r\nConnection: close\r\n\r\n" + out;
            ::send(c, resp.data(), resp.size(), MSG_NOSIGNAL);
            ::close(c);
        }
    }
};
#endif
}  // namespace

int main() {
    RUN_SUITE("ticket_client");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    // ---- A1. The request ----------------------------------------------------------------------------------------------------
    {
        okayac::backend::Request r; std::string err;
        CHECK(okayac::backend::SessionTicket("arena-1", "", kBind, r, &err));
        CHECK(r.method == "POST" && r.path == "/functions/v1/session-ticket");
        CHECK(r.body == "{\"aud\":\"arena-1\",\"cnf\":\"" + kBind + "\"}");
        const std::string hw(64, 'a');
        CHECK(okayac::backend::SessionTicket("203.0.113.7:45000", hw, "", r, &err) && r.body.find("\"hw_hash\":\"" + hw + "\"") != std::string::npos && r.body.find("cnf") == std::string::npos);
        for (const char* bad : {"", "a b", "arena\"1", "x=y", "a&b"}) CHECK(!okayac::backend::SessionTicket(bad, "", "", r, &err));
        CHECK(!okayac::backend::SessionTicket(std::string(65, 'a'), "", "", r, &err));
        CHECK(!okayac::backend::SessionTicket("arena-1", "", "short", r, &err));              // a bad binding is refused, not dropped
        CHECK(!okayac::backend::SessionTicket("arena-1", "nothex", "", r, &err));
        okayac::backend::TicketReply tr;
        CHECK(okayac::backend::ParseTicket(200, "{\"ticket\":\"okt1.a.b\",\"exp\":1800000600,\"iat\":1800000000}", tr) && tr.exp == 1800000600 && tr.iat == 1800000000);
        CHECK(okayac::backend::ParseTicket(200, "{\"ticket\":\"okt1.a.b\",\"exp\":5}", tr) && tr.iat == 0);   // an older service
        CHECK(!okayac::backend::ParseTicket(200, "{\"ticket\":\"nope\",\"exp\":5}", tr));
        CHECK(!okayac::backend::ParseTicket(403, kBanned, tr) && tr.banned);
    }

    // ---- A2. The client with a scripted service -----------------------------------------------------------------------------
    {   // Ok
        Clock c; Script s; s.replies = {Ok("okt1.t.s", c.now + 600, c.now)};
        tk::Client cl(s.Fn(), c.Opt());
        const auto r = cl.Acquire("arena-1", "", kBind);
        CHECK(r.outcome == tk::Outcome::Ok && r.ticket == "okt1.t.s" && r.attempts == 1 && r.skew == 0 && r.message.empty());
        CHECK(cl.Cached() == 0);   // a bound ticket is for one connection: never cached
        CHECK(cl.Acquire("arena-1", "", kBind).attempts == 1 && s.calls == 2);
    }
    {   // banned: final, no retry, never cached, the caller's hook fires through Decide
        Clock c; Script s; s.replies = {Status(403, kBanned), Ok("okt1.t.s", c.now + 600, c.now)};
        tk::Client cl(s.Fn(), c.Opt());
        const auto r = cl.Acquire("arena-1", "", "");
        CHECK(r.outcome == tk::Outcome::Banned && s.calls == 1 && r.ticket.empty() && r.message.find("anti-cheat ban") != std::string::npos);
        CHECK(c.sleeps.empty());
    }
    {   // 5xx then ok: retried with a growing wait
        Clock c; Script s; s.replies = {Status(503), Down(), Ok("okt1.t.s", c.now + 600, c.now)};
        tk::Client cl(s.Fn(), c.Opt());
        const auto r = cl.Acquire("arena-1", "", kBind);
        CHECK(r.outcome == tk::Outcome::Ok && r.attempts == 3 && s.calls == 3);
        CHECK(c.sleeps.size() == 2 && c.sleeps[1] > c.sleeps[0]);
    }
    {   // never answers: Unavailable after maxAttempts, bounded waits
        Clock c; Script s; s.replies = {Down()};
        tk::Client cl(s.Fn(), c.Opt());
        const auto r = cl.Acquire("arena-1", "", "");
        CHECK(r.outcome == tk::Outcome::Unavailable && r.attempts == 3 && s.calls == 3 && r.message.find("reach") != std::string::npos);
        double total = 0; for (double x : c.sleeps) total += x;
        CHECK(total < 12.0);
    }
    {   // the time budget stops the retries early
        Clock c; Script s; s.replies = {Status(500)};
        tk::Options o = c.Opt(); o.deadlineSeconds = 0.5; o.backoffSeconds = 0.4; o.maxAttempts = 6;
        tk::Client cl(s.Fn(), o);
        const auto r = cl.Acquire("arena-1", "", "");
        CHECK(r.outcome == tk::Outcome::Unavailable && s.calls <= 2);
    }
    {   // 429 is retried; a 400 / 404 are not
        Clock c; Script s; s.replies = {Status(429), Ok("okt1.t.s", c.now + 600, c.now)};
        tk::Client cl(s.Fn(), c.Opt());
        CHECK(cl.Acquire("arena-1", "", "").outcome == tk::Outcome::Ok && s.calls == 2);
        Script s2; s2.replies = {Status(400, "{\"error\":\"aud must be a game server id\"}")};
        tk::Client c2(s2.Fn(), c.Opt());
        CHECK(c2.Acquire("arena-1", "", "").outcome == tk::Outcome::Refused && s2.calls == 1);
        Script s3; s3.replies = {Status(404, "{\"code\":\"NOT_FOUND\"}")};
        tk::Client c3(s3.Fn(), c.Opt());
        const auto r3 = c3.Acquire("arena-1", "", "");
        CHECK(r3.outcome == tk::Outcome::NotConfigured && s3.calls == 1);
    }
    {   // 401: the sign-in is renewed once and the request repeated (not counted as an attempt); otherwise SignedOut
        Clock c; Script s; s.replies = {Status(401), Ok("okt1.t.s", c.now + 600, c.now)};
        int renewals = 0;
        tk::Options o = c.Opt(); o.refreshSignIn = [&] { ++renewals; return true; };
        tk::Client cl(s.Fn(), o);
        const auto r = cl.Acquire("arena-1", "", "");
        CHECK(r.outcome == tk::Outcome::Ok && renewals == 1 && r.signInRenewed && r.attempts == 1 && s.calls == 2);
        Script s2; s2.replies = {Status(401)};
        renewals = 0;
        tk::Client c2(s2.Fn(), o);
        CHECK(c2.Acquire("arena-1", "", "").outcome == tk::Outcome::SignedOut && renewals == 1 && s2.calls == 2);   // renewed, still refused
        Script s3; s3.replies = {Status(401)};
        tk::Options o3 = c.Opt(); o3.refreshSignIn = [] { return false; };
        tk::Client c3(s3.Fn(), o3);
        CHECK(c3.Acquire("arena-1", "", "").outcome == tk::Outcome::SignedOut && s3.calls == 1);
    }
    {   // clock skew: the player's clock is an hour behind the service; the ticket is judged on the service's time
        Clock c; Script s; const long svc = c.now + 3600;
        s.replies = {Ok("okt1.t.s", svc + 600, svc)};
        tk::Client cl(s.Fn(), c.Opt());
        const auto r = cl.Acquire("arena-1", "", "");
        CHECK(r.outcome == tk::Outcome::Ok && r.skew == 3600 && r.expLocal == c.now + 600);
        CHECK(r.message.find("behind") != std::string::npos);                                    // told to fix the clock, but joins
        CHECK(cl.Acquire("arena-1", "", "").fromCache && s.calls == 1);                           // unbound: cached on OUR clock
        c.now += 595;                                                                              // 5 s of life left: below minUseful
        CHECK(!cl.Acquire("arena-1", "", "").fromCache && s.calls == 2);
        // The player's clock ahead
        Clock c2; Script s2; s2.replies = {Ok("okt1.t.s", c2.now - 7200 + 600, c2.now - 7200)};
        tk::Client cl2(s2.Fn(), c2.Opt());
        const auto r2 = cl2.Acquire("arena-1", "", "");
        CHECK(r2.outcome == tk::Outcome::Ok && r2.skew == -7200 && r2.message.find("ahead") != std::string::npos);
    }
    {   // an old service (no iat) and a wrong local clock: the ticket looks expired -> refused with advice, never joined with
        Clock c; Script s; s.replies = {Ok("okt1.t.s", c.now - 100, 0)};
        tk::Client cl(s.Fn(), c.Opt());
        const auto r = cl.Acquire("arena-1", "", "");
        CHECK(r.outcome == tk::Outcome::Refused && r.ticket.empty() && r.message.find("clock") != std::string::npos);
        Script s2; s2.replies = {Ok("okt1.t.s", c.now + 3, c.now)};                               // issued with 3 s left
        tk::Client c2(s2.Fn(), c.Opt());
        CHECK(c2.Acquire("arena-1", "", "").outcome == tk::Outcome::Refused);
        Script s3; s3.replies = {Ok("garbage", c.now + 600, c.now)};
        tk::Client c3(s3.Fn(), c.Opt());
        CHECK(c3.Acquire("arena-1", "", "").outcome == tk::Outcome::Refused);
    }
    {   // the cache: an unbound ticket is reused; each server has its own; a ban answer is not cached; Forget drops them
        Clock c; Script s; s.replies = {Ok("okt1.one.s", c.now + 600, c.now), Ok("okt1.two.s", c.now + 600, c.now)};
        tk::Client cl(s.Fn(), c.Opt());
        CHECK(cl.Acquire("a", "", "").ticket == "okt1.one.s" && cl.Acquire("a", "", "").fromCache && s.calls == 1);
        CHECK(cl.Acquire("b", "", "").ticket == "okt1.two.s" && cl.Cached() == 2);
        cl.Forget();
        CHECK(cl.Cached() == 0);
    }
    {   // a request that can't be built never reaches the transport
        Clock c; Script s;
        tk::Client cl(s.Fn(), c.Opt());
        CHECK(cl.Acquire("bad id", "", "").outcome == tk::Outcome::Refused && s.calls == 0);
        CHECK(cl.Acquire("ok", "", "short").outcome == tk::Outcome::Refused && s.calls == 0);
    }

    // ---- A3. The decision: no ticket, no join (Required) ---------------------------------------------------------------------
    {
        Clock c; int banned = 0;
        tk::Hooks h; h.onBanned = [&] { ++banned; };
        tk::JoinTarget t; t.host = "203.0.113.7"; t.port = 45000;
        auto run = [&](tk::Mode m, std::vector<tk::Reply> replies, const std::string& cfgText = std::string(), bool signedIn = true) {
            Script s; s.replies = std::move(replies);
            tk::Client cl(s.Fn(), c.Opt());
            tk::Config cfg = tk::ParseConfig(cfgText); cfg.mode = m;
            tk::Hooks hk = h; hk.signedIn = [signedIn] { return signedIn; };
            return std::make_pair(tk::Decide(cfg, cl, hk, t, kBind), s.calls);
        };
        const tk::Reply good = Ok("okt1.t.s", c.now + 600, c.now);
        auto d = run(tk::Mode::Off, {Status(500)});
        CHECK(d.first.allow && !d.first.useTicket && d.second == 0);                              // off: nothing changes, nothing is asked
        d = run(tk::Mode::Required, {good});
        CHECK(d.first.allow && d.first.useTicket && d.first.ticket == "okt1.t.s");
        d = run(tk::Mode::Required, {Down()});
        CHECK(!d.first.allow && !d.first.message.empty() && d.first.outcome == tk::Outcome::Unavailable);   // required + service down: no join
        d = run(tk::Mode::Optional, {Down()});
        CHECK(d.first.allow && !d.first.useTicket);                                               // optional: try, then join as before
        d = run(tk::Mode::Required, {Status(403, kBanned)});
        CHECK(!d.first.allow && d.first.outcome == tk::Outcome::Banned && banned == 1);
        d = run(tk::Mode::Optional, {Status(403, kBanned)});
        CHECK(!d.first.allow && banned == 2);                                                     // a ban is never optional
        d = run(tk::Mode::Required, {good}, "", false);
        CHECK(!d.first.allow && d.first.outcome == tk::Outcome::SignedOut && d.second == 0);
        d = run(tk::Mode::Optional, {good}, "", false);
        CHECK(d.first.allow && !d.first.useTicket && d.second == 0);
        d = run(tk::Mode::Required, {good}, "server 203.0.113.7:45000 off\n");
        CHECK(d.first.allow && !d.first.useTicket && d.second == 0);                              // this server needs none
        d = run(tk::Mode::Required, {good}, "audience arena-1\n");
        CHECK(d.first.useTicket);
        // A server id that isn't a valid audience and no config: Required refuses (it can't ask), Optional goes ahead.
        tk::JoinTarget odd; odd.host = std::string(70, 'h'); odd.port = 1;
        { Script s; tk::Client cl(s.Fn(), c.Opt()); tk::Config cfg; cfg.mode = tk::Mode::Required;
          CHECK(!tk::Decide(cfg, cl, h, odd, "").allow && s.calls == 0);
          cfg.mode = tk::Mode::Optional; CHECK(tk::Decide(cfg, cl, h, odd, "").allow); }
    }

    // ---- A4. Config ---------------------------------------------------------------------------------------------------------
    {
        tk::Config c = tk::ParseConfig("# tickets\nmode required\naudience arena-1\nserver 10.0.0.5:45000 arena-2\nserver Relay:K7Q-29X off\n");
        CHECK(c.mode == tk::Mode::Required && c.audience == "arena-1" && !c.badLine);
        tk::JoinTarget a; a.host = "10.0.0.5"; a.port = 45000;
        CHECK(tk::AudienceFor(c, a) == "arena-2");
        tk::JoinTarget b; b.host = "x"; b.port = 1;
        CHECK(tk::AudienceFor(c, b) == "arena-1");
        tk::JoinTarget r; r.relayed = true; r.relayCode = "K7Q-29X";
        CHECK(tk::AudienceFor(c, r) == "off");
        CHECK(tk::ParseConfig("").mode == tk::Mode::Off);
        // A typo in the mode must not switch the gate off.
        tk::Config typo = tk::ParseConfig("mode requird\n");
        CHECK(typo.mode == tk::Mode::Required && typo.badLine);
        CHECK(tk::ParseConfig("audience bad id\n").badLine);
        tk::JoinTarget plain; plain.host = "203.0.113.7"; plain.port = 45000;
        CHECK(tk::AudienceFor(tk::Config(), plain) == "203.0.113.7:45000");                       // default: the address is the id
        CHECK(tk::LoadConfig("/nonexistent/okay_tickets.cfg").mode == tk::Mode::Off);
    }

    // ---- B. The whole chain: AccountService -> curl -> a local service -> NetworkManager.StartClient through the gate -----------
#if !defined(_WIN32)
    if (!net::SecureChannel::Available()) {
        std::printf("  (built without libsodium: the encrypted join isn't available; part B is skipped)\n");
    } else if (std::system("curl --version >/dev/null 2>&1") != 0) {
        std::printf("  (curl is not installed: part B is skipped)\n");
    } else {
        std::vector<std::uint8_t> pk, sk;
        CHECK(SessionTicket::GenerateKeyPair(pk, sk));
        FakeService svc;
        svc.sk = std::vector<unsigned char>(sk.begin(), sk.end());
        CHECK(svc.Start());

        const fs::path dir = fs::temp_directory_path() / ("okay_ticket_t_" + std::to_string((long)std::time(nullptr)) + "_" + std::to_string(std::rand()));
        std::error_code ec; fs::create_directories(dir, ec);
        auto writeSession = [&](const std::string& token) { std::ofstream(dir / "session.txt", std::ios::trunc) << "player\n" << token << "\nrefresh-1\n"; };
        writeSession("tok-1");
        okay::account::AccountService acct(dir, svc.Url(), "anon-key");
        CHECK(acct.IsLoggedIn() && acct.GetProvider() == okay::account::Provider::Supabase);

        auto transport = [&](int timeoutSec) -> tk::Transport {
            return [&acct, timeoutSec](const okayac::backend::Request& r) {
                const auto a = acct.ApiTimed(r.path, r.method, r.body, r.header, timeoutSec);
                tk::Reply out; out.reached = a.reached; out.status = a.status; out.body = a.body; return out;
            };
        };
        auto fast = [] { tk::Options o; o.backoffSeconds = 0.05; o.backoffMaxSeconds = 0.1; o.deadlineSeconds = 6; return o; };

        auto makeServer = [&](Scene& ss) {
            auto* s = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
            s->maxMessagesPerSecond = 0;
            s->anticheat.settings.enabled = true;
            s->anticheat.settings.requireEncryption = true;
            s->anticheat.settings.ticketKey = SessionTicket::Base64UrlEncode(pk);
            s->anticheat.settings.ticketAudience = "arena-1";
            return s;
        };
        struct Cli { Scene s{"C"}; NetworkManager* nm = nullptr; bool started = false; };
        auto join = [&](Cli& c, NetworkManager* server) {
            c.nm = c.s.CreateGameObject("Net")->AddComponent<NetworkManager>();
            c.nm->encryption = true;
            c.nm->SetAuthToken("account-access-token");   // what the game has before the gate: NOT a ticket
            c.started = c.nm->StartClient("127.0.0.1", server->ServerPort());
            c.s.Start();
        };
        auto pumpUntil = [&](Scene& server, Cli& c, const std::function<bool()>& done, int n = 300) {
            for (int i = 0; i < n && !done(); ++i) {
                server.Update(0.02f); c.s.Update(0.02f);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        };
        tk::Config cfg; cfg.mode = tk::Mode::Required; cfg.audience = "arena-1";
        int bannedHook = 0;
        tk::Hooks hooks; hooks.onBanned = [&] { ++bannedHook; };
        hooks.signedIn = [] { return true; };

        // B1. A good join: the gate asks the service (bound to this connection), the server verifies it and names the account.
        {
            tk::InstallJoinGate(cfg, hooks, transport(5), fast());
            Scene ss("Server"); auto* srv = makeServer(ss);
            CHECK(srv->StartServer(0)); ss.Start();
            Cli c; join(c, srv);
            CHECK(c.started);
            CHECK(tk::LooksLikeTicket(c.nm->AuthToken()));                                          // the ticket replaced the account token
            CHECK(svc.lastAuth == "tok-1" && svc.lastBody.find("\"aud\":\"arena-1\"") != std::string::npos && svc.lastBody.find("\"cnf\":\"") != std::string::npos);
            pumpUntil(ss, c, [&] { return c.nm->Joined() || c.nm->JoinRejected(); });
            CHECK(c.nm->Joined() && !c.nm->JoinRejected());
            CHECK(c.nm->LocalUserId() == "acct-1" && srv->PeerUserId(c.nm->LocalId()) == "acct-1");
        }
        // B2. A 401 from an expired access token: AccountService picks up the renewed token from the session file and retries.
        {
            { std::lock_guard<std::mutex> lk(svc.m); svc.expectToken = "tok-2"; }
            writeSession("tok-2");
            const int before = svc.unauthorized;
            Scene ss("Server"); auto* srv = makeServer(ss);
            CHECK(srv->StartServer(0)); ss.Start();
            Cli c; join(c, srv);
            CHECK(c.started && svc.unauthorized == before + 1 && svc.lastAuth == "tok-2");
            pumpUntil(ss, c, [&] { return c.nm->Joined() || c.nm->JoinRejected(); });
            CHECK(c.nm->Joined());
        }
        // B3. Banned: no join at all, the refusal says why, the hook fires (the launcher refreshes ac_my_bans there), the server sees nothing.
        {
            { std::lock_guard<std::mutex> lk(svc.m); svc.mode = FakeService::Banned; }
            Scene ss("Server"); auto* srv = makeServer(ss);
            CHECK(srv->StartServer(0)); ss.Start();
            Cli c; join(c, srv);
            CHECK(!c.started && bannedHook == 1);
            CHECK(NetworkManager::LastJoinRefusal().find("anti-cheat ban") != std::string::npos);
            CHECK(!c.nm->Joined() && !c.nm->IsClient());
            for (int i = 0; i < 40; ++i) { ss.Update(0.02f); c.s.Update(0.02f); std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
            CHECK(srv->PlayerCount() <= 1);   // just the server itself                                                          // no Join ever reached the server
        }
        // B4. The service is down (500 every time): Required = no join; Optional = a join without a ticket, which this server refuses.
        {
            { std::lock_guard<std::mutex> lk(svc.m); svc.mode = FakeService::Error500; }
            Scene ss("Server"); auto* srv = makeServer(ss);
            CHECK(srv->StartServer(0)); ss.Start();
            const int hitsBefore = svc.hits;
            Cli c; join(c, srv);
            CHECK(!c.started && svc.hits - hitsBefore == 3);                                         // three attempts, then it gives up
            CHECK(!c.nm->IsClient());
            tk::Config opt = cfg; opt.mode = tk::Mode::Optional;
            tk::InstallJoinGate(opt, hooks, transport(5), fast());
            Cli c2; join(c2, srv);
            CHECK(c2.started && !tk::LooksLikeTicket(c2.nm->AuthToken()));                         // no ticket: the account's own token
            pumpUntil(ss, c2, [&] { return c2.nm->Joined() || c2.nm->JoinRejected(); });
            CHECK(c2.nm->JoinRejected() && !c2.nm->Joined());                                        // the server requires tickets and says so
            tk::InstallJoinGate(cfg, hooks, transport(5), fast());
        }
        // B5. The service recovers after two failures: the retry inside one join gets the ticket.
        {
            { std::lock_guard<std::mutex> lk(svc.m); svc.mode = FakeService::Normal; svc.failFirst = 2; }
            Scene ss("Server"); auto* srv = makeServer(ss);
            CHECK(srv->StartServer(0)); ss.Start();
            Cli c; join(c, srv);
            CHECK(c.started);
            pumpUntil(ss, c, [&] { return c.nm->Joined() || c.nm->JoinRejected(); });
            CHECK(c.nm->Joined());
        }
        // B6. A service that stalls: each request is cut off by the timeout instead of hanging the join.
        {
            { std::lock_guard<std::mutex> lk(svc.m); svc.mode = FakeService::Slow; }
            tk::Options o = fast(); o.maxAttempts = 2;
            tk::InstallJoinGate(cfg, hooks, transport(1), o);
            Scene ss("Server"); auto* srv = makeServer(ss);
            CHECK(srv->StartServer(0)); ss.Start();
            const auto t0 = std::chrono::steady_clock::now();
            Cli c; join(c, srv);
            const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            CHECK(!c.started && took < 8.0);
            tk::InstallJoinGate(cfg, hooks, transport(5), fast());
            { std::lock_guard<std::mutex> lk(svc.m); svc.mode = FakeService::Normal; svc.failFirst = 0; }
        }
        // B7. The ticket service isn't deployed (404): Required refuses with that reason.
        {
            { std::lock_guard<std::mutex> lk(svc.m); svc.mode = FakeService::NoFunction; }
            Scene ss("Server"); auto* srv = makeServer(ss);
            CHECK(srv->StartServer(0)); ss.Start();
            Cli c; join(c, srv);
            CHECK(!c.started && NetworkManager::LastJoinRefusal().find("isn't set up") != std::string::npos);
            { std::lock_guard<std::mutex> lk(svc.m); svc.mode = FakeService::Normal; }
        }
        // B8. A ticket for another server: this server refuses it (the gate asked for the id the config says).
        {
            tk::Config other = cfg; other.audience = "arena-9";
            tk::InstallJoinGate(other, hooks, transport(5), fast());
            Scene ss("Server"); auto* srv = makeServer(ss);
            CHECK(srv->StartServer(0)); ss.Start();
            Cli c; join(c, srv);
            pumpUntil(ss, c, [&] { return c.nm->Joined() || c.nm->JoinRejected(); });
            CHECK(c.nm->JoinRejected() && c.nm->JoinRejectReason().find("another server") != std::string::npos);
            tk::InstallJoinGate(cfg, hooks, transport(5), fast());
        }
        // B9. A reconnect (StartClient again on the same component) asks for a NEW ticket: the old one was bound to the old key.
        {
            Scene ss("Server"); auto* srv = makeServer(ss);
            CHECK(srv->StartServer(0)); ss.Start();
            Cli c; join(c, srv);
            const std::string first = c.nm->AuthToken();
            const int hitsBefore = svc.hits;
            c.nm->Stop();
            CHECK(c.nm->StartClient("127.0.0.1", srv->ServerPort()));
            CHECK(svc.hits == hitsBefore + 1 && c.nm->AuthToken() != first && tk::LooksLikeTicket(c.nm->AuthToken()));
        }
        // B10. Gate off: no request, the account token is left as it was.
        {
            tk::Config off; off.mode = tk::Mode::Off;
            tk::InstallJoinGate(off, hooks, transport(5), fast());
            Scene ss("Server"); auto* srv = makeServer(ss);
            CHECK(srv->StartServer(0)); ss.Start();
            const int hitsBefore = svc.hits;
            Cli c; join(c, srv);
            CHECK(c.started && svc.hits == hitsBefore && c.nm->AuthToken() == "account-access-token");
        }
        NetworkManager::SetJoinGate(nullptr);
        svc.Shutdown();
        fs::remove_all(dir, ec);
    }
#endif
    // No gate installed (the default): StartClient behaves as before.
    {
        NetworkManager::SetJoinGate(nullptr);
        Scene s("S"); auto* nm = s.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(nm->StartClient("127.0.0.1", 45999));
        CHECK(NetworkManager::LastJoinRefusal().empty());
        nm->Stop();
    }
    Log::sink = sink;
    TEST_MAIN_RESULT();
}
