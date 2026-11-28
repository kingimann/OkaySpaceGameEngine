// The menus' scene actions (MenuActions.hpp), moved here from editor/main.cpp (RunMenuAction, HierEmptyAction,
// HierRowAction) so the editor's clicks and the automation ops run one copy. Our own code.
#include "MenuActions.hpp"

#include "InspectorModel.hpp"   // oked::SetObjectActive
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/UIScreens.hpp"
#include "okay/Scene/Transform.hpp"

#include <cmath>

namespace oked {
namespace menuactions {

using okay::GameObject;
using okay::SceneSerializer;
using okay::Transform;
using okay::Vec3;
using okay::editor::EditorState;

namespace {
void Say(Env& env, const std::string& text, int level = 0) { if (env.log) env.log(text, level); }
bool HasClip(Env& env) { return env.clipboard && !env.clipboard->empty(); }
void Copy(Env& env, GameObject& go) { if (env.clipboard) *env.clipboard = SceneSerializer::SerializeObject(go); }

// File > New 2D / 3D Scene: after the save prompt.
Outcome NewScene(EditorState& ed, Env& env, bool is3D) {
    auto make = [&ed, &env, is3D] {
        if (is3D) { ed.NewScene3D(); Say(env, "New 3D scene"); }
        else { ed.NewScene2D(); Say(env, "New 2D scene"); }
    };
    if (!env.afterSavePrompt) { make(); return Outcome::Done; }
    return env.afterSavePrompt(ed, "starting a new scene", make) ? Outcome::Done : Outcome::NeedsPerson;
}

void UndoRedo(EditorState& ed, Env& env, bool redo) {
    const bool mesh = env.meshHas && env.meshHas(redo);
    const std::string nm = mesh ? std::string("Mesh Edit") : (redo ? ed.RedoName() : ed.UndoName());
    const std::string verb = redo ? "Redo " : "Undo ";
    if (mesh && env.meshUndo && env.meshUndo(redo)) { ed.dirty = true; Say(env, verb + nm); }
    else if (redo ? ed.Redo() : ed.Undo()) Say(env, verb + nm);
    else Say(env, "Can't " + std::string(redo ? "redo " : "undo ") + nm + ": " + ed.UndoError(), 1);
}

void SelectAll(EditorState& ed) {
    std::vector<GameObject*> all;
    for (const auto& up : ed.scene().Objects()) all.push_back(up.get());
    SelectMany(ed, all);
}

void EmptyChildOf(EditorState& ed, Env& env, GameObject* parent, bool tell) {
    GameObject* child = ed.CreateEmpty("Child");   // takes its own undo step
    child->transform->SetParent(parent->transform, false);
    ed.Select(child); ed.dirty = true;
    if (tell) { Say(env, "Created an empty child of " + parent->name); ed.Achievement("FIRST_OBJECT"); }
}

// A row's menu (h.* with a row): what it does to that object (and the selection it is part of).
Outcome RunRow(EditorState& ed, const std::string& id, GameObject* node, Env& env, After& out) {
    if (id == "h.copy") Copy(env, *node);
    else if (id == "h.cut") { Copy(env, *node); out.cut = true; }   // deleted after the tree is drawn
    else if (id == "h.paste" || id == "h.pastechild") {
        if (!HasClip(env)) return Outcome::Done;
        ed.PushUndo();
        if (GameObject* p = SceneSerializer::InstantiateFromText(ed.scene(), *env.clipboard)) {
            if (id == "h.pastechild") p->transform->SetParent(node->transform, false);
            else if (node->transform->Parent()) p->transform->SetParent(node->transform->Parent(), false);
            ed.Select(p); ed.dirty = true;
        }
    }
    else if (id == "h.duplicate") { ed.DuplicateSelected(); Say(env, "Duplicated"); }
    else if (id == "h.delete") out.deleteSelection = true;        // deleted after the tree is drawn: the rows below still use it
    else if (id == "h.deletechildren") out.deleteChildren = true; // empties a container, keeps the object
    else if (id == "h.emptychild") EmptyChildOf(ed, env, node, false);
    else if (id == "h.createparent") {   // one object: wrap it, keeping its on-screen transform; several: group them
        if (RowTargets(ed, node).size() > 1) { ed.GroupSelected(); Say(env, "Grouped selection"); }
        else CreateEmptyParentOf(ed, node);
    }
    else if (id == "h.unparent") {
        ed.PushUndo();
        node->transform->SetParent(nullptr, true);   // keep world pose
        ed.dirty = true;
    }
    else if (id == "h.moveup")     { ed.PushUndo(); ed.scene().MoveSibling(node, -1); ed.dirty = true; }
    else if (id == "h.movedown")   { ed.PushUndo(); ed.scene().MoveSibling(node, +1); ed.dirty = true; }
    else if (id == "h.movetop")    { ed.PushUndo(); ed.scene().MoveSiblingToEdge(node, true);  ed.dirty = true; }
    else if (id == "h.movebottom") { ed.PushUndo(); ed.scene().MoveSiblingToEdge(node, false); ed.dirty = true; }
    else if (id == "h.sel.children" || id == "h.sel.below" || id == "h.sel.parent" || id == "h.sel.siblings" || id == "h.sel.samename") {
        std::vector<GameObject*> pick;
        Transform* par = node->transform->Parent();
        if (id == "h.sel.children") { for (Transform* c : node->transform->Children()) pick.push_back(c->gameObject); }
        else if (id == "h.sel.below") {
            for (const auto& up : ed.scene().Objects())
                if (up.get() != node && up->IsSelfOrDescendantOf(node)) pick.push_back(up.get());
        }
        else if (id == "h.sel.parent") { if (par) pick.push_back(par->gameObject); }
        else if (id == "h.sel.siblings") {
            for (const auto& up : ed.scene().Objects()) if (up->transform->Parent() == par) pick.push_back(up.get());
        }
        else {
            for (const auto& up : ed.scene().Objects()) if (up->name == node->name) pick.push_back(up.get());
        }
        if (!pick.empty()) { SelectMany(ed, pick); out.revealSelection = true; }   // open their parents so they show
    }
    else if (id == "h.active") {
        ed.PushUndo();
        const bool on = !node->active;
        for (GameObject* g : RowTargets(ed, node)) oked::SetObjectActive(g, on);   // the whole selection; children follow
        ed.dirty = true;
    }
    else if (id == "h.lock") {
        ed.PushUndo();
        const bool on = !node->editorLocked;
        for (GameObject* g : RowTargets(ed, node)) SetLockedRecursive(g, on);
        ed.dirty = true;
    }
    else if (id == "h.isolate") { if (env.isolate) *env.isolate = (*env.isolate == node) ? nullptr : node; }
    else if (id.rfind("h.tf.", 0) == 0) {
        auto each = [&](auto fn) { ed.PushUndo(); for (GameObject* g : RowTargets(ed, node)) fn(g->transform); ed.dirty = true; };
        if (id == "h.tf.resetpos") each([](Transform* t) { t->localPosition = Vec3::Zero; });
        else if (id == "h.tf.resetrot") each([](Transform* t) { t->localRotation = okay::Quat::Identity; });
        else if (id == "h.tf.resetscale") each([](Transform* t) { t->localScale = Vec3::One; });
        else if (id == "h.tf.resetall") each([](Transform* t) { t->localPosition = Vec3::Zero; t->localRotation = okay::Quat::Identity; t->localScale = Vec3::One; });
        else if (id == "h.tf.movetoview") each([&](Transform* t) { t->SetPosition(ed.camTarget); });
        else return Outcome::NotHandled;
    }
    else return Outcome::NotHandled;
    return Outcome::Done;
}
}  // namespace

const std::vector<oked::uimenu::ScreenGroup>& UIScreenGroups() {
    static const std::vector<oked::uimenu::ScreenGroup> groups = [] {
        std::vector<oked::uimenu::ScreenGroup> v;
        for (const okay::UIScreens::Section& sec : okay::UIScreens::Sections()) {
            oked::uimenu::ScreenGroup g; g.name = sec.name; g.tip = sec.tip;
            for (const okay::UIScreens::Def& d : okay::UIScreens::Screens())
                if (d.section == sec.id) g.items.push_back({d.id, d.name, d.desc});
            if (!g.items.empty()) v.push_back(std::move(g));
        }
        return v;
    }();
    return groups;
}

void SelectMany(EditorState& ed, const std::vector<GameObject*>& objs) {
    if (objs.empty()) return;
    ed.Select(objs.back());   // ToggleSelect makes each added object active, so go back to front
    for (std::size_t i = objs.size() - 1; i-- > 0;) if (!ed.IsSelected(objs[i])) ed.ToggleSelect(objs[i]);
}

std::vector<GameObject*> RowTargets(EditorState& ed, GameObject* node) {
    if (ed.IsSelected(node) && ed.MultiSelection().size() > 1) return ed.MultiSelection();
    return {node};
}

void SetLockedRecursive(GameObject* go, bool on) {
    if (!go) return;
    go->editorLocked = on;
    if (go->transform)
        for (Transform* c : go->transform->Children())
            if (c) SetLockedRecursive(c->gameObject, on);
}

void CreateEmptyParentOf(EditorState& ed, GameObject* node) {
    if (!node || !node->transform) return;
    ed.PushUndo();
    GameObject* parent = ed.scene().CreateGameObject(node->name + " Group");
    parent->transform->SetPosition(node->transform->Position());
    if (Transform* op = node->transform->Parent())
        parent->transform->SetParent(op, true);
    node->transform->SetParent(parent->transform, true);  // keep world pose
    ed.Select(parent); ed.dirty = true;
}

Outcome Run(EditorState& ed, const std::string& id, GameObject* row, Env& env, After& after) {
    if (row && id.rfind("h.", 0) == 0) return RunRow(ed, id, row, env, after);
    GameObject* sel = ed.selected();
    // File
    if (id == "file.new2d" || id == "file.new3d") return NewScene(ed, env, id == "file.new3d");
    // Edit
    if (id == "edit.undo" || id == "edit.redo") UndoRedo(ed, env, id == "edit.redo");
    else if (id == "edit.cut") {
        if (!sel) return Outcome::Done;
        Copy(env, *sel);
        Say(env, "Cut " + sel->name + " (Ctrl+V to paste)");
        ed.DeleteSelected();
    }
    else if (id == "edit.copy") { if (sel) { Copy(env, *sel); Say(env, "Copied " + sel->name); } }
    else if (id == "edit.paste" || id == "h.paste") {   // Edit > Paste lands a little to the side; the Hierarchy's empty space at the top level
        if (!HasClip(env)) return Outcome::Done;
        ed.PushUndo();
        if (GameObject* go = SceneSerializer::InstantiateFromText(ed.scene(), *env.clipboard)) {
            if (id == "edit.paste") go->transform->localPosition += Vec3{0.5f, 0.5f, 0.0f};
            ed.Select(go); ed.dirty = true;
            if (id == "edit.paste") Say(env, "Pasted " + go->name);
        }
    }
    else if (id == "edit.duplicate") { if (sel) { ed.DuplicateSelected(); Say(env, "Duplicated selection"); } }
    else if (id == "edit.delete") { if (sel) { ed.DeleteSelected(); Say(env, "Deleted selection"); } }
    else if (id == "edit.selectall" || id == "h.selectall") SelectAll(ed);
    else if (id == "edit.deselect" || id == "h.deselect") ed.Select(nullptr);
    // GameObject, and the Hierarchy's empty space and [+] popup
    else if (id == "go.empty" || id == "h.createempty") { ed.Select(ed.CreateEmpty()); Say(env, "Created empty GameObject"); ed.Achievement("FIRST_OBJECT"); }
    else if (id == "go.emptychild" || id == "h.emptychild") { if (sel) EmptyChildOf(ed, env, sel, true); }
    else if (id == "go.emptyparent") {
        if (!sel) return Outcome::Done;
        if (ed.MultiSelection().size() > 1) ed.GroupSelected();
        else CreateEmptyParentOf(ed, sel);
        Say(env, "Created an empty parent");
    }
    else if (id == "h.groupselected") { if (ed.GroupSelected()) Say(env, "Grouped selection under an empty parent"); }
    else if (id == "go.snap") {
        if (!sel) return Outcome::Done;
        ed.PushUndo();
        const float g = env.snapSize > 1e-4f ? env.snapSize : 1.0f;
        Vec3& p = sel->transform->localPosition;
        p = {std::round(p.x / g) * g, std::round(p.y / g) * g, std::round(p.z / g) * g};
        ed.dirty = true; Say(env, "Snapped to grid");
    }
    // View
    else if (id == "view.isolate") {
        if (!env.isolate) return Outcome::NotHandled;
        *env.isolate = *env.isolate ? nullptr : sel;
        Say(env, *env.isolate ? "Isolating '" + (*env.isolate)->name + "' (Shift+I to exit)" : std::string("Isolation off"));
    }
    else return Outcome::NotHandled;
    return Outcome::Done;
}

void ApplyAfter(EditorState& ed, const After& after, Env& env) {
    if (after.cut) { ed.DeleteSelected(); Say(env, "Cut to the clipboard (Ctrl+V to paste)"); }
    else if (after.deleteSelection) { ed.DeleteSelected(); Say(env, "Deleted"); }
    if (after.deleteChildren) {
        const int n = ed.DeleteChildrenOfSelected();
        Say(env, "Deleted " + std::to_string(n) + " child" + (n == 1 ? "" : "ren") + " (and everything below them)");
    }
}

}  // namespace menuactions
}  // namespace oked
