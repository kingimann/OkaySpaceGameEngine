#include "test_framework.hpp"
#include "HierarchyContextModel.hpp"
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// The Hierarchy panel's menus as data (editor/HierarchyContextModel.hpp, no ImGui): what a row's right-click menu holds and when each
// item is enabled, the plural labels ("Delete 3 Objects"), the reasons a greyed-out item gives, the empty-space menu and the [+] popup.
using namespace oked::hiermenu;
using oked::menubar::Entry;
using oked::menubar::Kind;
using oked::menubar::FindId;
using oked::menubar::CountLabel;
using oked::menubar::Walk;

namespace {

bool SeparatorsOk(const std::vector<Entry>& es) {
    if (es.empty()) return true;
    if (es.front().kind == Kind::Separator || es.back().kind == Kind::Separator) return false;
    for (std::size_t i = 1; i < es.size(); ++i)
        if (es[i].kind == Kind::Separator && es[i - 1].kind == Kind::Separator) return false;
    for (const Entry& e : es) if (!SeparatorsOk(e.children)) return false;
    return true;
}

bool LabelsUniquePerLevel(const std::vector<Entry>& es) {
    std::set<std::string> seen;
    for (const Entry& e : es) {
        if (e.kind == Kind::Separator || e.kind == Kind::Heading) continue;
        if (!seen.insert(e.label).second) return false;
        if (!LabelsUniquePerLevel(e.children)) return false;
    }
    return true;
}

// The invariants of every menu in every state: unique ids and labels, tidy separators, a reason for every greyed-out item, shortcuts that work.
void CheckMenu(const std::vector<Entry>& m) {
    CHECK(!m.empty());
    CHECK(SeparatorsOk(m));
    CHECK(LabelsUniquePerLevel(m));
    std::set<std::string> ids, keys;
    Walk(m, [&](const Entry& e, int) {
        if (e.kind == Kind::Separator || e.kind == Kind::Heading) return;
        CHECK(!e.id.empty());
        if (!e.id.empty()) CHECK(ids.insert(e.id).second);
        if (!e.enabled) CHECK(!e.reason.empty());
        if (e.enabled) CHECK(e.reason.empty());
        if (!e.shortcut.empty()) {
            bool known = false;
            for (const std::string& k : oked::menubar::KnownShortcuts()) if (k == e.shortcut) known = true;
            CHECK(known);
            // F is Focus, Shift+I Isolate...: each key once per menu (Ctrl+Shift+G is Group, which is shown once as well)
            CHECK(keys.insert(e.shortcut).second);
        }
        if (e.kind == Kind::Submenu) CHECK(!e.children.empty());
    });
}

std::vector<RowState> Rows() {
    std::vector<RowState> v;
    RowState a; v.push_back(a);                                                              // a lone root, nothing else
    RowState b; b.childCount = 3; b.hasParent = true; b.siblingIndex = 1; b.siblingCount = 4; b.clipboardHasObject = true;
    b.kindName = "Light"; b.sameKindOthers = 2; v.push_back(b);
    RowState c = b; c.selectedCount = 5; c.active = false; c.locked = true; c.isolated = true; v.push_back(c);
    RowState d = b; d.inPrefab = true; d.prefabName = "Door.okayprefab"; d.prefabChanged = true; d.prefabHasNested = true; d.modelTools = true; v.push_back(d);
    return v;
}

}  // namespace

std::string EditorSources() {
#ifdef OKAY_SOURCE_DIR
    std::string all;
    for (const auto& f : std::filesystem::directory_iterator(std::filesystem::path(OKAY_SOURCE_DIR) / "editor")) {
        if (f.path().extension() != ".cpp") continue;
        std::ifstream in(f.path());
        std::stringstream ss; ss << in.rdbuf();
        all += ss.str();
    }
    return all;
#else
    return {};
#endif
}

int main() {
    RUN_SUITE("hierarchy_context_model");

    {   // Every menu, every state: the shared invariants.
        for (const RowState& r : Rows()) CheckMenu(RowMenu(r));
        for (bool clip : {false, true})
            for (int objs : {0, 6})
                for (int sel : {0, 1, 4}) {
                    EmptyState e; e.clipboardHasObject = clip; e.objectCount = objs; e.selectedCount = sel; e.sortAZ = clip;
                    CheckMenu(EmptyMenu(e));
                    CheckMenu(CreatePopup(e));
                }
    }

    {   // A plain row: clipboard, rename / duplicate / delete, then building the hierarchy, selecting, state, components, copy.
        RowState r;
        const auto m = RowMenu(r);
        std::vector<std::string> top;
        for (const Entry& e : m) top.push_back(e.kind == Kind::Separator ? "---" : e.id);
        CHECK((top == std::vector<std::string>{
            "h.copy", "h.cut", "h.paste", "h.pastechild", "---",
            "h.rename", "h.duplicate", "h.delete", "h.deletechildren", "---",
            "h.emptychild", "h.createchild", "h.createparent", "h.parentto", "h.unparent", "h.order", "---",
            "h.select", "---",
            "h.active", "h.lock", "h.focus", "h.isolate", "h.transform", "---",
            "h.addcomponent", "h.saveprefab", "---", "h.copytext"}));
        CHECK(FindId(m, "h.rename")->shortcut == "F2");
        CHECK(FindId(m, "h.copy")->shortcut == "Ctrl+C");
        CHECK(FindId(m, "h.cut")->shortcut == "Ctrl+X");
        CHECK(FindId(m, "h.paste")->shortcut == "Ctrl+V");
        CHECK(FindId(m, "h.duplicate")->shortcut == "Ctrl+D");
        CHECK(FindId(m, "h.delete")->shortcut == "Del");
        CHECK(FindId(m, "h.focus")->shortcut == "F");
        CHECK(FindId(m, "h.isolate")->shortcut == "Shift+I");
        CHECK(FindId(m, "h.moveup")->shortcut == "Ctrl+Up");
        CHECK(FindId(m, "h.movedown")->shortcut == "Ctrl+Down");
        // New in the menu: Add Component, Select Same Type, Create Child with every category.
        CHECK(FindId(m, "h.addcomponent") != nullptr);
        CHECK(FindId(m, "h.sel.sametype") != nullptr);
        const Entry* cc = FindId(m, "h.createchild");
        CHECK(cc != nullptr && cc->children.size() == oked::menubar::CreateCategories().size());
        for (const char* c : {"create.2d", "create.3d", "create.light", "create.audio", "create.camera", "create.effects", "create.ui"})
            CHECK(FindId(cc->children, c) != nullptr);
    }

    {   // Plural labels: Delete / Duplicate / Deactivate / Group name the count; Cut is off for several, with the reason.
        RowState one;
        CHECK(FindId(RowMenu(one), "h.delete")->label == "Delete");
        CHECK(FindId(RowMenu(one), "h.duplicate")->label == "Duplicate");
        CHECK(FindId(RowMenu(one), "h.active")->label == "Deactivate");
        CHECK(FindId(RowMenu(one), "h.createparent")->label == "Create Parent");
        CHECK(FindId(RowMenu(one), "h.createparent")->shortcut.empty());
        CHECK(FindId(RowMenu(one), "h.cut")->enabled);
        CHECK(FindId(RowMenu(one), "h.copy")->tip.empty());
        RowState many; many.selectedCount = 3; many.active = false;
        const auto m = RowMenu(many);
        CHECK(FindId(m, "h.delete")->label == "Delete 3 Objects");
        CHECK(FindId(m, "h.duplicate")->label == "Duplicate 3 Objects");
        CHECK(FindId(m, "h.active")->label == "Activate 3 Objects");
        CHECK(FindId(m, "h.createparent")->label == "Group 3 Objects");
        CHECK(FindId(m, "h.createparent")->shortcut == "Ctrl+Shift+G");
        CHECK(!FindId(m, "h.cut")->enabled);
        CHECK(FindId(m, "h.cut")->reason.find("one object") != std::string::npos);
        CHECK(!FindId(m, "h.copy")->tip.empty());
        CHECK(FindId(m, "h.rename")->enabled);   // renames the row you clicked
        RowState zero; zero.selectedCount = 0;   // never below one
        CHECK(FindId(RowMenu(zero), "h.delete")->label == "Delete");
    }

    {   // Clipboard, children, parent, order: each greyed-out item says why.
        RowState r;
        auto m = RowMenu(r);
        CHECK(!FindId(m, "h.paste")->enabled && FindId(m, "h.paste")->reason == oked::menubar::kNothingCopied);
        CHECK(!FindId(m, "h.pastechild")->enabled);
        CHECK(!FindId(m, "h.deletechildren")->enabled && FindId(m, "h.deletechildren")->reason == "This object has no children.");
        CHECK(!FindId(m, "h.unparent")->enabled && FindId(m, "h.unparent")->reason == "It is already at the top level.");
        CHECK(!FindId(m, "h.sel.children")->enabled);
        CHECK(!FindId(m, "h.sel.below")->enabled);
        CHECK(!FindId(m, "h.sel.parent")->enabled);
        CHECK(FindId(m, "h.expandbelow") == nullptr);    // folding items appear only on a parent
        CHECK(FindId(m, "h.collapsebelow") == nullptr);
        // A lone root is both first and last: no move is possible.
        for (const char* id : {"h.moveup", "h.movetop", "h.movedown", "h.movebottom"}) CHECK(!FindId(m, id)->enabled);
        CHECK(FindId(m, "h.moveup")->reason == "It is already the first of its siblings.");
        CHECK(FindId(m, "h.movebottom")->reason == "It is already the last of its siblings.");

        r.clipboardHasObject = true; r.childCount = 2; r.hasParent = true; r.siblingIndex = 1; r.siblingCount = 3;
        m = RowMenu(r);
        for (const char* id : {"h.paste", "h.pastechild", "h.deletechildren", "h.unparent", "h.sel.children", "h.sel.below", "h.sel.parent",
                               "h.moveup", "h.movedown", "h.movetop", "h.movebottom"})
            CHECK(FindId(m, id)->enabled);
        CHECK(FindId(m, "h.expandbelow") != nullptr && FindId(m, "h.collapsebelow") != nullptr);
        r.siblingIndex = 0;   // first of three: it cannot go up
        m = RowMenu(r);
        CHECK(!FindId(m, "h.moveup")->enabled && !FindId(m, "h.movetop")->enabled);
        CHECK(FindId(m, "h.movedown")->enabled && FindId(m, "h.movebottom")->enabled);
        r.siblingIndex = 2;   // last
        m = RowMenu(r);
        CHECK(FindId(m, "h.moveup")->enabled && !FindId(m, "h.movedown")->enabled && !FindId(m, "h.movebottom")->enabled);
    }

    {   // Select Same Type names the kind, and is greyed out when no other object has it.
        RowState r;   // not counted (-1) and no name
        CHECK(FindId(RowMenu(r), "h.sel.sametype")->label == "Same Type");
        CHECK(FindId(RowMenu(r), "h.sel.sametype")->enabled);
        r.kindName = "Camera"; r.sameKindOthers = 3;
        CHECK(FindId(RowMenu(r), "h.sel.sametype")->label == "Same Type (Camera)");
        CHECK(FindId(RowMenu(r), "h.sel.sametype")->enabled);
        r.sameKindOthers = 0;
        CHECK(!FindId(RowMenu(r), "h.sel.sametype")->enabled);
        CHECK(FindId(RowMenu(r), "h.sel.sametype")->reason == "No other Camera in the scene.");
    }

    {   // Active / lock / isolation labels follow the state.
        RowState r;
        CHECK(FindId(RowMenu(r), "h.lock")->label == "Lock in Scene View");
        CHECK(FindId(RowMenu(r), "h.isolate")->label == "Isolate");
        r.active = false; r.locked = true; r.isolated = true;
        CHECK(FindId(RowMenu(r), "h.active")->label == "Activate");
        CHECK(FindId(RowMenu(r), "h.lock")->label == "Unlock in Scene View");
        CHECK(FindId(RowMenu(r), "h.isolate")->label == "Exit Isolation");
    }

    {   // The Prefab submenu appears for prefab parts only; Revert / Apply need changes; Unpack Completely needs nested prefabs.
        RowState r;
        CHECK(FindId(RowMenu(r), "h.prefab") == nullptr);
        r.inPrefab = true; r.prefabName = "Door.okayprefab";
        auto m = RowMenu(r);
        const Entry* p = FindId(m, "h.prefab");
        CHECK(p != nullptr && p->children.size() > 5 && p->children.front().kind == Kind::Heading && p->children.front().label == "Door.okayprefab");
        CHECK(!FindId(m, "h.prefab.revertall")->enabled && FindId(m, "h.prefab.revertall")->reason == "Nothing differs from the prefab.");
        CHECK(!FindId(m, "h.prefab.applyall")->enabled);
        CHECK(!FindId(m, "h.prefab.unpackall")->enabled);
        CHECK(FindId(m, "h.prefab.open")->enabled && FindId(m, "h.prefab.selectasset")->enabled && FindId(m, "h.prefab.unpack")->enabled);
        r.prefabChanged = true; r.prefabHasNested = true;
        m = RowMenu(r);
        CHECK(FindId(m, "h.prefab.revertall")->enabled && FindId(m, "h.prefab.applyall")->enabled && FindId(m, "h.prefab.unpackall")->enabled);
        CHECK(FindId(m, "h.saveprefab") != nullptr);   // Save As Prefab is there for any object
    }

    {   // Rig and IK items: one submenu the host fills, present only when they apply.
        RowState r;
        CHECK(FindId(RowMenu(r), "h.modeltools") == nullptr);
        r.modelTools = true;
        const Entry* s = FindId(RowMenu(r), "h.modeltools");
        CHECK(s != nullptr && s->kind == Kind::SlotMenu && s->label == "Rig & IK");
    }

    {   // Empty space: Create Empty, the categories, Paste, Select All / Deselect, fold, sort.
        EmptyState e;
        auto m = EmptyMenu(e);
        std::vector<std::string> top;
        for (const Entry& x : m) top.push_back(x.kind == Kind::Separator ? "---" : x.id);
        CHECK((top == std::vector<std::string>{
            "h.createempty", "---",
            "create.2d", "create.3d", "create.player", "create.vehicles", "create.light", "create.camera", "create.audio", "create.effects", "create.ui", "---",
            "h.paste", "---", "h.selectall", "h.deselect", "---", "h.expandall", "h.collapseall", "h.sortaz"}));
        CHECK(!FindId(m, "h.paste")->enabled && FindId(m, "h.paste")->reason == oked::menubar::kNothingCopied);
        CHECK(!FindId(m, "h.selectall")->enabled && FindId(m, "h.selectall")->reason == "The scene has no objects.");
        CHECK(!FindId(m, "h.deselect")->enabled && FindId(m, "h.deselect")->reason == "Nothing is selected.");
        CHECK(!FindId(m, "h.expandall")->enabled && !FindId(m, "h.collapseall")->enabled);
        CHECK(FindId(m, "h.createempty")->enabled);
        CHECK(FindId(m, "h.sortaz")->kind == Kind::Toggle);
        CHECK(FindId(m, "h.selectall")->shortcut == "Ctrl+A");
        e.clipboardHasObject = true; e.objectCount = 4; e.selectedCount = 2;
        m = EmptyMenu(e);
        for (const char* id : {"h.paste", "h.selectall", "h.deselect", "h.expandall", "h.collapseall"}) CHECK(FindId(m, id)->enabled);
    }

    {   // The [+] popup: create here or inside the selection, or group the selection.
        EmptyState e;
        auto m = CreatePopup(e);
        CHECK(FindId(m, "h.createempty")->enabled);
        CHECK(!FindId(m, "h.emptychild")->enabled && FindId(m, "h.emptychild")->reason == oked::menubar::kNeedSelection);
        CHECK(!FindId(m, "h.groupselected")->enabled);
        CHECK(FindId(m, "h.groupselected")->label == "Group Selected");
        CHECK(FindId(m, "h.groupselected")->shortcut == "Ctrl+Shift+G");
        for (const char* c : {"create.2d", "create.3d", "create.light", "create.audio", "create.camera", "create.effects", "create.ui"})
            CHECK(FindId(m, c) != nullptr);
        e.selectedCount = 1;
        m = CreatePopup(e);
        CHECK(FindId(m, "h.emptychild")->enabled && FindId(m, "h.groupselected")->enabled);
        e.selectedCount = 6;
        CHECK(FindId(CreatePopup(e), "h.groupselected")->label == "Group 6 Objects");
    }

    {   // Nothing the old Hierarchy menus offered is gone. Old right-click menu of a row -> id here.
        RowState r; r.childCount = 2; r.hasParent = true; r.siblingIndex = 1; r.siblingCount = 3; r.clipboardHasObject = true; r.inPrefab = true;
        r.modelTools = true; r.kindName = "Mesh";
        const auto m = RowMenu(r);
        for (const char* id : {"h.rename", "h.active", "h.lock", "h.cut", "h.copy", "h.paste", "h.pastechild", "h.duplicate", "h.createparent",
                               "h.focus", "h.isolate", "h.delete", "h.deletechildren", "h.modeltools", "h.order", "h.moveup", "h.movedown",
                               "h.movetop", "h.movebottom", "h.emptychild", "h.createchild", "h.parentto", "h.unparent", "h.select",
                               "h.sel.children", "h.sel.below", "h.sel.parent", "h.sel.siblings", "h.sel.samename", "h.expandbelow",
                               "h.collapsebelow", "h.transform", "h.tf.resetpos", "h.tf.resetrot", "h.tf.resetscale", "h.tf.resetall",
                               "h.tf.movetoview", "h.copytext", "h.copyname", "h.copypath", "h.prefab", "h.prefab.open", "h.prefab.revertall",
                               "h.prefab.applyall", "h.prefab.variant", "h.prefab.updateall", "h.prefab.unpack", "h.prefab.unpackall", "h.saveprefab"})
            CHECK(FindId(m, id) != nullptr);
        // The old empty-space menu: Create Empty / Sprite / Cube / Camera / Light, 3D Object, UI, Paste, Select All, Deselect, Expand All,
        // Collapse All, Sort A-Z. The five quick creators and the two submenus are inside the shared categories now.
        EmptyState e; e.objectCount = 1; e.selectedCount = 1; e.clipboardHasObject = true;
        const auto em = EmptyMenu(e);
        for (const char* id : {"h.createempty", "create.2d", "create.3d", "create.light", "create.camera", "create.ui", "h.paste",
                               "h.selectall", "h.deselect", "h.expandall", "h.collapseall", "h.sortaz"})
            CHECK(FindId(em, id) != nullptr);
        // The old [+] popup: Empty / Sprite / Camera / Cube, Group Selected, UI Button / Text, 3D UI.
        const auto pm = CreatePopup(e);
        for (const char* id : {"h.createempty", "h.groupselected", "create.2d", "create.3d", "create.camera", "create.ui"})
            CHECK(FindId(pm, id) != nullptr);
    }

    {   // Wiring: every id the three menus can produce is handled in the editor sources (`id == "h.copy"`, or its toggle flag).
        const std::string src = EditorSources();
        if (src.empty()) {
            std::cout << "  (editor sources not found: wiring check skipped)\n";
        } else {
            std::set<std::string> seen;
            auto check = [&](const std::vector<Entry>& m) {
                Walk(m, [&](const Entry& e, int) {
                    if (e.kind == Kind::Separator || e.kind == Kind::Heading || e.id.empty() || !seen.insert(e.id).second) return;
                    if (e.kind == Kind::Submenu) return;   // its children are checked on their own
                    const bool wired = src.find("id == \"" + e.id + "\"") != std::string::npos;
                    if (!wired) std::cerr << "  not wired in editor/*.cpp: " << e.id << "\n";
                    CHECK(wired);
                });
            };
            for (const RowState& r : Rows()) check(RowMenu(r));
            EmptyState e; e.objectCount = 3; e.selectedCount = 1; e.clipboardHasObject = true;
            check(EmptyMenu(e));
            check(CreatePopup(e));
            CHECK(seen.size() > 50);
        }
    }

    {   // SlotRecorder / SlotReplayer: a host-drawn list (Parent To, Rig & IK) recorded into entries for the OkayUI Hierarchy, and the
        // picked entry run again by replaying the same list (only that item answers true; only the sub-menus on its way open).
        std::vector<std::string> ran;
        auto slot = [&](oked::menubar::SlotUI& ui) {
            if (ui.Item("Crate")) ran.push_back("Crate");
            ui.Tip("a box");
            ui.Separator();
            if (ui.BeginMenu("Add IK Target")) {
                if (ui.Item("Left Hand")) ran.push_back("Left Hand");
                if (ui.Item("Head", false)) ran.push_back("Head");
                ui.Tip("needs a head bone");
                ui.EndMenu();
            }
            if (ui.BeginMenu("Closed", false)) { if (ui.Item("never")) ran.push_back("never"); ui.EndMenu(); }
            if (ui.Item("Crate")) ran.push_back("Crate 2");   // the same label twice: told apart by place
            ui.Note("(no eligible objects)");
        };
        oked::menubar::SlotRecorder rec("h.parentto");
        slot(rec);
        CHECK(ran.empty());   // recording runs nothing
        const std::vector<Entry> got = rec.Take();
        CHECK(got.size() == 6);
        CHECK(got[0].id == "h.parentto/0:Crate" && got[0].tip == "a box" && got[1].kind == Kind::Separator);
        CHECK(got[2].kind == Kind::Submenu && got[2].children.size() == 2 && got[2].children[0].id == "h.parentto/2:Add IK Target/0:Left Hand");
        CHECK(!got[2].children[1].enabled && got[2].children[1].reason == "needs a head bone");
        CHECK(got[3].kind == Kind::Submenu && !got[3].enabled && got[3].children.empty());
        CHECK(got[4].id == "h.parentto/4:Crate" && got[5].kind == Kind::Heading);
        oked::menubar::SlotReplayer a("h.parentto", got[2].children[0].id);
        slot(a);
        CHECK(a.Ran() && ran.size() == 1 && ran[0] == "Left Hand");
        ran.clear();
        oked::menubar::SlotReplayer b("h.parentto", got[4].id);
        slot(b);
        CHECK(b.Ran() && ran.size() == 1 && ran[0] == "Crate 2");
        ran.clear();
        oked::menubar::SlotReplayer c("h.parentto", "h.parentto/2:Add IK Target/1:Head");   // greyed out: does not run
        slot(c);
        CHECK(!c.Ran() && ran.empty());
        oked::menubar::SlotReplayer d("h.modeltools", got[0].id);   // another slot's id: nothing
        slot(d);
        CHECK(!d.Ran() && ran.empty());
    }

    TEST_MAIN_RESULT();
}
