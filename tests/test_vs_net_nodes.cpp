// The online visual-script nodes for hosting, joining and messaging, run AS NODES (VisualScriptComponent graphs) on
// loopback, unencrypted so it runs on every build: Fn.net_room, Fn.net_name, Fn.net_host, Fn.net_join,
// Fn.net_is_server, Fn.net_is_client, Fn.net_connected, Fn.net_id, Fn.net_peers, Fn.net_ping, Fn.net_send,
// Fn.net_poll, Fn.net_msg_data, Fn.net_msg_channel, Fn.net_msg_from, Fn.net_disconnect. The network suites cover the
// calls under them; this proves the nodes reach those calls with the right arguments and hand back the results.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/VisualScript/VsBuiltins.hpp"
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

using namespace okay;

namespace {
void Nap() { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }

// Builds graph text: exec chains of Fn nodes, each result stored with SetVar.
struct Graph {
    std::string text;
    int next = 1;
    int Node(const std::string& type) { const int id = next++; text += "node " + std::to_string(id) + " " + type + "\n"; return id; }
    void Default(int node, int pin, const std::string& value) { text += "default " + std::to_string(node) + " " + std::to_string(pin) + " " + value + "\n"; }
    void Exec(int from, int to) { text += "exec " + std::to_string(from) + " 0 " + std::to_string(to) + "\n"; }
    // Run `fn` after `prev`; when `var` is given, store its result in that variable. Returns the last node.
    int Call(int prev, const std::string& fn, std::initializer_list<std::string> args, const char* var) {
        const int f = Node("Fn." + fn);
        int pin = 0;
        for (const std::string& a : args) Default(f, pin++, a);
        Exec(prev, f);
        if (!var) return f;
        const int s = Node(std::string("SetVar ") + var);
        text += "data " + std::to_string(s) + " 0 " + std::to_string(f) + " 0\n";
        Exec(f, s);
        return s;
    }
    int Entry(const std::string& event) {
        const int e = Node(event.rfind("OnEvent:", 0) == 0 ? "OnEvent " + event.substr(8) : event);
        text += "entry " + event + " " + std::to_string(e) + "\n";
        return e;
    }
};

NetworkManager* NetOf(Scene& s) {
    for (const auto& up : s.Objects())
        if (up) if (auto* n = up->GetComponent<NetworkManager>()) return n;
    return nullptr;
}
} // namespace

int main() {
    RUN_SUITE("vs_net_nodes");
    for (const char* t : {"Fn.net_room", "Fn.net_name", "Fn.net_host", "Fn.net_join", "Fn.net_is_server", "Fn.net_is_client",
                          "Fn.net_connected", "Fn.net_id", "Fn.net_peers", "Fn.net_ping", "Fn.net_send", "Fn.net_poll",
                          "Fn.net_msg_data", "Fn.net_msg_channel", "Fn.net_msg_from", "Fn.net_disconnect"})
        CHECK(vs::IsBuiltinNode(t));

    // ---- the host: room, name, host on a free port; each frame its state and the last message -----------------
    Scene ss("Server");
    auto* sv = ss.CreateGameObject("Host")->AddComponent<VisualScriptComponent>();
    {
        Graph g;
        int n = g.Entry("OnStart");
        n = g.Call(n, "net_room", {"\"lobbyA\""}, nullptr);
        n = g.Call(n, "net_name", {"\"Host\""}, nullptr);
        n = g.Call(n, "net_host", {"0"}, "hostOk");
        n = g.Entry("OnUpdate");
        n = g.Call(n, "net_is_server", {}, "isServer");
        n = g.Call(n, "net_peers", {}, "peers");
        n = g.Call(n, "net_poll", {}, nullptr);
        n = g.Call(n, "net_msg_data", {}, "lastData");
        n = g.Call(n, "net_msg_channel", {}, "lastChannel");
        n = g.Call(n, "net_msg_from", {}, "lastFrom");
        n = g.Entry("OnEvent:Shout");
        g.Call(n, "net_send", {"\"news\"", "\"from host\""}, nullptr);
        std::string err;
        CHECK(sv->LoadFromText(g.text, &err));
        if (!err.empty()) std::printf("  host graph: %s\n", err.c_str());
    }
    ss.Start();
    ss.Update(0.02f);
    NetworkManager* hostNet = NetOf(ss);
    CHECK(sv->GetVariable("hostOk").AsFloat() == 1.0f);
    CHECK(hostNet != nullptr);
    if (!hostNet) TEST_MAIN_RESULT();
    CHECK(hostNet->IsServer());
    CHECK(hostNet->Room() == "lobbyA");
    CHECK(hostNet->LocalName() == "Host");
    const int port = hostNet->ServerPort();
    std::printf("  host on port %d\n", port);

    // ---- the guest: same room, a name, join; each frame its state; events to talk and to leave ------------------
    Scene cs("Client");
    auto* cv = cs.CreateGameObject("Guest")->AddComponent<VisualScriptComponent>();
    {
        Graph g;
        int n = g.Entry("OnStart");
        n = g.Call(n, "net_room", {"\"lobbyA\""}, nullptr);
        n = g.Call(n, "net_name", {"\"Guest\""}, nullptr);
        n = g.Call(n, "net_join", {"\"127.0.0.1\"", std::to_string(port)}, "joinOk");
        n = g.Entry("OnUpdate");
        n = g.Call(n, "net_connected", {}, "connected");
        n = g.Call(n, "net_is_client", {}, "isClient");
        n = g.Call(n, "net_id", {}, "myId");
        n = g.Call(n, "net_peers", {}, "peers");
        n = g.Call(n, "net_ping", {}, "ping");
        n = g.Call(n, "net_poll", {}, nullptr);
        n = g.Call(n, "net_msg_data", {}, "lastData");
        n = g.Entry("OnEvent:Talk");
        g.Call(n, "net_send", {"\"chat\"", "\"hello host\""}, nullptr);
        n = g.Entry("OnEvent:Leave");
        g.Call(n, "net_disconnect", {}, nullptr);
        std::string err;
        CHECK(cv->LoadFromText(g.text, &err));
        if (!err.empty()) std::printf("  guest graph: %s\n", err.c_str());
    }
    cs.Start();
    NetworkManager* guestNet = nullptr;
    auto frames = [&](int n) { for (int i = 0; i < n; ++i) { ss.Update(0.02f); cs.Update(0.02f); Nap(); } };
    // Waits are bounded by wall-clock time (5 s), not a frame count, so a loaded CI runner cannot fail them by being
    // slow; `frames` is the least number of frames to run (a few frames for every OnUpdate to read a new state).
    auto pump = [&](int frames, auto&& until) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        for (int i = 0;; ++i) {
            ss.Update(0.02f); cs.Update(0.02f); Nap();
            if (until()) break;
            if (i + 1 >= frames && std::chrono::steady_clock::now() >= deadline) break;
        }
    };
    pump(400, [&] { guestNet = NetOf(cs); return guestNet && guestNet->Joined() && sv->GetVariable("peers").AsFloat() >= 1.0f; });
    frames(10);   // a few frames so every OnUpdate has read the joined state
    CHECK(cv->GetVariable("joinOk").AsFloat() == 1.0f);
    CHECK(guestNet && guestNet->Joined());
    CHECK(cv->GetVariable("connected").AsFloat() == 1.0f);
    CHECK(cv->GetVariable("isClient").AsFloat() == 1.0f);
    CHECK(cv->GetVariable("myId").AsFloat() >= 1.0f);
    CHECK(cv->GetVariable("peers").AsFloat() >= 1.0f);
    CHECK(cv->GetVariable("ping").AsFloat() >= 0.0f);
    CHECK(sv->GetVariable("isServer").AsFloat() == 1.0f);
    CHECK(sv->GetVariable("peers").AsFloat() >= 1.0f);
    if (guestNet) {
        CHECK(guestNet->Room() == "lobbyA");
        CHECK(guestNet->LocalName() == "Guest");
        CHECK(hostNet->PeerName(guestNet->LocalId()) == "Guest");      // the name the node set reached the host
    }
    std::printf("  guest id %.0f, peers seen by host %.0f, guest ping %.1f ms\n", cv->GetVariable("myId").AsFloat(),
                sv->GetVariable("peers").AsFloat(), cv->GetVariable("ping").AsFloat());

    // ---- messages both ways through net_send / net_poll / net_msg_* ------------------------------------------------
    cv->Send("Talk");
    pump(200, [&] { return sv->GetVariable("lastData").AsString() == "hello host"; });
    CHECK(sv->GetVariable("lastData").AsString() == "hello host");
    CHECK(sv->GetVariable("lastChannel").AsString() == "chat");
    if (guestNet) CHECK(sv->GetVariable("lastFrom").AsFloat() == (float)guestNet->LocalId());
    sv->Send("Shout");
    pump(200, [&] { return cv->GetVariable("lastData").AsString() == "from host"; });
    CHECK(cv->GetVariable("lastData").AsString() == "from host");

    // ---- leave: net_disconnect stops the guest, and the host sees the peer go ---------------------------------------
    cv->Send("Leave");
    pump(400, [&] { return sv->GetVariable("peers").AsFloat() == 0.0f; });
    frames(3);
    CHECK(cv->GetVariable("connected").AsFloat() == 0.0f);
    CHECK(sv->GetVariable("peers").AsFloat() == 0.0f);
    TEST_MAIN_RESULT();
}
