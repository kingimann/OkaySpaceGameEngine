// The ia_* script functions (typed actions, maps, phases, local players, rebinding) driven from OkayScript with real Input fed by hand.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/TextRenderer.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Input/InputRuntime.hpp"

using namespace okay;

namespace {
std::string Text(Scene& s, const char* n = "T") { auto* t = s.Find(n)->GetComponent<TextRenderer>(); return t ? t->text : std::string(); }
bool Has(const std::string& h, const char* n) { return h.find(n) != std::string::npos; }
void Step(Scene& s, std::vector<char> keys) {
    Input::FeedKeys(keys); Input::FeedMouse({400, 300}, 0);
    InputActions::Tick(0.05f);
    s.Update(0.05f);
}
} // namespace

int main() {
    RUN_SUITE("input_script_api");
    Prefs::Clear();
    InputRuntime::Reset();
    Scene scene("s"); scene.physicsEnabled = false;
    GameObject* g = scene.CreateGameObject("T");
    g->AddComponent<TextRenderer>();
    auto* sc = g->AddComponent<ScriptComponent>();
    std::string err;
    CHECK(sc->LoadSource(R"(function start() { ia_use_default(); }
function update(dt) {
  var s = "scheme " + ia_scheme() + " players " + ia_players() + "\n";
  s = s + "move " + to_fixed(ia_x("Move"), 2) + "," + to_fixed(ia_y("Move"), 2) + " jump " + ia_down("Jump") + " pressed " + ia_pressed("Jump") + " performed " + ia_performed("Jump") + "\n";
  s = s + "phase " + ia_phase("Jump") + " label " + ia_label("Jump") + " ev " + ia_event() + "\n";
  s = s + "ui " + ia_map_enabled("UI") + " nav " + to_fixed(ia_y("Navigate"), 1) + " conflicts " + ia_conflicts("Jump", 0, 0, "key:e");
  set_text(s);
})", &err));
    if (!err.empty()) std::cerr << "  " << err << "\n";
    scene.Start();
    Step(scene, {});
    CHECK(Has(Text(scene), "players 1") && Has(Text(scene), "scheme Keyboard+Mouse"));   // the first read joined a single player
    Step(scene, {'w', ' '});
    CHECK(Has(Text(scene), "move 0.00,1.00") && Has(Text(scene), "jump true pressed true performed true") && Has(Text(scene), "label Space"));
    CHECK(Has(Text(scene), "ev Move:started"));
    Step(scene, {'w', ' '});
    CHECK(Has(Text(scene), "pressed false") && Has(Text(scene), "ui false") && Has(Text(scene), "conflicts Interact"));
    // the UI map on: it takes W; gameplay loses it
    err.clear();
    GameObject* g2 = scene.CreateGameObject("T2");
    g2->AddComponent<TextRenderer>();
    auto* sc2 = g2->AddComponent<ScriptComponent>();
    sc2->LoadSource(R"(function update(dt) {
  if (key_down("u")) { ia_map("UI", true); }
  if (key_down("r")) { ia_rebind("Jump", 0, 0, "key:j"); }
  set_text("move " + to_fixed(ia_y("Move"), 1) + " nav " + to_fixed(ia_y("Navigate"), 1) + " jump " + ia_down("Jump") + " " + ia_label("Jump"));
})", &err);
    if (!err.empty()) std::cerr << "  second: " << err << "\n";
    sc2->Start();
    Step(scene, {'u'}); Step(scene, {'w'});
        CHECK(Has(Text(scene, "T2"), "move 0.0 nav 1.0"));
    Step(scene, {'r'}); Step(scene, {'j'});
        CHECK(Has(Text(scene, "T2"), "jump true J"));
    InputRuntime::Reset();
    Prefs::Clear();
    TEST_MAIN_RESULT();
}
