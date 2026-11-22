#pragma once
// The editor's main menu bar as plain data (no ImGui, no editor state): what each top-level menu holds, in what order,
// with which shortcut, and when an item is greyed out and why. The editor (main.cpp) draws these lists with
// MenuEntriesUI.hpp and runs the item ids; the rules live here so tests/test_menu_bar_model.cpp can check them
// without a window. docs/editor/editor_menus.md is the audit and the final map.
//
// Written from the owner's request (2026-11-15, "clean the menu bar better") and the menu code that was in main.cpp; no
// code from another project.
//
// Four kinds of entry carry meaning for the drawer:
//   Item / Toggle   a leaf. An Item runs `id`; a Toggle shows a check mark bound to a live flag the host looks up by `id`.
//   Submenu         children drawn right here.
//   SlotMenu / Slot content the host draws itself because it depends on live data (recent files, saved layouts, the
//                   Examples found on disk, plugins) or is a big existing block (Rendering & Performance).
// Shortcuts are only ever shown for keys that really work (key:: below, mirrored by the Help > Keyboard Shortcuts window).
#include <cstddef>
#include <string>
#include <vector>

namespace oked {
namespace menubar {

enum class Kind { Item, Toggle, Submenu, SlotMenu, Slot, Separator, Heading };

struct Entry {
    Kind kind = Kind::Item;
    std::string id;          // what the host dispatches on ("edit.undo"); empty for separators and headings
    std::string label;
    std::string shortcut;    // right-aligned by the drawer; "" = none
    std::string tip;         // hover text
    std::string reason;      // why it is greyed out (hover text while disabled); set whenever enabled is false
    bool enabled = true;
    std::vector<Entry> children;   // Submenu only
};

// ---- the keys that work (HandleShortcuts and the Hierarchy in main.cpp; listed in Help > Keyboard Shortcuts) ----
namespace key {
inline constexpr const char* Open = "Ctrl+O";
inline constexpr const char* Save = "Ctrl+S";
inline constexpr const char* SaveAs = "Ctrl+Shift+S";
inline constexpr const char* SaveProject = "Ctrl+Alt+S";
inline constexpr const char* Build = "Ctrl+B";
inline constexpr const char* Undo = "Ctrl+Z";
inline constexpr const char* Redo = "Ctrl+Y";
inline constexpr const char* Cut = "Ctrl+X";
inline constexpr const char* Copy = "Ctrl+C";
inline constexpr const char* Paste = "Ctrl+V";
inline constexpr const char* Duplicate = "Ctrl+D";
inline constexpr const char* Delete = "Del";
inline constexpr const char* Group = "Ctrl+Shift+G";
inline constexpr const char* Frame = "F";
inline constexpr const char* Isolate = "Shift+I";
inline constexpr const char* Rename = "F2";
inline constexpr const char* SelectAll = "Ctrl+A";
inline constexpr const char* MoveUp = "Ctrl+Up";
inline constexpr const char* MoveDown = "Ctrl+Down";
}  // namespace key

/// Every key combination a menu may show. A shortcut on an item that is not in this list is a lie (the test checks).
inline const std::vector<std::string>& KnownShortcuts() {
    static const std::vector<std::string> k = {
        key::Open, key::Save, key::SaveAs, key::SaveProject, key::Build, key::Undo, key::Redo, key::Cut, key::Copy,
        key::Paste, key::Duplicate, key::Delete, key::Group, key::Frame, key::Isolate, key::Rename, key::SelectAll,
        key::MoveUp, key::MoveDown};
    return k;
}

// ---- builders (tiny, so the menus below read like the menus) ----
inline Entry Item(std::string id, std::string label, std::string shortcut = "", std::string tip = "") {
    Entry e; e.kind = Kind::Item; e.id = std::move(id); e.label = std::move(label); e.shortcut = std::move(shortcut); e.tip = std::move(tip);
    return e;
}
inline Entry Toggle(std::string id, std::string label, std::string tip = "") {
    Entry e; e.kind = Kind::Toggle; e.id = std::move(id); e.label = std::move(label); e.tip = std::move(tip);
    return e;
}
inline Entry Sub(std::string id, std::string label, std::vector<Entry> children, std::string tip = "") {
    Entry e; e.kind = Kind::Submenu; e.id = std::move(id); e.label = std::move(label); e.children = std::move(children); e.tip = std::move(tip);
    return e;
}
inline Entry SlotMenu(std::string id, std::string label, std::string tip = "") {
    Entry e; e.kind = Kind::SlotMenu; e.id = std::move(id); e.label = std::move(label); e.tip = std::move(tip);
    return e;
}
inline Entry Slot(std::string id) {
    Entry e; e.kind = Kind::Slot; e.id = std::move(id);
    return e;
}
inline Entry Sep() { Entry e; e.kind = Kind::Separator; return e; }
inline Entry Head(std::string label) { Entry e; e.kind = Kind::Heading; e.label = std::move(label); return e; }
/// `e` greyed out, with the sentence that says why.
inline Entry Off(Entry e, std::string why) { e.enabled = false; e.reason = std::move(why); return e; }
/// `e` greyed out when `ok` is false.
inline Entry OnlyIf(Entry e, bool ok, const std::string& why) { return ok ? e : Off(std::move(e), why); }

/// "Delete" for one object, "Delete 3 Objects" for several.
inline std::string Counted(const std::string& verb, int n) { return n > 1 ? verb + " " + std::to_string(n) + " Objects" : verb; }

// ---- host-drawn menu content, written once for every toolkit ----
/// What a SlotMenu's content is drawn with. A host writes its live list (Parent To's objects, Rig & IK) once against this, and it can
/// be drawn with Dear ImGui (MenuEntriesUI.hpp ImGuiSlotUI), recorded into entries for another toolkit (SlotRecorder: the OkayUI
/// Hierarchy draws those) and replayed to run the item that was picked there (SlotReplayer). Item and BeginMenu answer like Dear
/// ImGui's MenuItem and BeginMenu.
struct SlotUI {
    virtual ~SlotUI() = default;
    virtual bool Item(const char* label, bool enabled = true) = 0;
    virtual bool BeginMenu(const char* label, bool enabled = true) = 0;
    virtual void EndMenu() = 0;
    virtual void Separator() = 0;
    virtual void Note(const char* text) = 0;   ///< a dim line that is not an item
    virtual void Tip(const char* text) = 0;    ///< the hover text of the item just drawn
};

namespace detail {
// The id of the `index`th entry of a level: "<parent id>/<index>:<label>".
inline std::string SlotStep(const std::string& parent, int index, const char* label) {
    return parent + "/" + std::to_string(index) + ":" + (label ? label : "");
}
}

/// Records a slot's content as entries. Every entry's id starts with the slot's id, so a picked item can be told apart from the
/// menu's own items and handed to a SlotReplayer.
class SlotRecorder : public SlotUI {
public:
    explicit SlotRecorder(std::string slotId) { m_levels.push_back(Level{std::move(slotId), &m_root, 0}); }
    std::vector<Entry> Take() { return std::move(m_root); }
    bool Item(const char* label, bool enabled) override {
        Level& l = m_levels.back();
        Entry e; e.kind = Kind::Item; e.id = detail::SlotStep(l.id, l.n++, label); e.label = label ? label : ""; e.enabled = enabled;
        l.out->push_back(std::move(e));
        return false;
    }
    bool BeginMenu(const char* label, bool enabled) override {
        Level& l = m_levels.back();
        Entry e; e.kind = Kind::Submenu; e.id = detail::SlotStep(l.id, l.n++, label); e.label = label ? label : ""; e.enabled = enabled;
        l.out->push_back(std::move(e));
        if (!enabled) return false;
        Entry& sub = l.out->back();
        m_levels.push_back(Level{sub.id, &sub.children, 0});
        return true;
    }
    void EndMenu() override { if (m_levels.size() > 1) m_levels.pop_back(); }
    void Separator() override { Level& l = m_levels.back(); ++l.n; l.out->push_back(Sep()); }
    void Note(const char* text) override { Level& l = m_levels.back(); ++l.n; l.out->push_back(Head(text ? text : "")); }
    void Tip(const char* text) override {
        Level& l = m_levels.back();
        if (l.out->empty() || !text) return;
        Entry& e = l.out->back();
        if (e.enabled) e.tip = text; else e.reason = text;
    }
private:
    struct Level { std::string id; std::vector<Entry>* out; int n; };
    std::vector<Entry> m_root;
    std::vector<Level> m_levels;
};

/// Walks a slot's content again so that only the item whose id is `pick` (an id a SlotRecorder gave) answers true, and only the
/// sub-menus on its way open. The slot must draw the same list it drew when it was recorded (it is replayed right after the pick).
class SlotReplayer : public SlotUI {
public:
    SlotReplayer(std::string slotId, std::string pick) : m_pick(std::move(pick)) { m_levels.push_back(Level{std::move(slotId), 0}); }
    bool Item(const char* label, bool enabled) override {
        Level& l = m_levels.back();
        const bool hit = enabled && detail::SlotStep(l.id, l.n++, label) == m_pick;
        if (hit) m_ran = true;
        return hit;
    }
    bool BeginMenu(const char* label, bool enabled) override {
        Level& l = m_levels.back();
        const std::string id = detail::SlotStep(l.id, l.n++, label);
        if (!enabled || m_pick.compare(0, id.size() + 1, id + "/") != 0) return false;
        m_levels.push_back(Level{id, 0});
        return true;
    }
    void EndMenu() override { if (m_levels.size() > 1) m_levels.pop_back(); }
    void Separator() override { ++m_levels.back().n; }
    void Note(const char*) override { ++m_levels.back().n; }
    void Tip(const char*) override {}
    /// True once the picked item was found (and its code ran).
    bool Ran() const { return m_ran; }
private:
    struct Level { std::string id; int n; };
    std::string m_pick;
    std::vector<Level> m_levels;
    bool m_ran = false;
};

// ---- why-sentences, shared by every menu that greys the same thing out ----
inline constexpr const char* kNeedSelection = "Select an object in the Hierarchy or the Scene view first.";
inline constexpr const char* kStopPlayFirst = "Stop Play first: saving now would save the running game, not your scene.";
inline constexpr const char* kUndoOffInPlay = "Undo is off while the game runs: what you change in Play is thrown away when you press Stop.";
inline constexpr const char* kRedoOffInPlay = "Redo is off while the game runs.";
inline constexpr const char* kNothingCopied = "Nothing copied yet: select an object and press Ctrl+C.";

/// What the menus need to know about the editor, as plain values the host fills in each frame the menu is open.
struct State {
    bool playing = false;           // the game is running (Play)
    bool inPrefabMode = false;
    std::string scenePath;          // "" = the scene was never saved
    int  selectedCount = 0;
    int  objectCount = 0;           // objects in the scene
    bool canUndo = false, canRedo = false;
    std::string undoName, redoName; // "Move" makes "Undo Move"; "" makes "Undo"
    bool clipboardHasObject = false;
    int  recentCount = 0;           // scenes in Open Recent
    bool assimp = false;            // Import Model reads more formats (tooltip only)
    bool isolating = false;         // Isolate mode is on
    bool darkMode = true;           // the editor is in dark mode (false = light mode)
};

// ---- GameObject > Create: the categories every "create an object" menu shares ----
/// The category submenus of every Create menu (GameObject menu, the Hierarchy [+] button and right-click menus, Create
/// Child). Each one is a SlotMenu: the host draws the items (they are the creators the editor already had, grouped).
inline std::vector<Entry> CreateCategories() {
    return {
        SlotMenu("create.2d", "2D Object", "Sprites, premade shapes, sprite-sheet characters and tilemaps."),
        SlotMenu("create.3d", "3D Object", "Primitives, nature, props, structures, terrain, water and characters."),
        SlotMenu("create.player", "Player", "A fully set-up player (character, controller and physics) dropped into the scene."),
        SlotMenu("create.vehicles", "Vehicles", "Ready-made vehicles you can drive: cars, trucks, a bus, a buggy and a boat on the water."),
        SlotMenu("create.light", "Light", "Directional, point and spot lights."),
        SlotMenu("create.camera", "Camera", "A camera, and the Cinemachine virtual cameras, brain and dolly rails."),
        SlotMenu("create.audio", "Audio", "A sound source and a music player."),
        SlotMenu("create.effects", "Effects", "Particles, splines, decals, cables, foliage and volumes."),
        SlotMenu("create.ui", "UI", "Every UI element is its own object, placed under the Canvas."),
    };
}

// ---- the top-level menus ----
struct Top { std::string id, label; bool hostDrawn = false; };   // hostDrawn: the host draws the whole menu (Plugins)

/// The menu bar, left to right.
inline const std::vector<Top>& TopMenus() {
    static const std::vector<Top> t = {
        {"file", "File", false}, {"edit", "Edit", false}, {"gameobject", "GameObject", false}, {"view", "View", false},
        {"plugins", "Plugins", true}, {"help", "Help", false}};
    return t;
}

inline std::vector<Entry> FileMenu(const State& s) {
    std::vector<Entry> m;
    m.push_back(Item("file.new2d", "New 2D Scene", "", "Start an empty 2D scene: an orthographic camera and a sprite."));
    m.push_back(Item("file.new3d", "New 3D Scene", "", "Start an empty 3D scene: a perspective camera, a ground and a cube."));
    m.push_back(Item("file.open", "Open Scene...", key::Open));
    m.push_back(OnlyIf(SlotMenu("file.recent", "Open Recent"), s.recentCount > 0, "No recent scenes yet: scenes you open or save are listed here."));
    // File > Examples was removed on the owner's request (2026-11-16). The example scenes still ship in the "examples" folder
    // beside the program: open them with File > Open Scene.
    m.push_back(Sep());
    {
        Entry save = Item("file.save", "Save Scene", key::Save,
                          s.scenePath.empty() ? "This scene hasn't been saved yet: pick where it goes (Assets/Scenes)."
                                              : "Save this scene: " + s.scenePath);
        m.push_back(OnlyIf(save, !s.playing, kStopPlayFirst));
        m.push_back(OnlyIf(Item("file.saveas", "Save Scene As...", key::SaveAs), !s.playing, kStopPlayFirst));
        m.push_back(Item("file.saveproject", "Save Project", key::SaveProject,
                         "Save everything: the scene, every edited script, the open visual\nscript, project and build settings, and editor preferences."));
    }
    m.push_back(Sep());
    m.push_back(Item("file.newproject", "New Project...", "", "Make a new project folder from a template and open it.\n(The launcher's Create page makes the same thing.)"));
    m.push_back(Item("file.openproject", "Open Project Folder..."));
    m.push_back(Slot("file.upgrade"));   // Project Upgrade... (drawn by ProjectUpgradeUI.cpp, greyed out with its own reason)
    m.push_back(Sep());
    m.push_back(Item("file.import", "Import Model...", "",
                     std::string("FBX, glTF, GLB, OBJ, STL, PLY") + (s.assimp ? ", DAE, 3DS, ..." : "")));
    m.push_back(Item("file.merge", "Merge Scene into Current...", "",
                     "Combine another scene into this one to build a seamless world.\nThe Hierarchy marks where each merged scene begins.\n"
                     "Tip: you can also drag a .okayscene from Project onto the Hierarchy."));
    m.push_back(Sep());
    m.push_back(SlotMenu("file.autosave", "Autosave"));
    m.push_back(Item("file.build", "Build Settings...", key::Build));
    m.push_back(Sep());
    m.push_back(Item("file.exit", "Exit"));
    return m;
}

inline std::vector<Entry> EditMenu(const State& s) {
    std::vector<Entry> m;
    {   // The label says what Ctrl+Z would do; both are greyed out when there is nothing to do.
        Entry undo = Item("edit.undo", s.undoName.empty() ? "Undo" : "Undo " + s.undoName, key::Undo);
        Entry redo = Item("edit.redo", s.redoName.empty() ? "Redo" : "Redo " + s.redoName, key::Redo);
        if (!s.playing) undo.tip = "Scene edits. The Project panel, Timeline, string table, items / loot / recipes and the code editors have their own Undo while they have focus.";
        m.push_back(OnlyIf(undo, s.canUndo, s.playing ? kUndoOffInPlay : "Nothing to undo yet."));
        m.push_back(OnlyIf(redo, s.canRedo, s.playing ? kRedoOffInPlay : "Nothing to redo."));
    }
    m.push_back(Sep());
    const bool sel = s.selectedCount > 0;
    {
        Entry cut = Item("edit.cut", "Cut", key::Cut);
        if (s.selectedCount > 1)   // Ctrl+X keeps only the active object on the clipboard but deletes every selected one
            m.push_back(Off(cut, "Cut works on one object: select just one (cutting several would delete them all and keep only one)."));
        else
            m.push_back(OnlyIf(cut, sel, kNeedSelection));
    }
    {
        Entry copy = Item("edit.copy", "Copy", key::Copy);
        if (s.selectedCount > 1) copy.tip = "Copies the active object (the one last clicked).";
        m.push_back(OnlyIf(copy, sel, kNeedSelection));
    }
    m.push_back(OnlyIf(Item("edit.paste", "Paste", key::Paste), s.clipboardHasObject, kNothingCopied));
    m.push_back(OnlyIf(Item("edit.duplicate", Counted("Duplicate", s.selectedCount), key::Duplicate), sel, kNeedSelection));
    m.push_back(OnlyIf(Item("edit.delete", Counted("Delete", s.selectedCount), key::Delete), sel, kNeedSelection));
    m.push_back(Sep());
    m.push_back(OnlyIf(Item("edit.selectall", "Select All", "", "Selects every object in the scene. Ctrl+A does the same while the Hierarchy has focus."),
                       s.objectCount > 0, "The scene has no objects."));
    m.push_back(OnlyIf(Item("edit.deselect", "Deselect"), sel, "Nothing is selected."));
    m.push_back(Sep());
    m.push_back(OnlyIf(Item("edit.darkmode", "Dark Mode", "", "Dark editor colours."), !s.darkMode, "The editor is already in dark mode."));
    m.push_back(OnlyIf(Item("edit.lightmode", "Light Mode", "", "Light editor colours."), s.darkMode, "The editor is already in light mode."));
    m.push_back(Item("edit.projectsettings", "Project Settings..."));
    return m;
}

/// Old okay_settings.txt files saved one of three themes (0 Slate, 1 Light, 2 Classic). Only light is a light look; Slate and
/// Classic are dark, and anything unknown falls back to dark.
inline bool LegacyThemeIsDark(int oldTheme) { return oldTheme != 1; }

inline std::vector<Entry> GameObjectMenu(const State& s) {
    std::vector<Entry> m;
    const bool sel = s.selectedCount > 0;
    m.push_back(Item("go.empty", "Create Empty"));
    m.push_back(OnlyIf(Item("go.emptychild", "Create Empty Child", "", "A new empty object inside the selected one."), sel, kNeedSelection));
    {
        Entry parent = Item("go.emptyparent", s.selectedCount > 1 ? "Create Empty Parent for " + std::to_string(s.selectedCount) + " Objects" : "Create Empty Parent",
                            key::Group, "Wraps the selected object(s) in a new empty parent. Each keeps its place in the scene.");
        m.push_back(OnlyIf(parent, sel, kNeedSelection));
    }
    m.push_back(Sep());
    for (Entry& c : CreateCategories()) m.push_back(std::move(c));
    m.push_back(Sep());
    m.push_back(Item("go.instantiate", "Instantiate Prefab...", "", "Put a copy of a prefab from the project into the scene."));
    m.push_back(Sep());
    m.push_back(OnlyIf(Item("go.arraydup", "Array Duplicate...", "", "Make a row, grid or ring of copies of the selected object."), sel, kNeedSelection));
    m.push_back(OnlyIf(Item("go.snap", "Snap Position to Grid", "", "Round the selected object's position to the snap size."), sel, kNeedSelection));
    return m;
}

/// One panel or tool window the menu can show or hide (the host maps `id` to the live flag).
inline std::vector<Entry> PanelsSubmenu() {
    std::vector<Entry> p;
    p.push_back(Toggle("panel.hierarchy", "Hierarchy"));
    p.push_back(Toggle("panel.inspector", "Inspector"));
    p.push_back(Toggle("panel.project", "Project"));
    p.push_back(Toggle("panel.console", "Console"));
    p.push_back(Toggle("panel.problems", "Problems"));
    p.push_back(Toggle("panel.game", "Game"));
    p.push_back(Sep());
    p.push_back(Toggle("panel.services", "Services"));
    p.push_back(Toggle("panel.scenemanager", "Scene Manager"));
    p.push_back(Toggle("panel.net", "Net"));
    p.push_back(Toggle("panel.savemanager", "Save Manager"));
    p.push_back(Sep());
    p.push_back(Toggle("panel.history", "History"));
    p.push_back(Toggle("panel.stats", "Stats"));
    p.push_back(Toggle("panel.lighting", "Lighting"));
    p.push_back(Toggle("panel.profiler", "Profiler"));
    p.push_back(Toggle("panel.assetmanager", "Asset Manager"));
    p.push_back(Toggle("panel.crashlog", "Crash Log", "View past crash reports (okay_crashlog.txt) and copy them to send to support."));
    p.push_back(Toggle("panel.audiomixer", "Audio Mixer", "Master and group volumes (Music, SFX, UI, Voice), output levels while playing, and music ducking."));
    p.push_back(Toggle("panel.input", "Input", "Live keys, mouse, controllers and actions while the game plays, the last events, input recording and replay, and each action's response curve."));
    return p;
}

inline std::vector<Entry> ToolsSubmenu() {
    std::vector<Entry> t;
    t.push_back(Toggle("tool.visualscript", "Visual Script"));
    t.push_back(Toggle("tool.scripteditor", "Script Editor"));
    t.push_back(Sep());
    t.push_back(Toggle("tool.animation", "Animation"));
    t.push_back(Toggle("tool.animator", "Animator (state graph)"));
    t.push_back(Toggle("tool.timeline", "Timeline (cutscenes)", "Author cutscene sequences: camera shots, object keys, animation and audio cues, events and subtitles on one timeline."));
    t.push_back(Toggle("tool.navigation", "Navigation", "Bake the scene's colliders into a navigation mesh NPCs and Nav Mesh Agents path over, and see it in the Scene view."));
    t.push_back(Toggle("tool.worldpartition", "World Partition", "Split a big scene into a grid of streaming cells (the scene is kept); the new scene streams them in and out around the player."));
    t.push_back(Toggle("tool.dialogue", "Dialogue & Quests"));
    t.push_back(Toggle("tool.behaviortree", "Behavior Tree", "Author NPC AI as a behavior tree (.okaybt): patrol, perception, chase, attack, scripts; watch it run in Play."));
    t.push_back(Sep());
    t.push_back(Toggle("tool.modeling", "Modeling"));
    t.push_back(Toggle("tool.uveditor", "UV Editor"));
    t.push_back(Toggle("tool.spriteeditor", "Sprite Editor", "Paint custom 2D sprites pixel-by-pixel, start from a shape, and save them into the project's Assets/Sprites."));
    t.push_back(Sep());
    t.push_back(Toggle("tool.uieditor", "UI Editor (separate tab)", "Open a dedicated, dockable UI editing tab (flat canvas) beside the Scene view,\nso you can keep the 3D Scene and the UI layout visible at the same time."));
    t.push_back(Toggle("tool.uigallery", "UI Gallery", "Search every UI component, style, look, icon and ready-made screen, preview it and add it with one click."));
    t.push_back(Toggle("tool.uitheme", "UI Theme", "Define a reusable UI style (colors, corners, borders) and apply it to every widget at once: pick a preset or make your own, then restyle the whole UI in one click."));
    t.push_back(Toggle("tool.items", "Items, Loot & Recipes", "Define items, loot tables and crafting recipes. Saved as items.okayitems, .okayloot and .okayrecipes."));
    t.push_back(Toggle("tool.weapons", "Weapons", "Define swords and guns once (damage, swing or fire rate, range, rounds and reload, hit window, arc or hit box, combo and knockback). Melee and guns use these."));
    t.push_back(Toggle("tool.inputactions", "Input Actions", "Author an input asset (.okayinput): control schemes, action maps, typed actions (button, axis, 2D and 3D vector), bindings with processors, and interactions (hold, tap, multi-tap, repeat), with a check list and a live view."));
    t.push_back(Toggle("tool.localization", "Localization", "The project's string table: every text key in every language, how complete each language is, CSV for translators and a pseudo-localized preview."));
    return t;
}

/// View: the windows (Panels, Editors & Tools), the layouts that arrange them, then the Scene view's own commands and settings.
/// (The old Window menu held one item that Editors & Tools already had, so it is gone; "View > Editors & Tools > X" stays true in every doc.)
inline std::vector<Entry> ViewMenu(const State& s) {
    std::vector<Entry> m;
    m.push_back(Sub("view.panels", "Panels", PanelsSubmenu()));
    m.push_back(Sub("view.tools", "Editors & Tools", ToolsSubmenu()));
    m.push_back(SlotMenu("view.layout", "Layout", "Panel arrangements: built-in and saved layouts, side tabs, maximize the Scene view."));
    m.push_back(Sep());
    m.push_back(OnlyIf(Item("view.frame", "Frame Selected", key::Frame, "Centre the Scene view on the selection."), s.selectedCount > 0, kNeedSelection));
    m.push_back(OnlyIf(Item("view.isolate", s.isolating ? "Exit Isolation" : "Isolate Selection", key::Isolate,
                            "Dim everything except the selected object and its children. Esc leaves isolation."),
                       s.isolating || s.selectedCount > 0, kNeedSelection));
    m.push_back(Toggle("view.uionly", "UI Editing Mode", "Lock THIS Scene view to a flat UI canvas with the 3D scene hidden.\nAlso available as the 'UI Only' button on the Scene toolbar."));
    m.push_back(Sep());
    m.push_back(SlotMenu("view.overlays", "Scene Overlays"));
    m.push_back(SlotMenu("view.bookmarks", "Camera Bookmarks"));
    m.push_back(Sep());
    m.push_back(SlotMenu("view.rendering", "Rendering & Performance"));
    return m;
}

inline std::vector<Entry> HelpMenu(const State&) {
    std::vector<Entry> m;
    m.push_back(Item("help.scripting", "Scripting Reference"));
    m.push_back(Item("help.shortcuts", "Keyboard Shortcuts"));
    m.push_back(Sep());
    m.push_back(Item("help.updates", "Check for Updates..."));
    m.push_back(Item("help.bug", "Report a Bug...", "", "Open the Crash Log, where saved reports can be copied. The version line is copied first, so you can paste it into the report."));
    m.push_back(Sep());
    m.push_back(Item("help.about", "About OkaySpace"));
    return m;
}

/// The items of one top-level menu ("file", "edit", ...). Empty for an unknown id and for a hostDrawn menu.
inline std::vector<Entry> BuildMenu(const std::string& topId, const State& s) {
    if (topId == "file") return FileMenu(s);
    if (topId == "edit") return EditMenu(s);
    if (topId == "gameobject") return GameObjectMenu(s);
    if (topId == "view") return ViewMenu(s);
    if (topId == "help") return HelpMenu(s);
    return {};
}

// ---- looking inside a menu (the tests, and the "where did it go" audit) ----
/// Calls fn(entry, depth) for every entry, parents before their children.
template <class Fn>
void Walk(const std::vector<Entry>& es, Fn&& fn, int depth = 0) {
    for (const Entry& e : es) {
        fn(e, depth);
        if (!e.children.empty()) Walk(e.children, fn, depth + 1);
    }
}

/// The first entry with this id, or null.
inline const Entry* FindId(const std::vector<Entry>& es, const std::string& id) {
    for (const Entry& e : es) {
        if (e.id == id && e.kind != Kind::Separator) return &e;
        if (const Entry* c = FindId(e.children, id)) return c;
    }
    return nullptr;
}

/// How many entries carry this label (separators and headings never count).
inline int CountLabel(const std::vector<Entry>& es, const std::string& label) {
    int n = 0;
    Walk(es, [&](const Entry& e, int) { if (e.kind != Kind::Separator && e.kind != Kind::Heading && e.label == label) ++n; });
    return n;
}

}  // namespace menubar
}  // namespace oked
