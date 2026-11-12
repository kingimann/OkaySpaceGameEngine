// The no-code route to the save framework: the Saveable component stored in the scene
// file (with registry properties and variables), GameSaves (Save / Load / Delete /
// Continue / slot info / autosave) and the three ways to reach it without writing C++:
// script builtins, visual-script Fn.game_* nodes and Action List game_* actions.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/GameSaves.hpp"
#include "okay/Scene/UITemplates.hpp"
#include "okay/VisualScript/VsBuiltins.hpp"
#include "okay/VisualScript/VsVariables.hpp"
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

namespace {

std::string ReadText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
void WriteText(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary);
    f << s;
}
bool Contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
bool Near(const Vec3& a, const Vec3& b) { return (a - b).SqrMagnitude() < 1e-8f; }

long long g_now = 1700000000000LL;   // 2023-11-14 22:13 UTC
long long Clock() { return g_now; }

std::vector<std::string> g_errors;
void CaptureLog() {
    Log::console = false;
    Log::sink = [](Log::Level l, const std::string& m) { if (l == Log::Level::Error) g_errors.push_back(m); };
}
bool ErrorsMention(const std::string& part) {
    for (const std::string& e : g_errors) if (Contains(e, part)) return true;
    return false;
}

int Count(Scene& s, const std::string& name) {
    int n = 0;
    for (const auto& o : s.Objects()) if (o && o->name == name && !o->IsDestroyed()) ++n;
    return n;
}

/// The level file every test reloads: all marked with Saveable components, as the editor writes them.
void WriteLevel(const fs::path& file) {
    Scene lvl("Level1");
    lvl.physicsEnabled = false;
    GameObject* player = lvl.CreateGameObject("Player");
    auto* ps = player->AddComponent<Saveable>();
    ps->saveId = "player";
    ps->saveVariables = true;
    ps->saveSceneVariables = true;
    player->transform->localPosition = Vec3{1, 0, 0};
    auto* vc = player->AddComponent<VisualScriptComponent>();
    vc->LoadFromText("node 1 OnUpdate\nentry OnUpdate 1\n");
    auto* sc = player->AddComponent<ScriptComponent>("okayscript");
    sc->LoadSource("var gold = 5\nvar title = \"hero\"\nfunction start() {}\n");
    GameObject* lamp = lvl.CreateGameObject("Lamp");
    lamp->AddComponent<Light>()->intensity = 2.0f;
    auto* ls = lamp->AddComponent<Saveable>();
    ls->saveId = "lamp";
    ls->properties = {"Light.intensity", "GameObject.tag"};
    GameObject* coin = lvl.CreateGameObject("Coin");
    coin->AddComponent<Saveable>()->saveId = "coin-1";
    GameObject* rock = lvl.CreateGameObject("Rock");
    rock->AddComponent<Saveable>()->saveId = "rock-1";
    GameObject* door = lvl.CreateGameObject("Door");
    door->AddComponent<Saveable>()->saveId = "door-1";
    door->AddComponent<SaveConfig>();
    GameObject* host = lvl.CreateGameObject("Host");            // the object the no-code route tests work on
    host->AddComponent<Saveable>()->saveId = "host";
    auto* hal = host->AddComponent<ActionList>();
    hal->trigger = ActionList::Trigger::OnMessage;
    hal->triggerKey = "go";
    GameObject* free = lvl.CreateGameObject("Untracked");      // no Saveable: not stored
    free->transform->localPosition = Vec3{9, 9, 9};
    SceneSerializer::SaveToFile(lvl, file.string());
}

void Frames(Scene& s, int n = 4) { for (int i = 0; i < n; ++i) s.Update(0.016f); }

} // namespace

int main() {
    RUN_SUITE("save_nocode");
    CaptureLog();
    const fs::path root = fs::temp_directory_path() / "okay_test_save_nocode";
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path levelFile = root / "Level1.okayscene";
    std::string err;
    WriteLevel(levelFile);
    SceneManager::ClearScenes();
    SceneManager::AddScene(levelFile.string());

    SaveStore store((root / "saves").string());
    store.SetClock(Clock);
    SaveManager mgr(store);
    GameSaves::UseManager(&mgr);
    ActionList::ResetVars();

    // ---- the scene file keeps Saveable (flags, id, properties) and Save Settings ----
    {
        Scene s;
        std::string lerr;
        CHECK(SceneSerializer::LoadFromFile(s, levelFile.string(), &lerr));
        auto* ls = s.Find("Lamp")->GetComponent<Saveable>();
        CHECK(ls && ls->saveId == "lamp" && ls->properties.size() == 2 && ls->properties[1] == "GameObject.tag");
        auto* ps = s.Find("Player")->GetComponent<Saveable>();
        CHECK(ps && ps->saveVariables && ps->saveSceneVariables && ps->saveTransform && ps->saveActive);
        CHECK(s.Find("Door")->GetComponent<SaveConfig>() != nullptr);
        Scene t;
        GameObject* g = t.CreateGameObject("X");
        auto* sv = g->AddComponent<Saveable>();
        sv->dynamic = true; sv->prefab = "factory:Torch"; sv->saveActive = false; sv->saveId = "has space \"q\"";
        auto* ss = g->AddComponent<SaveConfig>();
        ss->autosaveInterval = 30.5f; ss->autosaveOnSceneStart = true; ss->autosaveSlots = 5;
        Scene u;
        CHECK(SceneSerializer::Deserialize(u, SceneSerializer::Serialize(t), &lerr));
        auto* sv2 = u.Find("X")->GetComponent<Saveable>();
        CHECK(sv2 && sv2->dynamic && sv2->prefab == "factory:Torch" && !sv2->saveActive && sv2->saveId == sv->saveId);
        auto* ss2 = u.Find("X")->GetComponent<SaveConfig>();
        CHECK(ss2 && ss2->autosaveInterval == 30.5f && ss2->autosaveOnSceneStart && ss2->autosaveSlots == 5);
    }

    // ---- save, mutate, load: exact state back, spawned objects recreated, destroyed ones stay destroyed ----
    {
        const fs::path prefab = root / "bullet.okayprefab";
        {
            Scene p("prefab");
            GameObject* b = p.CreateGameObject("Bullet");
            b->tag = "projectile";
            CHECK(SceneSerializer::SaveObjectToFile(*b, prefab.string()));
        }
        Scene game;
        CHECK(SceneSerializer::LoadFromFile(game, levelFile.string(), &err));
        game.Start();
        Frames(game, 2);
        auto setup = [&](Scene& s) {
            s.Find("Player")->GetComponent<VisualScriptComponent>()->SetVariable("score", vs::VsValue{42.0f});
            s.Find("Player")->GetComponent<ScriptComponent>()->VM()->SetGlobal("gold", vs::VsValue{77.0f});
            ActionList::UseScene(&s);
            ActionList::Vars()["lives"] = 3.0f;
            ActionList::StrVars()["name"] = "Ann";
            vs::SceneVariables(&s)["sv"] = vs::VsValue{std::string("sceneval")};
        };
        setup(game);
        game.Find("Player")->transform->localPosition = Vec3{4.5f, 1.25f, -3.0f};
        game.Find("Coin")->active = false;
        game.Find("Lamp")->GetComponent<Light>()->intensity = 7.5f;
        game.Find("Lamp")->tag = "lit";
        game.Find("Untracked")->transform->localPosition = Vec3{5, 5, 5};
        game.Destroy(game.Find("Rock"));
        Frames(game, 1);
        CHECK(game.Find("Rock") == nullptr);
        GameObject* bullet = SaveSystem::Spawn(game, prefab.string(), Vec3{1, 2, 3}, &err);
        CHECK(bullet != nullptr);
        Frames(game, 1);

        g_now += 60000;
        CHECK(GameSaves::Save(game, "slot_1", "Before the boss", &err));
        CHECK(GameSaves::Exists("slot_1") && GameSaves::Label("slot_1") == "Before the boss");

        // Mutate everything.
        game.Find("Player")->transform->localPosition = Vec3{-9, -9, -9};
        game.Find("Player")->GetComponent<VisualScriptComponent>()->SetVariable("score", vs::VsValue{1.0f});
        game.Find("Player")->GetComponent<ScriptComponent>()->VM()->SetGlobal("gold", vs::VsValue{0.0f});
        ActionList::Vars()["lives"] = 0.0f;
        ActionList::StrVars()["name"] = "Bob";
        vs::SceneVariables(&game)["sv"] = vs::VsValue{std::string("other")};
        game.Find("Coin")->active = true;
        game.Find("Lamp")->GetComponent<Light>()->intensity = 0.5f;
        game.Find("Lamp")->tag = "dark";
        game.Destroy(game.Find("Door"));
        SaveSystem::Spawn(game, prefab.string(), Vec3{8, 8, 8}, &err);
        Frames(game, 1);

        CHECK(GameSaves::Load(game, "slot_1", &err));
        CHECK(GameSaves::Restoring());                       // it happens at the start of the next frame
        Frames(game, 4);
        CHECK(!GameSaves::Restoring());
        GameObject* player = game.Find("Player");
        CHECK(player && Near(player->transform->localPosition, Vec3{4.5f, 1.25f, -3.0f}));
        CHECK(game.Find("Coin") && !game.Find("Coin")->active);
        CHECK(game.Find("Rock") == nullptr);                 // destroyed before the save: stays destroyed after a reload
        CHECK(game.Find("Door") != nullptr);                 // destroyed after the save: back
        CHECK(game.Find("Lamp")->GetComponent<Light>()->intensity == 7.5f && game.Find("Lamp")->tag == "lit");
        CHECK(Near(game.Find("Untracked")->transform->localPosition, Vec3{9, 9, 9}));   // not Saveable: the level's value
        CHECK(player->GetComponent<VisualScriptComponent>()->GetVariable("score").AsFloat() == 42.0f);
        CHECK(player->GetComponent<ScriptComponent>()->VM()->GetGlobal("gold").AsFloat() == 77.0f);
        ActionList::UseScene(&game);
        CHECK(ActionList::Vars()["lives"] == 3.0f && ActionList::StrVars()["name"] == "Ann");
        CHECK(vs::SceneVariables(&game)["sv"].AsString() == "sceneval");
        CHECK(Count(game, "Bullet") == 1);                   // the one made before the save, not the one after
        GameObject* b2 = game.Find("Bullet");
        CHECK(b2 && Near(b2->transform->localPosition, Vec3{1, 2, 3}) && b2->tag == "projectile");
        CHECK(b2->GetComponent<Saveable>() && b2->GetComponent<Saveable>()->dynamic);

        // A second round trip is stable: save the restored game, wreck it, load it again.
        CHECK(GameSaves::Save(game, "slot_2", "Again", &err));
        game.Find("Player")->transform->localPosition = Vec3{0, 0, 0};
        game.Destroy(game.Find("Coin"));
        Frames(game, 1);
        CHECK(GameSaves::Load(game, "slot_2", &err));
        Frames(game, 4);
        CHECK(Near(game.Find("Player")->transform->localPosition, Vec3{4.5f, 1.25f, -3.0f}));
        CHECK(game.Find("Coin") && !game.Find("Coin")->active && Count(game, "Bullet") == 1);
        CHECK(game.Find("Rock") == nullptr);

        // A load into another Scene object that is blank (Continue from a main menu).
        Scene menu("MainMenu");
        CHECK(GameSaves::Continue(menu, &err));
        Frames(menu, 4);
        CHECK(menu.Name() == "Level1" && menu.Find("Player") && Near(menu.Find("Player")->transform->localPosition, Vec3{4.5f, 1.25f, -3.0f}));
        CHECK(menu.Find("Rock") == nullptr && Count(menu, "Bullet") == 1);
    }

    // ---- slot management ----
    {
        Scene s;
        CHECK(SceneSerializer::LoadFromFile(s, levelFile.string(), &err));
        s.Start();
        Frames(s, 1);
        GameSaves::Delete("slot_2");
        CHECK(GameSaves::Delete("slot_1") && !GameSaves::Exists("slot_1"));
        CHECK(!GameSaves::Delete("slot_1"));                         // nothing to delete
        CHECK(GameSaves::SavedText("slot_1").empty() && GameSaves::SavedAt("slot_1") == 0 && GameSaves::SecondsSinceSave("slot_1") < 0);
        g_now = 1700000000000LL;
        CHECK(GameSaves::Save(s, "slot_3", "", &err));
        CHECK(GameSaves::Label("slot_3") == "Saved game");
        CHECK(GameSaves::SavedText("slot_3") == "2023-11-14 22:13");
        CHECK(GameSaves::SavedAt("slot_3") == 1700000000LL && GameSaves::SecondsSinceSave("slot_3") > 0.0);
        g_now += 120000;
        CHECK(GameSaves::Save(s, "slot_4", "Later", &err));
        CHECK(GameSaves::ContinueSlot() == "slot_4" && GameSaves::CanContinue());
        CHECK(GameSaves::Delete("slot_4") && GameSaves::ContinueSlot() == "slot_3");
        CHECK(!GameSaves::Save(s, "bad slot!", "x", &err) && Contains(err, "valid slot name"));
        CHECK(!GameSaves::Load(s, "../x", &err) && Contains(err, "valid slot name"));
        for (const char* slot : {"slot_2", "slot_3"}) GameSaves::Delete(slot);
        CHECK(!GameSaves::CanContinue() && GameSaves::ContinueSlot().empty());
    }

    // ---- missing, corrupt and newer-version slots: the Console says why, the scene is untouched ----
    {
        Scene s;
        CHECK(SceneSerializer::LoadFromFile(s, levelFile.string(), &err));
        s.Start();
        Frames(s, 1);
        s.Find("Player")->transform->localPosition = Vec3{3, 3, 3};
        g_errors.clear();
        CHECK(!GameSaves::Load(s, "ghost", &err) && Contains(err, "no save in slot"));
        CHECK(ErrorsMention("[save]") && ErrorsMention("ghost"));
        CHECK(!GameSaves::Restoring());

        CHECK(GameSaves::Save(s, "slot_c", "to damage", &err));
        std::string bytes = ReadText(store.SlotPath("slot_c"));
        bytes[bytes.size() - 20] ^= 0x01;
        WriteText(store.SlotPath("slot_c"), bytes);
        g_errors.clear();
        CHECK(!GameSaves::Load(s, "slot_c", &err));
        CHECK(!err.empty() && ErrorsMention("slot_c"));
        CHECK(!GameSaves::Restoring());
        CHECK(Near(s.Find("Player")->transform->localPosition, Vec3{3, 3, 3}));
        CHECK(!GameSaves::CanContinue());                             // a damaged slot is never "continue"
        g_errors.clear();
        CHECK(!GameSaves::Continue(s, &err) && ErrorsMention("Couldn't continue"));

        SaveStore future(store.Directory());
        future.SetClock(Clock);
        future.SetDataVersion(9);
        SaveDocument fdoc;
        fdoc.Writer("scene").Int("format", 1);
        CHECK(future.Write("slot_f", fdoc, SaveSlotInfo(), &err));
        g_errors.clear();
        CHECK(!GameSaves::Load(s, "slot_f", &err) && Contains(err, "newer version"));
        CHECK(ErrorsMention("newer version"));
        CHECK(!GameSaves::CanContinue());
        g_errors.clear();
        CHECK(!GameSaves::Continue(s, &err));
        CHECK(Contains(err, "slot_f") || Contains(err, "slot_c"));
        GameSaves::Delete("slot_c"); GameSaves::Delete("slot_f");
        fs::remove(store.SlotPath("slot_c") + ".bak");
    }

    // ---- versioned format with a migration hook ----
    {
        Scene s;
        CHECK(SceneSerializer::LoadFromFile(s, levelFile.string(), &err));
        s.Start();
        Frames(s, 1);
        s.Find("Player")->transform->localPosition = Vec3{6, 0, 0};
        CHECK(GameSaves::Save(s, "slot_m", "old", &err));             // data version 1
        SaveStore v2(store.Directory());
        v2.SetClock(Clock);
        v2.SetDataVersion(2);
        bool ran = false;
        v2.RegisterMigration(1, [&](SaveDocument& d, std::string*) { ran = true; d.Writer("game").Int("migrated", 1); return true; });
        SaveManager mgr2(v2);
        GameSaves::UseManager(&mgr2);
        s.Find("Player")->transform->localPosition = Vec3{0, 0, 0};
        CHECK(GameSaves::Load(s, "slot_m", &err));
        Frames(s, 4);
        CHECK(ran && Near(s.Find("Player")->transform->localPosition, Vec3{6, 0, 0}));
        SaveDocument got;
        CHECK(v2.Read("slot_m", got, nullptr, &err) && got.Find("game") && got.Find("game")->GetInt("migrated") == 1);
        GameSaves::UseManager(&mgr);
        GameSaves::Delete("slot_m");
    }

    // ---- autosave: interval and scene start, from a Save Settings component ----
    {
        Scene s;
        CHECK(SceneSerializer::LoadFromFile(s, levelFile.string(), &err));
        auto* ss = s.Find("Door")->GetComponent<SaveConfig>();
        ss->autosaveInterval = 10.0f;
        ss->autosaveOnSceneStart = true;
        ss->autosaveSlots = 2;
        s.Start();
        CHECK(!GameSaves::Exists("autosave_1"));
        s.Update(1.0f);                                               // the scene-start autosave lands next frame
        CHECK(GameSaves::Exists("autosave_1") && !GameSaves::Exists("autosave_2"));
        CHECK(GameSaves::Label("autosave_1") == "Autosave");
        CHECK(GameSaves::Settings().autosaveSlots == 2 && GameSaves::Settings().autosaveOnSceneStart);
        g_now += 1000;
        for (int i = 0; i < 8; ++i) s.Update(1.0f);
        CHECK(!GameSaves::Exists("autosave_2"));                      // 9 s since the start: not yet
        s.Update(1.5f);
        CHECK(GameSaves::Exists("autosave_2"));                       // the interval ran out
        g_now += 1000;
        const long long before = GameSaves::SavedAt("autosave_1");
        for (int i = 0; i < 11; ++i) s.Update(1.0f);
        CHECK(!GameSaves::Exists("autosave_3"));                      // a ring of 2: the oldest is replaced
        CHECK(GameSaves::SavedAt("autosave_1") > before);
        CHECK(GameSaves::ContinueSlot() == "autosave_1");

        // Not while a load is putting a save back.
        Scene t;
        CHECK(SceneSerializer::LoadFromFile(t, levelFile.string(), &err));
        t.Find("Door")->GetComponent<SaveConfig>()->autosaveInterval = 1.0f;
        t.Start();
        Frames(t, 1);
        CHECK(GameSaves::Load(t, "autosave_1", &err));
        const long long mark = GameSaves::SavedAt("autosave_2");
        g_now += 5000;
        t.Update(2.0f);                                               // restoring: the interval autosave is held back
        CHECK(GameSaves::SavedAt("autosave_2") == mark);
        Frames(t, 3);
        for (const char* slot : {"autosave_1", "autosave_2"}) GameSaves::Delete(slot);
    }

    // ---- reachable without C++: script builtins, visual-script nodes, Action List actions ----
    {
        // Text script.
        {
            Scene s;
            CHECK(SceneSerializer::LoadFromFile(s, levelFile.string(), &err));
            GameObject* host = s.CreateGameObject("ScriptHost");
            host->AddComponent<ScriptComponent>("okayscript")->LoadSource(
                "function start() {\n"
                "  game_save(\"slot_s\", \"From script\");\n"
                "  ui_probe = game_slot_exists(\"slot_s\") + game_slot_exists(\"nope\") * 10;\n"
                "  label = game_slot_label(\"slot_s\");\n"
                "  when = game_slot_text(\"slot_s\");\n"
                "  age = game_slot_age(\"slot_s\");\n"
                "  can = game_can_continue();\n"
                "  gone = game_delete_slot(\"slot_s\");\n"
                "  exists_after = game_slot_exists(\"slot_s\");\n"
                "  game_save(\"slot_s\", \"Again\");\n"
                "  game_autosave();\n"
                "}\n");
            s.Start();
            Frames(s, 1);
            auto* vm = host->GetComponent<ScriptComponent>()->VM();
            CHECK(vm->GetGlobal("ui_probe").AsFloat() == 1.0f);
            CHECK(vm->GetGlobal("label").AsString() == "From script");
            CHECK(Contains(vm->GetGlobal("when").AsString(), "-") && vm->GetGlobal("age").AsFloat() >= 0.0f);
            CHECK(vm->GetGlobal("can").AsFloat() == 1.0f && vm->GetGlobal("gone").AsFloat() == 1.0f);
            CHECK(vm->GetGlobal("exists_after").AsFloat() == 0.0f);
            CHECK(GameSaves::Exists("slot_s") && GameSaves::Exists("autosave_1"));
            GameSaves::Delete("slot_s"); GameSaves::Delete("autosave_1");
            // A script that loads: the level's Host object goes back to where it was saved.
            GameObject* h = s.Find("Host");
            h->transform->localPosition = Vec3{1, 2, 3};
            Frames(s, 1);
            CHECK(GameSaves::Save(s, "slot_s", "with host", &err));
            h->transform->localPosition = Vec3{0, 0, 0};
            h->AddComponent<ScriptComponent>("okayscript")->LoadSource("function start() { loaded = game_load(\"slot_s\") }\n");
            Frames(s, 5);
            CHECK(s.Find("Host") && Near(s.Find("Host")->transform->localPosition, Vec3{1, 2, 3}));
            GameSaves::Delete("slot_s");
        }
        // Visual script: Fn.game_save / Fn.game_slot_exists / Fn.game_delete_slot nodes.
        {
            Scene s; s.physicsEnabled = false;
            GameObject* go = s.CreateGameObject("Graph");
            auto* vc = go->AddComponent<VisualScriptComponent>();
            CHECK(vs::IsBuiltinNode("Fn.game_save") && vs::IsBuiltinNode("Fn.game_load") && vs::IsBuiltinNode("Fn.game_continue") &&
                  vs::IsBuiltinNode("Fn.game_delete_slot") && vs::IsBuiltinNode("Fn.game_slot_exists") &&
                  vs::IsBuiltinNode("Fn.game_slot_text") && vs::IsBuiltinNode("Fn.game_can_continue") && vs::IsBuiltinNode("Fn.game_autosave"));
            CHECK(vc->LoadFromText("node 1 OnUpdate\nentry OnUpdate 1\nnode 2 Fn.game_save\n"
                                   "default 2 0 \"slot_v\"\ndefault 2 1 \"From graph\"\nexec 1 0 2\n", &err));
            s.Start();
            s.Update(0.016f);
            CHECK(GameSaves::Exists("slot_v") && GameSaves::Label("slot_v") == "From graph");
            CHECK(vc->LoadFromText("node 1 OnUpdate\nentry OnUpdate 1\nnode 2 Fn.game_delete_slot\n"
                                   "default 2 0 \"slot_v\"\nexec 1 0 2\n", &err));
            s.Update(0.016f);
            CHECK(!GameSaves::Exists("slot_v"));
        }
        // Action List actions (the level's Host object has an Action List that runs on the message "go").
        {
            Scene s;
            CHECK(SceneSerializer::LoadFromFile(s, levelFile.string(), &err));
            s.Start();
            Frames(s, 1);
            auto run = [&](std::vector<ActionList::Item> items) {
                auto* al = s.Find("Host")->GetComponent<ActionList>();
                al->instructions = std::move(items);
                al->ReceiveMessage("go");
                Frames(s, 4);
            };
            run({
                {"game_save", {"slot_a", "From", "actions"}},
                {"game_slot_exists", {"slot_a", "has_a"}},
                {"game_slot_exists", {"slot_zz", "has_zz"}},
                {"game_slot_text", {"slot_a", "when_a"}},
                {"game_slot_age", {"slot_a", "age_a"}},
                {"game_can_continue", {"can_a"}},
            });
            ActionList::UseScene(&s);
            CHECK(ActionList::Vars()["has_a"] == 1.0f && ActionList::Vars()["has_zz"] == 0.0f);
            CHECK(Contains(ActionList::StrVars()["when_a"], "-") && ActionList::Vars()["age_a"] >= 0.0f);
            CHECK(ActionList::Vars()["can_a"] == 1.0f);
            CHECK(GameSaves::Label("slot_a") == "From actions");
            s.Find("Host")->transform->localPosition = Vec3{2, 2, 2};
            run({{"game_save", {"slot_a"}}});
            s.Find("Host")->transform->localPosition = Vec3{7, 7, 7};
            run({{"game_load", {"slot_a"}}});
            CHECK(Near(s.Find("Host")->transform->localPosition, Vec3{2, 2, 2}));
            run({{"game_continue", {}}, {"game_autosave", {}}, {"game_delete_slot", {"slot_a"}}});
            CHECK(!GameSaves::Exists("slot_a") && GameSaves::Exists("autosave_1"));
            GameSaves::Delete("autosave_1");
        }
    }

    // ---- the Save Slots screen: its buttons do what they say ----
    {
        Scene s;
        CHECK(SceneSerializer::LoadFromFile(s, levelFile.string(), &err));
        SceneManager::ClearScenes();                                  // not in the build list: a load puts the state on this scene, UI included
        GameObject* screen = UITemplates::AddSaveSlots(s);
        CHECK(screen && screen->name == "Save Slots");
        s.Start();
        Frames(s, 2);
        auto text = [&](const char* name) { return s.Find(name)->GetComponent<TextRenderer>()->text; };
        CHECK(text("Slot1Info") == "Empty");
        auto click = [&](const char* name) {
            GameObject* b = s.Find(name);
            CHECK(b != nullptr);
            if (!b) return;
            if (auto* bt = b->GetComponent<UIButton>()) bt->Press();   // fires the sibling script's on_click
            Frames(s, 1);
        };
        click("Slot1Save");
        CHECK(GameSaves::Exists("slot_1") && GameSaves::Label("slot_1") == "Slot 1");
        Frames(s, 2);
        CHECK(Contains(text("Slot1Info"), "Slot 1") && text("Slot2Info") == "Empty");
        CHECK(text("SaveStatus") == "Saved to slot 1");
        s.Find("Player")->transform->localPosition = Vec3{3, 3, 3};
        click("Slot1Load");
        Frames(s, 4);
        CHECK(Near(s.Find("Player")->transform->localPosition, Vec3{1, 0, 0}));
        click("Slot2Load");                                           // empty slot: a clear status, a Console error
        CHECK(Contains(text("SaveStatus"), "can't be loaded"));
        click("Slot1Delete");
        CHECK(!GameSaves::Exists("slot_1"));
        click("ContinueButton");
        CHECK(Contains(text("SaveStatus"), "no save"));
        click("CloseButton");
        CHECK(!s.Find("Save Slots")->active);
    }

    GameSaves::UseManager(nullptr);
    Log::sink = nullptr;
    fs::remove_all(root);
    TEST_MAIN_RESULT();
}
