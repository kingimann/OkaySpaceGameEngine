// One rebinding store for the two input-action layers (duplicate audit #422, item 2).
// The built-in controllers read InputActions (text bindings); the typed layer reads an ActionSet (InputRuntime player 0, the
// default ActionSet, from the .okayinput asset). A player who rebinds Jump on either Controls screen must be seen by both
// readers, and the saved rebinding must come back for both. Old saves (Prefs "input.<action>") keep loading.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/Prefs.hpp"
#include "okay/Input/RemapModel.hpp"
#include "okay/Scene/UIGallery.hpp"
#include "okay/Components/TextRenderer.hpp"

using namespace okay;

namespace {
void Frame(std::vector<char> keys) {
    Input::FeedKeys(keys);
    Input::FeedMouse({0.0f, 0.0f}, 0);
    Input::FeedGamepadRaw(0, {}, {}, 0, 0, 0, false);
    InputActions::Tick(1.0f / 60.0f);
}
void Fresh() { Prefs::Clear(); InputRuntime::Reset(); InputActions::Restore(); Frame({}); Frame({}); }
std::string SetPart(int player, const char* action, int binding, int part) {
    ActionSet* s = InputRuntime::Players().Player(player);
    const ActionDef* d = s ? s->Find(action) : nullptr;
    if (!d || binding >= (int)d->bindings.size() || part >= (int)d->bindings[(std::size_t)binding].parts.size()) return "-";
    return d->bindings[(std::size_t)binding].parts[(std::size_t)part];
}
UIKeybindField* Field(Scene& s, const char* name) { GameObject* g = s.Find(name); return g ? g->GetComponent<UIKeybindField>() : nullptr; }
} // namespace

int main() {
    RUN_SUITE("input_one_store");

    // ---- 1. The InputActions Controls screen model (RemapBind) -> the ActionSet sees it too ----
    {
        Fresh();
        CHECK(InputRuntime::Set(0) != nullptr);
        CHECK(RemapBind("Jump", "key:j").ok);
        CHECK(InputActions::Find("Jump")->bindings[0].ToString() == "key:j");       // the old reader (built-in controllers)
        CHECK(SetPart(0, "Jump", 0, 0) == "key:j");                                  // the new reader (ActionSet player 0)
        Frame({'j'});
        ActionSet* set = InputRuntime::Set(0);
        set->SetScheme("Keyboard+Mouse"); set->Update(0.016f);
        CHECK(InputActions::Get("Jump") && set->Held("Jump"));
        // one key of the Move composite
        CHECK(RemapBind("Move", "key:i", RemapResolve::Cancel, 0).ok);
        CHECK(SetPart(0, "Move", 0, 0) == "key:i" && SetPart(0, "Move", 0, 1) == "key:s");
        // saved for both: a new session's ActionSet loads it
        CHECK(Prefs::GetString("input.jump") == "key:j;pad:a");                    // the old layout is still written
        InputRuntime::Reset();
        CHECK(SetPart(0, "Jump", 0, 0) == "-" && InputRuntime::Set(0) && SetPart(0, "Jump", 0, 0) == "key:j");
        // Reset puts both back
        InputActions::Reset("Jump");
        CHECK(SetPart(0, "Jump", 0, 0) == "key:space" && InputActions::Find("Jump")->bindings[0].ToString() == "key:space");
    }

    // ---- 2. No local player joined yet: the rebind is stored for the one that joins later ----
    {
        Fresh();
        CHECK(InputActions::Rebind("Sprint", "pad:rb"));
        CHECK(InputRuntime::Players().Count() == 0);                                 // nobody was joined by the rebind
        CHECK(InputRuntime::Set(0) && SetPart(0, "Sprint", 1, 0) == "pad:rb");
    }

    // ---- 3. The typed Controls screen (UIGallery::AddControlsScreen, ia: fields) -> InputActions sees it too ----
    {
        Fresh();
        Scene scene("controls"); scene.physicsEnabled = false;
        CHECK(UIGallery::AddControlsScreen(scene) != nullptr);
        scene.Start();
        auto step = [&](std::vector<char> keys) { Frame(keys); scene.Update(0.016f); };
        step({});
        UIKeybindField* jump = Field(scene, "Key Jump");
        CHECK(jump != nullptr);
        if (jump) { jump->Listen(); step({}); step({'k'}); }
        CHECK(jump && jump->KeyLabel() == "K" && SetPart(0, "Jump", 0, 0) == "key:k");
        CHECK(InputActions::Find("Jump")->bindings[0].ToString() == "key:k");         // the built-in controllers follow it
        step({}); step({'k'});
        CHECK(InputActions::Get("Jump") && InputActions::ControlKey("Jump", ' '));
        step({}); step({' '});
        CHECK(!InputActions::Get("Jump") && !InputActions::ControlKey("Jump", ' '));   // the old key stops working
        CHECK(Prefs::GetString("input.jump") == "key:k;pad:a");
        // Backspace on the field: both back to Space
        if (jump) { step({}); jump->Listen(); step({}); step({'\b'}); }
        CHECK(SetPart(0, "Jump", 0, 0) == "key:space" && InputActions::Find("Jump")->bindings[0].ToString() == "key:space");
        CHECK(!Prefs::Has("input.jump"));
    }

    // ---- 4. Script API: ia_rebind on player 0 reaches InputActions (in memory; ia_save writes it) ----
    {
        Fresh();
        CHECK(InputRuntime::JoinNamed("keyboard") == 0 && InputRuntime::JoinNamed("pad1") == 1);
        CHECK(InputRuntime::Rebind(0, "Crouch", 0, 0, "key:c"));
        CHECK(InputActions::Find("Crouch")->bindings[0].ToString() == "key:c");
        CHECK(!Prefs::Has("input.crouch") && !Prefs::Has("inputov.player0"));       // not saved until asked
        InputRuntime::SaveOverrides(0);
        CHECK(Prefs::GetString("input.crouch") == "key:c;pad:b" && Prefs::Has("inputov.player0"));
        // another player's rebinding stays theirs
        CHECK(InputRuntime::Rebind(1, "Crouch", 1, 0, "pad:y"));
        CHECK(InputActions::Find("Crouch")->bindings[1].ToString() == "pad:b");
    }

    // ---- 5. Old saves: an "input.<action>" pref from an earlier version loads into both (one-time import) ----
    {
        Fresh();
        Prefs::SetString("input.jump", "key:x;pad:b");          // the format every earlier version wrote
        Prefs::SetString("input.move", "keys:i,k,j,l;keys:up,down,left,right;stick:left;dpad");
        InputActions::Load();
        CHECK(InputActions::Find("Jump")->bindings[0].ToString() == "key:x");
        CHECK(Prefs::Has("inputov.player0"));
        CHECK(InputRuntime::Set(0) && SetPart(0, "Jump", 0, 0) == "key:x" && SetPart(0, "Jump", 1, 0) == "pad:b");
        CHECK(SetPart(0, "Move", 0, 0) == "key:i" && SetPart(0, "Move", 0, 3) == "key:l");
        // a typed-only rebind saved by an earlier version (inputov.player0) reaches InputActions at load
        Fresh();
        Prefs::SetString("inputov.player0", "ov Reload 0 key:g\n");
        InputActions::Load();
        CHECK(InputActions::Find("Reload")->bindings[0].ToString() == "key:g");
        // both saved and disagreeing (the bug this fixes): the typed layer's override wins, then both match
        Fresh();
        Prefs::SetString("input.reload", "key:t;pad:y");
        Prefs::SetString("inputov.player0", "ov Reload 0 key:g\n");
        InputActions::Load();
        CHECK(InputActions::Find("Reload")->bindings[0].ToString() == "key:g" && Prefs::GetString("input.reload") == "key:g;pad:y");
    }

    // ---- 6. Actions the asset does not have (Vehicle, a game's own) stay InputActions-only, as before ----
    {
        Fresh();
        CHECK(InputActions::Rebind("Horn", "key:j"));
        CHECK(Prefs::GetString("input.horn") == "key:j" && !Prefs::Has("inputov.player0"));
        // a project asset whose Jump differs from the built-in defaults is not linked: its bindings are its own
        ActionSet custom;
        CHECK(ParseInputAsset("okayinput 1\naction Jump button\n  bind single key:f\n", custom));
        InputRuntime::Players().SetAsset(custom);
        CHECK(InputActions::Rebind("Jump", "key:u"));
        CHECK(InputRuntime::Set(0) && SetPart(0, "Jump", 0, 0) == "key:f");
    }

    // ---- 7. The script names keep their old results: ia_rebind true / false as before, nothing saved until ia_save,
    //         ia_reset_bindings puts the asset back; player 0's changes now also reach InputActions ----
    {
        Fresh();
        Scene scene("script"); scene.physicsEnabled = false;
        GameObject* g = scene.CreateGameObject("T");
        g->AddComponent<TextRenderer>();
        auto* sc = g->AddComponent<ScriptComponent>();
        std::string err;
        CHECK(sc->LoadSource(R"(function update(dt) {
  if (key_down("r")) { set_text("rebind " + ia_rebind("Jump", 0, 0, "key:j") + " " + ia_rebind("Nope", 0, 0, "key:j") + " " + ia_rebind("Jump", 9, 0, "key:j") + " " + ia_rebind("Jump", 0, 0, "not_a_key")); }
  if (key_down("v")) { ia_save(); }
  if (key_down("z")) { ia_reset_bindings(); }
})", &err));
        scene.Start();
        auto step = [&](std::vector<char> keys) { Frame(keys); scene.Update(0.016f); };
        step({}); step({'r'});
        TextRenderer* t = g->GetComponent<TextRenderer>();
        CHECK(t && t->text == "rebind true false false false");
        CHECK(SetPart(0, "Jump", 0, 0) == "key:j" && InputActions::Find("Jump")->bindings[0].ToString() == "key:j");
        CHECK(!Prefs::Has("inputov.player0") && !Prefs::Has("input.jump"));
        step({}); step({'v'});
        CHECK(Prefs::Has("inputov.player0") && Prefs::GetString("input.jump") == "key:j;pad:a");
        step({}); step({'z'});
        CHECK(SetPart(0, "Jump", 0, 0) == "key:space" && InputActions::Find("Jump")->bindings[0].ToString() == "key:space");
        CHECK(Prefs::Has("inputov.player0"));                                         // reset alone does not save, as before
        step({}); step({'v'});
        CHECK(!Prefs::Has("inputov.player0") && !Prefs::Has("input.jump"));
    }

    Prefs::Clear(); InputRuntime::Reset(); InputActions::Restore(); Input::FeedKeys({});
    TEST_MAIN_RESULT();
}
