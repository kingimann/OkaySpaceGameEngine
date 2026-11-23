// One store for every whole-game save (Grok #422 item 1). SaveGame (`save_game`, the C# SaveGame API, the
// AutoSave component) used to write <slot>.okaygame files that the save framework (GameSaves / SaveManager /
// SaveStore, `game_save`, Continue, the Save Slots screen) never saw. Now SaveGame is the framework's whole-scene
// capture mode: its saves are .okayslot files in the same store, and older .okaygame files still load, list
// and count for Continue.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/GameSaves.hpp"
#include "okay/Components/AutoSave.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

namespace {

void WriteText(const fs::path& p, const std::string& s) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << s;
}
bool Near(const Vec3& a, const Vec3& b) { return (a - b).SqrMagnitude() < 1e-6f; }

void Build(Scene& s) {
    GameObject* player = s.CreateGameObject("Player");
    player->transform->SetPosition(Vec3{1.0f, 2.0f, 3.0f});
    GameObject* crate = s.CreateGameObject("Crate");
    crate->AddComponent<Rigidbody3D>()->gravityScale = 0.0f;
    s.Start();
}

// A save exactly as SaveGame wrote it before the merge (format 2: header, scene text, state, end marker, checksum).
std::string LegacyFile(Scene& s, const std::string& label, long long savedAt, float playTime) {
    std::string t = "okaygame 2\nlabel\t" + label + "\nscene\t" + s.Name() + "\nsavedAt\t" + std::to_string(savedAt) +
                    "\nplayTime\t" + savefmt::FormatFloat(playTime) + "\n--scene--\n" + SceneSerializer::Serialize(s) +
                    "--state--\nbody3\tCrate\t0 4 1\nkv\tcoins\ti:7\n--end--\n";
    t += "checksum\t" + savefmt::Hex16(savefmt::Fnv1a(t.data(), t.size())) + "\n";
    return t;
}

bool Contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
int Count(const std::string& s, const std::string& part) {
    int n = 0;
    for (std::size_t p = s.find(part); p != std::string::npos; p = s.find(part, p + 1)) ++n;
    return n;
}
vs::VsValue RunScript(const std::string& src) {
    auto vm = CreateScriptVM("okayscript");
    std::string err;
    if (!vm->Load(src, &err)) { std::printf("  script error: %s\n", err.c_str()); return vs::VsValue{}; }
    return vm->GetGlobal("r");
}

bool Listed(const std::vector<SaveSlotInfo>& list, const std::string& slot) {
    for (const SaveSlotInfo& i : list) if (i.slot == slot) return true;
    return false;
}

} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "okay_test_save_merge";
    fs::remove_all(dir);
    fs::create_directories(dir);
    Save::SetBaseDir(dir.string());
    Log::console = false;
    SaveStore& store = GameSaves::Manager().Store();
    const fs::path saves = store.Directory();
    std::string err;

    RUN_SUITE("SaveGame saves land in the framework's store");
    {
        Scene s("Level");
        Build(s);
        CHECK(SaveGame::Save(s, "one", "First", &err));
        CHECK(store.Exists("one") && GameSaves::Exists("one"));
        CHECK(GameSaves::Label("one") == "First");
        CHECK(GameSaves::ContinueSlot() == "one");                       // Continue sees it
        CHECK(Listed(GameSaves::Manager().ListSlots(), "one"));          // so does the Save Slots screen
        CHECK(fs::exists(saves / "one.okayslot") && !fs::exists(saves / "one.okaygame"));
        CHECK(SaveGame::SlotPath("one") == store.SlotPath("one"));
        // The whole scene comes back through the framework's own load.
        s.Find("Player")->transform->SetPosition(Vec3{9, 9, 9});
        s.Find("Crate")->GetComponent<Rigidbody3D>()->velocity = Vec3{0, 0, 0};
        CHECK(GameSaves::Manager().Load(s, "one", {}, &err));
        CHECK(s.Find("Player") && Near(s.Find("Player")->transform->Position(), Vec3{1, 2, 3}));
        // A slot saved with GameSaves (objects marked Saveable, not the whole scene) is not a SaveGame slot: the
        // SaveGame names (has_save, save_valid, save_slots, load_game) answer "No save" for it, as they always did.
        Scene g("Level");
        Build(g);
        CHECK(GameSaves::Save(g, "two", "Second", &err));
        CHECK(!SaveGame::Exists("two") && !SaveGame::Info("two").exists);
        CHECK(!Listed(SaveGame::List(), "two") && Listed(SaveGame::List(), "one"));
        CHECK(!SaveGame::Check("two", &err) && Contains(err, "No save"));
        CHECK(!SaveGame::LoadNow(g, "two", &err) && Contains(err, "No save"));
        CHECK(!SaveGame::Load(g, "two", &err));
        CHECK(!SaveGame::Delete("two") && GameSaves::Exists("two"));     // not SaveGame's to delete
        CHECK(RunScript("var r = has_save(\"two\");").AsFloat() == 0.0f);
        CHECK(RunScript("var r = save_valid(\"two\");").AsFloat() == 0.0f);
        CHECK(RunScript("var r = has_save(\"one\");").AsFloat() == 1.0f);
        CHECK(GameSaves::Delete("two"));
    }

    RUN_SUITE("an older .okaygame file keeps loading and counts for Continue");
    {
        Scene s("Level");
        Build(s);
        s.Find("Player")->transform->SetPosition(Vec3{5, 6, 7});
        WriteText(saves / "old.okaygame", LegacyFile(s, "Old one", 4102444800LL, 30.0f));   // newest of all
        s.Find("Player")->transform->SetPosition(Vec3{0, 0, 0});
        CHECK(store.Exists("old") && GameSaves::Exists("old"));
        const SaveSlotInfo oi = store.Info("old");
        CHECK(oi.exists && !oi.corrupt && !oi.incompatible && oi.label == "Old one" && oi.savedAt == 4102444800LL);
        CHECK(Listed(store.ListSlots(), "old"));
        CHECK(GameSaves::ContinueSlot() == "old");
        CHECK(GameSaves::Manager().Load(s, "old", {}, &err));
        CHECK(s.Find("Player") && Near(s.Find("Player")->transform->Position(), Vec3{5, 6, 7}));
        CHECK(s.Find("Crate") && Near(s.Find("Crate")->GetComponent<Rigidbody3D>()->velocity, Vec3{0, 4, 1}));
        CHECK(Save::GetInt("coins") == 7);                               // the game's own keys came back too
        CHECK(SaveGame::PlayTime() >= 29.5f);                            // SaveGame's play-time clock
        // SaveGame's own load of the old file still works.
        CHECK(SaveGame::LoadNow(s, "old", &err));
        // Saving over it writes the new format; the old file does not come back after a delete.
        CHECK(SaveGame::Save(s, "old", "Now new", &err));
        CHECK(fs::exists(saves / "old.okayslot") && !fs::exists(saves / "old.okaygame"));
        int seen = 0;
        for (const SaveSlotInfo& i : store.ListSlots()) if (i.slot == "old") ++seen;
        CHECK(seen == 1 && store.Info("old").label == "Now new");
        CHECK(SaveGame::Delete("old") && !store.Exists("old") && !SaveGame::Exists("old"));
    }

    RUN_SUITE("the AutoSave component writes to the store as an autosave");
    {
        Scene s("Level");
        Build(s);
        AutoSave* a = s.CreateGameObject("Saver")->AddComponent<AutoSave>();
        a->slot = "autosave";
        CHECK(a->SaveNow());
        const SaveSlotInfo i = store.Info("autosave");
        CHECK(i.exists && i.kind == SlotKind::Auto && i.label == "Autosave");
    }

    RUN_SUITE("a replaced or failed whole-scene load never leaves GameSaves restoring");
    {
        Scene s("Level");
        Build(s);
        const std::string level = (dir / "level.okayscene").string();
        WriteText(level, SceneSerializer::Serialize(s));
        CHECK(SaveGame::Save(s, "w1", "W", &err));
        // Two loads in one frame: the first is replaced by the second.
        GameSaves::UseManager(nullptr);
        CHECK(GameSaves::Load(s, "w1", &err) && GameSaves::Load(s, "w1", &err));
        s.Update(0.016f);
        CHECK(!GameSaves::Restoring());
        // A load, then a scene change in the same frame.
        GameSaves::UseManager(nullptr);
        CHECK(GameSaves::Load(s, "w1", &err));
        s.RequestLoad(level);
        s.Update(0.016f);
        CHECK(!GameSaves::Restoring());
        // Saved scene text that does not load.
        SaveDocument bad;
        wholescene::State st;
        st.sceneText = "not a scene\n";
        wholescene::ToDocument(st, bad);
        SaveSlotInfo meta;
        meta.label = "Broken";
        CHECK(store.Write("badtext", bad, meta, &err));
        GameSaves::UseManager(nullptr);
        CHECK(GameSaves::Load(s, "badtext", &err));
        s.Update(0.016f);
        CHECK(!GameSaves::Restoring());
        GameSaves::UseManager(nullptr);
        store.Delete("badtext");
    }

    RUN_SUITE("the first save over an older .okaygame slot keeps it as the backup");
    {
        Scene s("Level");
        Build(s);
        s.Find("Player")->transform->SetPosition(Vec3{5, 6, 7});
        WriteText(saves / "prev.okaygame", LegacyFile(s, "Before", 1000, 10.0f));
        s.Find("Player")->transform->SetPosition(Vec3{1, 1, 1});
        CHECK(SaveGame::Save(s, "prev", "After", &err));
        CHECK(fs::exists(saves / "prev.okayslot.bak") && !fs::exists(saves / "prev.okaygame"));
        WriteText(saves / "prev.okayslot", "damaged\n");
        CHECK(SaveGame::LoadNow(s, "prev", &err) && SaveGame::LastLoadUsedBackup());
        CHECK(s.Find("Player") && Near(s.Find("Player")->transform->Position(), Vec3{5, 6, 7}));
        CHECK(SaveGame::Delete("prev"));
    }

    RUN_SUITE("a slot name both older systems used keeps both saves");
    {
        Scene s("Level");
        Build(s);
        s.Find("Player")->transform->SetPosition(Vec3{2, 2, 2});
        CHECK(SaveGame::Save(s, "twin", "New twin", &err));            // saved now
        s.Find("Player")->transform->SetPosition(Vec3{8, 8, 8});
        WriteText(saves / "twin.okaygame", LegacyFile(s, "Old twin", 1000, 5.0f));   // an older save
        CHECK(Listed(store.ListSlots(), "twin") && Listed(store.ListSlots(), "twin_old_save"));
        CHECK(Listed(SaveGame::List(), "twin") && Listed(SaveGame::List(), "twin_old_save"));
        CHECK(SaveGame::Info("twin").label == "New twin");
        const SaveSlotInfo oi = SaveGame::Info("twin_old_save");
        CHECK(oi.label == "Old twin" && oi.savedAt == 1000);
        CHECK(RunScript("var r = save_label(\"twin_old_save\");").AsString() == "Old twin");
        CHECK(SaveGame::LoadNow(s, "twin_old_save", &err));
        CHECK(s.Find("Player") && Near(s.Find("Player")->transform->Position(), Vec3{8, 8, 8}));
        CHECK(SaveGame::LoadNow(s, "twin", &err));
        CHECK(s.Find("Player") && Near(s.Find("Player")->transform->Position(), Vec3{2, 2, 2}));
        CHECK(SaveGame::Delete("twin") && SaveGame::Exists("twin_old_save"));
        CHECK(SaveGame::Delete("twin_old_save"));
        // Going back to an older release: the .okaygame is the newer save, so the .okayslot moves aside.
        s.Find("Player")->transform->SetPosition(Vec3{3, 3, 3});
        CHECK(SaveGame::Save(s, "back", "Before going back", &err));
        s.Find("Player")->transform->SetPosition(Vec3{4, 4, 4});
        WriteText(saves / "back.okaygame", LegacyFile(s, "Went back", 4102444800LL, 5.0f));
        CHECK(SaveGame::Info("back").label == "Went back");
        CHECK(SaveGame::Info("back_old_save").label == "Before going back");
        CHECK(SaveGame::LoadNow(s, "back", &err) && Near(s.Find("Player")->transform->Position(), Vec3{4, 4, 4}));
        CHECK(SaveGame::Delete("back") && SaveGame::Delete("back_old_save"));
    }

    RUN_SUITE("play_time is SaveGame's own clock");
    {
        Scene s("Level");
        Build(s);
        SaveSystem::SetPlayTime(0.0f);
        CHECK(GameSaves::Manager().Save(s, "cp0", {}, &err));          // a framework save at play time 0
        WriteText(saves / "t30.okaygame", LegacyFile(s, "Thirty", 2000, 30.0f));
        CHECK(SaveGame::LoadNow(s, "t30", &err) && SaveGame::PlayTime() >= 29.5f);
        CHECK(GameSaves::Manager().Load(s, "cp0", {}, &err));          // a checkpoint / game_load
        CHECK(SaveGame::PlayTime() >= 29.5f);                            // play_time() does not go back
        store.Delete("cp0");
        SaveGame::Delete("t30");
    }

    RUN_SUITE("a game's data migrations never run on whole-scene saves");
    {
        Scene s("Level");
        Build(s);
        CHECK(SaveGame::Save(s, "mig6", "M", &err));                    // written at data version 1
        bool ran = false;
        store.SetDataVersion(3);
        store.RegisterMigration(1, [&](SaveDocument&, std::string* e) { ran = true; if (e) *e = "game step"; return false; });
        store.RegisterMigration(2, [&](SaveDocument&, std::string*) { ran = true; return true; });
        CHECK(SaveGame::LoadNow(s, "mig6", &err) && !ran);
        CHECK(SaveGame::Save(s, "mig7", "M", &err));                    // written by a build at data version 3
        store.SetDataVersion(1);
        store.ClearMigrations();
        CHECK(SaveGame::LoadNow(s, "mig7", &err) && !SaveGame::Info("mig7").incompatible);
        SaveGame::Delete("mig6");
        SaveGame::Delete("mig7");
    }

    RUN_SUITE("small points: limits, error texts");
    {
        Scene s("Level");
        Build(s);
        CHECK(SaveGame::Save(s, "lim", "L", &err));
        SaveGame::SetLimits(100, 0);
        CHECK(!SaveGame::LoadNow(s, "lim", &err) && Contains(err, "too large"));
        CHECK(GameSaves::Save(s, "fwbig", "game_save after SetLimits", &err));   // the framework's cap is its own
        SaveGame::SetLimits(0, 3);
        CHECK(!SaveGame::LoadNow(s, "lim", &err) && Contains(err, "\"lim\"") && Contains(err, "too many lines"));
        SaveGame::SetLimits(0, 0);
        fs::create_directories(saves / "dup9.okayslot.tmp");
        CHECK(!SaveGame::Save(s, "dup9", "D", &err) && Contains(err, "dup9") && Count(err, "Couldn't") == 1);
        fs::remove_all(saves / "dup9.okayslot.tmp");
        GameSaves::Delete("fwbig");
        SaveGame::Delete("lim");
    }

    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
