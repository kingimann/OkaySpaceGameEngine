#include "test_framework.hpp"
#include <Okay.hpp>
#include "../editor/AntiCheatSetup.hpp"
#include "../editor/ProjectState.hpp"
#include "okay/Core/BuildPipeline.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

// Easy anti-cheat setup (editor/AntiCheatSetup.hpp): presets, the "Add Anti-Cheat" action, finding a
// project's networked prefabs, the checklist and its fixes, and the round trip of the chosen settings
// through the scene file, the .okayac file, Build Settings and the build's dependency walk.
// Temp folders only; no editor, no network.
namespace {
using namespace okay;
namespace as = okay::acsetup;
namespace fs = std::filesystem;

void Write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << text;
}
bool Has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
fs::path TempDir(const char* name) {
    fs::path d = fs::temp_directory_path() / (std::string("okay_acsetup_") + name);
    std::error_code ec; fs::remove_all(d, ec); fs::create_directories(d);
    return d;
}

// ---- (1) presets and "Add Anti-Cheat" ------------------------------------------------------------
void TestPresets() {
    RUN_SUITE("anti-cheat presets");
    const as::PrefabList prefabs = {"prefabs/crate.okayprefab", "prefabs/ball.okayprefab"};
    for (int i = 0; i < as::kPresetCount; ++i) {
        const as::Preset p = static_cast<as::Preset>(i);
        AntiCheatSettings s;
        as::ApplyPreset(s, p, prefabs, true);
        CHECK(s.enabled && s.ownerOnlySyncVars && s.requireEncryption);
        CHECK(s.spawnAllowlist == prefabs);
        CHECK(s.maxSyncSpeed > 0.0f && s.maxSyncVarsPerSecond > 0 && s.maxRpcsPerSecond > 0 && s.kickAfterViolations > 0);
        CHECK(!s.serverVars.empty());
        // The detection engine is on and only watching: no kick, ban request, shadow or rate limit.
        CHECK(s.detect.enabled && detectui::IsObserveMode(s.detect));
        CHECK(s.detect.kickAt == 0.0f && s.detect.banAt == 0.0f && s.detect.shadowAt == 0.0f && s.detect.rateLimitAt == 0.0f);
        // The text a preset makes is a valid .okayac file that says the same.
        AntiCheatSettings back; std::string err;
        CHECK(AntiCheatSettings::Parse(s.ToText(), back, &err));
        CHECK(back.enabled && back.spawnAllowlist == prefabs && back.maxSyncSpeed == s.maxSyncSpeed && back.serverVars == s.serverVars);
        CHECK(back.detect.enabled && detectui::IsObserveMode(back.detect));
        CHECK(std::string(as::Info(p).name).size() > 3 && std::string(as::Info(p).blurb).size() > 10 && std::string(as::Info(p).tip).size() > 10);
    }
    AntiCheatSettings casual, comp, econ;
    as::ApplyPreset(casual, as::Preset::CasualCoop, {}, true);
    as::ApplyPreset(comp, as::Preset::Competitive, {}, true);
    as::ApplyPreset(econ, as::Preset::Economy, {}, true);
    CHECK(comp.maxSyncSpeed < casual.maxSyncSpeed && comp.kickAfterViolations < casual.kickAfterViolations && comp.maxClockDrift > 0.0f && casual.maxClockDrift == 0.0f);
    CHECK(std::find(econ.serverVars.begin(), econ.serverVars.end(), "money") != econ.serverVars.end());
    CHECK(std::find(casual.serverVars.begin(), casual.serverVars.end(), "money") == casual.serverVars.end());
    CHECK(casual.spawnAllowlist.empty());   // no networked prefabs: clients may spawn nothing

    // No encryption library: the preset does not ask for what the build cannot do.
    AntiCheatSettings noSodium;
    as::ApplyPreset(noSodium, as::Preset::CasualCoop, {}, false);
    CHECK(!noSodium.requireEncryption && noSodium.enabled);

    // Picking another preset keeps what is the game maker's own: the ticket check and the weapons.
    AntiCheatSettings mine;
    mine.ticketKey = "k"; mine.ticketAudience = "arena-1"; mine.ticketAnyAudience = true; mine.weapons = {"rifle"}; mine.clientVars = {"player."};
    as::ApplyPreset(mine, as::Preset::Competitive, {}, true);
    CHECK(mine.ticketKey == "k" && mine.ticketAudience == "arena-1" && mine.ticketAnyAudience && mine.weapons.size() == 1 && mine.clientVars.size() == 1);
    // Applying a preset twice gives the same settings.
    AntiCheatSettings twice = mine;
    as::ApplyPreset(twice, as::Preset::Competitive, {}, true);
    CHECK(twice.ToText() == mine.ToText());
}

void TestNetworkedPrefabs() {
    RUN_SUITE("networked prefabs of a project");
    CHECK(as::PrefabTextIsNetworked("gameobject Crate\n  netsync 0 0 0 \"crate\"\n"));
    CHECK(as::PrefabTextIsNetworked("  netsync 2 1 1 \"\"\r\n"));
    CHECK(!as::PrefabTextIsNetworked("gameobject Crate\n  name netsyncfoo\n  netsyncx 1\n"));
    CHECK(!as::PrefabTextIsNetworked(""));

    const fs::path proj = TempDir("proj");
    Write(proj / "Assets/prefabs/crate.okayprefab", "gameobject Crate\n  netsync 0 0 0 \"crate\"\n");
    Write(proj / "Assets/prefabs/deep/ball.okayprefab", "gameobject Ball\n  netsync 1 0 0 \"ball\" 1\n");
    Write(proj / "Assets/prefabs/tree.okayprefab", "gameobject Tree\n  name Tree\n");   // not networked
    Write(proj / "Assets/notes.txt", "  netsync 0 0 0 \"x\"\n");                     // not a prefab

    Scene scene("S");
    auto* nm = scene.CreateGameObject("Net")->AddComponent<NetworkManager>();
    nm->spawnablePrefabs = {"prefabs/bullet.okayprefab", "prefabs/crate.okayprefab"};
    const as::PrefabList got = as::NetworkedPrefabs(proj.string(), nm);
    // The manager's own list first (kept even if the file is missing), then the rest sorted, no repeats.
    CHECK(got.size() == 3);
    if (got.size() == 3) CHECK(got[0] == "prefabs/bullet.okayprefab" && got[1] == "prefabs/crate.okayprefab" && got[2] == "prefabs/deep/ball.okayprefab");
    CHECK(as::NetworkedPrefabs("", nullptr).empty());
    CHECK(as::NetworkedPrefabs((proj / "nope").string(), nullptr).empty());
    std::error_code ec; fs::remove_all(proj, ec);
}

void TestAddAntiCheat() {
    RUN_SUITE("Add Anti-Cheat");
    const fs::path proj = TempDir("add");
    Write(proj / "Assets/prefabs/crate.okayprefab", "gameobject Crate\n  netsync 0 0 0 \"crate\"\n");

    Scene scene("S");
    GameObject* net = scene.CreateGameObject("Net");
    net->AddComponent<NetworkManager>();
    GameObject* other = scene.CreateGameObject("Other");
    as::AddResult r = as::AddAntiCheat(scene, other, as::Preset::Competitive, proj.string());
    CHECK(r.added && r.hasManager && r.config && r.config->gameObject == net);   // goes next to the Network Manager
    CHECK(r.config && r.config->settings.enabled && r.config->settings.spawnAllowlist.size() == 1 &&
          r.config->settings.spawnAllowlist[0] == "prefabs/crate.okayprefab");
    CHECK(r.config && detectui::IsObserveMode(r.config->settings.detect) && r.config->settings.detect.enabled);
    CHECK(Has(r.message, "Competitive"));
    // A second click changes nothing.
    const std::string before = r.config->settings.ToText();
    as::AddResult again = as::AddAntiCheat(scene, other, as::Preset::Economy, proj.string());
    CHECK(!again.added && again.config == r.config && r.config->settings.ToText() == before);
    CHECK(net->GetComponents<AntiCheatConfig>().size() == 1 && other->GetComponents<AntiCheatConfig>().empty());

    // No Network Manager: it goes on the object given, and says nothing is checked yet.
    Scene bare("B");
    GameObject* g = bare.CreateGameObject("G");
    as::AddResult b = as::AddAntiCheat(bare, g, as::Preset::CasualCoop, "");
    CHECK(b.added && !b.hasManager && b.config && b.config->gameObject == g && Has(b.message, "no Network Manager"));
    CHECK(as::FindManager(bare) == nullptr);   // it never adds one by itself
    // Nothing to put it on.
    Scene empty("E");
    as::AddResult e = as::AddAntiCheat(empty, nullptr, as::Preset::CasualCoop, "");
    CHECK(!e.added && !e.config && !e.message.empty());
    std::error_code ec; fs::remove_all(proj, ec);
}

} // namespace

int main() {
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};
    TestPresets();
    TestNetworkedPrefabs();
    TestAddAntiCheat();
    Log::sink = sink;
    TEST_MAIN_RESULT();
}
