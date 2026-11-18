#include "test_framework.hpp"
#include "MenuBarModel.hpp"
#include "AutoPerf.hpp"
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

// The editor's main menu bar as data (editor/MenuBarModel.hpp, no ImGui): the order of the menus, that nothing is listed twice
// (File > Examples was there three times, Items / Loot & Recipes twice, Reset Layout twice), that every greyed-out item says why,
// that every shown shortcut is a key that works, the plural labels, and that every item of the old menus still has a home.
using namespace oked::menubar;

namespace {

// Every top-level menu the model builds, for one editor state.
std::vector<std::pair<std::string, std::vector<Entry>>> AllMenus(const State& s) {
    std::vector<std::pair<std::string, std::vector<Entry>>> v;
    for (const Top& t : TopMenus()) v.push_back({t.id, BuildMenu(t.id, s)});
    return v;
}

// A representative set of editor states: nothing selected, one object, several, playing, prefab mode, with history.
std::vector<State> States() {
    std::vector<State> v;
    State a; v.push_back(a);                                   // a fresh editor
    State b; b.selectedCount = 1; b.objectCount = 5; b.canUndo = true; b.undoName = "Move"; b.clipboardHasObject = true;
    b.scenePath = "Assets/Scenes/Level1.okayscene"; b.recentCount = 3; b.assimp = true; v.push_back(b);
    State c = b; c.selectedCount = 4; c.canRedo = true; c.redoName = "Delete"; c.isolating = true; v.push_back(c);
    State d = b; d.playing = true; d.canUndo = false; v.push_back(d);
    State e = b; e.inPrefabMode = true; v.push_back(e);
    return v;
}

bool SeparatorsOk(const std::vector<Entry>& es) {
    if (es.empty()) return true;
    if (es.front().kind == Kind::Separator || es.back().kind == Kind::Separator) return false;
    for (std::size_t i = 1; i < es.size(); ++i)
        if (es[i].kind == Kind::Separator && es[i - 1].kind == Kind::Separator) return false;
    for (const Entry& e : es) if (!SeparatorsOk(e.children)) return false;
    return true;
}

// No two entries of one level share a label (ImGui would give them one ID), headings and separators aside.
bool LabelsUniquePerLevel(const std::vector<Entry>& es) {
    std::set<std::string> seen;
    for (const Entry& e : es) {
        if (e.kind == Kind::Separator || e.kind == Kind::Heading) continue;
        if (!seen.insert(e.label).second) return false;
        if (!LabelsUniquePerLevel(e.children)) return false;
    }
    return true;
}

const Entry* FindLabel(const std::vector<Entry>& es, const std::string& label) {
    for (const Entry& e : es) {
        if (e.kind != Kind::Separator && e.kind != Kind::Heading && e.label == label) return &e;
        if (const Entry* c = FindLabel(e.children, label)) return c;
    }
    return nullptr;
}

std::vector<std::string> Ids(const std::vector<Entry>& es) {
    std::vector<std::string> v;
    for (const Entry& e : es) v.push_back(e.kind == Kind::Separator ? "---" : e.kind == Kind::Heading ? "#" + e.label : e.id);
    return v;
}

}  // namespace

// The text of every editor/*.cpp (empty when the source directory is not known): the wiring check below looks for the ids in it.
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
    RUN_SUITE("menu_bar_model");

    {   // The bar: File Edit GameObject View Plugins Help. The old Engine menu and the old Window menu (one duplicate item) are gone.
        const auto& tops = TopMenus();
        std::vector<std::string> labels;
        for (const Top& t : tops) labels.push_back(t.label);
        CHECK((labels == std::vector<std::string>{"File", "Edit", "GameObject", "View", "Plugins", "Help"}));
        for (const Top& t : tops) {
            if (t.hostDrawn) CHECK(BuildMenu(t.id, State{}).empty());   // Plugins is drawn by the host
            else CHECK(!BuildMenu(t.id, State{}).empty());
        }
        CHECK(BuildMenu("engine", State{}).empty());
        CHECK(BuildMenu("window", State{}).empty());
        CHECK(BuildMenu("nope", State{}).empty());
    }

    {   // Rules every menu obeys in every state.
        for (const State& s : States()) {
            std::set<std::string> ids, toggleIds;
            for (const auto& m : AllMenus(s)) {
                CHECK(SeparatorsOk(m.second));
                CHECK(LabelsUniquePerLevel(m.second));
                std::set<std::string> shortcutsHere;
                Walk(m.second, [&](const Entry& e, int) {
                    if (e.kind == Kind::Separator) { CHECK(e.id.empty()); return; }
                    if (e.kind == Kind::Heading) { CHECK(!e.label.empty()); return; }
                    CHECK(!e.id.empty());                       // everything that can be clicked has a name the host dispatches on
                    if (e.kind != Kind::Slot) CHECK(!e.label.empty());
                    if (!e.id.empty()) CHECK(ids.insert(e.id).second);   // an id is used once in the whole bar
                    if (e.kind == Kind::Submenu) CHECK(!e.children.empty());
                    if (e.kind != Kind::Submenu) CHECK(e.children.empty());
                    if (!e.enabled) CHECK(!e.reason.empty());   // a greyed-out item says why
                    if (e.enabled) CHECK(e.reason.empty());
                    if (!e.shortcut.empty()) {                  // a shown key must work, and is shown once per menu
                        bool known = false;
                        for (const std::string& k : KnownShortcuts()) if (k == e.shortcut) known = true;
                        CHECK(known);
                        CHECK(shortcutsHere.insert(e.shortcut).second);
                    }
                    if (e.kind == Kind::Toggle) CHECK(toggleIds.insert(e.label).second);   // a window is listed once
                });
            }
        }
    }

    {   // The duplicates the owner saw are gone: each of these is in the bar exactly once.
        const State s;
        std::vector<Entry> all;
        for (const auto& m : AllMenus(s)) for (const Entry& e : m.second) all.push_back(e);
        for (const char* once : {"Layout", "Build Settings...", "Items, Loot & Recipes", "Check for Updates...", "Panels",
                                 "Editors & Tools", "Scene Overlays", "Camera Bookmarks", "Rendering & Performance", "Project Settings..."})
            CHECK(CountLabel(all, once) == 1);
        CHECK(CountLabel(all, "Examples") == 0);   // removed on the owner's request (2026-11-16)
        CHECK(CountLabel(all, "Reset Layout") == 0);   // it is inside the Layout menu (drawn there), not beside it
        CHECK(CountLabel(all, "Engine") == 0);
        // Edit holds the clipboard and delete commands; GameObject no longer repeats them.
        CHECK(FindId(BuildMenu("edit", s), "edit.duplicate") != nullptr);
        CHECK(FindId(BuildMenu("gameobject", s), "edit.duplicate") == nullptr);
        CHECK(CountLabel(BuildMenu("gameobject", s), "Copy") == 0);
        CHECK(CountLabel(BuildMenu("gameobject", s), "Paste") == 0);
    }

    {   // File: new / open / recent / examples, save, project, import, autosave + build, exit.
        State s; s.recentCount = 2;
        CHECK((Ids(FileMenu(s)) == std::vector<std::string>{
            "file.new2d", "file.new3d", "file.open", "file.recent", "---",
            "file.save", "file.saveas", "file.saveproject", "---",
            "file.newproject", "file.openproject", "file.upgrade", "---",
            "file.import", "file.merge", "---", "file.autosave", "file.build", "---", "file.exit"}));
        CHECK(FindId(FileMenu(s), "file.open")->label == "Open Scene...");
        CHECK(FindId(FileMenu(s), "file.open")->shortcut == "Ctrl+O");
        CHECK(FindId(FileMenu(s), "file.save")->shortcut == "Ctrl+S");
        CHECK(FindId(FileMenu(s), "file.saveas")->shortcut == "Ctrl+Shift+S");
        CHECK(FindId(FileMenu(s), "file.saveproject")->shortcut == "Ctrl+Alt+S");
        CHECK(FindId(FileMenu(s), "file.build")->shortcut == "Ctrl+B");
        CHECK(FindId(FileMenu(s), "file.recent")->enabled);
        State none;   // no recent scenes: greyed out, with the reason
        const auto noneMenu = FileMenu(none);
        const Entry* r = FindId(noneMenu, "file.recent");
        CHECK(!r->enabled && r->reason.find("No recent scenes") != std::string::npos);
        // Save while the game runs says what to do; the tip names the file once the scene has one.
        State play; play.playing = true;
        CHECK(!FindId(FileMenu(play), "file.save")->enabled);
        CHECK(FindId(FileMenu(play), "file.save")->reason == kStopPlayFirst);
        CHECK(!FindId(FileMenu(play), "file.saveas")->enabled);
        CHECK(FindId(FileMenu(State{}), "file.save")->enabled);
        CHECK(FindId(FileMenu(State{}), "file.save")->tip.find("hasn't been saved yet") != std::string::npos);
        State saved; saved.scenePath = "Assets/Scenes/A.okayscene";
        CHECK(FindId(FileMenu(saved), "file.save")->tip == "Save this scene: Assets/Scenes/A.okayscene");
        CHECK(FindId(FileMenu(State{}), "file.import")->tip.find("DAE") == std::string::npos);
        State asm_; asm_.assimp = true;
        CHECK(FindId(FileMenu(asm_), "file.import")->tip.find("DAE") != std::string::npos);
    }

    {   // Edit: undo / redo, clipboard, select, preferences. Labels name the step; plural forms; reasons.
        State s;
        CHECK((Ids(EditMenu(s)) == std::vector<std::string>{
            "edit.undo", "edit.redo", "---", "edit.cut", "edit.copy", "edit.paste", "edit.duplicate", "edit.delete", "---",
            "edit.selectall", "edit.deselect", "---", "edit.darkmode", "edit.lightmode", "edit.projectsettings"}));
        CHECK(FindId(EditMenu(s), "edit.undo")->label == "Undo");
        // One look choice: Dark Mode / Light Mode. The mode that is on is greyed with a reason; no Preferences menu is left.
        CHECK(FindId(EditMenu(s), "edit.preferences") == nullptr);
        CHECK(FindId(EditMenu(s), "edit.darkmode")->label == "Dark Mode");
        CHECK(FindId(EditMenu(s), "edit.lightmode")->label == "Light Mode");
        CHECK(!FindId(EditMenu(s), "edit.darkmode")->enabled && FindId(EditMenu(s), "edit.lightmode")->enabled);
        CHECK(FindId(EditMenu(s), "edit.darkmode")->reason == "The editor is already in dark mode.");
        { State l; l.darkMode = false;
          CHECK(FindId(EditMenu(l), "edit.darkmode")->enabled && !FindId(EditMenu(l), "edit.lightmode")->enabled); }
        // Old settings files: theme 0 Slate and 2 Classic are dark, 1 Light is light, unknown values fall back to dark.
        CHECK(LegacyThemeIsDark(0) && !LegacyThemeIsDark(1) && LegacyThemeIsDark(2) && LegacyThemeIsDark(99));
        CHECK(!FindId(EditMenu(s), "edit.undo")->enabled);
        CHECK(FindId(EditMenu(s), "edit.undo")->reason == "Nothing to undo yet.");
        CHECK(FindId(EditMenu(s), "edit.redo")->reason == "Nothing to redo.");
        s.canUndo = true; s.undoName = "Move"; s.canRedo = true; s.redoName = "Rotate";
        CHECK(FindId(EditMenu(s), "edit.undo")->label == "Undo Move");
        CHECK(FindId(EditMenu(s), "edit.redo")->label == "Redo Rotate");
        CHECK(FindId(EditMenu(s), "edit.undo")->enabled);
        CHECK(FindId(EditMenu(s), "edit.undo")->shortcut == "Ctrl+Z");
        CHECK(FindId(EditMenu(s), "edit.redo")->shortcut == "Ctrl+Y");
        s.playing = true; s.canUndo = false; s.canRedo = false;   // Undo is off in Play: the reason says so
        CHECK(FindId(EditMenu(s), "edit.undo")->reason == kUndoOffInPlay);
        CHECK(FindId(EditMenu(s), "edit.redo")->reason == kRedoOffInPlay);
        CHECK(FindId(EditMenu(s), "edit.undo")->tip.empty());

        State none;   // nothing selected, nothing copied, nothing in the scene
        for (const char* id : {"edit.cut", "edit.copy", "edit.duplicate", "edit.delete"}) {
            CHECK(!FindId(EditMenu(none), id)->enabled);
            CHECK(FindId(EditMenu(none), id)->reason == kNeedSelection);
        }
        CHECK(FindId(EditMenu(none), "edit.paste")->reason == kNothingCopied);
        CHECK(FindId(EditMenu(none), "edit.selectall")->reason == "The scene has no objects.");
        CHECK(FindId(EditMenu(none), "edit.deselect")->reason == "Nothing is selected.");
        CHECK(FindId(EditMenu(none), "edit.duplicate")->label == "Duplicate");

        State one; one.selectedCount = 1; one.objectCount = 4; one.clipboardHasObject = true;
        for (const char* id : {"edit.cut", "edit.copy", "edit.paste", "edit.duplicate", "edit.delete", "edit.selectall", "edit.deselect"})
            CHECK(FindId(EditMenu(one), id)->enabled);

        State many = one; many.selectedCount = 3;   // plural forms; Cut is off (it would delete all three and keep one)
        CHECK(FindId(EditMenu(many), "edit.duplicate")->label == "Duplicate 3 Objects");
        CHECK(FindId(EditMenu(many), "edit.delete")->label == "Delete 3 Objects");
        CHECK(FindId(EditMenu(many), "edit.delete")->shortcut == "Del");
        CHECK(!FindId(EditMenu(many), "edit.cut")->enabled);
        CHECK(FindId(EditMenu(many), "edit.cut")->reason.find("one object") != std::string::npos);
        CHECK(FindId(EditMenu(many), "edit.copy")->enabled);
        CHECK(!FindId(EditMenu(many), "edit.copy")->tip.empty());
    }

    {   // GameObject: Create Empty / Child / Parent, then the categories, prefab, then the selection tools.
        State none;
        CHECK((Ids(GameObjectMenu(none)) == std::vector<std::string>{
            "go.empty", "go.emptychild", "go.emptyparent", "---",
            "create.2d", "create.3d", "create.player", "create.light", "create.camera", "create.audio", "create.effects", "create.ui", "---",
            "go.instantiate", "---", "go.arraydup", "go.snap"}));
        CHECK(FindId(GameObjectMenu(none), "go.empty")->enabled);
        for (const char* id : {"go.emptychild", "go.emptyparent", "go.arraydup", "go.snap"}) {
            CHECK(!FindId(GameObjectMenu(none), id)->enabled);
            CHECK(FindId(GameObjectMenu(none), id)->reason == kNeedSelection);
        }
        State one; one.selectedCount = 1;
        for (const char* id : {"go.emptychild", "go.emptyparent", "go.arraydup", "go.snap"}) CHECK(FindId(GameObjectMenu(one), id)->enabled);
        CHECK(FindId(GameObjectMenu(one), "go.emptyparent")->label == "Create Empty Parent");
        CHECK(FindId(GameObjectMenu(one), "go.emptyparent")->shortcut == "Ctrl+Shift+G");
        State three; three.selectedCount = 3;
        CHECK(FindId(GameObjectMenu(three), "go.emptyparent")->label == "Create Empty Parent for 3 Objects");
        // The categories the owner listed are all there, as slot menus the host fills.
        const auto goMenu = GameObjectMenu(none);
        for (const char* label : {"2D Object", "3D Object", "Light", "Audio", "UI", "Effects", "Camera"}) {
            const Entry* c = FindLabel(goMenu, label);
            CHECK(c != nullptr && c->kind == Kind::SlotMenu);
        }
        CHECK(CreateCategories().size() == 8);
    }

    {   // View: the windows (each listed once) and the layouts, then the Scene view's commands, overlays and rendering settings.
        State s;
        const auto v = ViewMenu(s);
        CHECK((Ids(v) == std::vector<std::string>{"view.panels", "view.tools", "view.layout", "---", "view.frame", "view.isolate", "view.uionly", "---",
                                                  "view.overlays", "view.bookmarks", "---", "view.rendering"}));
        CHECK(FindLabel(v, "Hierarchy")->kind == Kind::Toggle);
        CHECK(FindLabel(v, "Items, Loot & Recipes")->kind == Kind::Toggle);
        CHECK(FindLabel(v, "Audio Mixer")->tip.find("ducking") != std::string::npos);
        CHECK(FindLabel(v, "Layout")->kind == Kind::SlotMenu);
        CHECK(!FindId(v, "view.frame")->enabled);
        CHECK(FindId(v, "view.frame")->shortcut == "F");
        CHECK(FindId(v, "view.isolate")->label == "Isolate Selection");
        State sel; sel.selectedCount = 1;
        CHECK(FindId(ViewMenu(sel), "view.frame")->enabled);
        CHECK(FindId(ViewMenu(sel), "view.isolate")->shortcut == "Shift+I");
        State iso; iso.isolating = true;   // leaving isolation never needs a selection
        CHECK(FindId(ViewMenu(iso), "view.isolate")->label == "Exit Isolation");
        CHECK(FindId(ViewMenu(iso), "view.isolate")->enabled);
    }

    {   // Help: docs, shortcuts, updates, bug report, about.
        CHECK((Ids(HelpMenu(State{})) == std::vector<std::string>{
            "help.scripting", "help.docs", "help.shortcuts", "---", "help.updates", "help.bug", "---", "help.about"}));
    }

    {   // The checker really finds the old problems: the pre-2026-11-15 File menu listed "Examples" three times and the old View
        // menu listed "Reset Layout" twice (once inside Layout, once beside it); the same checks reject them.
        std::vector<Entry> oldFile = {Item("a", "New 2D Scene"), Sub("e1", "Examples", {Item("n", "Node Playground")}), Sep(),
                                      Item("o", "Open..."), SlotMenu("e2", "Examples"), Sep(),
                                      Sub("e3", "Examples", {Item("v", "Visual Script Node Playground")}), Item("x", "Exit")};
        CHECK(CountLabel(oldFile, "Examples") == 3);
        CHECK(!LabelsUniquePerLevel(oldFile));
        std::vector<Entry> oldView = {Sub("l", "Layout", {Item("r", "Reset Layout")}), Item("r2", "Reset Layout")};
        CHECK(CountLabel(oldView, "Reset Layout") == 2);
        std::vector<Entry> oldWindow = {Toggle("i1", "Items, Loot & Recipes"), Toggle("i2", "Items, Loot & Recipes")};
        CHECK(!LabelsUniquePerLevel(oldWindow));
        CHECK(!SeparatorsOk({Sep(), Item("a", "A")}));
        CHECK(!SeparatorsOk({Item("a", "A"), Sep(), Sep(), Item("b", "B")}));
        CHECK(!SeparatorsOk({Item("a", "A"), Sep()}));
    }

    {   // Nothing disappeared. Each row: the item the old menus had -> where it is now. "id" is an entry of the model; a "slot:" row
        // is an item inside a SlotMenu / Slot the host draws (named by the id of that slot).
        struct Moved { const char* oldPlace; const char* oldLabel; const char* where; };
        const Moved moved[] = {
            {"File", "New Project...", "file.newproject"}, {"File", "Open Project Folder...", "file.openproject"},
            {"File", "Project Upgrade...", "file.upgrade"}, {"File", "New 2D Scene", "file.new2d"}, {"File", "New 3D Scene", "file.new3d"},
            {"File", "Import Model...", "file.import"}, {"File", "Open...", "file.open"}, {"File", "Open Recent", "file.recent"},
            {"File", "Save Scene", "file.save"}, {"File", "Save Scene As...", "file.saveas"}, {"File", "Save Project", "file.saveproject"},
            {"File", "Merge Scene into Current...", "file.merge"}, {"File", "Autosave", "file.autosave"},
            {"File", "Build Settings...", "file.build"}, {"File", "Exit", "file.exit"},
            {"Edit", "Undo", "edit.undo"}, {"Edit", "Redo", "edit.redo"}, {"Edit", "Project Settings...", "edit.projectsettings"},
            {"View", "Panels", "view.panels"}, {"View", "Editors & Tools", "view.tools"},
            {"View", "Rendering & Performance", "view.rendering"}, {"View", "Scene Overlays", "view.overlays"},
            {"View", "Appearance", "edit.darkmode"}, {"View", "Camera Bookmarks", "view.bookmarks"},
            {"View", "Layout", "view.layout"}, {"View", "Reset Layout", "view.layout"},
            {"View > Editors & Tools", "UI Editing Mode", "view.uionly"},
            {"Window", "Items, Loot & Recipes", "tool.items"},   // the Window menu held only this duplicate
            {"GameObject", "Create Empty", "go.empty"}, {"GameObject", "Create Sprite", "create.2d"},
            {"GameObject", "2D Character (Sprite Sheet)...", "create.2d"}, {"GameObject", "2D Shape", "create.2d"},
            {"GameObject", "Sprite Editor...", "tool.spriteeditor"}, {"GameObject", "Create Camera", "create.camera"},
            {"GameObject", "Player", "create.player"}, {"GameObject", "3D Object", "create.3d"}, {"GameObject", "Light", "create.light"},
            {"GameObject", "Cinemachine", "create.camera"}, {"GameObject", "Create Particle System", "create.effects"},
            {"GameObject", "Spline", "create.effects"}, {"GameObject", "Foliage Layer", "create.effects"},
            {"GameObject", "Decal Projector", "create.effects"}, {"GameObject", "Cable", "create.effects"},
            {"GameObject", "Volume", "create.effects"}, {"GameObject", "Create Tilemap", "create.2d"}, {"GameObject", "UI", "create.ui"},
            {"GameObject", "Instantiate Prefab...", "go.instantiate"}, {"GameObject", "Duplicate Selected", "edit.duplicate"},
            {"GameObject", "Array Duplicate...", "go.arraydup"}, {"GameObject", "Snap Position to Grid", "go.snap"},
            {"GameObject", "Copy", "edit.copy"}, {"GameObject", "Paste", "edit.paste"}, {"GameObject", "Delete Selected", "edit.delete"},
            {"Engine", "Check for Updates...", "help.updates"},
            {"Help", "Scripting Reference", "help.scripting"}, {"Help", "Keyboard Shortcuts", "help.shortcuts"}, {"Help", "About OkaySpace", "help.about"},
        };
        State s; s.selectedCount = 1; s.objectCount = 2; s.canUndo = s.canRedo = s.clipboardHasObject = true; s.recentCount = 1;
        std::vector<Entry> all;
        for (const auto& m : AllMenus(s)) for (const Entry& e : m.second) all.push_back(e);
        for (const Moved& m : moved) CHECK(FindId(all, m.where) != nullptr);
        // Panels and tool windows keep their labels and are each reachable.
        for (const char* label : {"Hierarchy", "Inspector", "Project", "Console", "Problems", "Game", "Services", "Scene Manager", "Net",
                                  "Save Manager", "History", "Stats", "Lighting", "Profiler", "Asset Manager",
                                  "Crash Log", "Audio Mixer", "Visual Script", "Script Editor", "Animation",
                                  "Animator (state graph)", "Timeline (cutscenes)", "Navigation", "Dialogue & Quests", "Behavior Tree",
                                  "Modeling", "UV Editor", "Sprite Editor", "UI Editor (separate tab)", "UI Theme", "Items, Loot & Recipes",
                                  "Weapons", "Localization", "UI Editing Mode"}) {
            const Entry* e = FindLabel(all, label);
            CHECK(e != nullptr && e->kind == Kind::Toggle);
        }
    }

    {   // Wiring: every id the model can produce is handled in the editor sources, so no menu entry is a dead click.
        // An Item must be run (`id == "file.save"`), a Toggle must have its flag (`{"panel.hierarchy",`), a slot must be drawn.
        const std::string src = EditorSources();
        if (src.empty()) {
            std::cout << "  (editor sources not found: wiring check skipped)\n";
        } else {
            std::set<std::string> seen;
            for (const State& st : States())
                for (const auto& m : AllMenus(st))
                    Walk(m.second, [&](const Entry& e, int) {
                        if (e.kind == Kind::Separator || e.kind == Kind::Heading || e.id.empty() || !seen.insert(e.id).second) return;
                        bool wired;
                        if (e.kind == Kind::Toggle) wired = src.find("{\"" + e.id + "\",") != std::string::npos;
                        else if (e.kind == Kind::Submenu) wired = true;   // children are checked on their own
                        else wired = src.find("id == \"" + e.id + "\"") != std::string::npos;
                        if (!wired) std::cerr << "  not wired in editor/*.cpp: " << e.id << "\n";
                        CHECK(wired);
                    });
            CHECK(seen.size() > 80);   // the walk really covered the menus (about 90 ids)
        }
    }

    {   // Auto for Rendering & Performance: best value for this PC, shown as "Auto (On)"; the user can still force a value.
        using namespace oked::autoperf;
        Hardware strong; strong.gpuRenderer = true; strong.cpuCores = 8; strong.ramMB = 16000; strong.displayHz = 144;
        Hardware weak;   weak.gpuRenderer = false;  weak.cpuCores = 2;   weak.ramMB = 4000;
        Hardware midGpu = strong; midGpu.cpuCores = 2;
        Hardware unknown;
        CHECK(AutoAntiAliasing(strong) && !AutoAntiAliasing(weak) && !AutoAntiAliasing(midGpu) && !AutoAntiAliasing(unknown));
        CHECK(AutoVSync(strong) && !AutoVSync(weak) && !AutoVSync(unknown));
        CHECK(Resolve(Auto, true) && !Resolve(Auto, false));
        CHECK(!Resolve(ForceOff, true) && Resolve(ForceOn, false));   // an override beats Auto
        CHECK(ClampMode(-1) == Auto && ClampMode(7) == Auto && ClampMode(2) == ForceOn);
        CHECK(Label(Auto, true) == "Auto (On)" && Label(Auto, false) == "Auto (Off)" && Label(ForceOff, false) == "Off" && Label(ForceOn, true) == "On");
    }
    TEST_MAIN_RESULT();
}
