// The editor's object clipboard (editor/EditClipboard.hpp, ImGui-free): Ctrl+X / Ctrl+C / Ctrl+V with
// several objects selected. The clipboard must hold EVERY selected object (each one once: a selected
// child of a selected parent comes with its parent), Ctrl+V must bring them all back with their names
// and components, Cut must be one undo step and Paste one undo step.
// Bug this covers: with three objects selected, Ctrl+X deleted all three but kept one on the clipboard,
// so Ctrl+V brought back one; Ctrl+C copied one.
#include "test_framework.hpp"
#include "../editor/EditorState.hpp"
#include "../editor/EditClipboard.hpp"
#include "okay/Scene/SceneSerializer.hpp"

#include <string>
#include <vector>

using namespace okay;
using okay::editor::EditorState;

namespace {

int CountNamed(EditorState& ed, const std::string& name) {
    int n = 0;
    for (const auto& up : ed.scene().Objects())
        if (up && !up->IsDestroyed() && up->name == name) ++n;
    return n;
}

int CountObjectsInText(const std::string& text) {
    int n = 0;
    std::size_t pos = 0;
    while ((pos = text.find("\ngameobject ", pos)) != std::string::npos) { ++n; ++pos; }
    return n;
}

// Three objects, each with something of its own, all three selected (Ctrl+click order A, B, C).
void MakeThree(EditorState& ed) {
    ed.NewScene();
    GameObject* a = ed.CreateCube("Alpha");
    a->AddComponent<Rigidbody2D>();
    a->transform->localPosition = {1, 2, 3};
    GameObject* b = ed.CreateSprite("Beta");
    b->tag = "Enemy";
    b->transform->localPosition = {-4, 0, 0};
    GameObject* c = ed.CreateCube("Gamma");
    c->transform->localPosition = {0, 0, 7};
    ed.CommitUndo();
    ed.undoStack().Clear();
    ed.Select(a);
    ed.ToggleSelect(b);
    ed.ToggleSelect(c);
}

void TestCutThenPasteBringsAllBack() {
    EditorState ed;
    MakeThree(ed);
    CHECK(ed.MultiSelection().size() == 3);
    std::string clip;
    const std::size_t cut = oked::CutSelection(ed, clip);
    CHECK(cut == 3);
    CHECK(CountObjectsInText(clip) == 3);
    CHECK(ed.scene().Find("Alpha") == nullptr);
    CHECK(ed.scene().Find("Beta") == nullptr);
    CHECK(ed.scene().Find("Gamma") == nullptr);
    CHECK(ed.selected() == nullptr);

    const std::vector<GameObject*> pasted = oked::PasteClipboard(ed, clip);
    CHECK(pasted.size() == 3);
    GameObject* a = ed.scene().Find("Alpha");
    GameObject* b = ed.scene().Find("Beta");
    GameObject* c = ed.scene().Find("Gamma");
    CHECK(a && b && c);
    if (a && b && c) {
        CHECK(a->GetComponent<Rigidbody2D>() != nullptr);
        CHECK(a->GetComponent<MeshRenderer>() != nullptr);
        CHECK(b->GetComponent<SpriteRenderer>() != nullptr);
        CHECK(b->tag == "Enemy");
        CHECK(c->GetComponent<MeshRenderer>() != nullptr);
        // Each pasted root is nudged half a unit so it is visible (as the one-object paste was).
        CHECK_NEAR(a->transform->localPosition.x, 1.5, 1e-4);
        CHECK_NEAR(a->transform->localPosition.y, 2.5, 1e-4);
        CHECK_NEAR(a->transform->localPosition.z, 3.0, 1e-4);
        CHECK_NEAR(b->transform->localPosition.x, -3.5, 1e-4);
        CHECK_NEAR(c->transform->localPosition.z, 7.0, 1e-4);
        // All three pasted objects are selected, in clipboard order.
        CHECK(ed.MultiSelection().size() == 3);
        CHECK(ed.IsSelected(a) && ed.IsSelected(b) && ed.IsSelected(c));
        CHECK(ed.selected() != nullptr);
    }
    CHECK(ed.dirty);
    // Pasting again makes three more (the clipboard is not used up).
    const std::vector<GameObject*> again = oked::PasteClipboard(ed, clip);
    CHECK(again.size() == 3);
    CHECK(CountNamed(ed, "Alpha") == 2 && CountNamed(ed, "Beta") == 2 && CountNamed(ed, "Gamma") == 2);
}

void TestCutIsOneUndoStepAndPasteIsOne() {
    EditorState ed;
    MakeThree(ed);
    const std::string before = SceneSerializer::Serialize(ed.scene());
    const int depth0 = ed.UndoDepth();
    std::string clip;
    CHECK(oked::CutSelection(ed, clip) == 3);
    ed.CommitUndo();
    CHECK(ed.UndoDepth() == depth0 + 1);
    // One Undo brings all three back.
    CHECK(ed.Undo());
    CHECK(ed.scene().Find("Alpha") && ed.scene().Find("Beta") && ed.scene().Find("Gamma"));
    CHECK(SceneSerializer::Serialize(ed.scene()) == before);
    // Redo cuts all three again.
    CHECK(ed.Redo());
    CHECK(!ed.scene().Find("Alpha") && !ed.scene().Find("Beta") && !ed.scene().Find("Gamma"));

    const int depth1 = ed.UndoDepth();
    CHECK(oked::PasteClipboard(ed, clip).size() == 3);
    ed.CommitUndo();
    CHECK(ed.UndoDepth() == depth1 + 1);
    CHECK(ed.Undo());   // one Undo takes the whole paste away
    CHECK(!ed.scene().Find("Alpha") && !ed.scene().Find("Beta") && !ed.scene().Find("Gamma"));
}

void TestCopyHoldsAll() {
    EditorState ed;
    MakeThree(ed);
    const std::string before = SceneSerializer::Serialize(ed.scene());
    std::string clip;
    CHECK(oked::CopySelection(ed, clip) == 3);
    CHECK(CountObjectsInText(clip) == 3);
    CHECK(SceneSerializer::Serialize(ed.scene()) == before);   // Copy changes nothing
    CHECK(ed.MultiSelection().size() == 3);                      // and keeps the selection

    // Paste into another, empty editor: all three arrive.
    EditorState other;
    other.NewScene();
    const std::size_t had = other.scene().Objects().size();
    const std::vector<GameObject*> pasted = oked::PasteClipboard(other, clip);
    CHECK(pasted.size() == 3);
    CHECK(other.scene().Objects().size() == had + 3);
    CHECK(other.scene().Find("Alpha") && other.scene().Find("Beta") && other.scene().Find("Gamma"));

    // Paste beside the originals: three more, the originals untouched.
    CHECK(oked::PasteClipboard(ed, clip).size() == 3);
    CHECK(CountNamed(ed, "Alpha") == 2 && CountNamed(ed, "Beta") == 2 && CountNamed(ed, "Gamma") == 2);
}

void TestParentAndChildSelectedCopiesParentOnce() {
    EditorState ed;
    ed.NewScene();
    GameObject* parent = ed.CreateCube("Parent");
    GameObject* child = ed.CreateCube("Child");
    child->transform->SetParent(parent->transform, false);
    child->transform->localPosition = {0, 1, 0};
    GameObject* other = ed.CreateSprite("Other");
    ed.CommitUndo();

    // Child picked first, then its parent, then an unrelated object.
    ed.Select(child);
    ed.ToggleSelect(parent);
    ed.ToggleSelect(other);
    const std::vector<GameObject*> roots = oked::ClipboardRoots(ed.MultiSelection(), ed.selected());
    CHECK(roots.size() == 2);
    if (roots.size() == 2) { CHECK(roots[0] == parent); CHECK(roots[1] == other); }

    std::string clip;
    CHECK(oked::CopySelection(ed, clip) == 2);
    CHECK(CountObjectsInText(clip) == 3);   // Parent, its Child (once), Other

    const std::vector<GameObject*> pasted = oked::PasteClipboard(ed, clip);
    CHECK(pasted.size() == 2);
    CHECK(CountNamed(ed, "Parent") == 2);
    CHECK(CountNamed(ed, "Child") == 2);   // the original and ONE copy, under the pasted parent
    CHECK(CountNamed(ed, "Other") == 2);
    if (pasted.size() == 2) {
        GameObject* np = pasted[0];
        CHECK(np->name == "Parent");
        CHECK(np->transform->ChildCount() == 1);
        if (np->transform->ChildCount() == 1) {
            GameObject* nc = np->transform->Children()[0]->gameObject;
            CHECK(nc->name == "Child");
            CHECK_NEAR(nc->transform->localPosition.y, 1.0, 1e-4);   // the child keeps its pose under the parent
        }
        CHECK(pasted[1]->name == "Other");
        CHECK(pasted[1]->transform->Parent() == nullptr);
    }

    // Cut of the same selection deletes the three and brings back all three.
    ed.Select(child);
    ed.ToggleSelect(parent);
    ed.ToggleSelect(other);
    std::string cutClip;
    CHECK(oked::CutSelection(ed, cutClip) == 2);
    CHECK(CountNamed(ed, "Parent") == 1 && CountNamed(ed, "Child") == 1 && CountNamed(ed, "Other") == 1);   // the pasted copies are left
    CHECK(oked::PasteClipboard(ed, cutClip).size() == 2);
    CHECK(CountNamed(ed, "Parent") == 2 && CountNamed(ed, "Child") == 2 && CountNamed(ed, "Other") == 2);
}

void TestOneObjectAndNothing() {
    EditorState ed;
    ed.NewScene();
    GameObject* a = ed.CreateCube("Solo");
    ed.CommitUndo();
    ed.Select(a);
    std::string clip;
    CHECK(oked::CopySelection(ed, clip) == 1);
    CHECK(clip == SceneSerializer::SerializeObject(*a));   // one object: the same text as before the change
    CHECK(oked::ClipboardLabel(oked::ClipboardRoots(ed.MultiSelection(), ed.selected())) == "Solo");

    // Nothing selected: the clipboard is left as it was, nothing is deleted.
    ed.Select(nullptr);
    std::string keep = "kept";
    CHECK(oked::CopySelection(ed, keep) == 0);
    CHECK(keep == "kept");
    const std::size_t n = ed.scene().Objects().size();
    CHECK(oked::CutSelection(ed, keep) == 0);
    CHECK(keep == "kept");
    CHECK(ed.scene().Objects().size() == n);

    // An empty or broken clipboard pastes nothing and leaves no undo step.
    const int depth = ed.UndoDepth();
    CHECK(oked::PasteClipboard(ed, "").empty());
    CHECK(oked::PasteClipboard(ed, "not a scene").empty());
    ed.CommitUndo();
    CHECK(ed.UndoDepth() == depth);
    CHECK(ed.scene().Objects().size() == n);

    // Labels for the console.
    GameObject* b = ed.CreateCube("Two");
    CHECK(oked::ClipboardLabel({a, b}) == "2 objects");
    CHECK(oked::ClipboardLabel({}) == "nothing");
}

}  // namespace

int main() {
    RUN_SUITE("edit_clipboard");
    Log::console = false;
    Game::Reset();
    Time::ResetForTesting();
    TestCutThenPasteBringsAllBack();
    TestCutIsOneUndoStepAndPasteIsOne();
    TestCopyHoldsAll();
    TestParentAndChildSelectedCopiesParentOnce();
    TestOneObjectAndNothing();
    TEST_MAIN_RESULT();
}
