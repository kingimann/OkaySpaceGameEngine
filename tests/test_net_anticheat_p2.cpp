#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/SessionTicket.hpp"

#include <chrono>
#include <ctime>
#include <thread>

using namespace okay;
using net::SessionTicket;
using net::SessionTicketClaims;

// Anti-cheat P2: signed session tickets (only the ticket service can mint them;
// the server checks them with a public key) and a transport that refuses
// replayed packets, with plaintext game traffic from joined clients dropped.
// Every key here is generated at test time and thrown away.
int main() {
    RUN_SUITE("net_anticheat_p2");
    if (!net::SecureChannel::Available()) {
        std::printf("  (built without libsodium: P2 needs it; only the refusal is checked)\n");
        SessionTicketClaims c; std::string err;
        CHECK(!SessionTicket::Verify("okt1.a.b", std::vector<std::uint8_t>(32), "", 0, c, &err));
        CHECK(err.find("libsodium") != std::string::npos);
        TEST_MAIN_RESULT();
    }
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    std::vector<std::uint8_t> pk, sk, otherPk, otherSk;
    CHECK(SessionTicket::GenerateKeyPair(pk, sk));
    CHECK(SessionTicket::GenerateKeyPair(otherPk, otherSk));
    const std::int64_t now = (std::int64_t)std::time(nullptr);
    auto ticket = [&](const std::string& sub, const std::string& aud, std::int64_t iat, std::int64_t exp,
                      const std::vector<std::uint8_t>& key) {
        SessionTicketClaims c; c.userId = sub; c.audience = aud; c.issuedAt = iat; c.expiresAt = exp;
        return SessionTicket::Sign(c, key);
    };

    // ---- Tickets ------------------------------------------------------------------
    {
        SessionTicketClaims c; std::string err;
        const std::string good = ticket("user-a", "arena-1", now, now + 600, sk);
        CHECK(good.rfind("okt1.", 0) == 0);
        CHECK(SessionTicket::Verify(good, pk, "arena-1", now, c, &err));
        CHECK(c.userId == "user-a" && c.audience == "arena-1" && c.expiresAt == now + 600);
        CHECK(SessionTicket::Verify(good, pk, "", now, c));                             // any server
        CHECK(SessionTicket::Verify(ticket("u", "*", now, now + 60, sk), pk, "arena-1", now, c));

        auto refused = [&](const std::string& t, const char* why, const std::vector<std::uint8_t>& key, const char* aud = "arena-1") {
            std::string e;
            const bool ok = SessionTicket::Verify(t, key, aud, now, c, &e);
            if (ok || e.find(why) == std::string::npos) std::printf("  expected '%s', got '%s'\n", why, ok ? "accepted" : e.c_str());
            return !ok && e.find(why) != std::string::npos;
        };
        CHECK(refused(good, "signature", otherPk));                                       // someone else's key
        CHECK(refused(ticket("user-a", "arena-1", now, now + 600, otherSk), "signature", pk));
        // Edit the payload (a different account) but keep the signature.
        std::string forged = good;
        {
            const std::size_t dot = good.find('.', 5);
            const std::string evil = "sub=admin&aud=arena-1&iat=" + std::to_string(now) + "&exp=" + std::to_string(now + 600);
            forged = "okt1." + SessionTicket::Base64UrlEncode(std::vector<std::uint8_t>(evil.begin(), evil.end())) + good.substr(dot);
        }
        CHECK(refused(forged, "signature", pk));
        CHECK(refused(ticket("user-a", "arena-1", now - 1000, now - 100, sk), "expired", pk));
        CHECK(refused(ticket("user-a", "arena-1", now + 1000, now + 2000, sk), "future", pk));
        CHECK(refused(ticket("user-a", "arena-2", now, now + 600, sk), "another server", pk));
        CHECK(refused(ticket("user-a", "arena-1", now, now + 7 * 86400, sk), "lifetime", pk));
        CHECK(refused(ticket("", "arena-1", now, now + 600, sk), "missing", pk));
        CHECK(refused("", "not a session ticket", pk));
        CHECK(refused("okt1.abc", "malformed", pk));
        CHECK(refused("okt1.a.b.c", "malformed", pk));
        CHECK(refused("okt1.!!!.AAAA", "malformed", pk));
        CHECK(refused(good, "no ticket key", std::vector<std::uint8_t>(5)));
        // base64url round trip, every length
        for (std::size_t n = 0; n < 40; ++n) {
            std::vector<std::uint8_t> b(n), back;
            for (std::size_t i = 0; i < n; ++i) b[i] = (std::uint8_t)(i * 37 + 11);
            CHECK(SessionTicket::Base64UrlDecode(SessionTicket::Base64UrlEncode(b), back) && back == b);
        }
        // .okayac takes the key as base64url and refuses anything else.
        AntiCheatSettings s; std::string e;
        CHECK(AntiCheatSettings::Parse("enabled true\nrequireEncryption true\nticketKey " + SessionTicket::Base64UrlEncode(pk) +
                                       "\nticketAudience arena-1\n", s, &e));
        CHECK(s.requireEncryption && s.ticketAudience == "arena-1");
        AntiCheatSettings back; CHECK(AntiCheatSettings::Parse(s.ToText(), back) && back.ticketKey == s.ticketKey);
        CHECK(!AntiCheatSettings::Parse("ticketKey abc\n", back, &e));
    }

    // ---- Tickets from the Edge function's signer (supabase/functions/session-ticket/ticket.ts) ----
    // Made once with that code and a throwaway key pair (only the public half is
    // here), so the TypeScript signer and this verifier must keep agreeing.
    {
        std::vector<std::uint8_t> tsPk;
        CHECK(SessionTicket::Base64UrlDecode("50hpvYHnDKFbOvGAFvdxFOKz5rCSusfPu2TPkdUlyPE", tsPk) && tsPk.size() == 32);
        const std::int64_t iat = 1800000000;
        SessionTicketClaims c; std::string err;
        CHECK(SessionTicket::Verify("okt1.c3ViPTZmMWMyYTllLTBiN2QtNGMzZS05YTUxLTJkOGU3ZjYwYjRhYSZhdWQ9YXJlbmEtMSZpYXQ9MTgwMDAwMDAwMCZleHA9MTgwMDAwMDYwMA.Pde7ZpalhJOfUNHT6v0jslN8svXZdqrBWZiKb69dk49Mm2CRmJHqa6uhyX29ulefD2PPcpWrf8a5UeAPwsrtDw", tsPk, "arena-1", iat + 5, c, &err));
        CHECK(c.userId == "6f1c2a9e-0b7d-4c3e-9a51-2d8e7f60b4aa" && c.audience == "arena-1" && c.expiresAt == iat + 600);
        CHECK(SessionTicket::Verify("okt1.c3ViPXVzZXItYiZhdWQ9KiZpYXQ9MTgwMDAwMDAwMCZleHA9MTgwMDAwMDA2MA.LAZeykukw34CuxXjIc6hGhvKl2TAl5PuJc-NWhTui5kzg6PtMmon_WzTU9vamXNg0Sbhan5jiCsSB6kUAwtjCQ", tsPk, "arena-9", iat, c, &err) && c.userId == "user-b");   // aud "*"
        CHECK(!SessionTicket::Verify("okt1.c3ViPTZmMWMyYTllLTBiN2QtNGMzZS05YTUxLTJkOGU3ZjYwYjRhYSZhdWQ9YXJlbmEtMSZpYXQ9MTgwMDAwMDAwMCZleHA9MTgwMDAwMDYwMA.Pde7ZpalhJOfUNHT6v0jslN8svXZdqrBWZiKb69dk49Mm2CRmJHqa6uhyX29ulefD2PPcpWrf8a5UeAPwsrtDw", tsPk, "arena-1", iat + 601 + SessionTicket::kClockSkew, c, &err));   // expired
        CHECK(!SessionTicket::Verify("okt1.c3ViPTZmMWMyYTllLTBiN2QtNGMzZS05YTUxLTJkOGU3ZjYwYjRhYSZhdWQ9YXJlbmEtMSZpYXQ9MTgwMDAwMDAwMCZleHA9MTgwMDAwMDYwMA.Pde7ZpalhJOfUNHT6v0jslN8svXZdqrBWZiKb69dk49Mm2CRmJHqa6uhyX29ulefD2PPcpWrf8a5UeAPwsrtDw", pk, "arena-1", iat + 5, c, &err));   // another key
        if (!err.empty() && err.find("signature") == std::string::npos) std::printf("  ts ticket: %s\n", err.c_str());
    }

    // ---- Replay window --------------------------------------------------------------
    {
        net::SecureChannel cli, srv;
        cli.GenerateKeys(); srv.GenerateKeys();
        CHECK(cli.Establish(srv.PublicKey(), false) && srv.Establish(cli.PublicKey(), true));
        cli.SetSequenced(true); srv.SetSequenced(true);
        auto seal = [&](const std::string& s) { std::vector<std::uint8_t> o; cli.Seal((const std::uint8_t*)s.data(), s.size(), o); return o; };
        auto open = [&](const std::vector<std::uint8_t>& b, std::string* text = nullptr) {
            std::vector<std::uint8_t> o;
            const bool ok = srv.Open(b.data(), b.size(), o);
            if (ok && text) *text = std::string(o.begin(), o.end());
            return ok;
        };
        const auto m1 = seal("one"), m2 = seal("two"), m3 = seal("three");
        std::string t;
        CHECK(open(m2, &t) && t == "two");
        CHECK(open(m1, &t) && t == "one");                         // late but new: fine
        CHECK(!open(m2));                                          // the same packet again: refused
        CHECK(open(m3, &t) && t == "three");
        CHECK(!open(m1) && !open(m3));
        CHECK(srv.Replays() == 3);
        std::vector<std::vector<std::uint8_t>> burst;
        for (int i = 0; i < 70; ++i) burst.push_back(seal("x"));
        CHECK(open(burst.back()));
        CHECK(!open(burst.front()));                               // 69 behind: too old to tell, refused
        CHECK(open(burst[30]));                                    // 39 behind, never seen: fine
        // Unsequenced (anti-cheat P2 off): exactly the old format both ways.
        net::SecureChannel a, b; a.GenerateKeys(); b.GenerateKeys();
        a.Establish(b.PublicKey(), false); b.Establish(a.PublicKey(), true);
        std::vector<std::uint8_t> sealed, plain;
        const std::string msg = "hello";
        a.Seal((const std::uint8_t*)msg.data(), msg.size(), sealed);
        CHECK(sealed.size() == 24 + msg.size() + 16);               // nonce + text + tag, no counter
        CHECK(b.Open(sealed.data(), sealed.size(), plain) && std::string(plain.begin(), plain.end()) == msg);
        CHECK(b.Open(sealed.data(), sealed.size(), plain));         // (no window when off)
    }

    // ---- A server that requires encryption and tickets --------------------------------
    {
        Scene ss("Server");
        auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->maxMessagesPerSecond = 0;
        srv->anticheat.settings.enabled = true;
        srv->anticheat.settings.requireEncryption = true;
        srv->anticheat.settings.ticketKey = SessionTicket::Base64UrlEncode(pk);
        srv->anticheat.settings.ticketAudience = "arena-1";
        int heard = 0;
        srv->SetMessageHandler([&](const NetworkManager::NetMessage& m) { if (m.channel == "hi") ++heard; });
        CHECK(srv->StartServer(0));
        ss.Start();

        struct Cli { Scene s{"C"}; NetworkManager* nm = nullptr; };
        auto join = [&](Cli& c, bool encrypt, const std::string& token) {
            c.nm = c.s.CreateGameObject("Net")->AddComponent<NetworkManager>();
            c.nm->encryption = encrypt;
            c.nm->SetAuthToken(token);
            c.nm->StartClient("127.0.0.1", srv->ServerPort());
            c.s.Start();
        };
        Cli good, plain, forged, expired;
        join(good, true, ticket("user-a", "arena-1", now, now + 600, sk));
        join(plain, false, ticket("user-b", "arena-1", now, now + 600, sk));
        join(forged, true, ticket("user-c", "arena-1", now, now + 600, otherSk));
        join(expired, true, ticket("user-d", "arena-1", now - 900, now - 300, sk));
        auto pump = [&](int n) {
            for (int i = 0; i < n; ++i) {
                ss.Update(0.02f);
                for (Cli* c : {&good, &plain, &forged, &expired}) c->s.Update(0.02f);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        };
        for (int i = 0; i < 300 && !(good.nm->Joined() && plain.nm->Joined() && forged.nm->JoinRejected() &&
                                     expired.nm->JoinRejected()); ++i) pump(1);
        CHECK(good.nm->Joined());
        CHECK(good.nm->Encrypted());
        CHECK(good.nm->ReplayProtected());
        CHECK(good.nm->LocalUserId() == "user-a");                 // bound to the ticket's account
        CHECK(srv->PeerUserId(good.nm->LocalId()) == "user-a");
        // No encryption: its plaintext Join is refused, so it turns encryption on and joins sealed.
        CHECK(plain.nm->Joined() && plain.nm->Encrypted() && plain.nm->encryption);
        CHECK(forged.nm->JoinRejected() && !forged.nm->Joined());   // signed by someone else
        CHECK(expired.nm->JoinRejected() && !expired.nm->Joined());
        // Sequenced, encrypted traffic still flows both ways.
        pump(10);
        for (int i = 0; i < 5; ++i) { good.nm->Send("hi", "there"); pump(2); }
        pump(10);
        CHECK(heard == 5);
        srv->SetVar("round", "3");
        pump(20);
        CHECK(good.nm->GetVar("round") == "3");
    }

    Log::sink = sink;
    TEST_MAIN_RESULT();
}
