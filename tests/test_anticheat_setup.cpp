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

const as::Item* Find(const std::vector<as::Item>& v, const std::string& id) {
    for (const auto& i : v) if (id == i.id) return &i;
    return nullptr;
}
as::Status StatusOf(const std::vector<as::Item>& v, const std::string& id) {
    const as::Item* i = Find(v, id);
    return i ? i->status : as::Status::Info;
}
// ---- (2) the checklist ------------------------------------------------------------------------------
void TestChecklistFacts() {
    RUN_SUITE("anti-cheat checklist (rules)");
    // An empty scene: everything missing, nothing else is judged.
    {
        as::Facts f; f.libsodium = true;
        auto v = as::Evaluate(f);
        CHECK(StatusOf(v, "manager") == as::Status::Missing && Find(v, "manager")->fix == as::Fix::AddManager);
        CHECK(StatusOf(v, "config") == as::Status::Missing && Find(v, "config")->fix == as::Fix::AddConfig);
        CHECK(StatusOf(v, "manifest") == as::Status::Warn && Find(v, "manifest")->fix == as::Fix::ManifestReport);
        CHECK(!Find(v, "encryption") && !Find(v, "authority") && !Find(v, "detect") && !Find(v, "unclaimed") && !Find(v, "tickets"));
        for (const auto& i : v) CHECK(!i.title.empty() && !i.detail.empty());
    }
    // A disabled component.
    {
        as::Facts f; f.hasManager = true; f.hasConfig = true; f.rules.enabled = false;
        CHECK(StatusOf(as::Evaluate(f), "config") == as::Status::Missing && Find(as::Evaluate(f), "config")->fix == as::Fix::EnableConfig);
    }
    // A fully set up game: every line is OK (tickets is optional info).
    {
        as::Facts f; f.hasManager = true; f.hasConfig = true; f.libsodium = true; f.manifestMode = 2;
        as::ApplyPreset(f.rules, as::Preset::Competitive, {}, true);
        auto v = as::Evaluate(f);
        as::Summary s = as::Summarize(v);
        CHECK(s.warn == 0 && s.missing == 0 && s.ok >= 6 && s.info == 1);
        CHECK(StatusOf(v, "tickets") == as::Status::Info && Find(v, "tickets")->fix == as::Fix::None);
        CHECK(StatusOf(v, "detect") == as::Status::Ok && Has(Find(v, "detect")->detail, "observe"));
    }
    // Individual lines.
    {
        as::Facts f; f.hasManager = true; f.hasConfig = true; f.libsodium = true;
        f.rules = AntiCheatSettings{}; f.rules.enabled = true; f.rules.ownerOnlySyncVars = false; f.rules.maxSyncSpeed = 0.0f;
        auto v = as::Evaluate(f);
        CHECK(StatusOf(v, "authority") == as::Status::Warn && Find(v, "authority")->fix == as::Fix::ServerAuthority);
        CHECK(StatusOf(v, "encryption") == as::Status::Warn && Find(v, "encryption")->fix == as::Fix::EnableEncryption);
        CHECK(StatusOf(v, "detect") == as::Status::Warn && Find(v, "detect")->fix == as::Fix::DetectObserve);
        f.authoritativeMovement = true; f.rules.ownerOnlySyncVars = true;   // the server moves players: that counts as a move check
        CHECK(StatusOf(as::Evaluate(f), "authority") == as::Status::Ok);
        f.libsodium = false;   // a build without encryption cannot offer the fix, and says why
        auto nv = as::Evaluate(f);
        CHECK(StatusOf(nv, "encryption") == as::Status::Warn && Find(nv, "encryption")->fix == as::Fix::None && Has(Find(nv, "encryption")->detail, "libsodium"));
        // Detection on and NOT observing: OK, and it says it can kick.
        f.rules.detect.enabled = true;
        CHECK(StatusOf(as::Evaluate(f), "detect") == as::Status::Ok && Has(Find(as::Evaluate(f), "detect")->detail, "may kick"));
        // Tickets: a key without an audience is a hard problem (the server refuses every join).
        f.rules.ticketKey = "abc";
        CHECK(StatusOf(as::Evaluate(f), "tickets") == as::Status::Missing);
        f.rules.ticketAudience = "arena-1";
        CHECK(StatusOf(as::Evaluate(f), "tickets") == as::Status::Ok && Has(Find(as::Evaluate(f), "tickets")->detail, "arena-1"));
        // Manifest modes.
        f.manifestMode = 1; CHECK(StatusOf(as::Evaluate(f), "manifest") == as::Status::Ok && Has(Find(as::Evaluate(f), "manifest")->title, "Report"));
        f.manifestMode = 2; CHECK(Has(Find(as::Evaluate(f), "manifest")->title, "Enforce"));
        f.manifestMode = 0; CHECK(Has(Find(as::Evaluate(f), "manifest")->title, "Off"));
    }
    // A rules file that does not load fails closed: Missing, and no button pretends to fix it.
    {
        as::Facts f; f.hasManager = true; f.hasConfig = true; f.rules.enabled = true; f.rulesFile = "rules/arena.okayac"; f.rulesFileError = "can't open rules/arena.okayac";
        auto v = as::Evaluate(f);
        CHECK(StatusOf(v, "file") == as::Status::Missing && Find(v, "file")->fix == as::Fix::None && Has(Find(v, "file")->detail, "refuses every join"));
        // Rules that come from a file: no fix button changes the component, so none is offered.
        f.rulesFileError.clear(); f.rulesFromFile = true; f.libsodium = true;
        v = as::Evaluate(f);
        CHECK(StatusOf(v, "file") == as::Status::Ok);
        for (const auto& i : v) CHECK(i.fix != as::Fix::EnableEncryption && i.fix != as::Fix::ServerAuthority && i.fix != as::Fix::DetectObserve);
        CHECK(Find(v, "encryption") && Has(Find(v, "encryption")->detail, "rules file"));
    }
}

void TestChecklistScene() {
    RUN_SUITE("anti-cheat checklist (scene + fixes)");
    const fs::path proj = TempDir("check");
    Scene scene("S");
    GameObject* net = scene.CreateGameObject("Net");
    net->AddComponent<NetworkManager>();
    // A door the server drives, a ball nobody owns, a coin players may claim, a pawn a player drives, and a player template.
    auto* door = scene.CreateGameObject("Door")->AddComponent<NetworkSync>(); door->authority = NetworkSync::Authority::Host; door->netId = "door";
    auto* ball = scene.CreateGameObject("Ball")->AddComponent<NetworkSync>(); ball->authority = NetworkSync::Authority::Manual; ball->netId = "ball";
    auto* coin = scene.CreateGameObject("Coin")->AddComponent<NetworkSync>(); coin->authority = NetworkSync::Authority::Manual; coin->clientClaimable = true;
    auto* pawn = scene.CreateGameObject("Pawn")->AddComponent<NetworkSync>(); pawn->authority = NetworkSync::Authority::Mine;
    GameObject* tmplGo = scene.CreateGameObject("PlayerTemplate");   // cloned per player at run time: each clone gets an owner
    auto* tmpl = tmplGo->AddComponent<NetworkSync>(); tmpl->authority = NetworkSync::Authority::Mine; tmplGo->active = false;

    int manifest = 0;
    as::Facts f = as::GatherFacts(scene, manifest, proj.string());
    CHECK(f.hasManager && !f.hasConfig && f.manifestMode == 0 && f.unclaimed.size() == 2 && f.unclaimed[0] == "Ball (ball)" && f.unclaimed[1] == "Pawn");   // not the door (server drives it), the claimable coin or the inactive template

    // Fix 1: add anti-cheat. Everything that follows is judged.
    CHECK(as::ApplyFix(scene, as::Fix::AddConfig, manifest, proj.string(), net));
    CHECK(!as::ApplyFix(scene, as::Fix::AddConfig, manifest, proj.string(), net));   // nothing more to add
    auto v = as::Evaluate(as::GatherFacts(scene, manifest, proj.string()));
    CHECK(StatusOf(v, "config") == as::Status::Ok);
    const as::Item* un = Find(v, "unclaimed");
    CHECK(un && un->status == as::Status::Warn && un->objects.size() == 2 && un->fix == as::Fix::None);   // no safe automatic fix: the maker decides per object
    // The preset already made the game safe except for what the build and the Build Settings say.
    CHECK(StatusOf(v, "authority") == as::Status::Ok && StatusOf(v, "detect") == as::Status::Ok);

    // Break things on purpose, then use each fix button.
    AntiCheatConfig* ac = as::FindConfig(scene);
    CHECK(ac != nullptr);
    ac->settings.enabled = false;
    CHECK(as::ApplyFix(scene, as::Fix::EnableConfig, manifest, proj.string()) && ac->settings.enabled);
    ac->settings.ownerOnlySyncVars = false; ac->settings.maxSyncSpeed = 0.0f;
    CHECK(as::ApplyFix(scene, as::Fix::ServerAuthority, manifest, proj.string()) && ac->settings.ownerOnlySyncVars && ac->settings.maxSyncSpeed == 20.0f);
    CHECK(!as::ApplyFix(scene, as::Fix::ServerAuthority, manifest, proj.string()));
    ac->settings.maxSyncSpeed = 7.0f; ac->settings.ownerOnlySyncVars = false;   // a maker's own number stays
    CHECK(as::ApplyFix(scene, as::Fix::ServerAuthority, manifest, proj.string()) && ac->settings.maxSyncSpeed == 7.0f);
    ac->settings.detect = DetectSettings{};
    CHECK(as::ApplyFix(scene, as::Fix::DetectObserve, manifest, proj.string()));
    CHECK(ac->settings.detect.enabled && detectui::IsObserveMode(ac->settings.detect));
    CHECK(!as::ApplyFix(scene, as::Fix::DetectObserve, manifest, proj.string()));
    ac->settings.requireEncryption = false;
    const bool sodium = net::SecureChannel::Available();
    CHECK(as::ApplyFix(scene, as::Fix::EnableEncryption, manifest, proj.string()) == sodium && ac->settings.requireEncryption == sodium);
    CHECK(as::ApplyFix(scene, as::Fix::ManifestReport, manifest, proj.string()) && manifest == 1);
    CHECK(!as::ApplyFix(scene, as::Fix::ManifestReport, manifest, proj.string()) && manifest == 1);
    manifest = 2;
    CHECK(!as::ApplyFix(scene, as::Fix::ManifestReport, manifest, proj.string()) && manifest == 2);   // never lowers Enforce

    // A rules file is read from the project, and a missing one is reported.
    ac->file = "rules/arena.okayac";
    as::Facts bad = as::GatherFacts(scene, manifest, proj.string());
    CHECK(!bad.rulesFileError.empty() && !bad.rulesFromFile);
    Write(proj / "Assets/rules/arena.okayac", "enabled true\nmaxSyncSpeed 9\nrequireEncryption true\n");
    as::Facts good = as::GatherFacts(scene, manifest, proj.string());
    CHECK(good.rulesFileError.empty() && good.rulesFromFile && good.rules.maxSyncSpeed == 9.0f && good.rules.requireEncryption);

    // The manager fix adds one only when the scene has none.
    Scene bare("B");
    int m2 = 0;
    CHECK(as::ApplyFix(bare, as::Fix::AddManager, m2, "") && as::FindManager(bare) != nullptr);
    CHECK(!as::ApplyFix(bare, as::Fix::AddManager, m2, ""));
    std::error_code ec; fs::remove_all(proj, ec);
}

} // namespace

int main() {
    auto sink = Log::sink;
    Log::sink = [](Log::Level, const std::string&) {};
    TestPresets();
    TestNetworkedPrefabs();
    TestAddAntiCheat();
    TestChecklistFacts();
    TestChecklistScene();
    Log::sink = sink;
    TEST_MAIN_RESULT();
}
