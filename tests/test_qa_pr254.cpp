// QA regression tests for PR #254 (script file names, relay limits).
// Each block reproduces a bug QA reported on the PR; they fail until it's fixed.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Net/RelayServer.hpp"
#include "okay/Net/RelayProtocol.hpp"
#include "okay/Net/Packet.hpp"
#include <filesystem>
#include <random>
#include <string>

using namespace okay;
using namespace okay::net;
namespace fs = std::filesystem;

namespace {
void Marker(const fs::path& p, const char* name) {
    fs::create_directories(p.parent_path());
    Scene m("m");
    m.CreateGameObject(name);
    SceneSerializer::SaveToFile(m, p.string());
}
// Run a script's start() in a scene for a few frames; true if `marker` got loaded in.
bool ScriptLoads(const std::string& src, const char* marker) {
    Scene s("t");
    auto* sc = s.CreateGameObject("o")->AddComponent<ScriptComponent>("okayscript");
    std::string err;
    if (!sc->LoadSource(src, &err)) return false;
    s.Start();
    for (int i = 0; i < 4; ++i) s.Update(1.0f / 60.0f);
    return s.Find(marker) != nullptr;
}
void RunActions(const std::vector<ActionList::Item>& items, Scene& s) {
    auto* al = s.CreateGameObject("o")->AddComponent<ActionList>();
    al->trigger = ActionList::Trigger::OnStart;
    al->instructions = items;
    s.Start();
    for (int i = 0; i < 4; ++i) s.Update(1.0f / 60.0f);
}
} // namespace

int main() {
    RUN_SUITE("qa_pr254");

    std::mt19937 rng(std::random_device{}());
    const fs::path root = fs::temp_directory_path() / ("okay_qa254_" + std::to_string(rng()));
    fs::create_directories(root / "game");
    const fs::path oldCwd = fs::current_path();
    fs::current_path(root / "game");
    Log::sink = [](Log::Level, const std::string&) {};
    Marker(root / "outside.okayscene", "OUTSIDE");

    // ---- Every scene loader refuses a path outside the game (load_scene already does) ----
    CHECK(!ScriptLoads(R"(func start() { load_scene("../outside.okayscene") })", "OUTSIDE"));
    CHECK(!ScriptLoads(R"(func start() { load_scene_name("../outside.okayscene") })", "OUTSIDE"));
    CHECK(!ScriptLoads("func start() { load_scene_name(\"" + (root / "outside.okayscene").generic_string() + "\") }", "OUTSIDE"));
    CHECK(!ScriptLoads(R"(func start() { SceneManager.LoadScene("../outside.okayscene") })", "OUTSIDE"));
    CHECK(!ScriptLoads(R"(func start() { load_scene_additive("../outside.okayscene") })", "OUTSIDE"));

    // ---- Action lists are game content too ----
    {
        Scene s("al");
        RunActions({{"set_prefs", {"k", "1"}}, {"save_prefs", {"../escaped.okayprefs"}},
                    {"save_prefs", {(root / "escaped_abs.okayprefs").string()}}}, s);
        CHECK(!fs::exists(root / "escaped.okayprefs"));        // written outside the game folder
        CHECK(!fs::exists(root / "escaped_abs.okayprefs"));
    }
    {
        Scene s("al2");
        RunActions({{"load_scene", {"../outside.okayscene"}}}, s);
        CHECK(s.Find("OUTSIDE") == nullptr);
    }

    fs::current_path(oldCwd);
    std::error_code ec;
    fs::remove_all(root, ec);

    // ---- One address can't lock everyone else out of the relay ----
    {
        RelayServer relay;
        CHECK(relay.Start(0));
        for (std::uint32_t i = 0; i < 5000; ++i) {   // one IP, many source ports, a new code each
            Packet p(RelayHello);
            p.Write(static_cast<std::uint8_t>(RelayHost));
            p.Write("S" + std::to_string(i));
            relay.HandleDatagram(Endpoint{0x0A000001u, static_cast<std::uint16_t>(1000 + i)}, p.Data(), p.Size());
        }
        Packet legit(RelayHello);
        legit.Write(static_cast<std::uint8_t>(RelayHost));
        legit.Write(std::string("REAL-GAME"));
        const Endpoint real{0xC0A80002u, 5000};
        relay.HandleDatagram(real, legit.Data(), legit.Size());
        CHECK(relay.SlotOf(real) != 0);                 // was refused: every session slot taken by 10.0.0.1
    }

    TEST_MAIN_RESULT();
}
