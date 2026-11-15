// The visual-script nodes Fn.net_pin_host and Fn.net_join_link, run AS NODES in a VisualScriptComponent graph against
// a loopback server with a host identity: a well-formed pin of the right key then a join by an okay://join link gives
// a sealed join; a malformed key returns 0 and leaves the pin unset; a wrong key never joins; text that is not a join
// link returns 0. (The pinning itself is covered by net_anticheat_ph3; this proves the nodes reach it.)
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Net/JoinLink.hpp"
#include "okay/Net/SecureChannel.hpp"
#include "okay/Net/SessionTicket.hpp"
#include "okay/VisualScript/VsBuiltins.hpp"
#include <chrono>
#include <cstdio>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace okay;
using net::SessionTicket;

namespace {
void Nap() { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
std::string RandomKey() {
    std::random_device rd;
    std::vector<std::uint8_t> s(32);
    for (auto& b : s) b = static_cast<std::uint8_t>(rd());
    return SessionTicket::Base64UrlEncode(s);
}
std::string Quote(const std::string& s) { return "\"" + s + "\""; }

// A client scene whose graph pins `pin` then joins `link` on start, storing both node results.
struct Client {
    Scene scene{"Client"};
    NetworkManager* net = nullptr;
    VisualScriptComponent* vc = nullptr;
    std::string err;
    Client(const std::string& pin, const std::string& link, bool encrypted) {
        net = scene.CreateGameObject("Net")->AddComponent<NetworkManager>();
        net->encryption = encrypted;
        GameObject* host = scene.CreateGameObject("Logic");
        vc = host->AddComponent<VisualScriptComponent>();
        const std::string graph =
            "node 1 OnStart\n"
            "entry OnStart 1\n"
            "node 2 Fn.net_pin_host\n"
            "default 2 0 " + Quote(pin) + "\n"
            "node 3 SetVar pinOk\n"
            "exec 1 0 2\n"
            "exec 2 0 3\n"
            "data 3 0 2 0\n"
            "node 4 Fn.net_join_link\n"
            "default 4 0 " + Quote(link) + "\n"
            "node 5 SetVar joinOk\n"
            "exec 3 0 4\n"
            "exec 4 0 5\n"
            "data 5 0 4 0\n";
        CHECK(vc->LoadFromText(graph, &err));
        if (!err.empty()) std::printf("  graph: %s\n", err.c_str());
        scene.Start();
    }
    float Var(const char* n) { return vc->GetVariable(n).AsFloat(); }
};
} // namespace

int main() {
    RUN_SUITE("vs_net_link_pin");
    CHECK(vs::IsBuiltinNode("Fn.net_pin_host"));
    CHECK(vs::IsBuiltinNode("Fn.net_join_link"));
    // Encrypted sessions need a crypto backend (libsodium, or okay::crypto when built in). Without one an encrypted
    // server does not host, so the pinned-join cases run only with crypto; the rest runs on any build.
    const bool crypto = net::SecureChannel::Available();

    struct Server {
        Scene scene{"Server"};
        NetworkManager* net = nullptr;
        std::string hostPub, link;
        explicit Server(bool encrypted) {
            net = scene.CreateGameObject("Net")->AddComponent<NetworkManager>();
            net->encryption = encrypted;
            if (encrypted) { CHECK(net->SetHostIdentity(RandomKey())); hostPub = net->HostIdentityPublicKey(); }
            CHECK(net->StartServer(0));
            scene.Start();
            JoinLink l;
            l.game = "Test Game";
            l.addr = "127.0.0.1:" + std::to_string(net->ServerPort());
            link = l.Format();
        }
    };
    auto pump = [](Server& sv, Client& c, int frames, bool untilJoined) {
        for (int i = 0; i < frames; ++i) {
            sv.scene.Update(0.02f); c.scene.Update(0.02f); Nap();
            if (untilJoined && c.net->Joined()) break;
        }
    };

    {   // any build: join by link (no encryption); the pin node's result for good, bad and empty keys
        Server sv(false);
        std::printf("  link: %s\n", sv.link.c_str());
        Client c("", sv.link, false);                                      // "" removes the pin: returns 1
        pump(sv, c, 300, true);
        CHECK(c.Var("pinOk") == 1.0f);
        CHECK(c.net->pinnedHostKey.empty());
        CHECK(c.Var("joinOk") == 1.0f);
        CHECK(c.net->Joined());
        Client bad("not-a-key", "okay://join?game=x", false);              // malformed key: 0, no pin; a link with no address: 0
        CHECK(bad.Var("pinOk") == 0.0f);
        CHECK(bad.net->pinnedHostKey.empty());
        CHECK(bad.Var("joinOk") == 0.0f);
        const std::string k = RandomKey();
        Client good(k, "https://example.invalid/join", false);            // well-formed key: 1, pinned; not a link: 0
        CHECK(good.Var("pinOk") == 1.0f);
        CHECK(good.net->pinnedHostKey == k);
        CHECK(good.Var("joinOk") == 0.0f);
        CHECK(!good.net->Joined());
    }

    if (!crypto) {
        std::printf("SKIP the pinned, encrypted join cases: this build has no crypto backend\n");
        TEST_MAIN_RESULT();
    }
    {   // the right key: pinned, joined by the link, sealed
        Server sv(true);
        Client c(sv.hostPub, sv.link, true);
        pump(sv, c, 300, true);
        CHECK(c.Var("pinOk") == 1.0f);
        CHECK(c.Var("joinOk") == 1.0f);
        CHECK(c.net->pinnedHostKey == sv.hostPub);
        CHECK(c.net->Joined());
        CHECK(c.net->SealedJoin());
    }
    {   // a well-formed but wrong key: pinned, and the join never completes
        Server sv(true);
        Client c(RandomKey(), sv.link, true);
        pump(sv, c, 200, true);
        CHECK(c.Var("pinOk") == 1.0f);
        CHECK(c.Var("joinOk") == 1.0f);                                    // the attempt starts...
        CHECK(!c.net->Joined());                                           // ...but the wrong host is refused
    }
    TEST_MAIN_RESULT();
}
