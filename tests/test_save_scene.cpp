// Save framework on a Scene: Saveable capture/apply keyed so objects are found again
// after a reload, dynamic objects respawned, destroyed static objects removed, the
// built-in adapters, quick save, the autosave ring, Continue and checkpoints.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/SaveFramework.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Components/Stats.hpp"
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
std::string ReadText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
bool Contains(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

long long g_now = 1700000000000LL;
long long Clock() { return g_now; }

bool Near(const Vec3& a, const Vec3& b) { return (a - b).SqrMagnitude() < 1e-8f; }

/// A component of the game's own that saves its fields.
class DoorSave : public Saveable {
public:
    bool open = false;
    int uses = 0;
    std::string note;
    int completes = 0;
    std::string SaveName() const override { return "door"; }
    void OnSave(SaveWriter& w) override { w.Bool("open", open); w.Int("uses", uses); w.String("note", note); }
    void OnLoad(SaveReader& r) override { open = r.Bool("open", open); uses = r.Int("uses", uses); note = r.String("note", note); }
    void OnLoadComplete() override { ++completes; }
};

/// A dynamic object that respawns through a factory and keeps its fuel.
class TorchSave : public Saveable {
public:
    float fuel = 1.0f;
    std::string SaveName() const override { return "torch"; }
    void OnSave(SaveWriter& w) override { w.Float("fuel", fuel); }
    void OnLoad(SaveReader& r) override { fuel = r.Float("fuel", fuel); }
};

int Count(Scene& s, const std::string& name) {
    int n = 0;
    for (const auto& o : s.Objects()) if (o && o->name == name && !o->IsDestroyed()) ++n;
    return n;
}

struct Level {
    GameObject *player, *crateA, *crateB, *coinA, *coinB, *door, *chest, *lid, *rock;
};

/// The same small level every time: what a scene reload gives back.
Level Build(Scene& s) {
    Level l{};
    s.SetName("Level1");
    s.physicsEnabled = false;                                  // bodies keep the poses the test sets
    l.player = s.CreateGameObject("Player");
    l.player->tag = "Player";
    l.player->AddComponent<Saveable>()->saveId = "player";
    l.player->AddComponent<Stats>();
    l.player->AddComponent<Inventory>();
    l.player->AddComponent<Rigidbody3D>();
    l.crateA = s.CreateGameObject("Crate");
    l.crateA->AddComponent<Saveable>();
    l.crateA->AddComponent<Rigidbody2D>();
    l.crateB = s.CreateGameObject("Crate");                    // same name: saved as Crate#1
    l.crateB->AddComponent<Saveable>();
    l.coinA = s.CreateGameObject("Coin");
    l.coinA->AddComponent<Saveable>()->saveId = "coin-a";
    l.coinA->transform->localPosition = Vec3{1, 0, 0};
    l.coinB = s.CreateGameObject("Coin");
    l.coinB->AddComponent<Saveable>()->saveId = "coin-b";
    l.coinB->transform->localPosition = Vec3{2, 0, 0};
    l.door = s.CreateGameObject("Door");
    l.door->AddComponent<DoorSave>();
    l.chest = s.CreateGameObject("Chest");
    l.chest->AddComponent<Saveable>();
    l.lid = s.CreateGameObject("Lid");
    l.lid->transform->SetParent(l.chest->transform, false);
    l.lid->AddComponent<Saveable>();
    l.rock = s.CreateGameObject("Rock");                       // no Saveable: not saved by default
    s.Start();
    return l;
}

} // namespace

int main() {
    RUN_SUITE("save_scene");
    const fs::path root = fs::temp_directory_path() / "okay_test_save_scene";
    fs::remove_all(root);
    fs::create_directories(root);
    std::string err;

    // ---- capture, clear, rebuild, apply ----
    {
        Scene s("Level1");
        Level l = Build(s);
        SaveSystem::MarkSceneLoaded(s);
        l.player->transform->localPosition = Vec3{5, 6, 7};
        l.player->transform->localRotation = Quat::Euler(Vec3{0, 90, 0});
        l.player->transform->localScale = Vec3{2, 2, 2};
        auto* stats = l.player->GetComponent<Stats>();
        stats->health = 33.0f; stats->level = 4; stats->xp = 77; stats->mana = 1.5f;
        auto* inv = l.player->GetComponent<Inventory>();
        inv->capacity = 12;
        inv->slots.push_back(Inventory::Slot{"sword", 1, ItemInstanceData{}});
        Inventory::Slot worn; worn.item = "axe"; worn.count = 1; worn.data.durability = 0.5f; worn.data.uid = 99; worn.data.Set("enchant", "fire = hot");
        inv->slots.push_back(worn);
        inv->slots.push_back(Inventory::Slot{"wood", 40, ItemInstanceData{}});
        l.player->GetComponent<Rigidbody3D>()->velocity = Vec3{1, -2, 3};
        l.player->GetComponent<Rigidbody3D>()->angularVelocity = Vec3{0, 4, 0};
        l.crateA->GetComponent<Rigidbody2D>()->velocity = Vec2{3, -1};
        l.crateA->transform->localPosition = Vec3{10, 0, 0};
        l.crateB->transform->localPosition = Vec3{20, 0, 0};
        auto* door = l.door->GetComponent<DoorSave>();
        door->open = true; door->uses = 3; door->note = "a\tb\nc = d";
        l.coinA->active = false;
        l.lid->transform->localPosition = Vec3{0, 1, 0};
        l.rock->transform->localPosition = Vec3{9, 9, 9};

        SaveCaptureReport rep;
        SaveDocument doc = SaveSystem::CaptureScene(s, {}, &rep);
        CHECK(rep.warnings.empty() && rep.destroyed == 0 && rep.dynamic == 0);
        CHECK(rep.objects == 8);                                     // everything but the Rock
        CHECK(doc.Find("scene")->GetString("name") == "Level1" && doc.Find("scene")->GetInt("count") == 8);
        std::string why;
        CHECK(SaveSystem::VerifyCapture(s, &why));

        // Keys: an explicit id, the scene path, #n for same-named siblings, children below their parent.
        CHECK(SaveSystem::KeyOf(*l.player) == "id:player");
        CHECK(SaveSystem::KeyOf(*l.crateA) == "path:Crate" && SaveSystem::KeyOf(*l.crateB) == "path:Crate#1");
        CHECK(SaveSystem::KeyOf(*l.lid) == "path:Chest/Lid" && SaveSystem::KeyOf(*l.coinB) == "id:coin-b");

        // Through text and back, like a slot does.
        SaveDocument viaText;
        CHECK(SaveDocument::Parse(doc.Serialize(), viaText, &err) && viaText == doc);

        s.Clear();
        Level n = Build(s);
        CHECK(Near(n.player->transform->localPosition, Vec3::Zero) && n.player->GetComponent<Stats>()->level == 1);
        CHECK(n.door->GetComponent<DoorSave>()->uses == 0 && n.coinA->active);
        auto apply = SaveSystem::ApplyScene(s, viaText);
        CHECK(apply.ok && apply.restored == 8 && apply.spawned == 0 && apply.destroyed == 0 && apply.missing == 0);
        CHECK(Near(n.player->transform->localPosition, Vec3{5, 6, 7}) && Near(n.player->transform->localScale, Vec3{2, 2, 2}));
        CHECK(Quat::Dot(n.player->transform->localRotation, Quat::Euler(Vec3{0, 90, 0})) > 0.9999f);
        auto* st2 = n.player->GetComponent<Stats>();
        CHECK(st2->health == 33.0f && st2->level == 4 && st2->xp == 77 && st2->mana == 1.5f);
        auto* inv2 = n.player->GetComponent<Inventory>();
        CHECK(inv2->capacity == 12 && inv2->slots.size() == 3 && inv2->slots[0].item == "sword" && inv2->slots[2].count == 40);
        CHECK(inv2->slots[1].data.durability == 0.5f && inv2->slots[1].data.uid == 99 && inv2->slots[1].data.Get("enchant") == "fire = hot");
        CHECK(inv2->slots[0].data.Empty());
        CHECK(Near(n.player->GetComponent<Rigidbody3D>()->velocity, Vec3{1, -2, 3}) && Near(n.player->GetComponent<Rigidbody3D>()->angularVelocity, Vec3{0, 4, 0}));
        CHECK(n.crateA->GetComponent<Rigidbody2D>()->velocity.x == 3.0f && n.crateA->GetComponent<Rigidbody2D>()->velocity.y == -1.0f);
        CHECK(Near(n.crateA->transform->localPosition, Vec3{10, 0, 0}) && Near(n.crateB->transform->localPosition, Vec3{20, 0, 0}));
        auto* d2 = n.door->GetComponent<DoorSave>();
        CHECK(d2->open && d2->uses == 3 && d2->note == "a\tb\nc = d" && d2->completes == 1);
        CHECK(!n.coinA->active && n.coinB->active);
        CHECK(Near(n.lid->transform->localPosition, Vec3{0, 1, 0}));
        CHECK(Near(n.rock->transform->localPosition, Vec3::Zero));   // never saved, so untouched

        // The saved id survives a rename; a path key doesn't.
        n.player->name = "Hero";
        n.crateB->name = "Renamed";
        n.player->transform->localPosition = Vec3::Zero;
        n.crateB->transform->localPosition = Vec3::Zero;
        apply = SaveSystem::ApplyScene(s, doc);
        CHECK(apply.ok && apply.missing == 1);                       // "Crate#1" is gone
        CHECK(Near(n.player->transform->localPosition, Vec3{5, 6, 7}) && Near(n.crateB->transform->localPosition, Vec3::Zero));
    }

    // ---- objects destroyed while playing stay gone ----
    {
        Scene s("Level1");
        Level l = Build(s);
        SaveSystem::MarkSceneLoaded(s);
        s.Destroy(l.coinB);
        s.Destroy(l.chest);                                          // takes its child with it
        s.Update(0.016f);
        SaveCaptureReport rep;
        const SaveDocument doc = SaveSystem::CaptureScene(s, {}, &rep);
        CHECK(rep.destroyed == 3 && rep.objects == 5);               // coin-b, Chest, Chest/Lid
        CHECK(doc.Find("scene")->GetStrings("destroyed").size() == 3);

        s.Clear();
        Level n = Build(s);
        CHECK(Count(s, "Coin") == 2 && Count(s, "Chest") == 1);
        auto apply = SaveSystem::ApplyScene(s, doc);
        s.Update(0.016f);
        CHECK(apply.ok && apply.destroyed == 3 && Count(s, "Coin") == 1 && Count(s, "Chest") == 0 && Count(s, "Lid") == 0);
        CHECK(s.Find("Coin") == n.coinA);
        // A later capture still knows they are gone, and applying twice changes nothing.
        CHECK(SaveSystem::CaptureScene(s, {}, &rep).Find("scene")->GetStrings("destroyed").size() == 3);
        apply = SaveSystem::ApplyScene(s, doc);
        CHECK(apply.ok && apply.destroyed == 0);
        // A destroyed object that is back in the level file is destroyed again: the save wins.
        s.Clear();
        n = Build(s);
        apply = SaveSystem::ApplyScene(s, doc);
        s.Update(0.016f);
        CHECK(Count(s, "Coin") == 1 && Count(s, "Chest") == 0);
    }

    // ---- dynamic objects: spawned from a prefab file or a factory, found again by id ----
    {
        const fs::path prefab = root / "bullet.okayprefab";
        {
            Scene p("prefab");
            GameObject* b = p.CreateGameObject("Bullet");
            b->tag = "projectile";
            b->layer = 3;
            b->AddComponent<Rigidbody3D>();
            CHECK(SceneSerializer::SaveObjectToFile(*b, prefab.string()));
        }
        SaveSystem::RegisterFactory("Torch", [](Scene& sc) {
            GameObject* g = sc.CreateGameObject("Torch");
            g->AddComponent<TorchSave>();
            return g;
        });

        Scene s("Level1");
        Level l = Build(s);
        SaveSystem::MarkSceneLoaded(s);
        GameObject* bullet = SaveSystem::Spawn(s, prefab.string(), Vec3{1, 2, 3}, &err);
        CHECK(bullet && bullet->name == "Bullet" && err.empty());
        auto* bsv = bullet->GetComponent<Saveable>();
        CHECK(bsv && bsv->dynamic && !bsv->saveId.empty() && bsv->prefab == prefab.string());
        bullet->GetComponent<Rigidbody3D>()->velocity = Vec3{0, 0, 50};
        bullet->active = false;
        GameObject* torch = SaveSystem::Spawn(s, "factory:Torch", Vec3{4, 0, 0});
        torch->GetComponent<TorchSave>()->fuel = 0.25f;
        torch->transform->SetParent(l.crateA->transform, false);      // a dynamic object parented to a level object
        torch->transform->localPosition = Vec3{0, 1, 0};
        const std::string bulletId = bsv->saveId, torchId = torch->GetComponent<Saveable>()->saveId;
        CHECK(bulletId != torchId && SaveSystem::KeyOf(*torch) == "id:" + torchId);
        s.Start();

        SaveCaptureReport rep;
        const SaveDocument doc = SaveSystem::CaptureScene(s, {}, &rep);
        CHECK(rep.dynamic == 2 && rep.warnings.empty() && rep.objects == 10);

        // 1. The scene is rebuilt without them: both come back.
        s.Clear();
        Level n = Build(s);
        CHECK(Count(s, "Bullet") == 0 && Count(s, "Torch") == 0);
        auto apply = SaveSystem::ApplyScene(s, doc);
        CHECK(apply.ok && apply.spawned == 2 && apply.restored == 10 && apply.warnings.empty());
        GameObject* b2 = s.Find("Bullet");
        GameObject* t2 = s.Find("Torch");
        CHECK(b2 && t2 && Count(s, "Bullet") == 1 && Count(s, "Torch") == 1);
        CHECK(Near(b2->transform->localPosition, Vec3{1, 2, 3}) && !b2->active && b2->tag == "projectile" && b2->layer == 3);
        CHECK(Near(b2->GetComponent<Rigidbody3D>()->velocity, Vec3{0, 0, 50}));
        CHECK(b2->GetComponent<Saveable>()->dynamic && b2->GetComponent<Saveable>()->saveId == bulletId);
        CHECK(t2->GetComponent<TorchSave>()->fuel == 0.25f && t2->GetComponent<Saveable>()->saveId == torchId);
        CHECK(t2->transform->Parent() == n.crateA->transform && Near(t2->transform->localPosition, Vec3{0, 1, 0}));

        // 2. Applying again to the same scene doesn't duplicate them.
        apply = SaveSystem::ApplyScene(s, doc);
        CHECK(apply.ok && apply.spawned == 0 && Count(s, "Bullet") == 1 && Count(s, "Torch") == 1);

        // 3. An object spawned after the save didn't exist then: it is removed.
        GameObject* extra = SaveSystem::Spawn(s, "factory:Torch");
        s.Start();
        CHECK(Count(s, "Torch") == 2);
        apply = SaveSystem::ApplyScene(s, doc);
        s.Update(0.016f);
        CHECK(apply.ok && apply.destroyed == 1 && Count(s, "Torch") == 1 && extra->IsDestroyed());
        SaveApplyOptions keep;
        keep.removeUnsavedDynamic = false;
        GameObject* extra2 = SaveSystem::Spawn(s, "factory:Torch");
        s.Start();
        SaveSystem::ApplyScene(s, doc, keep);
        s.Update(0.016f);
        CHECK(Count(s, "Torch") == 2 && !extra2->IsDestroyed());

        // 4. A prefab that can't be loaded or built is reported, not fatal.
        SaveDocument broken = doc;
        for (const std::string& name : broken.Names()) {
            SaveBlock& b = broken.Block(name);
            if (b.GetBool("dynamic", false)) b.SetString("prefab", (root / "missing.okayprefab").string());
        }
        s.Clear();
        Level n2 = Build(s);
        CHECK(n2.player != nullptr);
        apply = SaveSystem::ApplyScene(s, broken);
        CHECK(apply.ok && apply.spawned == 0 && apply.warnings.size() == 2 && Contains(apply.warnings[0], "couldn't respawn"));
        SaveSystem::UnregisterFactory("Torch");
        s.Clear();
        Level n3 = Build(s);
        CHECK(n3.player != nullptr);
        apply = SaveSystem::ApplyScene(s, doc);
        CHECK(apply.ok && apply.spawned == 1 && apply.warnings.size() == 1 && Contains(apply.warnings[0], "factory"));

        // A dynamic object with nothing to respawn it from is flagged when it is saved.
        Scene bare("Level1");
        GameObject* orphan = bare.CreateGameObject("Orphan");
        auto* osv = orphan->AddComponent<Saveable>();
        osv->dynamic = true;
        SaveCaptureReport orep;
        SaveSystem::CaptureScene(bare, {}, &orep);
        CHECK(orep.warnings.size() == 1 && Contains(orep.warnings[0], "no prefab") && !osv->saveId.empty());
        CHECK(!SaveSystem::VerifyCapture(bare, &err) && Contains(err, "no prefab"));
        GameObject* none = SaveSystem::Spawn(bare, "", Vec3::Zero, &err);
        CHECK(none == nullptr && !err.empty());
        CHECK(SaveSystem::Spawn(bare, "factory:Nope", Vec3::Zero, &err) == nullptr && Contains(err, "factory"));
    }

    // ---- which objects are captured ----
    {
        Scene s("Level1");
        Level l = Build(s);
        GameObject* marked = s.CreateGameObject("Marked");
        marked->tag = "persist";
        GameObject* generated = s.CreateGameObject("Generated");
        generated->transient = true;
        generated->tag = "persist";
        SaveCaptureOptions byTag;
        byTag.includeTag = "persist";
        SaveCaptureReport rep;
        SaveSystem::CaptureScene(s, byTag, &rep);
        CHECK(rep.objects == 9);                                      // + Marked; the transient one never
        SaveCaptureOptions everything;
        everything.allObjects = true;
        SaveSystem::CaptureScene(s, everything, &rep);
        CHECK(rep.objects == 10);                                     // + Rock; still not the transient one
        marked->transform->localPosition = Vec3{3, 3, 3};
        const SaveDocument doc = SaveSystem::CaptureScene(s, byTag);
        marked->transform->localPosition = Vec3::Zero;
        l.rock->transform->localPosition = Vec3{1, 1, 1};
        SaveSystem::ApplyScene(s, doc);
        CHECK(Near(marked->transform->localPosition, Vec3{3, 3, 3}) && Near(l.rock->transform->localPosition, Vec3{1, 1, 1}));
        // A disabled Saveable opts its object out; flags choose what is stored.
        l.crateB->GetComponent<Saveable>()->enabled = false;
        l.crateA->GetComponent<Saveable>()->saveTransform = false;
        l.crateA->GetComponent<Saveable>()->saveComponents = false;
        const SaveDocument d2 = SaveSystem::CaptureScene(s);
        bool sawCrateA = false, sawCrateB = false;
        for (const std::string& name : d2.Names()) {
            const SaveBlock& b = *d2.Find(name);
            if (b.GetString("key") == "path:Crate") { sawCrateA = true; CHECK(!b.Has("lp") && !b.HasPrefix("a.")); }
            if (b.GetString("key") == "path:Crate#1") sawCrateB = true;
        }
        CHECK(sawCrateA && !sawCrateB);
        // Components off for the whole capture.
        SaveCaptureOptions noAdapters;
        noAdapters.components = false;
        const SaveDocument d3 = SaveSystem::CaptureScene(s, noAdapters);
        for (const std::string& name : d3.Names()) CHECK(!d3.Find(name)->HasPrefix("a."));

        // Two objects with one id: the second isn't saved, and says so.
        l.crateB->GetComponent<Saveable>()->enabled = true;
        l.crateA->GetComponent<Saveable>()->saveId = "same";
        l.crateB->GetComponent<Saveable>()->saveId = "same";
        SaveCaptureReport drep;
        SaveSystem::CaptureScene(s, {}, &drep);
        CHECK(drep.warnings.size() == 1 && Contains(drep.warnings[0], "id:same"));
        CHECK(!SaveSystem::VerifyCapture(s, &err) && Contains(err, "id:same"));
    }

    // ---- apply errors, adapters, prepare hook ----
    {
        Scene s("Level1");
        Level l = Build(s);
        SaveDocument empty;
        auto apply = SaveSystem::ApplyScene(s, empty);
        CHECK(!apply.ok && Contains(apply.error, "no scene data"));
        SaveDocument doc = SaveSystem::CaptureScene(s);
        Scene other("Another");
        apply = SaveSystem::ApplyScene(other, doc);
        CHECK(apply.ok && apply.warnings.size() == 1 && Contains(apply.warnings[0], "Level1"));
        SaveApplyOptions strict;
        strict.sceneMustMatch = true;
        apply = SaveSystem::ApplyScene(other, doc, strict);
        CHECK(!apply.ok && Contains(apply.error, "Level1") && Contains(apply.error, "Another"));
        SaveDocument future = doc;
        future.Block("scene").SetInt("format", 99);
        CHECK(!SaveSystem::ApplyScene(s, future).ok);
        // A hostile object count doesn't make the loader loop.
        SaveDocument hostile = doc;
        hostile.Block("scene").SetInt("count", 2000000000);
        CHECK(SaveSystem::ApplyScene(s, hostile).ok);
        // A bag with a missing block in the middle: reported, the rest is applied.
        SaveDocument hole = doc;
        hole.Remove("obj.1");
        apply = SaveSystem::ApplyScene(s, hole);
        CHECK(apply.ok && apply.restored == 7 && apply.warnings.size() == 1 && Contains(apply.warnings[0], "object block 1"));

        // A custom adapter for a component the engine doesn't know how to save.
        SaveAdapter tagger;
        tagger.name = "tagger";
        tagger.save = [](GameObject& go, SaveWriter& w) { w.String("t", go.tag); return true; };
        tagger.load = [](GameObject& go, SaveReader& r) { go.tag = r.String("t", go.tag); };
        SaveSystem::RegisterAdapter(tagger);
        CHECK(SaveSystem::AdapterNames().size() == 5);
        l.chest->tag = "treasure";
        const SaveDocument d = SaveSystem::CaptureScene(s);
        l.chest->tag = "";
        SaveSystem::ApplyScene(s, d);
        CHECK(l.chest->tag == "treasure");
        SaveSystem::ResetAdapters();
        CHECK(SaveSystem::AdapterNames().size() == 4 && SaveSystem::AdapterNames()[0] == "rigidbody3d");
        l.chest->tag = "";
        SaveSystem::ApplyScene(s, d);                                // the adapter is gone: its data is ignored
        CHECK(l.chest->tag.empty());

        // prepare runs first and may attach components the level file can't hold.
        Scene bare("Level1");
        GameObject* p = bare.CreateGameObject("Player");
        GameObject* c = bare.CreateGameObject("Crate");
        SaveCaptureOptions all; all.allObjects = true;
        Scene src("Level1");
        Level sl = Build(src);
        sl.door->GetComponent<DoorSave>()->uses = 7;
        const SaveDocument sd = SaveSystem::CaptureScene(src, all);
        GameObject* dr = bare.CreateGameObject("Door");
        SaveApplyOptions prep;
        prep.prepare = [](Scene& sc) {
            sc.Find("Door")->AddComponent<DoorSave>();
            sc.Find("Player")->AddComponent<Saveable>()->saveId = "player";
        };
        apply = SaveSystem::ApplyScene(bare, sd, prep);
        CHECK(apply.ok && dr->GetComponent<DoorSave>() && dr->GetComponent<DoorSave>()->uses == 7);
        CHECK(SaveSystem::KeyOf(*p) == "id:player" && c != nullptr);
    }

    // ---- the manager: save, load, extras, reload from a level file ----
    {
        SaveStore store((root / "mgr").string());
        store.SetClock(Clock);
        store.SetGameVersion("0.9");
        SaveManager mgr(store);
        Scene s("Level1");
        Level l = Build(s);
        SaveSystem::MarkSceneLoaded(s);
        l.player->transform->localPosition = Vec3{8, 0, 0};
        l.door->GetComponent<DoorSave>()->open = true;
        SaveSystem::SetPlayTime(123.0f);

        SaveRequest req;
        req.label = "Chapter 1";
        req.thumbnail = "ch1.png";
        req.fields["difficulty"] = "hard";
        req.extra.Writer("game").Int("gold", 120);
        req.collect = [](SaveDocument& d) { d.Writer("profile").String("name", "Ann"); };
        g_now = 1000;
        CHECK(mgr.Save(s, "slot1", req, &err));
        const SaveSlotInfo info = store.Info("slot1");
        CHECK(info.label == "Chapter 1" && info.scene == "Level1" && info.kind == "manual" && info.gameVersion == "0.9");
        CHECK(info.thumbnail == "ch1.png" && info.fields.at("difficulty") == "hard" && info.savedAtMs == 1000);
        CHECK_NEAR(info.playTime, 123.0, 1.0);
        CHECK(store.Verify("slot1", &err));
        // A game block can't take a name the scene uses.
        SaveRequest reserved;
        reserved.extra.Writer("scene").Int("x", 1);
        CHECK(!mgr.Save(s, "bad", reserved, &err) && Contains(err, "reserved") && !store.Exists("bad"));
        SaveRequest reserved2;
        reserved2.collect = [](SaveDocument& d) { d.Writer("obj.7").Int("x", 1); };
        CHECK(!mgr.Save(s, "bad", reserved2, &err) && Contains(err, "reserved"));
        CHECK(!mgr.Save(s, "bad name", {}, &err) && Contains(err, "valid slot name"));

        l.player->transform->localPosition = Vec3::Zero;
        l.door->GetComponent<DoorSave>()->open = false;
        SaveSystem::SetPlayTime(5.0f);
        SaveLoadOptions lo;
        int loadedGold = 0;
        std::string loadedName;
        lo.onLoaded = [&](const SaveDocument& d, const SaveSlotInfo& si) {
            loadedGold = d.Find("game")->GetInt("gold");
            loadedName = d.Find("profile")->GetString("name") + "@" + si.slot;
        };
        SaveDocument got;
        SaveApplyReport rep;
        lo.outDocument = &got;
        lo.outReport = &rep;
        CHECK(mgr.Load(s, "slot1", lo, &err));
        CHECK(Near(l.player->transform->localPosition, Vec3{8, 0, 0}) && l.door->GetComponent<DoorSave>()->open);
        CHECK(loadedGold == 120 && loadedName == "Ann@slot1" && got.Has("scene") && rep.ok && rep.restored == 8);
        CHECK_NEAR(SaveSystem::PlayTime(), 123.0, 1.0);               // play time is carried through the load
        CHECK(!mgr.Load(s, "nope", {}, &err) && Contains(err, "No save"));
        // A slot from a newer build is refused by the manager with a clear message.
        SaveStore newer((root / "mgr").string()); newer.SetClock(Clock); newer.SetDataVersion(4);
        CHECK(newer.Write("fut", got, SaveSlotInfo(), &err));
        CHECK(!mgr.Load(s, "fut", {}, &err) && Contains(err, "newer version"));

        // Reload the level from its file, then put the saved state back (the save marks objects by tag).
        const fs::path levelFile = root / "level1.okayscene";
        {
            Scene lvl("Level1");
            GameObject* a = lvl.CreateGameObject("Hero"); a->tag = "keep";
            GameObject* b = lvl.CreateGameObject("Barrel"); b->tag = "keep";
            GameObject* junk = lvl.CreateGameObject("Junk");
            junk->transform->localPosition = Vec3{7, 7, 7};
            CHECK(SceneSerializer::SaveToFile(lvl, levelFile.string()));
        }
        Scene game("Level1");
        CHECK(SceneSerializer::LoadFromFile(game, levelFile.string(), &err));
        game.Start();
        SaveSystem::MarkSceneLoaded(game, [] { SaveCaptureOptions o; o.includeTag = "keep"; return o; }());
        game.Find("Hero")->transform->localPosition = Vec3{4, 4, 4};
        game.Find("Barrel")->transform->localPosition = Vec3{6, 0, 0};
        game.Destroy(game.Find("Barrel"));
        game.Update(0.016f);
        SaveRequest treq;
        treq.capture.includeTag = "keep";
        treq.label = "by tag";
        CHECK(mgr.Save(game, "tagged", treq, &err));
        Scene fresh("blank");
        bool done = false, doneOk = false;
        SaveLoadOptions ro;
        ro.reloadScene = levelFile.string();
        ro.onDone = [&](bool ok, const std::string&) { done = true; doneOk = ok; };
        CHECK(mgr.Load(fresh, "tagged", ro, &err));
        CHECK(!done && fresh.Find("Hero") == nullptr);                // nothing happens until the frame ends
        fresh.Update(0.016f);
        CHECK(done && doneOk && fresh.Name() == "Level1");
        CHECK(Near(fresh.Find("Hero")->transform->localPosition, Vec3{4, 4, 4}));
        fresh.Update(0.016f);                                         // the Barrel's destroy lands with the frame
        CHECK(fresh.Find("Barrel") == nullptr);                       // it was picked up in that save
        CHECK(Near(fresh.Find("Junk")->transform->localPosition, Vec3{7, 7, 7}));
        // A level file that can't be loaded reports it.
        Scene fresh2("blank");
        done = false; doneOk = true;
        ro.reloadScene = (root / "missing.okayscene").string();
        CHECK(mgr.Load(fresh2, "tagged", ro, &err));
        fresh2.Update(0.016f);
        CHECK(done && !doneOk);
    }

    // ---- deferred saves and loads run at the start of the next frame ----
    {
        SaveStore store((root / "deferred").string());
        store.SetClock(Clock);
        SaveManager mgr(store);
        Scene s("Level1");
        Level l = Build(s);
        l.player->transform->localPosition = Vec3{2, 0, 0};
        bool saved = false, savedOk = false;
        SaveRequest req;
        req.deferred = true;
        req.onDone = [&](bool ok, const std::string&) { saved = true; savedOk = ok; };
        g_now = 5000;
        CHECK(mgr.Save(s, "later", req, &err));
        CHECK(!store.Exists("later") && !saved);
        s.Update(0.016f);
        CHECK(store.Exists("later") && saved && savedOk);
        l.player->transform->localPosition = Vec3{9, 0, 0};
        SaveLoadOptions lo;
        lo.deferred = true;
        bool loaded = false;
        lo.onDone = [&](bool ok, const std::string&) { loaded = ok; };
        CHECK(mgr.Load(s, "later", lo, &err));
        CHECK(Near(l.player->transform->localPosition, Vec3{9, 0, 0}) && !loaded);
        s.Update(0.016f);
        CHECK(Near(l.player->transform->localPosition, Vec3{2, 0, 0}) && loaded);
        CHECK(!mgr.Load(s, "absent", lo, &err));                      // a missing slot fails right away
    }

    // ---- quick save, autosave ring, continue ----
    {
        SaveStore store((root / "slots").string());
        store.SetClock(Clock);
        SaveManager mgr(store);
        Scene s("Level1");
        Level l = Build(s);

        CHECK(!mgr.CanContinue() && mgr.ContinueSlot().empty() && !mgr.HasQuickSave());
        CHECK(!mgr.Continue(s, {}, &err) && Contains(err, "no save"));

        g_now = 10000;
        l.player->transform->localPosition = Vec3{1, 0, 0};
        CHECK(mgr.QuickSave(s, {}, &err) && mgr.HasQuickSave());
        CHECK(store.Info("quicksave").kind == "quick" && store.Info("quicksave").label == "Quick save");
        l.player->transform->localPosition = Vec3{2, 0, 0};
        CHECK(mgr.QuickLoad(s, {}, &err));
        CHECK(Near(l.player->transform->localPosition, Vec3{1, 0, 0}));
        mgr.SetQuickSlot("q2");
        CHECK(!mgr.HasQuickSave() && mgr.QuickSlot() == "q2");
        mgr.SetQuickSlot("quicksave");

        // The ring: autosave_1..3, each save to the first empty place, then to the oldest.
        CHECK(mgr.AutosaveCount() == 3 && mgr.AutosaveSlots().size() == 3 && mgr.AutosaveSlots()[2] == "autosave_3");
        std::string used, order;
        for (int i = 1; i <= 5; ++i) {
            g_now = 20000 + i * 1000;
            l.player->transform->localPosition = Vec3{(float)(10 * i), 0, 0};
            SaveRequest r;
            r.label = "auto " + std::to_string(i);
            CHECK(mgr.AutoSave(s, r, &err, &used));
            order += used + ",";
        }
        CHECK(order == "autosave_1,autosave_2,autosave_3,autosave_1,autosave_2,");
        CHECK(store.Info("autosave_1").label == "auto 4" && store.Info("autosave_2").label == "auto 5" && store.Info("autosave_3").label == "auto 3");
        CHECK(store.Info("autosave_1").kind == "auto");
        int autos = 0;
        for (const auto& i : store.ListSlots()) if (i.kind == "auto") ++autos;
        CHECK(autos == 3);
        CHECK(mgr.NextAutosaveSlot() == "autosave_3");
        // A damaged slot in the ring is the first to be replaced.
        WriteText(store.SlotPath("autosave_2"), "garbage");
        CHECK(mgr.NextAutosaveSlot() == "autosave_2");
        mgr.SetAutosaveCount(2);
        CHECK(mgr.AutosaveSlots().size() == 2 && mgr.NextAutosaveSlot() == "autosave_2");
        mgr.SetAutosaveCount(3);
        g_now = 30000; CHECK(mgr.AutoSave(s, {}, &err, &used) && used == "autosave_2" && store.Info("autosave_2").label == "Autosave");

        // Continue loads the newest valid slot, of any kind.
        g_now = 40000;
        l.player->transform->localPosition = Vec3{77, 0, 0};
        CHECK(mgr.Save(s, "manual", {}, &err));
        l.player->transform->localPosition = Vec3::Zero;
        std::string cont;
        CHECK(mgr.CanContinue() && mgr.ContinueSlot() == "manual");
        CHECK(mgr.Continue(s, {}, &err, &cont) && cont == "manual" && Near(l.player->transform->localPosition, Vec3{77, 0, 0}));
        // The newest is damaged (payload flip: the header still lists it): the next newest is used.
        std::string bytes = ReadText(store.SlotPath("manual"));
        bytes[bytes.size() - 6] ^= 0x08;
        WriteText(store.SlotPath("manual"), bytes);
        CHECK(!store.Info("manual").corrupt && store.Info("manual", true).corrupt);
        CHECK(mgr.ContinueSlot() == "autosave_2");
        l.player->transform->localPosition = Vec3::Zero;
        CHECK(mgr.Continue(s, {}, &err, &cont) && cont == "autosave_2");
        // Management passes through to the store.
        CHECK(mgr.Copy("autosave_2", "keeper", false, &err) && store.Exists("keeper"));
        CHECK(mgr.Rename("keeper", "keeper2", false, &err) && store.Exists("keeper2") && !store.Exists("keeper"));
        CHECK(mgr.Delete("keeper2") && !store.Exists("keeper2"));
        CHECK(mgr.ListSlots().size() >= 5);
    }

    // ---- checkpoints ----
    {
        SaveStore store((root / "cp").string());
        store.SetClock(Clock);
        SaveManager mgr(store);
        CheckpointManager cm(mgr);
        Scene s("Level1");
        Level l = Build(s);
        std::string reached;
        cm.onReached = [&](const std::string& n) { reached += n + ","; };

        CHECK(cm.Last().empty() && cm.Names().empty() && !cm.RestoreLast(s, {}, &err) && Contains(err, "no checkpoint"));
        CHECK(!cm.Reach(s, "", false, &err));
        CHECK(CheckpointManager::SlotFor("a b/c") == "checkpoint_a_b_c" && CheckpointManager::SlotFor("gate-1") == "checkpoint_gate-1");
        CHECK(CheckpointManager::SlotFor(std::string(200, 'x')).size() == 64);

        g_now = 1000; l.player->transform->localPosition = Vec3{0, 0, 0};
        CHECK(cm.Reach(s, "start", false, &err));
        g_now = 2000; l.player->transform->localPosition = Vec3{10, 0, 0}; l.door->GetComponent<DoorSave>()->open = true;
        CHECK(cm.Reach(s, "bridge", false, &err));
        CHECK(reached == "start,bridge,");
        CHECK(cm.Last() == "bridge" && cm.Names() == (std::vector<std::string>{"bridge", "start"}) && cm.Has("start"));
        CHECK(store.Info("checkpoint_start").kind == "checkpoint" && store.Info("checkpoint_start").fields.at("checkpoint") == "start");
        l.player->transform->localPosition = Vec3{99, 0, 0};
        l.door->GetComponent<DoorSave>()->open = false;
        CHECK(cm.RestoreLast(s, {}, &err));
        CHECK(Near(l.player->transform->localPosition, Vec3{10, 0, 0}) && l.door->GetComponent<DoorSave>()->open);
        CHECK(cm.Restore(s, "start", {}, &err));
        CHECK(Near(l.player->transform->localPosition, Vec3::Zero) && !l.door->GetComponent<DoorSave>()->open);
        CHECK(!cm.Restore(s, "nowhere", {}, &err) && Contains(err, "No save"));
        // A damaged newest checkpoint (no backup of it) falls back to the one before.
        const std::string bridgeBytes = ReadText(store.SlotPath("checkpoint_bridge"));
        std::string damaged = bridgeBytes;
        damaged[damaged.size() - 6] ^= 0x01;
        WriteText(store.SlotPath("checkpoint_bridge"), damaged);
        l.player->transform->localPosition = Vec3{50, 0, 0};
        CHECK(cm.RestoreLast(s, {}, &err) && Near(l.player->transform->localPosition, Vec3::Zero));
        WriteText(store.SlotPath("checkpoint_bridge"), bridgeBytes);
        // Reaching an old one again makes it the last.
        g_now = 3000; CHECK(cm.Reach(s, "start", false, &err));
        CHECK(cm.Last() == "start");
        CHECK(cm.Remove("start") && !cm.Has("start") && cm.Names() == (std::vector<std::string>{"bridge"}));
        CHECK(cm.Clear() == 1 && cm.Names().empty());
        // A cap keeps only the newest few.
        cm.SetMaxCheckpoints(2);
        for (int i = 1; i <= 4; ++i) { g_now = 5000 + i * 100; CHECK(cm.Reach(s, "cp" + std::to_string(i), false, &err)); }
        CHECK(cm.Names() == (std::vector<std::string>{"cp4", "cp3"}) && cm.MaxCheckpoints() == 2);
        // A checkpoint can be saved at the start of the next frame.
        reached.clear();
        g_now = 9000;
        CHECK(cm.Reach(s, "later", true, &err) && !cm.Has("later"));
        s.Update(0.016f);
        CHECK(cm.Has("later") && reached == "later," && cm.Last() == "later");
        // Checkpoints don't show up as quick or auto slots.
        CHECK(!mgr.HasQuickSave());
    }

    // ---- CheckpointTrigger ----
    {
        SaveStore store((root / "trigger").string());
        store.SetClock(Clock);
        SaveManager mgr(store);
        CheckpointManager cm(mgr);
        CheckpointManager::SetActive(&cm);
        CHECK(&CheckpointManager::Active() == &cm);

        Scene s("Level1");
        Level l = Build(s);
        GameObject* gate = s.CreateGameObject("Gate");
        gate->transform->localPosition = Vec3{5, 0, 0};
        auto* trig = gate->AddComponent<CheckpointTrigger>();
        trig->checkpoint = "gate";
        trig->radius = 2.0f;
        s.Start();
        l.player->transform->localPosition = Vec3{0, 0, 0};
        g_now = 100;
        s.Update(0.2f);
        s.Update(0.2f);
        CHECK(!trig->triggered && !cm.Has("gate"));                   // too far away
        l.player->transform->localPosition = Vec3{4.5f, 0, 0};
        s.Update(0.2f);                                               // reaches it; the save is queued
        CHECK(trig->triggered && !cm.Has("gate"));
        s.Update(0.016f);                                             // ... and written at the start of the next frame
        CHECK(cm.Has("gate") && cm.Last() == "gate");
        // The saved state has the player where it was when it reached the gate.
        l.player->transform->localPosition = Vec3{40, 0, 0};
        CHECK(cm.RestoreLast(s, {}, &err) && Near(l.player->transform->localPosition, Vec3{4.5f, 0, 0}));
        // Once only: nothing is written again, but firing by hand is refused too.
        const long long first = store.Info("checkpoint_gate").savedAtMs;
        g_now = 99999;
        s.Update(0.2f); s.Update(0.2f);
        CHECK(store.Info("checkpoint_gate").savedAtMs == first && !trig->Fire());
        trig->once = false;
        CHECK(trig->Fire());
        s.Update(0.016f);
        CHECK(store.Info("checkpoint_gate").savedAtMs == 99999);

        // Trigger colliders: only an object with the target tag counts.
        GameObject* pad = s.CreateGameObject("Pad");
        auto* t2 = pad->AddComponent<CheckpointTrigger>();
        t2->checkpoint = "pad";
        t2->radius = 0.0f;
        t2->deferred = false;
        auto* pc = l.player->AddComponent<BoxCollider3D>();
        auto* cc = l.crateA->AddComponent<BoxCollider3D>();
        s.Start();
        t2->OnTriggerEnter3D(cc);                                     // a crate: wrong tag
        CHECK(!t2->triggered && !cm.Has("pad"));
        t2->OnTriggerEnter3D(nullptr);
        t2->OnTriggerEnter3D(pc);
        CHECK(t2->triggered && cm.Has("pad"));                         // saved at once (not deferred)
        // A trigger with an empty tag reacts to anything; radius checks need a tag.
        GameObject* any = s.CreateGameObject("Any");
        auto* t3 = any->AddComponent<CheckpointTrigger>();
        t3->checkpoint = "any"; t3->targetTag = ""; t3->deferred = false;
        t3->OnTriggerEnter3D(cc);
        CHECK(t3->triggered && cm.Has("any"));
        CheckpointManager::SetActive(nullptr);
        CHECK(&CheckpointManager::Active() == &CheckpointManager::Global());
    }

    // ---- play time ----
    {
        SaveSystem::SetPlayTime(42.5f);
        CHECK_NEAR(SaveSystem::PlayTime(), 42.5, 0.5);
        SaveSystem::SetPlayTime(-3.0f);
        CHECK(SaveSystem::PlayTime() >= 0.0f && SaveSystem::PlayTime() < 1.0f);
        CHECK(SaveSystem::NewId().size() == 16 && SaveSystem::NewId() != SaveSystem::NewId());
    }

    fs::remove_all(root);
    TEST_MAIN_RESULT();
}
