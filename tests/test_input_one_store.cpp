// One input system (duplicate audit #422, item 2; owner 2026-11-23 "combine it so you have one").
// There is one action table (InputRuntime::Actions(), an ActionSet) and one rebinding store. InputActions (what the built-in
// controllers read) is its name-based front; player 0's typed set (ia_*, the "ia:" Keybind field) has the same bindings. A
// player who rebinds Jump on either Controls screen is seen by every reader at once, and the saved rebinding comes back for
// all of them. Old saves (Prefs "input.<action>") keep loading. Blocks 5 to 12 were written for the earlier two-store link
// (PR #1139); their expectations now follow the one store, each change noted where it is.
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

    // ---- 5. Old saves: an "input.<action>" pref from an earlier version loads into both. Load itself writes nothing; the
    //         link (and its one-time import) happens when player 0 joins, when the asset in use is known ----
    {
        Fresh();
        Prefs::SetString("input.jump", "key:x;pad:b");          // the format every earlier version wrote
        Prefs::SetString("input.move", "keys:i,k,j,l;keys:up,down,left,right;stick:left;dpad");
        InputActions::Load();
        CHECK(InputActions::Find("Jump")->bindings[0].ToString() == "key:x");
        CHECK(!Prefs::Has("inputov.player0"));
        CHECK(InputRuntime::Set(0) && SetPart(0, "Jump", 0, 0) == "key:x" && SetPart(0, "Jump", 1, 0) == "pad:b");
        CHECK(SetPart(0, "Move", 0, 0) == "key:i" && SetPart(0, "Move", 0, 3) == "key:l");
        CHECK(!Prefs::Has("inputov.player0"));                                        // one store: a join writes nothing either
        // a typed rebind saved by an earlier version (inputov.player0) is in the one store at once, before any join
        Fresh();
        Prefs::SetString("inputov.player0", "ov Reload 0 key:g\n");
        InputActions::Load();
        CHECK(InputActions::Find("Reload")->bindings[0].ToString() == "key:g");
        CHECK(InputRuntime::Set(0) && InputActions::Find("Reload")->bindings[0].ToString() == "key:g");
        // both saved and disagreeing (only something outside writes them apart now): "input.<action>", the form every
        // version writes, wins for that action, so a rebind made in an older build after this one still comes through
        Fresh();
        Prefs::SetString("input.reload", "key:t;pad:y");
        Prefs::SetString("inputov.player0", "ov Reload 0 key:g\n");
        InputActions::Load();
        CHECK(InputRuntime::Set(0) && InputActions::Find("Reload")->bindings[0].ToString() == "key:t" && SetPart(0, "Reload", 0, 0) == "key:t");
        CHECK(Prefs::GetString("input.reload") == "key:t;pad:y" && Prefs::GetString("inputov.player0") == "ov Reload 0 key:g\n");   // nothing rewritten
    }

    // ---- 6. Actions the asset does not have (Vehicle, a game's own) are in the one table and the one store too ----
    {
        Fresh();
        CHECK(InputActions::Rebind("Horn", "key:j"));
        CHECK(Prefs::GetString("input.horn") == "key:j" && Prefs::GetString("inputov.player0").find("ov Horn 0 key:j") != std::string::npos);
        // a project asset's Jump is THE Jump (the built-in default only fills in what an asset lacks): a rebind changes it
        ActionSet custom;
        CHECK(ParseInputAsset("okayinput 1\naction Jump button\n  bind single key:f\n", custom));
        InputRuntime::Players().SetAsset(custom);
        CHECK(InputActions::Find("Jump")->bindings.size() == 1 && InputActions::Find("Jump")->bindings[0].ToString() == "key:f");
        CHECK(InputActions::Find("Horn")->bindings[0].ToString() == "key:j");     // the saved rebinding came along
        CHECK(InputActions::Rebind("Jump", "key:u"));
        CHECK(InputRuntime::Set(0) && SetPart(0, "Jump", 0, 0) == "key:u");
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

    // ==== Review of PR #1139: each block failed on 2957a101d ====
    // ---- 8. A Move part rebound to something the name-based front cannot show stays in the store (not undone, not erased) ----
    {
        Fresh();
        CHECK(InputRuntime::Rebind(0, "Move", 0, 0, "pad:y"));
        CHECK(InputActions::Find("Move")->bindings[0].kind == InputBinding::Kind::None);   // one store: shown as "cannot show", still read
        InputRuntime::SaveOverrides(0);
        CHECK(!Prefs::Has("input.move"));                                                // the per-action form cannot hold it
        CHECK(SetPart(0, "Move", 0, 0) == "pad:y");
        CHECK(Prefs::GetString("inputov.player0").find("ov Move 0 pad:y,key:s,key:a,key:d") != std::string::npos);
        // an old save with such a part survives the start-up Load and a join
        Fresh();
        Prefs::SetString("inputov.player0", "ov Move 0 mouse:middle,key:s,key:a,key:d\n");
        InputActions::Load();
        CHECK(Prefs::GetString("inputov.player0") == "ov Move 0 mouse:middle,key:s,key:a,key:d\n");
        CHECK(InputRuntime::Set(0) && SetPart(0, "Move", 0, 0) == "mouse:middle");
        CHECK(Prefs::GetString("inputov.player0") == "ov Move 0 mouse:middle,key:s,key:a,key:d\n");
        CHECK(InputActions::Find("Move")->bindings[0].kind == InputBinding::Kind::None);
    }

    // ---- 9. A custom .okayinput: nothing is read or written against the built-in asset before the real one is in use ----
    {
        // 9a. overrides made against a custom asset (Jump: pad first, key second): once that asset is in use the table is
        //     built from it again and the saved line lands where it was made
        Fresh();
        Prefs::SetString("inputov.player0", "ov Jump 0 pad:x\n");
        InputActions::Load();
        ActionSet custom;
        CHECK(ParseInputAsset("okayinput 1\naction Jump button\n  bind single pad:a\n  bind single key:space\naction Dash button\n  bind single key:q\n", custom));
        InputRuntime::Players().SetAsset(custom);
        CHECK(InputRuntime::Set(0) && SetPart(0, "Jump", 0, 0) == "pad:x");
        CHECK(InputActions::Find("Jump")->bindings[0].ToString() == "pad:x" && InputActions::Find("Jump")->bindings[1].ToString() == "key:space");
        // 9b. first launch: a line for an action only the custom asset has is kept, also when an old input.<action> exists
        Fresh();
        Prefs::SetString("inputov.player0", "ov Dash 0 key:v\n");
        Prefs::SetString("input.jump", "key:x;pad:a");
        InputActions::Load();
        CHECK(Prefs::GetString("inputov.player0").find("ov Dash 0 key:v") != std::string::npos);
        InputActions::Rebind("Fire", "key:f");                                       // one store: the save writes it, and keeps the Dash line
        CHECK(Prefs::GetString("inputov.player0").find("ov Dash 0 key:v") != std::string::npos);
        CHECK(Prefs::GetString("inputov.player0").find("ov Fire 0 key:f") != std::string::npos);
    }

    // ---- 10. One store, one save: a later save (any Controls screen) also keeps an earlier unsaved typed rebind ----
    {
        Fresh();
        CHECK(InputRuntime::Rebind(0, "Crouch", 0, 0, "key:c"));                    // not saved yet: no ia_save
        CHECK(!Prefs::Has("inputov.player0"));
        CHECK(RemapBind("Fire", "key:f").ok);
        const std::string ov = Prefs::GetString("inputov.player0");
        CHECK(ov.find("ov Crouch 0 key:c") != std::string::npos && ov.find("ov Fire 0 key:f") != std::string::npos);
        CHECK(Prefs::GetString("input.crouch") == "key:c;pad:b");
        CHECK(SetPart(0, "Crouch", 0, 0) == "key:c");
    }

    // ---- 11. Controls screen Unbind: the typed layer gets no conflict the player did not make ----
    {
        Fresh();
        ActionSet* set = InputRuntime::Set(0);
        RemapBind("Jump", "key:e", RemapResolve::Unbind);
        CHECK(InputActions::Find("Jump")->bindings[0].ToString() == "key:e");
        CHECK(set && set->Conflicts("Jump", 0, 0, SetPart(0, "Jump", 0, 0)).empty());
        CHECK(set && set->Conflicts("Interact", 0, 0, SetPart(0, "Interact", 0, 0)).empty());
        // a swap is mirrored whole (both sides at once)
        Fresh();
        set = InputRuntime::Set(0);
        CHECK(RemapBind("Jump", "key:e", RemapResolve::Swap).ok);
        CHECK(SetPart(0, "Jump", 0, 0) == "key:e" && SetPart(0, "Interact", 0, 0) == "key:space");
    }

    // ---- 12. A typed rebind is THE rebind: what ia_rebind allows (a lone key on Look) every reader now gets. InputActions'
    //         own Rebind still refuses a lone key for a direction action, as before. ----
    {
        Fresh();
        CHECK(InputRuntime::Rebind(0, "Look", 0, 0, "key:q"));
        CHECK(InputActions::Find("Look")->bindings[0].ToString() == "key:q");
        CHECK(InputRuntime::Rebind(0, "Move", 2, 0, "pad:y"));
        CHECK(InputActions::Find("Move")->bindings[2].ToString() == "pad:y");
        InputRuntime::SaveOverrides(0);
        CHECK(SetPart(0, "Look", 0, 0) == "key:q" && InputActions::Find("Look")->bindings[0].ToString() == "key:q");
        CHECK(!InputActions::Rebind("Look", "key:z"));                                  // refused as before
    }

    // ==== One input system (owner 2026-11-23): old names of all three former systems read the one table ====
    // ---- 13. One table: every action once; the asset's and the built-in defaults' actions side by side ----
    {
        Fresh();
        const auto& all = InputActions::All();
        for (std::size_t i = 0; i < all.size(); ++i)
            for (std::size_t j = i + 1; j < all.size(); ++j) CHECK(all[i].name != all[j].name);
        CHECK(all.size() == InputRuntime::Actions().Actions().size());
        for (const char* n : {"Move", "Look", "Jump", "Navigate", "Submit", "Cancel", "Dig", "Prone", "Horn"}) CHECK(InputActions::Has(n));
        CHECK(InputActions::Find("Navigate")->context == "UI" && InputActions::Find("Horn")->context == "Vehicle");
    }

    // ---- 14. Same key -> same action through every old name: InputActions (controllers), action_* and ia_* scripts,
    //          the engine-API Input.GetButtonDown, the typed set; a rebind on the game's Controls screen moves all of them ----
    {
        Fresh();
        Scene scene("names"); scene.physicsEnabled = false;
        GameObject* g = scene.CreateGameObject("T");
        g->AddComponent<TextRenderer>();
        auto* sc = g->AddComponent<ScriptComponent>();
        std::string err;
        CHECK(sc->LoadSource(R"(function update(dt) {
  set_text("" + action("Jump") + " " + ia_down("Jump") + " " + action_x("Move") + " " + ia_x("Move"));
})", &err));
        scene.Start();
        auto step = [&](std::vector<char> keys) { Frame(keys); scene.Update(0.016f); };
        TextRenderer* t = g->GetComponent<TextRenderer>();
        step({' ', 'd'}); step({' ', 'd'});
        CHECK(t && t->text == "true true 1 1");
        CHECK(InputActions::Get("Jump") && InputActions::ControlKey("Jump", ' ') && InputRuntime::Set(0)->Held("Jump"));
        // the player rebinds Jump to J and Move right to L in the game's Controls screen
        CHECK(RemapBind("Jump", "key:j").ok);
        CHECK(RemapBind("Move", "key:l", RemapResolve::Allow, 3).ok);
        step({' ', 'd'}); step({' ', 'd'});
        CHECK(t && t->text == "false false 0 0");                                     // the old keys stopped for every reader
        CHECK(!InputActions::ControlKey("Jump", ' '));
        step({'j', 'l'}); step({'j', 'l'});
        CHECK(t && t->text == "true true 1 1");
        CHECK(InputActions::ControlKey("Jump", ' ') && InputRuntime::Set(0)->Held("Jump"));
        // and it comes back after a restart (a new table from the saved store)
        InputRuntime::Reset(); InputActions::Restore();
        step({'j', 'l'}); step({'j', 'l'});
        CHECK(t && t->text == "true true 1 1");
        CHECK(InputActions::Find("Jump")->bindings[0].ToString() == "key:j");
    }

    // ---- 15. Contexts are the table's maps: the pause menu's Block context stops the typed player 0 too ----
    {
        Fresh();
        ActionSet* p0 = InputRuntime::Set(0);
        Frame({' '}); Frame({' '});
        CHECK(InputActions::Get("Jump") && p0 && p0->Held("Jump"));
        InputActions::PushContext("Pause", InputActions::ContextMode::Block);
        Frame({' '}); Frame({' '});
        CHECK(!InputActions::Get("Jump") && !p0->Held("Jump"));
        InputActions::PopContext("Pause");
        Frame({' '}); Frame({' '});
        CHECK(InputActions::Get("Jump") && p0->Held("Jump"));
    }

    Prefs::Clear(); InputRuntime::Reset(); InputActions::Restore(); Input::FeedKeys({});
    TEST_MAIN_RESULT();
}
