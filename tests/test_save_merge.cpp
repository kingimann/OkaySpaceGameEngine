// One store for every whole-game save (Grok #422 item 1). SaveGame (`save_game`, the C# SaveGame API, the
// AutoSave component) used to write <slot>.okaygame files that the save framework (GameSaves / SaveManager /
// SaveStore, `game_save`, Continue, the Save Slots screen) never saw. Now SaveGame is the framework's whole-scene
// capture mode: its saves are .okayslot files in the same store, and older .okaygame files still load, list
// and count for Continue.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/GameSaves.hpp"
#include "okay/Components/AutoSave.hpp"
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
        // A slot saved with GameSaves is seen by the SaveGame names (has_save, save_label, load_game).
        Scene g("Level");
        Build(g);
        CHECK(GameSaves::Save(g, "two", "Second", &err));
        CHECK(SaveGame::Exists("two") && SaveGame::Info("two").label == "Second");
        CHECK(Listed(SaveGame::List(), "two") && Listed(SaveGame::List(), "one"));
        CHECK(SaveGame::Delete("two") && !GameSaves::Exists("two"));
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
        CHECK(SaveGame::PlayTime() >= 29.5f && SaveSystem::PlayTime() >= 29.5f);   // one play-time clock
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

    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
