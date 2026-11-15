// The visual-script nodes Fn.net_pin_host and Fn.net_join_link, run AS NODES in a VisualScriptComponent graph against
// a loopback server with a host identity: a well-formed pin of the right key then a join by an okay://join link gives
// a sealed join; a malformed key returns 0 and leaves the pin unset; a wrong key never joins; text that is not a join
// link returns 0. (The pinning itself is covered by net_anticheat_ph3; this proves the nodes reach it.)
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Net/JoinLink.hpp"
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
    Client(const std::string& pin, const std::string& link) {
        net = scene.CreateGameObject("Net")->AddComponent<NetworkManager>();
        net->encryption = true;
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

    Scene ss("Server");
    auto* srv = ss.CreateGameObject("Net")->AddComponent<NetworkManager>();
    srv->encryption = true;
    CHECK(srv->SetHostIdentity(RandomKey()));
    const std::string hostPub = srv->HostIdentityPublicKey();
    CHECK(srv->StartServer(0));
    ss.Start();
    JoinLink l;
    l.game = "Test Game";
    l.addr = "127.0.0.1:" + std::to_string(srv->ServerPort());
    const std::string link = l.Format();
    std::printf("  link: %s\n", link.c_str());

    auto pump = [&](Client& c, int frames, bool untilJoined) {
        for (int i = 0; i < frames; ++i) {
            ss.Update(0.02f); c.scene.Update(0.02f); Nap();
            if (untilJoined && c.net->Joined()) break;
        }
    };

    {   // the right key: pinned, joined by the link, sealed
        Client c(hostPub, link);
        pump(c, 300, true);
        CHECK(c.Var("pinOk") == 1.0f);
        CHECK(c.Var("joinOk") == 1.0f);
        CHECK(c.net->pinnedHostKey == hostPub);
        CHECK(c.net->Joined());
        CHECK(c.net->SealedJoin());
    }
    {   // a malformed key: the node returns 0 and sets no pin (the join by link still runs, unpinned)
        Client c("not-a-key", link);
        pump(c, 300, true);
        CHECK(c.Var("pinOk") == 0.0f);
        CHECK(c.net->pinnedHostKey.empty());
        CHECK(c.Var("joinOk") == 1.0f);
        CHECK(c.net->Joined());
    }
    {   // a well-formed but wrong key: pinned, and the join never completes
        Client c(RandomKey(), link);
        pump(c, 200, true);
        CHECK(c.Var("pinOk") == 1.0f);
        CHECK(c.Var("joinOk") == 1.0f);                                    // the attempt starts...
        CHECK(!c.net->Joined());                                           // ...but the wrong host is refused
    }
    {   // not a join link
        Client c(hostPub, "https://example.invalid/join");
        pump(c, 5, false);
        CHECK(c.Var("joinOk") == 0.0f);
        CHECK(!c.net->Joined());
    }
    TEST_MAIN_RESULT();
}
