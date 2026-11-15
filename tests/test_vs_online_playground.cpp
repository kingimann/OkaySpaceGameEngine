// The Online demo of the Node Playground: examples/visual_script/playground/online.okayscene and online.okayvs. It is
// where a person tries the two nodes Net Join Link (Fn.net_join_link) and Net Pin Host (Fn.net_pin_host) (File > Examples >
// Visual Script Node Playground > Online; docs/try_it.md). This test plays the shipped files:
//   1. The scene loads from disk, its graph runs for three seconds with no visual-script error, and the first line on
//      screen says what the two nodes answer with no server: a made-up key is refused (0), an empty key clears the pin
//      (1), and the word hello is not a join link (0).
//   2. The same graph in two scenes of one process, the way two windows would run it: H hosts, J joins by link, and the
//      live line shows hosting, connected and players. (The graph says port 47777 so two windows find each other; the
//      test swaps in a free port so it never fights another program for 47777.)
//   3. The editor knows the demo (File > Examples > Visual Script Node Playground lists it, and the two nodes' Try it
//      button opens it) and the Windows packaging step copies the playground folder.
// Written for this engine.
#include "test_framework.hpp"
#include "test_sandbox.hpp"
#include <Okay.hpp>
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/VisualScript/NodeGraph.hpp"
#include "VsPlayground.hpp"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace okay;

namespace {
std::vector<std::string> g_errs;
void OnErr(const vs::VsRuntimeError& e) { g_errs.push_back(e.nodeType + " " + e.message); }

std::string Dir() { return std::string(OKAY_SOURCE_DIR) + "/examples/visual_script/playground"; }
std::string Slurp(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss; ss << in.rdbuf();
    return ss.str();
}
std::string ReplaceAll(std::string s, const std::string& from, const std::string& to) {
    for (std::size_t at = 0; (at = s.find(from, at)) != std::string::npos; at += to.size()) s.replace(at, from.size(), to);
    return s;
}
std::size_t Count(const std::string& s, const std::string& what) {
    std::size_t n = 0;
    for (std::size_t at = 0; (at = s.find(what, at)) != std::string::npos; at += what.size()) ++n;
    return n;
}
std::string TextOf(Scene& s, const char* name) {
    GameObject* g = s.Find(name);
    auto* t = g ? g->GetComponent<TextRenderer>() : nullptr;
    return t ? t->text : std::string("<no text on ") + name + ">";
}
void Nap() { std::this_thread::sleep_for(std::chrono::milliseconds(2)); }

// One window: a Net object, the three text lines and the Lobby with the demo graph (the graph text given).
struct Window {
    Scene scene;
    NetworkManager* net = nullptr;
    VisualScriptComponent* vc = nullptr;
    explicit Window(const std::string& graph) {
        net = scene.CreateGameObject("Net")->AddComponent<NetworkManager>();
        for (const char* n : {"Readout", "Status", "Live"}) scene.CreateGameObject(n)->AddComponent<TextRenderer>();
        vc = scene.CreateGameObject("Lobby")->AddComponent<VisualScriptComponent>();
        std::string err;
        CHECK(vc->LoadFromText(graph, &err));
        if (!err.empty()) std::printf("  graph: %s\n", err.c_str());
        scene.Start();
    }
};
void Key(Window& w, char k) {              // one frame with the key down, one with it up: a press
    Input::FeedKeys({k}); w.scene.Update(0.02f);
    Input::FeedKeys({});  w.scene.Update(0.02f);
}
} // namespace

int main() {
    RUN_SUITE("vs_online_playground");
    okaytest::Sandbox sandbox("vs_online_playground", OKAY_SOURCE_DIR);
    const std::string graphText = Slurp(Dir() + "/online.okayvs");
    CHECK(!graphText.empty());
    // The graph uses both nodes this demo is about.
    CHECK(graphText.find("Fn.net_join_link") != std::string::npos);
    CHECK(graphText.find("Fn.net_pin_host") != std::string::npos);

    // ---- 1. the shipped scene, played ----------------------------------------------------------------------------
    vs::SetRuntimeErrorHook(&OnErr);
    {
        Scene scene;
        std::string err;
        CHECK(SceneSerializer::LoadFromFile(scene, Dir() + "/online.okayscene", &err));
        if (!err.empty()) std::printf("  load: %s\n", err.c_str());
        CHECK(scene.Find("Readout") && scene.Find("Status") && scene.Find("Live") && scene.Find("Instructions"));
        CHECK(scene.Find("Net") && scene.Find("Net")->GetComponent<NetworkManager>());
        GameObject* lobby = scene.Find("Lobby");
        auto* vc = lobby ? lobby->GetComponent<VisualScriptComponent>() : nullptr;
        CHECK(vc && vc->Graph() && vc->Graph()->NodeCount() > 10);
        const std::string help = TextOf(scene, "Instructions");
        CHECK(help.find("Press Play") != std::string::npos);
        CHECK(help.find("47777") != std::string::npos && help.find("two windows") != std::string::npos);
        const auto* nm = scene.Find("Net")->GetComponent<NetworkManager>();
        CHECK(nm && nm->autoStart == NetworkManager::AutoStart::None);   // opening the scene must not host or join by itself
        g_errs.clear();
        scene.Start();
        for (int i = 0; i < 180; ++i) scene.Update(1.0f / 60.0f);
        for (const auto& e : g_errs) std::printf("  error: %s\n", e.c_str());
        CHECK(g_errs.empty());
        std::printf("  readout: %s\n", TextOf(scene, "Readout").c_str());
        std::printf("  live:    %s\n", TextOf(scene, "Live").c_str());
        CHECK(TextOf(scene, "Readout") == "Pin a made-up key: 0   Clear the pin: 1   Join hello: 0");
        CHECK(TextOf(scene, "Live") == "Hosting: 0   Connected: 0   Players: 0");
        CHECK(vc && vc->GetVariable("pinMadeUp").AsFloat() == 0.0f);
        CHECK(vc && vc->GetVariable("pinCleared").AsFloat() == 1.0f);
        CHECK(vc && vc->GetVariable("joinHello").AsFloat() == 0.0f);
        CHECK(!nm->IsServer() && !nm->Joined());                          // none of that touched the network

        // ---- 3. the editor's menu and the Windows packaging ----------------------------------------------------------
        using namespace okay::editor::vsed;
        const VsPlaygroundDemo* demo = VsPlaygroundById("online");
        CHECK(demo != nullptr);
        if (demo) {
            CHECK(std::string(demo->scene) == "online.okayscene" && std::string(demo->graph) == "online.okayvs");
            CHECK(std::filesystem::exists(Dir() + "/" + demo->scene) && std::filesystem::exists(Dir() + "/" + demo->graph));
            GameObject* focus = scene.Find(demo->focus);                   // the object the editor selects: it carries the graph
            auto* fvc = focus ? focus->GetComponent<VisualScriptComponent>() : nullptr;
            CHECK(fvc && std::filesystem::path(fvc->Path()).filename().string() == demo->graph);
        }
        for (const char* node : {"Fn.net_join_link", "Fn.net_pin_host"}) {
            const char* d = VsPlaygroundForNode(node);
            CHECK(d && std::strcmp(d, "online") == 0);                     // the node's Try it opens this demo
        }
        const std::string dist = Slurp(std::string(OKAY_SOURCE_DIR) + "/scripts/build-windows-dist.sh");
        CHECK(dist.find("examples/visual_script/playground") != std::string::npos);   // the zip and the installer carry it
    }

    // ---- 2. two windows of the same graph: H hosts, J joins by link -------------------------------------------------
    CHECK(Count(graphText, " 0 47777\n") == 1);                          // the host port, once
    CHECK(Count(graphText, "127.0.0.1:47777") == 2);                     // in the link and in its label
    const std::string hostGraph = ReplaceAll(graphText, " 0 47777\n", " 0 0\n");          // any free port
    Window host(hostGraph);
    Key(host, 'h');
    CHECK(host.net->IsServer());
    CHECK(host.vc->GetVariable("hostOk").AsFloat() == 1.0f);
    const int port = host.net->ServerPort();
    CHECK(port > 0);
    std::printf("  host on port %d: %s\n", port, TextOf(host.scene, "Status").c_str());
    CHECK(TextOf(host.scene, "Status") == "Host on UDP 47777: 1");       // (the label keeps the demo's own port)
    const std::string guestGraph = ReplaceAll(graphText, "127.0.0.1:47777\"", "127.0.0.1:" + std::to_string(port) + "\"");
    Window guest(guestGraph);
    Key(guest, 'j');
    CHECK(guest.vc->GetVariable("joinOk").AsFloat() == 1.0f);            // the link was understood, the join began
    for (int i = 0; i < 400 && !(guest.net->Joined() && host.vc->GetVariable("players").AsFloat() >= 1.0f); ++i) {
        host.scene.Update(0.02f); guest.scene.Update(0.02f); Nap();
    }
    for (int i = 0; i < 5; ++i) { host.scene.Update(0.02f); guest.scene.Update(0.02f); }
    CHECK(guest.net->Joined());
    CHECK(guest.net->pinnedHostKey.empty());                              // J clears the pin first
    std::printf("  host live:  %s\n", TextOf(host.scene, "Live").c_str());
    std::printf("  guest live: %s\n", TextOf(guest.scene, "Live").c_str());
    CHECK(TextOf(host.scene, "Live") == "Hosting: 1   Connected: 1   Players: 1");   // (Connected = in a session, host or guest)
    CHECK(TextOf(guest.scene, "Live") == "Hosting: 0   Connected: 1   Players: 1");
    CHECK(TextOf(guest.scene, "Status").find("Join 127.0.0.1:47777: 1") == 0);

    vs::SetRuntimeErrorHook(nullptr);
    CHECK(sandbox.Verify());   // nothing was written into the source tree
    TEST_MAIN_RESULT();
}
