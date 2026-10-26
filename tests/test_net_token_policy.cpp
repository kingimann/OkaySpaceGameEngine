#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/Packet.hpp"
#include "okay/Net/UdpSocket.hpp"
#include "okay/Net/ServerQuery.hpp"

#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace okay;
using namespace okay::net;

// A signed-in player's account token goes only to a host that checks sign-ins
// (the server browser's "auth" flag). Any other host, including one that
// doesn't say, gets a Join without it.
namespace {
void Nap() { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
enum : std::uint8_t { kJoin = 1, kWelcome = 2, kQuery = 17, kQueryInfo = 18 };

// A raw host: answers the query (with `flags`, or not at all) and records the
// token in each Join it gets.
struct RawHost {
    UdpSocket sock;
    int answerFlags;   // -1 = never answer the query
    int queries = 0, joins = 0;
    std::string token = "<none>";
    explicit RawHost(int flags) : answerFlags(flags) { sock.Open(); sock.Bind(0); }
    void Serve() {
        std::uint8_t buf[4096]; Endpoint from; int n;
        while ((n = sock.RecvFrom(buf, sizeof buf, from)) > 0) {
            Packet p(buf, static_cast<std::size_t>(n));
            const std::uint8_t t = p.ReadU8();
            if (t == kQuery) {
                ++queries;
                const std::string magic = p.ReadString();
                const std::uint32_t nonce = p.ReadU32();
                if (answerFlags < 0 || n < (int)kQueryMinSize || magic != kQueryMagic) continue;
                Packet q(kQueryInfo);
                q.Write(std::string(kQueryMagic)); q.Write(nonce); q.Write(std::uint8_t(kQueryProtocol));
                q.Write(std::string("raw")); q.Write(std::string()); q.Write(std::string());
                q.Write(std::uint32_t(0)); q.Write(std::uint32_t(8)); q.Write(std::uint8_t(answerFlags));
                sock.SendTo(from, q.Data(), q.Size());
            } else if (t == kJoin) {
                ++joins;
                p.ReadString(); p.ReadString(); p.ReadString();
                token = p.ReadString();
                Packet w(kWelcome); w.Write(std::uint32_t(4)); w.Write(std::string("raw"));
                w.Write(std::string()); w.Write(std::string());
                sock.SendTo(from, w.Data(), w.Size());
            }
        }
    }
};

// Join `host` with a token; returns whether the client joined.
bool Join(RawHost& host, bool anyHost) {
    Scene cs("Client");
    auto* cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
    cli->SetAuthToken("tok-ACCOUNT-1");
    cli->authTokenToAnyHost = anyHost;
    cli->StartClient("127.0.0.1", host.sock.LocalPort());
    cs.Start();
    for (int i = 0; i < 200 && !cli->Joined(); ++i) { host.Serve(); cs.Update(0.02f); Nap(); }
    const bool joined = cli->Joined();
    cli->Stop();
    return joined;
}
} // namespace

int main() {
    RUN_SUITE("net_token_policy");
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};

    {   // A host that doesn't check sign-ins: no token.
        RawHost h(0);
        CHECK(Join(h, false));
        CHECK(h.queries >= 1 && h.joins >= 1);
        CHECK(h.token.empty());
    }
    {   // A host that does: the token.
        RawHost h(kQueryAuth);
        CHECK(Join(h, false));
        CHECK(h.token == "tok-ACCOUNT-1");
    }
    {   // A host that doesn't answer (an older one): asked 4 times, no token.
        RawHost h(-1);
        CHECK(Join(h, false));
        CHECK(h.queries == 4);
        CHECK(h.token.empty());
    }
    {   // authTokenToAnyHost: the old behaviour, no question.
        RawHost h(0);
        CHECK(Join(h, true));
        CHECK(h.queries == 0 && h.token == "tok-ACCOUNT-1");
    }
    {   // No token to give: no question either.
        RawHost h(-1);
        Scene cs("C");
        auto* cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        cli->StartClient("127.0.0.1", h.sock.LocalPort());
        cs.Start();
        for (int i = 0; i < 100 && !cli->Joined(); ++i) { h.Serve(); cs.Update(0.02f); Nap(); }
        CHECK(cli->Joined() && h.queries == 0);
    }
    {   // A real server that verifies sign-ins advertises it and gets the token.
        Scene ss("Server");
        auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
        std::string seen;
        srv->SetTokenVerifier([&](const std::string& t, std::string& uid) { seen = t; uid = "u"; return true; });
        CHECK(srv->StartServer(0));
        ss.Start();
        Scene cs("C");
        auto* cli = cs.CreateGameObject("Net")->AddComponent<NetworkManager>();
        cli->SetAuthToken("tok-ACCOUNT-2");
        cli->StartClient("127.0.0.1", srv->ServerPort());
        cs.Start();
        for (int i = 0; i < 200 && !cli->Joined(); ++i) { ss.Update(0.02f); cs.Update(0.02f); Nap(); }
        CHECK(cli->Joined() && cli->HostWantsAuth() && seen == "tok-ACCOUNT-2");
        // And one that doesn't verify never sees it.
        Scene s2("Plain");
        auto* plain = s2.CreateGameObject("Net")->AddComponent<NetworkManager>();
        CHECK(plain->StartServer(0));
        s2.Start();
        auto* c2 = cs.CreateGameObject("Net2")->AddComponent<NetworkManager>();
        c2->SetAuthToken("tok-ACCOUNT-3");
        c2->StartClient("127.0.0.1", plain->ServerPort());
        for (int i = 0; i < 200 && !c2->Joined(); ++i) { s2.Update(0.02f); cs.Update(0.02f); Nap(); }
        CHECK(c2->Joined() && !c2->HostWantsAuth());
        CHECK(plain->PeerUserId(c2->LocalId()).empty());
        srv->Stop(); plain->Stop();
    }

    Log::sink = sink;
    TEST_MAIN_RESULT();
}
