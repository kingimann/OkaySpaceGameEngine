// Editor hide and selection sets (editor/EditorVisibility, ImGui-free): hide/unhide of a selection and its
// children, picking and drawing skip hidden objects, Play and `active` are untouched, the scene file round
// trip and an old file, how Hide / Isolate / Lock combine, sets (create, rename, delete, select, add, remove,
// hide, lock), sets whose objects were deleted, and one undo step for each action.
#include "test_framework.hpp"
#include "EditorState.hpp"
#include "EditorVisibility.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace okay;
using namespace okay::editor;
namespace fs = std::filesystem;

static GameObject* Make(Scene& s, const std::string& name, GameObject* parent = nullptr) {
    GameObject* g = s.CreateGameObject(name);
    if (parent) g->transform->SetParent(parent->transform, false);
    return g;
}
static GameObject* Find(Scene& s, const std::string& name) { return s.Find(name); }
static std::string Roundtrip(const Scene& s) { return SceneSerializer::Serialize(s); }

static void TestHideBasics() {
    EditorState ed;
    GameObject* a = Make(ed.scene(), "A");
    GameObject* b = Make(ed.scene(), "B", a);
    GameObject* c = Make(ed.scene(), "C");
    ed.Select(a);
    CHECK(vis::HideSelection(ed) == 1);
    CHECK(a->editorHidden && !b->editorHidden && !c->editorHidden);
    CHECK(vis::IsHidden(a) && vis::IsHidden(b) && !vis::IsHidden(c));   // children follow the parent
    CHECK(a->active && b->active);                                      // never touches `active`
    CHECK(ed.selected() == nullptr);                                    // what you hid leaves the selection
    CHECK(vis::HiddenCount(ed.scene()) == 1);
    CHECK(!vis::SceneViewVisible(a, nullptr) && !vis::SceneViewVisible(b, nullptr) && vis::SceneViewVisible(c, nullptr));
    CHECK(!vis::ScenePickable(b, nullptr) && vis::ScenePickable(c, nullptr));
    CHECK(vis::HideSelection(ed) == 0);                                 // nothing selected: nothing to do
    // Hiding a child of a hidden parent is its own flag; showing the parent leaves the child hidden.
    CHECK(vis::SetHidden(ed, {b}, true) == 1);
    CHECK(vis::SetHidden(ed, {a}, false) == 1);
    CHECK(vis::IsHidden(b) && !vis::IsHidden(a));
    CHECK(vis::UnhideAll(ed) == 1);
    CHECK(vis::HiddenCount(ed.scene()) == 0 && !vis::IsHidden(b));
    CHECK(vis::UnhideAll(ed) == 0);
}

static void TestHideDeselectsCreatedObject() {
    // CreateCube & co. make the object the primary selection without putting it in the multi list.
    EditorState ed;
    GameObject* c = ed.CreateCube();
    CHECK(ed.selected() == c);
    CHECK(vis::HideSelection(ed) == 1);
    CHECK(ed.selected() == nullptr && !ed.IsSelected(c));
}

static void TestSceneViewScope() {
    EditorState ed;
    GameObject* a = Make(ed.scene(), "A");
    GameObject* b = Make(ed.scene(), "B", a);
    GameObject* c = Make(ed.scene(), "C");
    GameObject* off = Make(ed.scene(), "Off"); off->active = false;
    a->editorHidden = true;
    {
        vis::SceneViewHideScope scope(ed.scene(), nullptr);
        CHECK(scope.Count() == 2);                    // A and B (Off was already inactive and stays so)
        CHECK(!a->active && !b->active && c->active && !off->active);
    }
    CHECK(a->active && b->active && c->active && !off->active);   // exactly what it changed is restored
    // Objects destroyed while the scope is open do not crash the restore.
    {
        vis::SceneViewHideScope scope(ed.scene(), nullptr);
        ed.scene().Destroy(b);
        ed.scene().FlushDestroyed();
    }
    CHECK(a->active);
}

static void TestIsolateLockHide() {
    EditorState ed;
    Scene& s = ed.scene();
    GameObject* root = Make(s, "Root");
    GameObject* kid = Make(s, "Kid", root);
    GameObject* other = Make(s, "Other");
    GameObject* lamp = Make(s, "Lamp"); lamp->AddComponent<Light>();
    // Isolate alone.
    CHECK(vis::SceneViewVisible(root, root) && vis::SceneViewVisible(kid, root));
    CHECK(!vis::SceneViewVisible(other, root) && !vis::ScenePickable(other, root));
    CHECK(vis::SceneViewVisible(lamp, root));                         // lights stay on, as before
    // Hide wins over isolate: a hidden child of the isolated object stays hidden ...
    kid->editorHidden = true;
    CHECK(!vis::SceneViewVisible(kid, root) && vis::SceneViewVisible(root, root));
    // ... and isolating a hidden object shows none of it.
    root->editorHidden = true;
    CHECK(!vis::SceneViewVisible(root, root) && !vis::SceneViewVisible(kid, root));
    root->editorHidden = false; kid->editorHidden = false;
    // Lock only stops clicks; it does not hide, and hiding does not touch it.
    root->editorLocked = true;
    CHECK(vis::SceneViewVisible(root, nullptr) && !vis::ScenePickable(root, nullptr));
    root->editorHidden = true;
    CHECK(root->editorLocked && root->editorHidden && !vis::SceneViewVisible(root, nullptr));
    root->editorHidden = false;
    CHECK(root->editorLocked && vis::ScenePickable(kid, nullptr));    // the lock flag is per object
    // Hide + lock + isolate together: visible only when none of the rules removes it.
    for (int mask = 0; mask < 8; ++mask) {
        other->editorHidden = mask & 1; other->editorLocked = mask & 2;
        const GameObject* iso = (mask & 4) ? root : nullptr;
        const bool visible = !(mask & 1) && !(mask & 4);
        CHECK(vis::SceneViewVisible(other, iso) == visible);
        CHECK(vis::ScenePickable(other, iso) == (visible && !(mask & 2)));
    }
    // Hierarchy selection still works on anything: Select does not look at any of the three flags.
    other->editorHidden = true; other->editorLocked = true;
    ed.Select(other);
    CHECK(ed.selected() == other);
}

static void TestSaveLoad() {
    EditorState ed;
    Scene& s = ed.scene();
    GameObject* a = Make(s, "A"); GameObject* b = Make(s, "B", a); GameObject* c = Make(s, "C");
    a->editorHidden = true; c->editorLocked = true;
    ed.Select(b);
    const int idx = vis::CreateSet(ed, "Walls");
    CHECK(idx == 0);
    const std::string text = Roundtrip(s);
    CHECK(text.find("  hidden 1\n") != std::string::npos);
    CHECK(text.find("selset \"Walls\"") != std::string::npos);
    Scene t("t");
    CHECK(SceneSerializer::Deserialize(t, text));
    CHECK(Find(t, "A")->editorHidden && !Find(t, "B")->editorHidden && !Find(t, "C")->editorHidden);
    CHECK(Find(t, "C")->editorLocked);
    CHECK(Find(t, "A")->active && Find(t, "B")->active);              // hidden never made anything inactive
    CHECK(t.selectionSets.size() == 1 && t.selectionSets[0].name == "Walls" && t.selectionSets[0].guids.size() == 1);
    CHECK(t.selectionSets[0].guids[0] == Find(t, "B")->Guid());
    CHECK(Roundtrip(t) == text);                                      // stable
    // A file without any of it (an older scene) loads with nothing hidden and no sets.
    Scene plain("plain");
    Make(plain, "X");
    std::string old = Roundtrip(plain);
    CHECK(old.find("hidden") == std::string::npos && old.find("selset") == std::string::npos);
    Scene u("u");
    u.selectionSets.push_back({"stale", {}});
    CHECK(SceneSerializer::Deserialize(u, old));
    CHECK(u.selectionSets.empty() && !Find(u, "X")->editorHidden);    // and a load replaces what was there
    // A hidden name with quotes and a line break survives as one record.
    ed.Select(c);
    int q = vis::CreateSet(ed, "my \"set\"\nline");
    CHECK(q >= 0);
    Scene v("v");
    CHECK(SceneSerializer::Deserialize(v, Roundtrip(s)));
    CHECK(v.selectionSets.size() == 2 && v.selectionSets[1].name == "my \"set\" line");
    // A prefab file never carries hide, and an instance keeps its own hide in the scene.
    const fs::path dir = fs::temp_directory_path() / "okay_hide_prefab_test";
    fs::create_directories(dir);
    const std::string path = (dir / "thing.okayprefab").string();
    Scene src("src");
    GameObject* root = Make(src, "Thing"); Make(src, "Arm", root);
    root->editorHidden = true;
    CHECK(SceneSerializer::SaveObjectToFile(*root, path));
    { std::ifstream f(path); std::stringstream ss; ss << f.rdbuf(); CHECK(ss.str().find("hidden") == std::string::npos); }
    Scene live("live");
    GameObject* inst = SceneSerializer::InstantiateFromFile(live, path);
    CHECK(inst && !inst->editorHidden);
    inst->editorHidden = true;                                        // hide the instance root
    GameObject* arm = live.Find("Arm");
    CHECK(arm && vis::IsHidden(arm));                                 // its children follow
    Scene back("back");
    CHECK(SceneSerializer::Deserialize(back, Roundtrip(live)));
    GameObject* inst2 = back.Find("Thing");
    CHECK(inst2 && inst2->editorHidden && inst2->prefabSource == path && vis::IsHidden(back.Find("Arm")));
    CHECK(inst2->active);
    fs::remove_all(dir);
}

struct Probe : public Behaviour {
    int updates = 0;
    void Update(float) override { ++updates; }
};

static void TestPlayUnaffected() {
    EditorState ed;
    GameObject* a = Make(ed.scene(), "A");
    Probe* p = a->AddComponent<Probe>();
    Make(ed.scene(), "B", a);
    a->editorHidden = true;
    ed.Play();
    for (int i = 0; i < 3; ++i) ed.PlayFrame(0.016, false);
    CHECK(p->updates == 3);                         // a hidden object runs like any other
    CHECK(a->active && a->editorHidden);
    ed.Stop();
    CHECK(Find(ed.scene(), "A")->editorHidden && Find(ed.scene(), "A")->active);   // Stop restores it as saved
}

static void TestSets() {
    EditorState ed;
    Scene& s = ed.scene();
    GameObject* a = Make(s, "A"); GameObject* b = Make(s, "B"); GameObject* c = Make(s, "C"); GameObject* d = Make(s, "D", c);
    ed.Select(nullptr);
    CHECK(vis::CreateSet(ed, "x") == -1);                              // nothing selected
    ed.Select(a); ed.ToggleSelect(b);
    CHECK(vis::CreateSet(ed, "") == 0 && s.selectionSets[0].name == "Set 1");
    CHECK(vis::CreateSet(ed, "Set 1") == 1 && s.selectionSets[1].name == "Set 1 2");
    CHECK(vis::RenameSet(ed, 1, "  Props  ") && s.selectionSets[1].name == "Props");
    CHECK(!vis::RenameSet(ed, 1, "Set 1"));                            // taken
    CHECK(!vis::RenameSet(ed, 1, "   "));                              // empty
    CHECK(!vis::RenameSet(ed, 9, "z"));
    CHECK(vis::DeleteSet(ed, 1) && s.selectionSets.size() == 1 && !vis::DeleteSet(ed, 5));
    // Select set replaces the selection; additive keeps it.
    ed.Select(c);
    int dropped = -1;
    CHECK(vis::SelectSet(ed, 0, false, &dropped) == 2 && dropped == 0);
    CHECK(ed.IsSelected(a) && ed.IsSelected(b) && !ed.IsSelected(c));
    ed.Select(c);
    CHECK(vis::SelectSet(ed, 0, true) == 2 && ed.IsSelected(a) && ed.IsSelected(c));
    // Add / remove the selection.
    ed.Select(d);
    CHECK(vis::AddSelectionToSet(ed, 0) == 1 && vis::AddSelectionToSet(ed, 0) == 0);
    CHECK(s.selectionSets[0].guids.size() == 3);
    CHECK(vis::RemoveSelectionFromSet(ed, 0) == 1 && vis::RemoveSelectionFromSet(ed, 0) == 0);
    CHECK(s.selectionSets[0].guids.size() == 2);
    // Hide / show / lock / unlock the set.
    CHECK(vis::SetSetHidden(ed, 0, true) == 2 && a->editorHidden && b->editorHidden && !c->editorHidden);
    CHECK(vis::SetSetHidden(ed, 0, true) == 0);
    CHECK(vis::SetSetHidden(ed, 0, false) == 2 && !a->editorHidden);
    CHECK(vis::SetSetLocked(ed, 0, true) == 2 && a->editorLocked && b->editorLocked && !c->editorLocked);
    CHECK(vis::SetSetLocked(ed, 0, false) == 2 && !a->editorLocked);
    // Locking a set locks the whole subtree of each member, like the Hierarchy lock.
    ed.Select(c); CHECK(vis::AddSelectionToSet(ed, 0) == 1);
    CHECK(vis::SetSetLocked(ed, 0, true) == 3 && d->editorLocked);     // C, plus A and B; D follows C
    // A set holding a hidden object and a locked one keeps both flags.
    CHECK(vis::SetSetHidden(ed, 0, true) >= 1 && c->editorHidden && c->editorLocked);
}

static void TestDeadGuids() {
    EditorState ed;
    Scene& s = ed.scene();
    GameObject* a = Make(s, "A"); GameObject* b = Make(s, "B"); GameObject* c = Make(s, "C");
    ed.Select(a); ed.ToggleSelect(b); ed.ToggleSelect(c);
    CHECK(vis::CreateSet(ed, "All") == 0);
    ed.Select(b);
    ed.DeleteSelected();
    CHECK(Find(s, "B") == nullptr);
    vis::SetResolve r = vis::ResolveSet(s, 0);
    CHECK(r.live.size() == 2 && r.dead == 1);
    CHECK(s.selectionSets[0].guids.size() == 3);                       // looking does not change anything
    int dropped = 0;
    CHECK(vis::SelectSet(ed, 0, false, &dropped) == 2 && dropped == 1);
    CHECK(s.selectionSets[0].guids.size() == 2);                       // now it is forgotten
    vis::SelectSet(ed, 0, false, &dropped);
    CHECK(dropped == 0);
    // Hide a set with a dead member: the live ones are hidden and the dead one counted.
    ed.Select(c); ed.DeleteSelected();
    dropped = 0;
    CHECK(vis::SetSetHidden(ed, 0, true, &dropped) == 1 && dropped == 1 && Find(s, "A")->editorHidden);
    CHECK(s.selectionSets[0].guids.size() == 1);
    // Lock a set whose members are all gone: the set is emptied, nothing crashes.
    ed.Select(Find(s, "A")); ed.DeleteSelected();
    dropped = 0;
    CHECK(vis::SetSetLocked(ed, 0, true, &dropped) == 0 && dropped == 1 && s.selectionSets[0].guids.empty());
    CHECK(vis::SelectSet(ed, 0) == 0);
    // Out-of-range indices are refused, never read.
    CHECK(vis::SelectSet(ed, 7) == 0 && vis::SetSetHidden(ed, -1, true) == 0 && vis::ResolveSet(s, 3).live.empty());
}

static void TestUndo() {
    EditorState ed;
    Scene& s = ed.scene();
    GameObject* a = Make(s, "A"); Make(s, "B");
    ed.Select(a);
    // Hide: one undo step, redo brings it back.
    const int d0 = ed.UndoDepth();
    vis::HideSelection(ed);
    CHECK(ed.UndoDepth() == d0 + 1 && Find(s, "A")->editorHidden);
    CHECK(ed.Undo() && !Find(s, "A")->editorHidden);
    CHECK(ed.Redo() && Find(s, "A")->editorHidden);
    CHECK(ed.Undo());
    // Unhide all.
    Find(s, "A")->editorHidden = true; Find(s, "B")->editorHidden = true;
    const int d1 = ed.UndoDepth();
    CHECK(vis::UnhideAll(ed) == 2 && ed.UndoDepth() == d1 + 1);
    CHECK(ed.Undo() && Find(s, "A")->editorHidden && Find(s, "B")->editorHidden);
    CHECK(ed.Redo() && !Find(s, "A")->editorHidden);
    // Sets: create, rename, add, remove, hide, lock, delete: each one step, each undone and redone.
    ed.Select(Find(s, "A"));
    const int base = ed.UndoDepth();
    CHECK(vis::CreateSet(ed, "S") == 0);
    CHECK(vis::RenameSet(ed, 0, "T"));
    ed.Select(Find(s, "B"));
    CHECK(vis::AddSelectionToSet(ed, 0) == 1);
    CHECK(vis::RemoveSelectionFromSet(ed, 0) == 1);
    CHECK(vis::SetSetHidden(ed, 0, true) == 1);
    CHECK(vis::SetSetLocked(ed, 0, true) == 1);
    CHECK(vis::DeleteSet(ed, 0));
    CHECK(ed.UndoDepth() == base + 7);
    CHECK(s.selectionSets.empty());
    CHECK(ed.Undo() && s.selectionSets.size() == 1 && s.selectionSets[0].name == "T");
    CHECK(ed.Undo() && !Find(s, "A")->editorLocked);
    CHECK(ed.Undo() && !Find(s, "A")->editorHidden);
    CHECK(ed.Undo() && s.selectionSets[0].guids.size() == 2);
    CHECK(ed.Undo() && s.selectionSets[0].guids.size() == 1);
    CHECK(ed.Undo() && s.selectionSets[0].name == "S");
    CHECK(ed.Undo() && s.selectionSets.empty());
    CHECK(ed.Redo() && s.selectionSets.size() == 1 && s.selectionSets[0].name == "S");
    CHECK(ed.Redo() && s.selectionSets[0].name == "T");
    // A set's members still resolve after an undo rebuilt every object (they are found by GUID).
    CHECK(ed.Redo() && ed.Redo());
    CHECK(vis::ResolveSet(s, 0).live.size() == 1 && vis::ResolveSet(s, 0).dead == 0);   // A, as after the Remove step
    ed.Select(Find(s, "B"));
    CHECK(vis::AddSelectionToSet(ed, 0) == 1);
    // Pruning dead members is part of one step and undoable.
    ed.DeleteSelected();
    int dropped = 0;
    vis::SelectSet(ed, 0, false, &dropped);
    CHECK(dropped == 1 && s.selectionSets[0].guids.size() == 1);
    CHECK(ed.Undo() && s.selectionSets[0].guids.size() == 2);          // back, with the deleted object's GUID
}

int main() {
    RUN_SUITE("editor_visibility");
    TestHideBasics();
    TestHideDeselectsCreatedObject();
    TestSceneViewScope();
    TestIsolateLockHide();
    TestSaveLoad();
    TestPlayUnaffected();
    TestSets();
    TestDeadGuids();
    TestUndo();
    TEST_MAIN_RESULT();
}
