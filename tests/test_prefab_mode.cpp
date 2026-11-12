// Prefab mode in the editor state (ImGui-free EditorState): open a prefab on its own, edit it, save it, go back.
// The state machine (open / save / revert / close, nested levels), unsaved-change tracking, one undo history per
// level, instances in the scene rebuilt from the saved file with what they override kept, Play refused, the
// scene / selection / view restored exactly, and the refusals (variants, missing files, a prefab already open).
#include "test_framework.hpp"
#include "../editor/EditorState.hpp"
#include "okay/Scene/PrefabTools.hpp"
#include "okay/Scene/SceneSerializer.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using namespace okay;
using okay::editor::EditorState;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        static int n = 0;
        path = fs::temp_directory_path() / ("okay_pmode_" + std::to_string(++n) + "_" +
               std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
    std::string Sub(const std::string& s) const { return (path / s).string(); }
};

std::string ReadAll(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
std::string Ser(const EditorState& ed) { return SceneSerializer::Serialize(ed.scene()); }
bool Has(const std::string& hay, const std::string& n) { return hay.find(n) != std::string::npos; }

// Enemy (SpriteRenderer) with a child Gun; Squad holding an Enemy instance as a nested prefab.
struct Files {
    std::string enemy, squad, scene;
};
Files MakeFiles(const TempDir& d) {
    Files f;
    f.enemy = d.Sub("enemy.okayprefab");
    f.squad = d.Sub("squad.okayprefab");
    f.scene = d.Sub("level.okayscene");
    {
        Scene sc("t");
        GameObject* e = sc.CreateGameObject("Enemy");
        e->AddComponent<SpriteRenderer>();
        GameObject* gun = sc.CreateGameObject("Gun");
        gun->transform->SetParent(e->transform, false);
        std::string err;
        PrefabTools::SavePrefab(*e, f.enemy, &err);
    }
    {
        Scene sc("t");
        GameObject* s = sc.CreateGameObject("Squad");
        std::string err;
        GameObject* inner = PrefabTools::Instantiate(sc, f.enemy, {}, &err);
        inner->transform->SetParent(s->transform, false);
        inner->name = "Lead";
        PrefabTools::SavePrefab(*s, f.squad, &err);
    }
    return f;
}

// A scene: Pre (plain), two Enemy instances (the second moved and red), Post (plain), a Squad instance. Saved and reopened clean.
void MakeScene(EditorState& ed, const Files& f) {
    ed.NewScene();
    Scene& sc = ed.scene();
    sc.CreateGameObject("Pre");
    std::string err;
    GameObject* a = PrefabTools::Instantiate(sc, f.enemy, {}, &err);
    a->name = "EnemyA";
    GameObject* b = PrefabTools::Instantiate(sc, f.enemy, {}, &err);
    b->name = "EnemyB";
    b->transform->localPosition = {5, 2, 0};
    b->GetComponent<SpriteRenderer>()->color = Color{1, 0, 0, 1};
    sc.CreateGameObject("Post");
    GameObject* sq = PrefabTools::Instantiate(sc, f.squad, {}, &err);
    sq->name = "SquadA";
    sq->transform->localPosition = {-3, 1, 0};
    CHECK(ed.Save(f.scene, &err));
    CHECK(ed.Load(f.scene, &err));
}

GameObject* Find(EditorState& ed, const char* n) { return ed.scene().Find(n); }
int Count(EditorState& ed, const char* n) { int c = 0; for (const auto& u : ed.scene().Objects()) if (!u->IsDestroyed() && u->name == n) ++c; return c; }
std::vector<std::string> TopLevel(EditorState& ed) {
    std::vector<std::string> v;
    for (const auto& u : ed.scene().Objects()) if (!u->IsDestroyed() && !u->transient && !u->transform->Parent()) v.push_back(u->name);
    return v;
}
void AddHat(EditorState& ed, const char* name = "Hat") {
    ed.PushUndo(std::string("Add ") + name);
    GameObject* h = ed.scene().CreateGameObject(name);
    h->transform->SetParent(ed.PrefabRootObject()->transform, false);
    ed.dirty = true;
}

void TestRefusals() {
    RUN_SUITE("prefab mode: what can and cannot be opened");
    TempDir d; Files f = MakeFiles(d);
    EditorState ed; MakeScene(ed, f);
    std::string why;
    CHECK(!ed.CanOpenPrefab(d.Sub("nope.okayprefab"), &why) && Has(why, "doesn't exist"));
    CHECK(!ed.CanOpenPrefab(f.scene, &why) && Has(why, "not a prefab"));
    // A variant keeps its link to the base: it is not opened as a plain prefab.
    Scene sc("t"); std::string err;
    GameObject* inst = PrefabTools::Instantiate(sc, f.enemy, {}, &err);
    const std::string var = d.Sub("boss.okayprefab");
    CHECK(PrefabTools::SaveVariant(*inst, var, &err));
    CHECK(!ed.CanOpenPrefab(var, &why) && Has(why, "variant"));
    CHECK(!ed.OpenPrefab(var, &why) && !ed.InPrefabMode());
    CHECK(!ed.ClosePrefab(false, &why) && Has(why, "No prefab is open"));
    CHECK(!ed.SavePrefab(&why) && !ed.RevertPrefab(&why));
    CHECK(ed.CanOpenPrefab(f.enemy));
    CHECK(ed.OpenPrefab(f.enemy));
    CHECK(!ed.CanOpenPrefab(f.enemy, &why) && Has(why, "prefab you are editing"));   // not itself
    CHECK(ed.PlayBlockedReason().find("Play is off") == 0);
}

void TestOpenSaveClose() {
    RUN_SUITE("prefab mode: open, edit, save, go back");
    TempDir d; Files f = MakeFiles(d);
    EditorState ed; MakeScene(ed, f);
    CHECK(!ed.InPrefabMode() && ed.PrefabDepth() == 0 && ed.PlayBlockedReason().empty());
    // something to restore: an edit with its undo step, a selection, a camera
    ed.PushUndo("Move Pre");
    Find(ed, "Pre")->transform->localPosition = {9, 9, 0};
    ed.dirty = true;
    ed.Select(Find(ed, "EnemyB"));
    ed.cameraPos = {12, 7}; ed.cameraZoom = 31.0f; ed.camYaw = 77.0f; ed.camTarget = {4, 5, 6}; ed.view3D = true;
    const std::string selGuid = Find(ed, "EnemyB")->Guid();
    const std::string guidA = Find(ed, "EnemyA")->Guid();
    ed.CommitUndo();
    const int sceneUndo0 = ed.UndoDepth();
    const std::string sceneName = ed.scene().Name();
    CHECK(sceneUndo0 >= 1 && ed.dirty);
    const std::vector<std::string> order0 = TopLevel(ed);

    std::string err;
    CHECK(ed.OpenPrefab(f.enemy, &err));
    CHECK(ed.InPrefabMode() && ed.PrefabDepth() == 1 && ed.PrefabPath() == f.enemy);
    // the prefab alone: root + Gun, the root is a plain object (it IS the prefab), the scene's objects are not here
    CHECK(Count(ed, "Enemy") == 1 && Count(ed, "Gun") == 1 && Count(ed, "Pre") == 0 && Count(ed, "EnemyA") == 0);
    GameObject* root = ed.PrefabRootObject();
    CHECK(root && root->name == "Enemy" && root->prefabSource.empty() && ed.selected() == root);
    CHECK(ed.scene().Name() == "enemy");
    // a fresh history and no unsaved mark: the scene's step is not in this mode's undo list
    CHECK(ed.UndoDepth() == 0 && !ed.CanUndo() && !ed.dirty && !ed.HasUnsavedChanges());
    ed.Tick(0.016f);                         // a frame of the editor does not change the prefab by itself
    ed.CaptureUnannouncedEdits();
    CHECK(ed.UndoDepth() == 0 && !ed.HasUnsavedChanges());
    // crumbs: Scene > enemy; the scene has unsaved changes, the prefab does not
    auto cr = ed.PrefabCrumbs();
    CHECK(cr.size() == 2 && cr[0].path.empty() && cr[0].unsaved && cr[1].label == "enemy" && !cr[1].unsaved);
    CHECK(ed.PrefabOuterUnsaved());
    // Play is off; saving or opening a scene is refused (the scene is put aside)
    ed.Play();
    CHECK(!ed.isPlaying());
    CHECK(!ed.Save(f.scene, &err) && Has(err, "prefab is open"));
    CHECK(!ed.Load(f.scene, &err) && Has(err, "prefab is open"));
    CHECK(ed.InPrefabMode());

    // ---- dirty tracking and undo in this level
    AddHat(ed);
    ed.CommitUndo();
    CHECK(ed.dirty && ed.HasUnsavedChanges() && ed.UndoDepth() == 1 && ed.UndoName() == "Add Hat");
    CHECK(ed.PrefabCrumbs()[1].unsaved && ed.PrefabUnsavedFrom(1) && !ed.PrefabUnsavedFrom(2));
    CHECK(Count(ed, "Hat") == 1);
    CHECK(ed.Undo());
    CHECK(Count(ed, "Hat") == 0 && !ed.HasUnsavedChanges() && !ed.dirty);   // back on the saved text: clean again
    CHECK(ed.Redo());
    CHECK(Count(ed, "Hat") == 1 && ed.HasUnsavedChanges());
    CHECK(ed.PrefabRootObject() && ed.PrefabRootObject()->name == "Enemy");

    // ---- an object outside the root is reported, not saved
    ed.PushUndo("Stray");
    ed.scene().CreateGameObject("Stray");
    std::string warn;
    CHECK(ed.SavePrefab(&err, &warn));
    CHECK(Has(warn, "1 object sits outside"));
    CHECK(!Has(ReadAll(f.enemy), "Stray") && Has(ReadAll(f.enemy), "Hat"));
    CHECK(!ed.dirty && !ed.HasUnsavedChanges());
    CHECK(ed.UndoDepth() >= 1);   // saving keeps the history

    // ---- back: the scene is as it was, and the instances are rebuilt from the saved file
    const std::string undoFlowBefore = ed.UndoName();
    (void)undoFlowBefore;
    CHECK(ed.ClosePrefab(false, &err));
    CHECK(!ed.InPrefabMode() && ed.PrefabDepth() == 0 && ed.scene().Name() == sceneName);
    CHECK(ed.cameraPos.x == 12 && ed.cameraPos.y == 7 && ed.cameraZoom == 31.0f && ed.camYaw == 77.0f && ed.camTarget.z == 6.0f && ed.view3D);
    CHECK(Count(ed, "Hat") == 3 && Count(ed, "Pre") == 1 && Count(ed, "Stray") == 0);   // both Enemy instances and the one inside the Squad got the Hat
    CHECK(Find(ed, "Pre")->transform->localPosition.x == 9);                            // the scene's own unsaved edit is untouched
    CHECK(ed.selected() && ed.selected()->name == "EnemyB" && ed.selected()->Guid() == selGuid);   // selection restored by id
    CHECK(Find(ed, "EnemyA")->Guid() == guidA);                                         // rebuilt instances keep their ids
    CHECK(Find(ed, "EnemyB")->transform->localPosition.x == 5 && Find(ed, "EnemyB")->transform->localPosition.y == 2);
    CHECK(Find(ed, "EnemyB")->GetComponent<SpriteRenderer>()->color.r == 1 && Find(ed, "EnemyB")->GetComponent<SpriteRenderer>()->color.g == 0);   // overrides kept
    CHECK(Find(ed, "SquadA")->transform->localPosition.x == -3);
    CHECK(TopLevel(ed) == order0);                                                      // top-level order unchanged
    CHECK(Has(ed.PrefabNote(), "Updated 3 instances of enemy"));
    // the Squad instance holds an Enemy too: it was rebuilt as well (3 Hats would mean the nested one too)
    int hatsUnderSquad = 0;
    for (const auto& u : ed.scene().Objects()) if (u->name == "Hat" && u->transform->Parent() && u->transform->Parent()->gameObject->name == "Lead") ++hatsUnderSquad;
    CHECK(hatsUnderSquad == 1);
    // the scene has its own history back, plus two steps made by going into and out of prefab mode:
    // the overrides of its instances were recorded (EnemyB was moved and recoloured), and the refresh
    CHECK(ed.dirty && ed.UndoDepth() == sceneUndo0 + 2 && ed.UndoName() == "Update Prefab Instances");
    CHECK(ed.Undo());
    CHECK(Count(ed, "Hat") == 0 && Find(ed, "EnemyB")->Guid() == selGuid && Find(ed, "EnemyB")->transform->localPosition.x == 5);
    CHECK(ed.UndoName() == "Record Prefab Overrides");
    CHECK(ed.Undo());
    CHECK(ed.UndoName() == "Move Pre");
    CHECK(ed.Redo() && ed.Redo());
    CHECK(Count(ed, "Hat") == 3 && Find(ed, "EnemyB")->transform->localPosition.x == 5);
}

void TestDiscardAndRevert() {
    RUN_SUITE("prefab mode: discard, revert, nothing to refresh");
    TempDir d; Files f = MakeFiles(d);
    EditorState ed; MakeScene(ed, f);
    std::string err;
    CHECK(ed.OpenPrefab(f.enemy, &err) && ed.ClosePrefabTo(0, false, &err));   // a first visit records the instances' overrides (a step); later ones change nothing
    const std::string before = Ser(ed);
    const std::string fileBefore = ReadAll(f.enemy);
    const int undo0 = ed.UndoDepth();
    CHECK(ed.OpenPrefabOf(Find(ed, "EnemyB"), &err));
    CHECK(ed.PrefabPath() == f.enemy);
    AddHat(ed);
    ed.CommitUndo();
    CHECK(ed.HasUnsavedChanges());
    // Revert reads the file again: one undo step, the level counts as saved
    CHECK(ed.RevertPrefab(&err));
    CHECK(Count(ed, "Hat") == 0 && !ed.dirty && !ed.HasUnsavedChanges() && ed.PrefabRootObject() && ed.selected() == ed.PrefabRootObject());
    CHECK(ed.UndoName() == "Revert Prefab");
    CHECK(ed.Undo());                       // Undo brings the edits back, and they count as unsaved again
    CHECK(Count(ed, "Hat") == 1 && ed.HasUnsavedChanges() && ed.PrefabRootObject());
    CHECK(ed.Redo());
    CHECK(Count(ed, "Hat") == 0 && !ed.HasUnsavedChanges());
    // edit again and leave without saving: the file and the scene are untouched
    AddHat(ed);
    ed.CommitUndo();
    CHECK(ed.ClosePrefab(false, &err));
    CHECK(ReadAll(f.enemy) == fileBefore);
    CHECK(Ser(ed) == before);
    CHECK(ed.UndoDepth() == undo0 && !ed.dirty && ed.PrefabNote().empty());
    // Save then Back with no edit at all: the file is rewritten the same and nothing breaks
    CHECK(ed.OpenPrefab(f.enemy, &err));
    CHECK(ed.ClosePrefabTo(0, true, &err) && !ed.InPrefabMode());
}

void TestSaveFailureAndDeletedRoot() {
    RUN_SUITE("prefab mode: a failed save keeps you in the prefab");
    TempDir d; Files f = MakeFiles(d);
    EditorState ed; MakeScene(ed, f);
    std::string err;
    CHECK(ed.OpenPrefab(f.enemy, &err));
    AddHat(ed);
    ed.CommitUndo();
    ed.PushUndo("Delete root");
    ed.scene().Destroy(ed.PrefabRootObject());
    ed.scene().FlushDestroyed();
    ed.Select(nullptr);
    ed.dirty = true;
    CHECK(ed.PrefabRootObject() == nullptr);
    const std::string file = ReadAll(f.enemy);
    CHECK(!ed.SavePrefab(&err) && Has(err, "root object was deleted"));
    CHECK(!ed.ClosePrefab(true, &err) && ed.InPrefabMode());   // leaving with Save is cancelled
    CHECK(ReadAll(f.enemy) == file);
    CHECK(ed.Undo());                                          // undo the delete: saving works again
    CHECK(ed.PrefabRootObject() != nullptr);
    CHECK(ed.ClosePrefab(true, &err) && !ed.InPrefabMode());
    CHECK(Has(ReadAll(f.enemy), "Hat"));
}

void TestNested() {
    RUN_SUITE("prefab mode: nested prefabs, drill in and back out");
    TempDir d; Files f = MakeFiles(d);
    EditorState ed; MakeScene(ed, f);
    std::string err;
    const std::string sceneBefore = Ser(ed);
    CHECK(ed.OpenPrefabOf(Find(ed, "SquadA"), &err));
    CHECK(ed.PrefabPath() == f.squad && ed.PrefabDepth() == 1);
    GameObject* lead = ed.scene().Find("Lead");
    CHECK(lead && !lead->prefabSource.empty());            // the nested prefab is a linked instance here
    // an edit in the Squad, then drill into the nested Enemy
    ed.PushUndo("Rename Squad");
    ed.PrefabRootObject()->name = "Squad";
    ed.dirty = true;
    ed.CommitUndo();
    CHECK(ed.OpenPrefabOf(lead, &err));
    CHECK(ed.PrefabDepth() == 2 && ed.PrefabPath() == f.enemy);
    auto cr = ed.PrefabCrumbs();
    CHECK(cr.size() == 3 && cr[0].path.empty() && cr[1].label == "squad" && cr[2].label == "enemy");
    CHECK(cr[1].unsaved && !cr[2].unsaved);                 // the Squad level keeps its own unsaved mark
    CHECK(ed.UndoDepth() == 0);                             // and the nested level its own history
    // the prefab being edited further out cannot be opened again; a prefab not open can
    std::string why;
    CHECK(!ed.CanOpenPrefab(f.squad, &why) && Has(why, "already open"));
    AddHat(ed);
    ed.CommitUndo();
    CHECK(ed.SavePrefab(&err));
    // back one level: the Squad doc has the rebuilt Lead (with the Hat), its own edit, and is still unsaved
    CHECK(ed.ClosePrefab(false, &err));
    CHECK(ed.PrefabDepth() == 1 && ed.PrefabPath() == f.squad);
    CHECK(ed.PrefabRootObject() && ed.PrefabRootObject()->name == "Squad");
    CHECK(Count(ed, "Hat") == 1 && ed.scene().Find("Lead") && !ed.scene().Find("Lead")->prefabSource.empty());
    CHECK(ed.dirty && Has(ed.PrefabNote(), "Updated 1 instance of enemy"));
    CHECK(ed.UndoName() == "Update Prefab Instances");
    // save the Squad: the nested prefab stays a link in the file, not a copy
    CHECK(ed.SavePrefab(&err));
    CHECK(Has(ReadAll(f.squad), "nestedprefab"));
    // back to the scene: the Squad instance is rebuilt, nested Enemy and its Hat inside
    CHECK(ed.ClosePrefabTo(0, false, &err));
    CHECK(!ed.InPrefabMode());
    GameObject* sq = Find(ed, "SquadA");
    CHECK(sq && sq->name == "SquadA" && sq->transform->localPosition.x == -3);
    CHECK(Count(ed, "Hat") == 3);                           // EnemyA and EnemyB (instances of the Enemy saved in the inner level) and the one inside the Squad
    CHECK(Find(ed, "EnemyB")->transform->localPosition.x == 5 && Find(ed, "EnemyB")->GetComponent<SpriteRenderer>()->color.g == 0);   // their overrides survived
    CHECK(Has(ed.PrefabNote(), "Updated 3 instances"));
    bool hatInLead = false;
    for (const auto& u : ed.scene().Objects()) if (u->name == "Hat" && u->transform->Parent() && u->transform->Parent()->gameObject->name == "Lead") hatInLead = true;
    CHECK(hatInLead);
    CHECK(Ser(ed) != sceneBefore && ed.dirty);

    // Back out of two levels at once with unsaved edits in both: discarded together, the scene untouched
    const std::string s1 = Ser(ed);
    CHECK(ed.OpenPrefabOf(Find(ed, "SquadA"), &err));
    ed.PushUndo("Rename Squad"); ed.PrefabRootObject()->name = "Renamed"; ed.dirty = true; ed.CommitUndo();
    CHECK(ed.OpenPrefabOf(ed.scene().Find("Lead"), &err));
    AddHat(ed, "Hat2"); ed.CommitUndo();
    CHECK(ed.PrefabUnsavedFrom(1));
    CHECK(ed.ClosePrefabTo(0, false, &err));
    CHECK(Ser(ed) == s1);
    CHECK(!ed.ClosePrefabTo(3, false, &err) && Has(err, "not open"));
}

void TestSaveAllAndLeavingTheSession() {
    RUN_SUITE("prefab mode: save everything, new scene, asset moves");
    TempDir d; Files f = MakeFiles(d);
    EditorState ed; MakeScene(ed, f);
    std::string err;
    // a scene edit, then two prefab levels each with an edit: SaveAll writes the scene and both prefabs
    ed.PushUndo("Move Pre"); Find(ed, "Pre")->transform->localPosition = {7, 0, 0}; ed.dirty = true; ed.CommitUndo();
    CHECK(ed.OpenPrefabOf(Find(ed, "SquadA"), &err));
    ed.PushUndo("Rename Squad"); ed.PrefabRootObject()->name = "SquadX"; ed.dirty = true; ed.CommitUndo();
    CHECK(ed.OpenPrefabOf(ed.scene().Find("Lead"), &err));
    AddHat(ed, "Cap"); ed.CommitUndo();
    CHECK(ed.PrefabOuterUnsaved());
    CHECK(ed.SaveAllPrefabLevels(&err));
    CHECK(Has(ReadAll(f.enemy), "Cap") && Has(ReadAll(f.squad), "SquadX") && Has(ReadAll(f.scene), "\"Pre\""));
    CHECK(!ed.HasUnsavedChanges() && !ed.PrefabOuterUnsaved());
    SceneSerializer::ClearPrefabCache();
    {   // the scene file now holds the unsaved scene edit
        Scene chk("c"); CHECK(SceneSerializer::LoadFromFile(chk, f.scene, nullptr));
        CHECK(chk.Find("Pre")->transform->localPosition.x == 7);
    }
    // an asset the scene uses moves while the prefab is open: the scene put aside is rewritten too
    CHECK(ed.ClosePrefabTo(0, false, &err));
    ed.PushUndo(); Find(ed, "Pre")->AddComponent<SpriteRenderer>()->texture = "Assets/old.png"; ed.dirty = true; ed.CommitUndo();
    CHECK(ed.OpenPrefab(f.enemy, &err));
    CHECK(ed.RewriteAssetPaths([](const std::string& s) {
        std::string r = s;
        for (std::size_t p; (p = r.find("Assets/old.png")) != std::string::npos;) r.replace(p, 14, "Assets/new.png");
        return r;
    }) == false);   // this prefab does not use it
    CHECK(ed.ClosePrefabTo(0, false, &err));
    CHECK(Find(ed, "Pre")->GetComponent<SpriteRenderer>()->texture == "Assets/new.png");
    // starting a new scene ends the session
    CHECK(ed.OpenPrefab(f.enemy, &err));
    ed.NewScene();
    CHECK(!ed.InPrefabMode() && ed.PrefabDepth() == 0 && ed.PlayBlockedReason().empty());
    // and Play works again outside prefab mode
    ed.Play();
    CHECK(ed.isPlaying());
    ed.Stop();
    // opening while Play runs is refused
    ed.Play();
    CHECK(!ed.OpenPrefab(f.enemy, &err) && Has(err, "Stop Play"));
    ed.Stop();
}

void TestRefreshKeepsPlace() {
    RUN_SUITE("prefab instances: a rebuilt top-level instance keeps its place");
    TempDir d; Files f = MakeFiles(d);
    EditorState ed; MakeScene(ed, f);
    const std::vector<std::string> order = TopLevel(ed);
    Scene& sc = ed.scene();
    GameObject* a = sc.Find("EnemyA");
    std::string err;
    GameObject* fresh = PrefabTools::Reapply(sc, *a, &err);
    CHECK(fresh != nullptr);
    sc.FlushDestroyed();
    CHECK(TopLevel(ed) == order);
}

}  // namespace

int main() {
    RUN_SUITE("prefab_mode");
    Log::console = false;
    Game::Reset();
    Time::ResetForTesting();
    TestRefusals();
    TestOpenSaveClose();
    TestDiscardAndRevert();
    TestSaveFailureAndDeletedRoot();
    TestNested();
    TestSaveAllAndLeavingTheSession();
    TestRefreshKeepsPlace();
    TEST_MAIN_RESULT();
}
