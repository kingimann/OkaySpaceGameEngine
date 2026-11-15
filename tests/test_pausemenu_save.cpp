// PauseMenu save round trip. Before this fix the "pausemenu" record kept only the key, the Resume and Quit
// switches, the two colours, the title and the main menu scene, so hint, showRestart, showVolume and
// restartScene went back to their defaults on every save, duplicate and prefab (a data-loss bug).
// Written from SceneSerializer's record format; old lines without the new fields must still load.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/PauseMenu.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include <string>

using namespace okay;

static PauseMenu* FirstMenu(Scene& s) {
    for (GameObject* go : s.Roots())
        if (auto* pm = go->GetComponent<PauseMenu>()) return pm;
    return nullptr;
}

static void TestRoundTrip() {
    RUN_SUITE("pausemenu: every authored field survives save and load");
    Scene a("a");
    GameObject* go = a.CreateGameObject("Menu");
    auto* pm = go->AddComponent<PauseMenu>();
    pm->hint = "Press P to go on";
    pm->showRestart = true;          // default false
    pm->showVolume = false;          // default true
    pm->restartScene = "levels/one \"quoted\".okayscene";
    pm->localized = true;
    pm->title = "Stopped";
    const std::string text = SceneSerializer::Serialize(a);

    Scene b("b");
    std::string err;
    CHECK(SceneSerializer::Deserialize(b, text, &err));
    PauseMenu* back = FirstMenu(b);
    CHECK(back != nullptr);
    if (!back) return;
    CHECK(back->hint == "Press P to go on");
    CHECK(back->showRestart);
    CHECK(!back->showVolume);
    CHECK(back->restartScene == "levels/one \"quoted\".okayscene");
    CHECK(back->localized);
    CHECK(back->title == "Stopped");
    CHECK(SceneSerializer::Serialize(b) == text);   // saving again changes nothing
}

static void TestOldLineLoads() {
    RUN_SUITE("pausemenu: an old record without the new fields keeps the defaults");
    Scene a("a");
    a.CreateGameObject("Menu")->AddComponent<PauseMenu>();
    std::string text = SceneSerializer::Serialize(a);
    // Cut the line back to the old format: everything after the main menu scene.
    const std::size_t at = text.find("  pausemenu ");
    CHECK(at != std::string::npos);
    if (at == std::string::npos) return;
    const std::size_t eol = text.find('\n', at);
    std::string line = text.substr(at, eol - at);
    // old line = key, two switches, 8 colour numbers, title and main menu scene (two quoted strings)
    std::size_t q = 0; int quotes = 0;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '\\') { ++i; continue; }
        if (line[i] == '"' && ++quotes == 4) { q = i + 1; break; }
    }
    CHECK(quotes == 4);
    text.replace(at, eol - at, line.substr(0, q));
    Scene b("b");
    std::string err;
    CHECK(SceneSerializer::Deserialize(b, text, &err));
    PauseMenu* back = FirstMenu(b);
    CHECK(back != nullptr);
    if (!back) return;
    const PauseMenu def;
    CHECK(back->hint == def.hint);
    CHECK(back->showRestart == def.showRestart);
    CHECK(back->showVolume == def.showVolume);
    CHECK(back->restartScene == def.restartScene);
}

static void TestRegistered() {
    RUN_SUITE("pausemenu: the saved fields are registry properties");
    Scene s("s");
    GameObject* go = s.CreateGameObject("Menu");
    auto* pm = go->AddComponent<PauseMenu>();
    CHECK(PropertyRegistry::SetProperty(*go, "PauseMenu.hint", PropertyValue::MakeString("x")) && pm->hint == "x");
    CHECK(PropertyRegistry::SetProperty(*go, "PauseMenu.showRestart", PropertyValue::MakeBool(true)) && pm->showRestart);
    CHECK(PropertyRegistry::SetProperty(*go, "PauseMenu.showVolume", PropertyValue::MakeBool(false)) && !pm->showVolume);
    CHECK(PropertyRegistry::SetProperty(*go, "PauseMenu.restartScene", PropertyValue::MakeString("a.okayscene")) && pm->restartScene == "a.okayscene");
    CHECK(PropertyRegistry::SetProperty(*go, "PauseMenu.localized", PropertyValue::MakeBool(true)) && pm->localized);
}

// The hand-written sample scene in examples/property_registry loads with the values its How to try it uses.
static void TestExampleScene() {
    RUN_SUITE("pausemenu: the sample scene loads with its values");
    Scene s("s");
    std::string err;
    CHECK(SceneSerializer::LoadFromFile(s, std::string(OKAY_SOURCE_DIR) + "/examples/property_registry/registry_try.okayscene", &err));
    if (!err.empty()) std::cerr << "  " << err << "\n";
    PauseMenu* pm = FirstMenu(s);
    CHECK(pm && pm->hint == "Saved with this scene since the fix" && pm->showRestart && !pm->showVolume &&
          pm->restartScene == "registry_try.okayscene");
    HealthStat* hs = nullptr;
    for (GameObject* go : s.Roots()) if (auto* h = go->GetComponent<HealthStat>()) hs = h;
    CHECK(hs && hs->maxHealth == 250000.0f);   // above the old 1e5 bound: test registry_large_values
    if (!hs) return;
    // a registry write of the same value does not cut it down
    GameObject* go = nullptr;
    for (GameObject* g : s.Roots()) if (g->GetComponent<HealthStat>()) go = g;
    CHECK(go && PropertyRegistry::SetProperty(*go, "HealthStat.maxHealth", PropertyValue::MakeFloat(250000.0f)) && hs->maxHealth == 250000.0f);
}

int main() {
    TestExampleScene();
    TestRoundTrip();
    TestOldLineLoads();
    TestRegistered();
    TEST_MAIN_RESULT();
}
