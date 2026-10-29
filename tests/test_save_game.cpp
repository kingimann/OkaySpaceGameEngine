#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/VisualScript/VsApi.hpp"
#include <fstream>
#include <filesystem>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

namespace {

int CountItems(GridInventory* inv) {
    int n = 0;
    for (const auto& it : inv->items) n += it.count;
    return n;
}

// A small game: a player that moves, carries a bag and has a script score;
// a coin; a falling crate; a counter graph.
void Build(Scene& s) {
    GameObject* player = s.CreateGameObject("Player");
    player->transform->SetPosition(Vec3{1.0f, 2.0f, 3.0f});
    auto* bag = player->AddComponent<GridInventory>();
    bag->AddItem("Apple", 1, 1, 3);
    auto* sc = player->AddComponent<ScriptComponent>("okayscript");
    sc->LoadSource("var score = 0\nvar title = \"start\"\nfunction start() {}\n");
    GameObject* hand = s.CreateGameObject("Hand");
    hand->transform->SetParent(player->transform, false);
    s.CreateGameObject("Coin");
    s.CreateGameObject("Coin");   // two coins: paths Coin and Coin#1
    GameObject* crate = s.CreateGameObject("Crate");
    auto* rb = crate->AddComponent<Rigidbody3D>();
    rb->gravityScale = 0.0f;
    auto* graph = s.CreateGameObject("Counter")->AddComponent<VisualScriptComponent>();
    graph->LoadFromText("okayvs 1\n");
    s.Start();
}

} // namespace

int main() {
    RUN_SUITE("save_game");
    const fs::path dir = fs::temp_directory_path() / "okay_test_save_game";
    fs::remove_all(dir);
    fs::create_directories(dir);
    Save::SetBaseDir(dir.string());

    // ---- Save, change everything, load: it all comes back ----
    {
        Scene s("Level 1");
        Build(s);
        GameObject* player = s.Find("Player");
        auto* sc = player->GetComponent<ScriptComponent>();
        sc->VM()->SetGlobal("score", vs::VsValue{42.0f});
        sc->VM()->SetGlobal("title", vs::VsValue{std::string("Hero of the Mine")});
        s.Find("Counter")->GetComponent<VisualScriptComponent>()->SetVariable("laps", vs::VsValue{3.0f});
        s.Find("Crate")->GetComponent<Rigidbody3D>()->velocity = Vec3{0.0f, 4.0f, 1.0f};
        s.Destroy(s.Find("Coin"));                         // the first coin was picked up
        s.FlushDestroyed();
        GameObject* spawned = s.CreateGameObject("Torch"); // something placed at runtime
        spawned->transform->SetPosition(Vec3{7.0f, 0.0f, 0.0f});
        s.Update(0.0f);
        Save::SetInt("gold", 120);

        std::string err;
        CHECK(SaveGame::Save(s, "1", "Chapter 1", &err));
        CHECK(err.empty());
        CHECK(SaveGame::Exists("1"));
        const SaveSlotInfo info = SaveGame::Info("1");
        CHECK(info.exists);
        CHECK(info.label == "Chapter 1");
        CHECK(info.scene == "Level 1");
        CHECK(info.savedAt > 0);

        // Play on: move, spend, change everything.
        player->transform->SetPosition(Vec3{50.0f, 0.0f, 0.0f});
        player->GetComponent<GridInventory>()->AddItem("Rock", 1, 1, 5);
        sc->VM()->SetGlobal("score", vs::VsValue{1.0f});
        s.Find("Counter")->GetComponent<VisualScriptComponent>()->SetVariable("laps", vs::VsValue{99.0f});
        s.Destroy(s.Find("Torch"));
        s.FlushDestroyed();
        Save::SetInt("gold", 0);
        Save::SetInt("junk", 1);

        CHECK(SaveGame::LoadNow(s, "1", &err));
        player = s.Find("Player");
        CHECK(player != nullptr);
        if (player) {
            CHECK_NEAR(player->transform->Position().x, 1.0f, 1e-4);
            CHECK_NEAR(player->transform->Position().z, 3.0f, 1e-4);
            CHECK(player->transform->Children().size() == 1);
            auto* bag = player->GetComponent<GridInventory>();
            CHECK(bag && CountItems(bag) == 3);
            auto* sc2 = player->GetComponent<ScriptComponent>();
            CHECK(sc2 && sc2->VM());
            if (sc2 && sc2->VM()) {
                CHECK_NEAR(sc2->VM()->GetGlobal("score").AsFloat(), 42.0f, 1e-4);
                CHECK(sc2->VM()->GetGlobal("title").AsString() == "Hero of the Mine");
            }
        }
        CHECK(s.Find("Torch") != nullptr);                // the runtime object came back
        int coins = 0;
        for (const auto& o : s.Objects()) if (o && o->name == "Coin") ++coins;
        CHECK(coins == 1);                                // the picked-up coin stays gone
        auto* crate = s.Find("Crate") ? s.Find("Crate")->GetComponent<Rigidbody3D>() : nullptr;
        CHECK(crate && crate->velocity.y > 3.9f && crate->velocity.z > 0.9f);
        auto* counter = s.Find("Counter") ? s.Find("Counter")->GetComponent<VisualScriptComponent>() : nullptr;
        CHECK(counter && counter->GetVariable("laps").AsFloat() == 3.0f);
        CHECK(Save::GetInt("gold") == 120);               // the game's own save keys too
        CHECK(!Save::Has("junk"));
    }

    // ---- Loading from a script happens at the end of the frame ----
    {
        Scene s("Level 1");
        Build(s);
        s.Find("Player")->transform->SetPosition(Vec3{-9.0f, 0.0f, 0.0f});
        CHECK(SaveGame::Load(s, "1"));
        CHECK_NEAR(s.Find("Player")->transform->Position().x, -9.0f, 1e-4);   // not yet
        s.Update(0.016f);
        CHECK_NEAR(s.Find("Player")->transform->Position().x, 1.0f, 1e-4);
        CHECK(s.Find("Torch") != nullptr);
    }

    // ---- Script calls ----
    {
        Scene s("Script");
        GameObject* g = s.CreateGameObject("Saver");
        auto* sc = g->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "var lives = 3\n"
            "function save_it() { return save_game(\"quick\", \"Quick save\") }\n"
            "function has_it() { return has_save(\"quick\") }\n"
            "function label_it() { return save_label(\"quick\") }\n"
            "function load_it() { return load_game(\"quick\") }\n"));
        s.Start();
        CHECK(sc->VM()->CallEventArgs("save_it", {}).AsFloat() == 1.0f);
        CHECK(sc->VM()->CallEventArgs("has_it", {}).AsFloat() == 1.0f);
        CHECK(sc->VM()->CallEventArgs("label_it", {}).AsString() == "Quick save");
        sc->VM()->SetGlobal("lives", vs::VsValue{1.0f});
        CHECK(sc->VM()->CallEventArgs("load_it", {}).AsFloat() == 1.0f);
        s.Update(0.016f);
        auto* back = s.Find("Saver") ? s.Find("Saver")->GetComponent<ScriptComponent>() : nullptr;
        CHECK(back && back->VM() && back->VM()->GetGlobal("lives").AsFloat() == 3.0f);
        CHECK(vs::VsApiFind("SaveGame.Save/2") != nullptr);
        CHECK(vs::VsApiFind("SaveGame.Load/1") != nullptr);
        CHECK(vs::VsApiFind("SaveGame.Exists/1") != nullptr);
    }

    // ---- Slots: names, listing, deleting, missing saves ----
    {
        Scene s("x");
        s.Start();
        CHECK(!SaveGame::ValidSlot("../evil"));
        CHECK(!SaveGame::ValidSlot("a b"));
        CHECK(!SaveGame::ValidSlot(""));
        CHECK(SaveGame::ValidSlot("slot_2-b"));
        std::string err;
        CHECK(!SaveGame::Save(s, "../evil", "", &err));
        CHECK(!err.empty());
        CHECK(!SaveGame::Load(s, "nothing-here", &err));
        CHECK(!SaveGame::Exists("nothing-here"));
        const auto list = SaveGame::List();
        CHECK(list.size() == 2);                          // "1" and "quick"
        CHECK(SaveGame::Delete("quick"));
        CHECK(!SaveGame::Exists("quick"));
        CHECK(SaveGame::List().size() == 1);
        // A damaged file is refused, not half-loaded.
        { std::ofstream f(SaveGame::SlotPath("bad")); f << "hello\n"; }
        CHECK(!SaveGame::LoadNow(s, "bad", &err));
    }

    // ---- Auto Save: on start and every N seconds, kept through scene files ----
    {
        Scene s("auto");
        auto* as = s.CreateGameObject("Saver")->AddComponent<AutoSave>();
        as->interval = 1.0f; as->slot = "auto1"; as->saveOnStart = false;
        s.Start();
        s.Update(0.6f);
        CHECK(!SaveGame::Exists("auto1"));
        s.Update(0.6f);
        CHECK(SaveGame::Exists("auto1"));
        CHECK(SaveGame::Info("auto1").label == "Autosave");
        Scene back;
        CHECK(SceneSerializer::Deserialize(back, SceneSerializer::Serialize(s)));
        auto* as2 = back.Find("Saver") ? back.Find("Saver")->GetComponent<AutoSave>() : nullptr;
        CHECK(as2 && as2->slot == "auto1" && as2->interval == 1.0f);
    }

    Save::SetBaseDir("");
    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
