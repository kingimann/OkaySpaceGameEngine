// The editor's undo (ImGui-free EditorState + editor/SceneUndo.hpp): every kind of scene edit goes through
// okay::UndoStack as a command, and for each one the scene text after Undo equals the text before the edit,
// after Redo equals the text after it, and a second Undo is the same as the first (state equality by
// SceneSerializer::Serialize, not by spot checks). Also: merging, grouping, GUID-exact restore of deleted
// objects, the step limit, new / opened scenes ending the history, Play never recording, and the
// saved / unsaved flag following the history.
#include "test_framework.hpp"
#include "../editor/EditorState.hpp"
#include "../editor/EditUndo.hpp"
#include "okay/Scene/SceneSerializer.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace okay;
using okay::editor::EditorState;
using okay::editor::SceneDeltaCommand;
using okay::editor::TextDelta;

namespace {

std::string Ser(const EditorState& ed) { return SceneSerializer::Serialize(ed.scene()); }
GameObject* Find(EditorState& ed, const char* n) { return ed.scene().Find(n); }

struct TempDir {
    fs::path path;
    TempDir() {
        static int n = 0;
        path = fs::temp_directory_path() / ("okay_undo_" + std::to_string(++n) + "_" +
               std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
    std::string Sub(const std::string& s) const { return (path / s).string(); }
};

// A scene with a little of everything: a parent with two children (one with a component), a root.
void Populate(EditorState& ed) {
    GameObject* parent = ed.CreateCube("Parent");
    GameObject* a = ed.CreateCube("ChildA");
    GameObject* b = ed.CreateSprite("ChildB");
    a->transform->SetParent(parent->transform, false);
    b->transform->SetParent(parent->transform, false);
    a->transform->localPosition = {1, 2, 3};
    ed.CreateSprite("Lonely")->AddComponent<Rigidbody2D>();
    ed.CommitUndo();
    ed.undoStack().Clear();       // the setup is not part of what is tested
}

// do / undo / redo / undo-again equality for an edit: `edit` makes the change (and calls PushUndo itself).
template <class F>
void RoundTrip(EditorState& ed, const char* label, const std::string& expectName, F edit) {
    ed.CommitUndo();
    const std::string s0 = Ser(ed);
    const int depth = ed.UndoDepth();
    edit();
    ed.CommitUndo();
    const std::string s1 = Ser(ed);
    const bool changed = s0 != s1;
    if (!changed) { std::cout << "  (no change: " << label << ")\n"; }
    CHECK(changed);
    CHECK(ed.UndoDepth() == depth + 1);
    CHECK(ed.UndoName() == expectName);
    if (ed.UndoName() != expectName) std::cout << "  name for " << label << ": '" << ed.UndoName() << "'\n";
    CHECK(ed.Undo());
    CHECK(Ser(ed) == s0);                      // undo: the scene as it was
    CHECK(ed.CanRedo() && ed.RedoName() == expectName);
    CHECK(ed.Redo());
    CHECK(Ser(ed) == s1);                      // redo: the scene as the edit left it
    CHECK(ed.Undo());
    CHECK(Ser(ed) == s0);                      // undo again: the same as the first time
    CHECK(ed.Redo());
    CHECK(Ser(ed) == s1);
    CHECK(ed.UndoDepth() == depth + 1);
}

// ---- 1. the text delta -------------------------------------------------------------------------------

void TestTextDelta() {
    RUN_SUITE("TextDelta: only the differing stretch is kept");
    const std::string a = "line one\nline two\nline three\nline four\n";
    const std::string b = "line one\nline 2\nline three\nline four\n";
    TextDelta d = TextDelta::Make(a, b);
    CHECK(d.before == "two" && d.after == "2");
    CHECK(d.Bytes() < 10);
    std::string out;
    CHECK(d.ApplyForward(a, out) && out == b);
    CHECK(d.ApplyBackward(b, out) && out == a);
    CHECK(!d.ApplyForward(b, out));                              // not the state it expects: refused
    CHECK(!d.ApplyForward("line one\nline XXX\nline three\nline four\n", out));
    CHECK(TextDelta::Make(a, a).Empty());
    // insertion, deletion and edges
    for (const auto& pr : std::vector<std::pair<std::string, std::string>>{
             {"", "abc"}, {"abc", ""}, {"abc", "abcd"}, {"abc", "xabc"}, {"aaa", "aa"}, {"aXa", "aXXa"}, {"abc", "abc"}}) {
        TextDelta t = TextDelta::Make(pr.first, pr.second);
        std::string o;
        CHECK(t.ApplyForward(pr.first, o) && o == pr.second);
        CHECK(t.ApplyBackward(pr.second, o) && o == pr.first);
    }
    // Compose: two edits of the same place become one; edits far apart do not
    const std::string t0 = "head\nvalue 1\ntail\n", t1 = "head\nvalue 25\ntail\n", t2 = "head\nvalue 399\ntail\n";
    TextDelta ab = TextDelta::Make(t0, t1), bc = TextDelta::Make(t1, t2), ac;
    CHECK(TextDelta::Compose(ab, bc, ac));
    CHECK(ac.ApplyForward(t0, out) && out == t2);
    CHECK(ac.ApplyBackward(t2, out) && out == t0);
    CHECK(ac.before == "1" && ac.after == "399");
    const std::string far0 = "AAAA----------BBBB", far1 = "XAAA----------BBBB", far2 = "XAAA----------BBBY";
    TextDelta f1 = TextDelta::Make(far0, far1), f2 = TextDelta::Make(far1, far2), fj;
    CHECK(!TextDelta::Compose(f1, f2, fj));                      // the text between is not stored: stay two steps
    TextDelta g1 = TextDelta::Make("abcdef", "abXXef"), g2 = TextDelta::Make("abXXef", "abXYZef"), gj;   // overlapping
    CHECK(TextDelta::Compose(g1, g2, gj) && gj.ApplyForward("abcdef", out) && out == "abXYZef");
    TextDelta h1 = TextDelta::Make("abcdef", "abXdef"), h2 = TextDelta::Make("abXdef", "abXYef"), hj;   // next to each other
    CHECK(TextDelta::Compose(h1, h2, hj) && hj.ApplyForward("abcdef", out) && out == "abXYef" && hj.ApplyBackward("abXYef", out) && out == "abcdef");
    // back where it began: no-op
    TextDelta r1 = TextDelta::Make("abc", "abd"), r2 = TextDelta::Make("abd", "abc"), rj;
    CHECK(TextDelta::Compose(r1, r2, rj) && rj.Empty());
}

// ---- 2. the command on a stack, against a stand-in scene ------------------------------------------------

void TestCommandOnStack() {
    RUN_SUITE("SceneDeltaCommand on an UndoStack");
    auto text = std::make_shared<std::string>("a=1\nb=2\n");
    auto acc = std::make_shared<okay::editor::SceneAccess>();
    acc->capture = [text] { return *text; };
    acc->apply = [text](const std::string& t, const okay::editor::SelectionRef&) { *text = t; return true; };
    UndoStack st;
    auto record = [&](const std::string& next, const char* name, std::uint64_t key = 0) {
        const std::string before = *text;
        *text = next;
        return st.Record(std::make_unique<SceneDeltaCommand>(name, acc, TextDelta::Make(before, next),
                                                             okay::editor::SelectionRef{}, okay::editor::SelectionRef{}, key));
    };
    record("a=5\nb=2\n", "Set A");
    record("a=5\nb=9\n", "Set B");
    CHECK(st.UndoCount() == 2 && st.NextUndoName() == "Set B");
    CHECK(st.Undo() && *text == "a=5\nb=2\n");
    CHECK(st.Undo() && *text == "a=1\nb=2\n");
    CHECK(st.Redo() && *text == "a=5\nb=2\n" && st.Redo() && *text == "a=5\nb=9\n");
    // stale: the text was changed behind the stack's back: the step refuses and is dropped, nothing is corrupted
    *text = "a=5\nb=9\nc=7\n";
    CHECK(!st.Undo());
    CHECK(*text == "a=5\nb=9\nc=7\n");
    CHECK(st.UndoCount() == 1 && !st.LastError().empty());
    // merging
    UndoStack m;
    auto rec2 = [&](const std::string& next, std::uint64_t key) {
        const std::string before = *text;
        *text = next;
        return m.Record(std::make_unique<SceneDeltaCommand>("Drag", acc, TextDelta::Make(before, next),
                                                            okay::editor::SelectionRef{}, okay::editor::SelectionRef{}, key));
    };
    *text = "v=0\n";
    rec2("v=1\n", 7); rec2("v=12\n", 7); rec2("v=123\n", 7);
    CHECK(m.UndoCount() == 1);                                    // same key, same place: one step
    CHECK(m.Undo() && *text == "v=0\n" && m.Redo() && *text == "v=123\n");
    rec2("v=1234\n", 8);
    CHECK(m.UndoCount() == 2);                                    // another key: a new step
    rec2("v=1234\n", 8);                                          // nothing changed: nothing recorded
    CHECK(m.UndoCount() == 2);
}

// ---- 3. each kind of editor edit -----------------------------------------------------------------------------

void TestEdits() {
    RUN_SUITE("every kind of scene edit: do, undo, redo, undo again");
    EditorState ed;
    Populate(ed);

    RoundTrip(ed, "create", "Create Cube", [&] { ed.CreateCube("Cube"); });
    RoundTrip(ed, "create sprite", "Create Spr", [&] { ed.CreateSprite("Spr"); });
    RoundTrip(ed, "create camera", "Create Cam", [&] { ed.CreateCamera("Cam"); });
    RoundTrip(ed, "create empty", "Create Empty1", [&] { ed.CreateEmpty("Empty1"); });
    RoundTrip(ed, "duplicate", "Create ChildA (1)", [&] { ed.Select(Find(ed, "ChildA")); ed.DuplicateSelected(); });
    RoundTrip(ed, "rename", "Edit Loner", [&] { ed.PushUndo(); Find(ed, "Lonely")->name = "Loner"; });
    RoundTrip(ed, "tag / active", "Edit Loner", [&] { ed.PushUndo(); Find(ed, "Loner")->active = false; Find(ed, "Loner")->tag = "Enemy"; });
    RoundTrip(ed, "move (gizmo)", "Move Parent", [&] { ed.PushUndo(); Find(ed, "Parent")->transform->localPosition = {5, 6, 7}; });
    RoundTrip(ed, "rotate and scale", "Move Parent", [&] {
        ed.PushUndo();
        Find(ed, "Parent")->transform->localRotation = Quat::LookRotation(Vec3{0, 0, 1});
        Find(ed, "Parent")->transform->localScale = {2, 2, 2};
    });
    RoundTrip(ed, "reparent", "Reparent Loner", [&] { ed.PushUndo(); Find(ed, "Loner")->transform->SetParent(Find(ed, "Parent")->transform, false); });
    RoundTrip(ed, "unparent", "Reparent Loner", [&] { ed.PushUndo(); Find(ed, "Loner")->transform->SetParent(nullptr, false); });
    RoundTrip(ed, "reorder siblings", "Reorder Hierarchy", [&] { ed.PushUndo(); Find(ed, "ChildB")->transform->SetSiblingIndex(0); });
    RoundTrip(ed, "add component", "Edit Parent", [&] { ed.PushUndo(); Find(ed, "Parent")->AddComponent<Rigidbody2D>(); });
    RoundTrip(ed, "component property", "Edit Parent", [&] { ed.PushUndo(); Find(ed, "Parent")->GetComponent<Rigidbody2D>()->mass = 7.5f; });
    RoundTrip(ed, "component enable", "Edit Parent", [&] { ed.PushUndo(); Find(ed, "Parent")->GetComponent<Rigidbody2D>()->enabled = false; });
    RoundTrip(ed, "remove component", "Edit Parent", [&] { ed.PushUndo(); Find(ed, "Parent")->RemoveComponent(Find(ed, "Parent")->GetComponent<Rigidbody2D>()); });
    RoundTrip(ed, "sprite colour", "Edit ChildB", [&] { ed.PushUndo(); Find(ed, "ChildB")->GetComponent<SpriteRenderer>()->color = Color::Green; });
    RoundTrip(ed, "scene settings", "Scene Settings", [&] { ed.PushUndo(); ed.scene().physics().gravity.y = -3.0f; });
    RoundTrip(ed, "scene name", "Scene Settings", [&] { ed.PushUndo(); ed.scene().SetName("Renamed scene"); });
    RoundTrip(ed, "lighting", "Scene Settings", [&] { ed.PushUndo(); ed.scene().renderSettings.fog = true; ed.scene().renderSettings.fogStart = 3.0f; });
    RoundTrip(ed, "delete", "Delete Cube", [&] { ed.Select(Find(ed, "Cube")); ed.DeleteSelected(); });
    RoundTrip(ed, "group", "Group Selection", [&] { ed.Select(Find(ed, "Loner")); ed.GroupSelected(); });
    RoundTrip(ed, "delete children", "Delete Children", [&] { ed.Select(Find(ed, "Parent")); ed.DeleteChildrenOfSelected(); });
    RoundTrip(ed, "explicit name", "Paint Roof", [&] { ed.PushUndo("Paint Roof"); Find(ed, "Loner")->name = "Roof"; });
}

void TestDeleteRestoresTheSameObjects() {
    RUN_SUITE("undo of delete: same GUID, components, children, place, selection");
    EditorState ed;
    Populate(ed);
    GameObject* parent = Find(ed, "Parent");
    parent->AddComponent<Rigidbody2D>()->mass = 4.0f;
    ed.CommitUndo(); ed.undoStack().Clear();
    const std::string parentGuid = parent->Guid(), aGuid = Find(ed, "ChildA")->Guid(), bGuid = Find(ed, "ChildB")->Guid();
    const std::string lonelyGuid = Find(ed, "Lonely")->Guid();
    CHECK(!parentGuid.empty() && parentGuid != aGuid && aGuid != bGuid);
    const std::string before = Ser(ed);
    const std::size_t count = ed.scene().Objects().size();

    ed.Select(parent);
    ed.DeleteSelected();
    CHECK(!Find(ed, "Parent") && !Find(ed, "ChildA") && !Find(ed, "ChildB") && Find(ed, "Lonely"));   // the children went with it
    CHECK(ed.selected() == nullptr);
    CHECK(ed.UndoName() == "Edit");        // the step is still open: named when it closes
    ed.CommitUndo();
    CHECK(ed.UndoName() == "Delete 3 Objects");
    CHECK(ed.Undo());
    CHECK(ed.scene().Objects().size() == count);
    CHECK(Find(ed, "Parent") && Find(ed, "Parent")->Guid() == parentGuid);
    CHECK(Find(ed, "ChildA")->Guid() == aGuid && Find(ed, "ChildB")->Guid() == bGuid && Find(ed, "Lonely")->Guid() == lonelyGuid);
    CHECK(Find(ed, "ChildA")->transform->Parent() == Find(ed, "Parent")->transform);
    CHECK(Find(ed, "Parent")->transform->ChildCount() == 2 && Find(ed, "Parent")->transform->GetChild(0)->gameObject->name == "ChildA");
    CHECK(Find(ed, "Parent")->GetComponent<Rigidbody2D>() && Find(ed, "Parent")->GetComponent<Rigidbody2D>()->mass == 4.0f);
    CHECK(Find(ed, "ChildA")->transform->localPosition.x == 1.0f && Find(ed, "ChildA")->transform->localPosition.z == 3.0f);
    CHECK(Find(ed, "ChildB")->GetComponent<SpriteRenderer>() != nullptr);
    CHECK(Ser(ed) == before);
    CHECK(ed.selected() && ed.selected()->Guid() == parentGuid && ed.MultiSelection().size() == 1);   // the deleted object is selected again
    CHECK(ed.Redo());
    CHECK(!Find(ed, "Parent") && ed.selected() == nullptr);
    CHECK(ed.Undo() && Find(ed, "Parent")->Guid() == parentGuid);   // and again: still the same object

    // a multi selection comes back as a multi selection, with the same primary
    ed.Select(Find(ed, "ChildB"));
    ed.ToggleSelect(Find(ed, "Lonely"));
    ed.DeleteSelected();
    CHECK(ed.Undo());
    CHECK(ed.MultiSelection().size() == 2 && ed.selected() && ed.selected()->name == "Lonely" && ed.IsSelected(Find(ed, "ChildB")));
}

void TestSelectionAndRedoBranch() {
    RUN_SUITE("selection after undo / redo; a new edit ends the redo branch");
    EditorState ed;
    Populate(ed);
    ed.Select(Find(ed, "Lonely"));
    GameObject* c = ed.CreateCube("New");           // selects the new object
    CHECK(ed.selected() == c);
    CHECK(ed.Undo());
    CHECK(ed.selected() && ed.selected()->name == "Lonely");   // what was selected before the edit
    CHECK(ed.Redo());
    CHECK(ed.selected() && ed.selected()->name == "New");      // what the edit left selected
    CHECK(ed.Undo());
    CHECK(ed.CanRedo());
    ed.CreateCube("Other");                          // a new edit: the redo branch is gone
    CHECK(!ed.CanRedo() && ed.RedoName().empty() && !ed.Redo());
    CHECK(Find(ed, "Other") && !Find(ed, "New"));
    // Undo with nothing to undo
    ed.CommitUndo();
    ed.undoStack().Clear();
    CHECK(!ed.CanUndo() && ed.UndoName().empty() && !ed.Undo() && !ed.Redo());
}

void TestGroupingAndNames() {
    RUN_SUITE("compound actions are one step; names say what happened");
    EditorState ed;
    Populate(ed);
    // Several changes made between two boundaries are one step
    const std::string s0 = Ser(ed);
    ed.PushUndo("Rebuild Lonely");
    Find(ed, "Lonely")->name = "A";
    Find(ed, "A")->transform->localPosition = {9, 9, 9};
    Find(ed, "A")->AddComponent<BoxCollider2D>();
    ed.scene().CreateGameObject("Extra");
    CHECK(ed.UndoDepth() == 1);
    ed.CommitUndo();
    CHECK(ed.undoStack().UndoCount() == 1 && ed.UndoName() == "Rebuild Lonely");
    CHECK(ed.Undo() && Ser(ed) == s0);
    // Duplicate several selected objects: one step, named for the count
    ed.Select(Find(ed, "ChildA"));
    ed.ToggleSelect(Find(ed, "Lonely"));
    ed.DuplicateSelected();
    ed.CommitUndo();
    CHECK(ed.UndoName().rfind("Create", 0) == 0);
    CHECK(ed.Undo() && Ser(ed) == s0);
    // an editor-side edit joins the step; the step exists even if the scene did not change
    struct Cmd : ICommand {
        int* v; int before, after;
        Cmd(int* p, int b, int a) : v(p), before(b), after(a) {}
        bool Do() override { *v = after; return true; }
        bool Undo() override { *v = before; return true; }
        std::string Name() const override { return "Draft"; }
    };
    int draft = 1;
    ed.PushUndo("Edit Animation", std::make_unique<Cmd>(&draft, 1, 2));
    draft = 2;
    ed.CommitUndo();
    CHECK(ed.undoStack().UndoCount() == 1 && ed.UndoName() == "Edit Animation");
    CHECK(Ser(ed) == s0);
    CHECK(ed.Undo() && draft == 1 && Ser(ed) == s0);
    CHECK(ed.Redo() && draft == 2);
    CHECK(ed.Undo() && draft == 1);
    // ... and together with a scene change: still one step, undone together
    ed.PushUndo("Edit Animation", std::make_unique<Cmd>(&draft, 1, 2));
    draft = 2;
    Find(ed, "Lonely")->transform->localPosition = {4, 4, 4};
    ed.CommitUndo();
    CHECK(ed.undoStack().UndoCount() == 1);
    CHECK(ed.Undo() && draft == 1 && Ser(ed) == s0);
    CHECK(ed.Redo() && draft == 2 && Find(ed, "Lonely")->transform->localPosition.x == 4.0f);
    // PushUndo that changes nothing records nothing
    ed.CommitUndo(); ed.undoStack().Clear();
    ed.PushUndo("Nothing");
    ed.CommitUndo();
    CHECK(!ed.CanUndo() && ed.UndoDepth() == 0);
    // an unannounced change is caught at the next boundary as its own step, never lost or merged into another
    Find(ed, "Lonely")->name = "Sneaky";
    ed.PushUndo("Announced");
    Find(ed, "ChildA")->name = "Known";
    ed.CommitUndo();
    CHECK(ed.undoStack().UndoCount() == 2);
    CHECK(ed.Undo() && Find(ed, "ChildA") && Find(ed, "Sneaky"));
    CHECK(ed.Undo() && Find(ed, "Lonely") && !Find(ed, "Sneaky"));
}

// ---- 4. merging drags / sliders / text ---------------------------------------------------------------------

void TestMerge() {
    RUN_SUITE("a drag, a slider, a text box: one step");
    EditorState ed;
    Populate(ed);
    const std::string s0 = Ser(ed);
    oked::EditUndo u;
    GameObject* p = Find(ed, "Lonely");
    // one drag over many frames
    u.Begin(ed);
    for (int i = 1; i <= 30; ++i) p->transform->localPosition.x = (float)i;
    CHECK(u.End(ed, 11));
    CHECK(ed.undoStack().UndoCount() == 1);
    // the same widget touched again right away: the same step grows
    u.Begin(ed);
    p->transform->localPosition.x = 99.0f;
    CHECK(u.End(ed, 11));
    CHECK(ed.undoStack().UndoCount() == 1);
    CHECK(ed.Undo() && Ser(ed) == s0);                     // all the way back in one go
    CHECK(ed.Redo() && Find(ed, "Lonely")->transform->localPosition.x == 99.0f);
    // another widget: its own step
    u.Begin(ed);
    Find(ed, "Lonely")->transform->localPosition.y = 5.0f;
    CHECK(u.End(ed, 12));
    CHECK(ed.undoStack().UndoCount() == 2);
    // a widget with no id never merges
    u.Begin(ed); Find(ed, "Lonely")->transform->localPosition.z = 1.0f; u.End(ed, 0);
    u.Begin(ed); Find(ed, "Lonely")->transform->localPosition.z = 2.0f; u.End(ed, 0);
    CHECK(ed.undoStack().UndoCount() == 4);
    // after the merge window, the same widget starts a new step
    u.Begin(ed); Find(ed, "Lonely")->transform->localPosition.x = 7.0f; u.End(ed, 13);
    ed.TickServices(2.0f);
    u.Begin(ed); Find(ed, "Lonely")->transform->localPosition.x = 8.0f; u.End(ed, 13);
    CHECK(ed.undoStack().UndoCount() == 6);
    // dragged away and back: nothing to undo
    ed.CommitUndo(); ed.undoStack().Clear();
    const float x0 = Find(ed, "Lonely")->transform->localPosition.x;
    u.Begin(ed); Find(ed, "Lonely")->transform->localPosition.x = x0 + 5; u.End(ed, 20);
    u.Begin(ed); Find(ed, "Lonely")->transform->localPosition.x = x0; u.End(ed, 20);
    CHECK(ed.undoStack().UndoCount() == 0 && !ed.CanUndo());
    // typing in a name box: one step for the whole session
    u.Begin(ed);
    {
        std::string cur = "Lonely";
        for (const char* n : {"L", "Lo", "Lon", "Lone", "Lonel", "Lonely2"}) { Find(ed, cur.c_str())->name = n; cur = n; }
    }
    CHECK(u.End(ed, 21));
    CHECK(ed.undoStack().UndoCount() == 1 && ed.Undo() && Find(ed, "Lonely"));
    // a widget that announces itself (PushUndo) is not recorded twice
    const int n0 = ed.UndoDepth();
    u.Begin(ed); ed.PushUndo(); Find(ed, "Lonely")->name = "Twice"; CHECK(!u.End(ed, 30));
    ed.CommitUndo();
    CHECK(ed.UndoDepth() == n0 + 1);
}

// ---- 5. limits, memory ---------------------------------------------------------------------------------------

void TestLimits() {
    RUN_SUITE("step limit, memory");
    EditorState ed;
    Populate(ed);
    for (int i = 0; i < 150; ++i) { ed.PushUndo(); Find(ed, "Lonely")->transform->localPosition.x = (float)(i + 1) * 3.5f; ed.CommitUndo(); }
    CHECK(ed.UndoDepth() == 100);                          // the oldest steps fell off (EditorState::kMaxUndo)
    int n = 0;
    while (ed.Undo()) ++n;
    CHECK(n == 100);
    CHECK(Find(ed, "Lonely")->transform->localPosition.x == 50.0f * 3.5f);   // state after the 50th edit: the first 50 are gone
    // memory: a property edit in a big scene costs a few hundred bytes, not a copy of the scene
    EditorState big;
    for (int i = 0; i < 400; ++i) big.CreateCube("Obj" + std::to_string(i));
    big.CommitUndo(); big.undoStack().Clear();
    const std::size_t sceneBytes = Ser(big).size();
    for (int i = 0; i < 20; ++i) { big.PushUndo(); Find(big, "Obj200")->transform->localPosition.y = (float)(i + 1); big.CommitUndo(); }
    CHECK(big.undoStack().UndoCount() == 20);
    CHECK(big.undoStack().MemoryBytes() < 20 * 2048);
    CHECK(big.undoStack().MemoryBytes() < sceneBytes);                     // 20 steps cost less than ONE copy of the scene
    std::cout << "  scene " << sceneBytes << " bytes, 20 steps " << big.undoStack().MemoryBytes() << " bytes\n";
    // the stack's own limits apply
    big.undoStack().SetMaxCommands(5);
    CHECK(big.undoStack().UndoCount() == 5);
}

// ---- 6. a new scene, an opened scene, Play ---------------------------------------------------------------------

void TestSceneChangeAndPlay() {
    RUN_SUITE("new / opened scenes end the history; Play never records");
    TempDir t;
    EditorState ed;
    Populate(ed);
    ed.CreateCube("X");
    CHECK(ed.CanUndo());
    CHECK(ed.Save(t.Sub("a.okayscene")));
    ed.CreateCube("Y");
    CHECK(ed.CanUndo());
    ed.NewScene();
    CHECK(!ed.CanUndo() && !ed.CanRedo() && ed.UndoDepth() == 0 && !ed.Undo() && !ed.dirty);
    ed.CreateCube("Z");
    ed.NewScene3D();
    CHECK(!ed.CanUndo() && !ed.Undo() && !ed.HasUnsavedChanges());
    ed.CreateCube("W");
    CHECK(ed.CanUndo());
    CHECK(ed.Load(t.Sub("a.okayscene")));
    CHECK(!ed.CanUndo() && !ed.CanRedo() && !ed.Undo());
    CHECK(Find(ed, "X") && !Find(ed, "W") && !ed.dirty);
    // undo works on the loaded scene
    ed.CreateCube("After load");
    CHECK(ed.Undo() && !Find(ed, "After load") && Find(ed, "X"));
    // a failed Open leaves the history alone
    ed.CreateCube("Keep");
    const int depth = ed.UndoDepth();
    CHECK(!ed.Load(t.Sub("missing.okayscene")));
    CHECK(ed.UndoDepth() == depth && ed.CanUndo());
    CHECK(ed.Undo() && !Find(ed, "Keep"));

    // Play: nothing is recorded, Undo / Redo are off, and the history from before works again after Stop
    ed.CreateCube("Before play");
    const int d0 = ed.UndoDepth();
    const std::string edited = Ser(ed);
    ed.scene().CreateGameObject("Prober");
    ed.CommitUndo();
    ed.undoStack().Clear();
    ed.CreateCube("Pre");
    const int d1 = ed.UndoDepth();
    (void)d0;
    ed.Play();
    CHECK(ed.isPlaying());
    CHECK(ed.UndoDepth() == d1);                           // Play closed the step, nothing added
    ed.PushUndo("In play");
    ed.CreateCube("Made in play");
    ed.scene().CreateGameObject("Made in play 2");
    ed.PushUndoText("", "x");
    ed.Tick(0.5f);
    CHECK(ed.UndoDepth() == d1 && ed.undoStack().UndoCount() == (std::size_t)d1);
    CHECK(!ed.CanUndo() && !ed.CanRedo() && !ed.Undo() && !ed.Redo());
    CHECK(ed.UndoName().empty());
    ed.Stop();
    CHECK(!Find(ed, "Made in play") && Find(ed, "Pre"));
    CHECK(ed.CanUndo() && ed.UndoDepth() == d1);
    ed.CommitUndo();
    CHECK(ed.UndoDepth() == d1);                           // Stop restored the scene exactly: no phantom step
    CHECK(ed.Undo() && !Find(ed, "Pre") && Find(ed, "Prober"));
    CHECK(ed.Redo() && Find(ed, "Pre"));
    (void)edited;
}

// ---- 7. saved / unsaved follows the history --------------------------------------------------------------------

void TestSavedTracking() {
    RUN_SUITE("saved state follows undo and redo");
    TempDir t;
    EditorState ed;
    Populate(ed);
    CHECK(ed.Save(t.Sub("s.okayscene")) && !ed.dirty && !ed.undoStack().IsDirty());
    ed.CreateCube("A");
    CHECK(ed.dirty && ed.HasUnsavedChanges());
    CHECK(ed.Undo());
    CHECK(!ed.dirty && !ed.HasUnsavedChanges() && !ed.undoStack().IsDirty());      // back on what is on disk
    CHECK(ed.Redo());
    CHECK(ed.dirty && ed.HasUnsavedChanges() && ed.undoStack().IsDirty());
    CHECK(ed.Save(t.Sub("s.okayscene")) && !ed.dirty && !ed.undoStack().IsDirty());
    CHECK(ed.Undo() && ed.dirty && ed.HasUnsavedChanges());                          // saved with A, undo takes A away: unsaved
    CHECK(ed.Redo() && !ed.dirty && !ed.HasUnsavedChanges());
    // an edit that was never announced is still unsaved, and saved by Save
    Find(ed, "Lonely")->name = "Sneaky";
    CHECK(ed.HasUnsavedChanges());
    CHECK(ed.Save(t.Sub("s.okayscene")) && !ed.HasUnsavedChanges());
    CHECK(ed.Undo() && Find(ed, "Lonely") && ed.dirty);                              // the sneaky edit was recorded at the save
    CHECK(ed.Redo() && !ed.dirty);
    // an edit after a save is never merged into the saved step
    oked::EditUndo u;
    u.Begin(ed); Find(ed, "Sneaky")->transform->localPosition.x = 3.0f; u.End(ed, 5);
    CHECK(ed.Save(t.Sub("s.okayscene")) && !ed.dirty);
    u.Begin(ed); Find(ed, "Sneaky")->transform->localPosition.x = 4.0f; u.End(ed, 5);
    CHECK(ed.undoStack().UndoCount() >= 2);
    CHECK(ed.Undo() && !ed.dirty && Find(ed, "Sneaky")->transform->localPosition.x == 3.0f);
    // edits made while Play runs never count as unsaved, with or without the stack
    ed.Play();
    ed.scene().CreateGameObject("Play only");
    CHECK(!ed.HasUnsavedChanges());
    ed.Stop();
    CHECK(!ed.dirty && !ed.HasUnsavedChanges());
}

// ---- 8. other ways the scene is replaced -------------------------------------------------------------------------

void TestAssetRewrite() {
    RUN_SUITE("moving an asset the scene uses ends the history; an unrelated move keeps it");
    EditorState ed;
    Populate(ed);
    Find(ed, "ChildB")->GetComponent<SpriteRenderer>()->texture = "Assets/old.png";
    ed.CommitUndo();
    ed.CreateCube("Z");
    CHECK(ed.CanUndo());
    // a rewrite that finds nothing to change leaves everything alone
    CHECK(!ed.RewriteAssetPaths([](const std::string& s) { return s; }));
    CHECK(ed.CanUndo() && ed.Undo() && !Find(ed, "Z"));
    CHECK(ed.Redo());
    // one that changes the scene starts a new history (the old pieces still name the old path)
    CHECK(ed.RewriteAssetPaths([](const std::string& s) {
        std::string r = s;
        for (std::size_t p; (p = r.find("Assets/old.png")) != std::string::npos;) r.replace(p, 14, "Assets/new.png");
        return r;
    }));
    CHECK(Find(ed, "ChildB")->GetComponent<SpriteRenderer>()->texture == "Assets/new.png");
    CHECK(!ed.CanUndo() && !ed.CanRedo());
    CHECK(ed.dirty);   // it was unsaved before, and still is
}

}  // namespace

int main() {
    RUN_SUITE("editor_undo");
    Log::console = false;
    Game::Reset();
    Time::ResetForTesting();
    TestTextDelta();
    TestCommandOnStack();
    TestEdits();
    TestDeleteRestoresTheSameObjects();
    TestSelectionAndRedoBranch();
    TestGroupingAndNames();
    TestMerge();
    TestLimits();
    TestSceneChangeAndPlay();
    TestSavedTracking();
    TestAssetRewrite();
    TEST_MAIN_RESULT();
}
