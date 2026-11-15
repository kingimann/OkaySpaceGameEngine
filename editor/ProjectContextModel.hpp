#pragma once
// The Project panel's right-click menus as plain data, with no window code in it (so the rules can be tested without
// ImGui). A menu is a list of Items: label, shortcut, enabled, the reason it is disabled, a separator, or a submenu
// with its own items. The panel (editor/main.cpp, DrawProject) fills a State from what is selected, asks for the list,
// draws it (oked::projectpanel::DrawContextMenu) and handles the id of the item that was chosen.
//
//   ItemMenu          right-click an asset or a folder in the grid or the list (one item or a selection)
//   FolderTreeMenu    right-click a folder in the tree on the left (same rules, one folder, Assets root included)
//   FavouriteMenu     right-click a starred item in the FAVOURITES list
//   BackgroundMenu    right-click the empty space of the folder being shown
//   CreateItems       the Create submenu (also the toolbar "+"), grouped
//
// Rules the lists follow (all of them are checked in tests/test_project_context_model.cpp):
//   * An item that never applies to what you clicked is left out (Find References on a folder). An item that applies but
//     cannot run now stays in, greyed, with the reason as its tooltip ("Select one item to rename it.").
//   * With several items selected the labels say how many ("Delete 3 Items") and items that only make sense for one
//     item (Rename, Open, Properties, Find References) are greyed or left out.
//   * No separator is ever first, last or doubled.
//
// Also here, as pure functions with tests: text for Copy Name / Relative Path / Full Path (several items: one per
// line), the Assets-relative path, a check that a path is safe to hand to another program, the "can this move there"
// rule of the Move to... picker, "Set as Start Scene" for the Scenes In Build list, and the rows of the Properties window.
// Paths are checked with okayproj::Normalized / IsSameOrInside (shared/ProjectDelete.hpp), the same test the launcher uses.
//
// Written from this repository's own panel behaviour and the owner's request; no outside code.
#include "../shared/ProjectDelete.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace okaypb {
namespace ctx {

// ---- the item list -----------------------------------------------------------------------------

struct Item {
    std::string id;                 ///< what the panel acts on: "open", "rename", "create:folder", "label:3", "copy:rel" ...
    std::string label;
    std::string shortcut;           ///< "Ctrl+D", "" for none
    bool enabled = true;
    std::string reason;             ///< why it is disabled (the tooltip of a greyed item)
    std::string tip;                ///< what it does (the tooltip of an enabled item), optional
    bool separator = false;
    bool heading = false;           ///< a grey line that cannot be clicked ("3 items selected", a group title)
    bool checked = false;           ///< a tick (the view and sort choices)
    std::string submenu;            ///< non-empty: this item opens a submenu ("create", "copy", "label", "tags", "view", "sort"); its entries are `children`
    std::vector<Item> children;
};

enum class Kind { Folder, Scene, Prefab, Script, VisualScript, Model, Image, Audio, Material, Data, Other };
enum class Where { Item, Tree, Favourite, Background };

/// What the menus need to know. The panel fills this; nothing here reads the disk.
struct State {
    Where where = Where::Item;
    int count = 1;                  ///< items the menu acts on (the selection, or 1)
    Kind kind = Kind::Other;        ///< the item that was right-clicked (for Background: Folder, the folder being shown)
    bool favourite = false;         ///< the clicked item is starred
    bool atRoot = false;            ///< the clicked item (or the folder being shown) is the Assets folder itself
    bool readOnly = false;          ///< no project is open: nothing can be changed
    bool smartView = false;         ///< a smart filter is showing (the list is not one folder)
    bool hasClipboard = false;      ///< something was copied or cut
    int clipboardCount = 0;
    bool canUndo = false;
    std::string undoLabel;          ///< "Rename Hero.png"
    bool hierarchySelection = false;///< an object is selected in the Hierarchy (Prefab from Selection, Place as Child)
    bool isStartScene = false;      ///< the clicked scene already starts the game (first in Scenes In Build)
    bool shellSafe = true;          ///< the path can be handed to another program (see IsShellSafe)
    bool hasSubfolders = false;     ///< the clicked folder has folders in it
    bool canOpen = true;            ///< an editor window opens this type (false for pictures, audio, fonts, ...: Open is greyed and points to Open with Default App)
    bool indexed = true;            ///< the asset database knows the item (References and Properties need it)
    int shownCount = 0;             ///< items listed in the folder (Select All)
    int view = 0;                   ///< 0 grid, 1 list
    int sort = 0;                   ///< 0 Name, 1 Type, 2 Size, 3 Date modified
    int tilePreset = 0;             ///< 1 Small, 2 Medium, 3 Large when the tile size is exactly one of them, else 0
    std::string fileManager = "File Manager";   ///< "Explorer" on Windows
    std::vector<std::string> tags;  ///< tags of the clicked asset (Tags > Remove)
    std::vector<std::string> labelNames;   ///< colour labels, index 0 = none
    int label = 0;                  ///< the clicked item's colour label (index into labelNames)
};

namespace detail {

/// Why nothing can be created, pasted or imported "here" while a smart filter is showing.
inline const char* const kSmartViewReason = "Leave the smart view first (the x next to the path at the top), then you can do this in a folder.";

inline Item Sep() { Item i; i.separator = true; return i; }
inline Item Head(std::string text) { Item i; i.heading = true; i.label = std::move(text); i.enabled = false; return i; }
inline Item Cmd(std::string id, std::string label, std::string shortcut = {}, bool enabled = true, std::string reason = {}, std::string tip = {}) {
    Item i;
    i.id = std::move(id); i.label = std::move(label); i.shortcut = std::move(shortcut);
    i.enabled = enabled; i.reason = enabled ? std::string() : std::move(reason); i.tip = std::move(tip);
    return i;
}
inline Item Sub(std::string submenu, std::string label, std::vector<Item> kids, bool enabled = true, std::string reason = {}) {
    Item i;
    i.id = submenu; i.submenu = std::move(submenu); i.label = std::move(label); i.children = std::move(kids);
    i.enabled = enabled; i.reason = enabled ? std::string() : std::move(reason);
    return i;
}
inline std::string Plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

/// "Delete" for one, "Delete 3 Items" for several.
inline std::string Counted(const std::string& verb, int n) { return n > 1 ? verb + " " + std::to_string(n) + " Items" : verb; }

/// Drop separators that would be first, last or doubled, and submenus with nothing in them.
inline void Tidy(std::vector<Item>& v) {
    for (Item& it : v) Tidy(it.children);
    std::vector<Item> out;
    for (Item& it : v) {
        if (!it.submenu.empty() && it.children.empty()) continue;
        if (it.separator && (out.empty() || out.back().separator)) continue;
        out.push_back(std::move(it));
    }
    while (!out.empty() && out.back().separator) out.pop_back();
    v = std::move(out);
}

/// Kinds that are plain text on disk, so an outside editor makes sense for them.
inline bool IsTextKind(Kind k) {
    return k == Kind::Script || k == Kind::VisualScript || k == Kind::Material || k == Kind::Data || k == Kind::Scene || k == Kind::Prefab;
}
} // namespace detail

// ---- the Create submenu ------------------------------------------------------------------------

inline std::vector<Item> CreateItems(const State& st) {
    using namespace detail;
    const bool ok = !st.readOnly && !st.smartView;
    const std::string no = st.readOnly ? "Open a project first." : kSmartViewReason;
    std::vector<Item> v;
    v.push_back(Cmd("create:folder", "Folder", "", ok, no));
    v.push_back(Sep());
    v.push_back(Head("Code"));
    v.push_back(Cmd("create:script", "Script", "", ok, no, "A script you attach to objects."));
    v.push_back(Cmd("create:visualscript", "Visual Script", "", ok, no, "A graph of nodes. It opens in the Visual Script editor."));
    v.push_back(Sep());
    v.push_back(Head("Scenes and objects"));
    v.push_back(Cmd("create:scene", "Scene", "", ok, no, "An empty level with a camera."));
    v.push_back(Cmd("create:prefab", st.hierarchySelection ? "Prefab from Selection" : "Prefab", "", ok, no,
                    st.hierarchySelection ? "Saves the object selected in the Hierarchy, with its children." : "An empty object you can fill in later."));
    v.push_back(Sep());
    v.push_back(Head("Story and AI"));
    v.push_back(Cmd("create:dialogue", "Dialogue", "", ok, no, "A conversation graph with one starting line. It opens in the Dialogue window."));
    v.push_back(Cmd("create:quest", "Quest File", "", ok, no, "A quest list with one example quest. It opens in the Dialogue window (Quests tab)."));
    v.push_back(Cmd("create:behaviortree", "Behavior Tree", "", ok, no, "A tree with a Selector at the top. It opens in the Behavior Tree window."));
    v.push_back(Cmd("create:sequence", "Cutscene (Timeline)", "", ok, no, "An empty cutscene sequence. It opens in the Timeline window."));
    v.push_back(Sep());
    v.push_back(Head("Looks and data"));
    v.push_back(Cmd("create:material", "Material", "", ok, no));
    v.push_back(Cmd("create:data", "Data Asset", "", ok, no, "Fields as key = value."));
    v.push_back(Cmd("create:item", "Item", "", ok, no));
    v.push_back(Cmd("create:loot", "Loot Table", "", ok, no));
    v.push_back(Cmd("create:recipe", "Recipe Book", "", ok, no));
    v.push_back(Sep());
    v.push_back(Cmd("create:stdfolders", "Standard Folders", "", ok, no, "Create Scenes, Scripts, Prefabs, Materials, Models, Textures and Audio folders here."));
    return v;
}

// ---- shared submenus -----------------------------------------------------------------------------

/// Copy Text: name, path as scenes write it, full path. Several items: one per line.
inline Item CopyTextMenu(const State& st) {
    using namespace detail;
    const bool many = st.count > 1;
    std::vector<Item> k;
    k.push_back(Cmd("copy:name", many ? "Names (one per line)" : (st.where == Where::Background ? "Folder Name" : "Name")));
    k.push_back(Cmd("copy:rel", many ? "Relative Paths (one per line)" : "Relative Path", "", true, "",
                    "Starts with Assets/, the way scenes and scripts write a path."));
    k.push_back(Cmd("copy:full", many ? "Full Paths (one per line)" : "Full Path"));
    return Sub("copy", "Copy Text", std::move(k));
}

inline Item ViewMenu(const State& st) {
    using namespace detail;
    std::vector<Item> k;
    Item grid = Cmd("view:grid", "Grid"); grid.checked = st.view == 0; k.push_back(grid);
    Item list = Cmd("view:list", "List"); list.checked = st.view == 1; k.push_back(list);
    k.push_back(Sep());
    const char* names[] = {"Small Tiles", "Medium Tiles", "Large Tiles"};
    for (int i = 0; i < 3; ++i) {
        Item t = Cmd(std::string("tiles:") + std::to_string(i + 1), names[i], "", st.view == 0, "Tile size only applies to the grid.");
        t.checked = st.view == 0 && st.tilePreset == i + 1;
        k.push_back(t);
    }
    return Sub("view", "View", std::move(k));
}

inline Item SortMenu(const State& st) {
    using namespace detail;
    const char* names[] = {"Name", "Type", "Size", "Date Modified"};
    std::vector<Item> k;
    for (int i = 0; i < 4; ++i) {
        Item s = Cmd(std::string("sort:") + std::to_string(i), names[i]);
        s.checked = st.sort == i;
        k.push_back(s);
    }
    return Sub("sort", "Sort By", std::move(k));
}

// ---- the menus -----------------------------------------------------------------------------------

/// Right-click an asset or a folder (grid, list or tree).
inline std::vector<Item> ItemMenu(const State& s) {
    using namespace detail;
    const bool tree = s.where == Where::Tree;
    const bool one = s.count == 1, many = s.count > 1;
    const bool isDir = s.kind == Kind::Folder;
    const bool file = !isDir;
    const bool ro = s.readOnly;
    const std::string noProject = "Open a project first.";
    const std::string rootMsg = "The Assets folder itself can't be changed.";
    // why a change is refused (one reason, in the order a person would hit them)
    auto changeBlock = [&](bool needOne = false) -> std::string {
        if (ro) return noProject;
        if (s.atRoot) return rootMsg;
        if (needOne && many) return "Select one item first.";
        return {};
    };
    std::vector<Item> v;

    if (many && !tree) v.push_back(Head(Plural(s.count, "item", "items") + " selected"));

    // ---- Open ----
    {
        const char* openLabel = isDir ? "Open Folder" : (s.kind == Kind::Prefab ? "Open Prefab" : "Open");
        v.push_back(Cmd("open", openLabel, tree ? "" : "Enter", !many && s.canOpen,
                        many ? "Select one item to open it." : "No editor window opens this type. Use Open with Default App.",
                        s.kind == Kind::Prefab ? "Edit this prefab on its own: the Scene view shows only the prefab, and Save updates every instance." : ""));
        if (one && s.kind == Kind::Prefab) {
            v.push_back(Cmd("prefab:place", "Place in Scene", "", true, "", "Adds a copy of this prefab to the open scene."));
            v.push_back(Cmd("prefab:child", "Place as Child of Selected", "", s.hierarchySelection, "Select an object in the Hierarchy first.",
                            "Adds a copy of this prefab under the object selected in the Hierarchy."));
            v.push_back(Cmd("prefab:variant", "Create Variant", "", !ro, noProject,
                            "Make a variant of this prefab next to it. Place it in a scene and change it; the variant keeps only the differences."));
            v.push_back(Cmd("prefab:validate", "Validate Prefab", "", true, "",
                            "Check this prefab and every prefab it holds or is based on: missing files, cycles, stale overrides. The result is in the Console."));
        }
        if (one && s.kind == Kind::Scene) {
            v.push_back(Cmd("scene:add", "Add to Open Scene", "", true, "", "Load its objects into the scene you have open (additive), instead of switching scenes."));
            v.push_back(Cmd("scene:start", "Set as Start Scene", "", !s.isStartScene && !ro,
                            ro ? noProject : "This scene already starts the game.",
                            "Puts this scene first in Scenes In Build, ticked, so a built game starts here."));
        }
        if (one && s.kind == Kind::Model)
            v.push_back(Cmd("model:player", "Make Playable Character", "", true, "",
                            "Add a Third Person player wearing this model, with humanoid IK.\nIf one player is selected, the model is attached to it.\nOtherwise other active players are deactivated (not deleted)."));
        if (one && s.kind == Kind::Script) v.push_back(Cmd("script:edit", "Edit Script", "", true, "", "Open it in the Script Editor."));
        if (one && file && IsTextKind(s.kind))
            v.push_back(Cmd("open:external", "Open in External Editor", "", s.shellSafe,
                            "The path has a character (a quote, $, % or `) that can't be passed safely to another program. Rename the file or its folders.",
                            "Opens it in Visual Studio Code when that is installed, otherwise in the program your system uses for this file type."));
        if (one && file)
            v.push_back(Cmd("open:default", "Open with Default App", "", s.shellSafe,
                            "The path has a character (a quote, $, % or `) that can't be passed safely to another program. Rename the file or its folders.",
                            "Opens it in the program your system uses for this file type."));
    }
    // ---- Create (inside this folder) ----
    if (one && isDir) {
        v.push_back(Sep());
        v.push_back(Sub("create", "Create", CreateItems(s), !ro, noProject));
    }
    v.push_back(Sep());
    // ---- Edit ----
    v.push_back(Cmd("rename", "Rename", tree ? "" : "F2", !many && changeBlock().empty(), many ? "Select one item to rename it." : changeBlock(),
                    "Keeps the file type. Everything that used the old name is updated."));
    v.push_back(Cmd("duplicate", Counted("Duplicate", s.count), tree ? "" : "Ctrl+D", changeBlock().empty(), changeBlock()));
    v.push_back(Cmd("cut", Counted("Cut", s.count), tree ? "" : "Ctrl+X", changeBlock().empty(), changeBlock()));
    v.push_back(Cmd("copy", Counted("Copy", s.count), tree ? "" : "Ctrl+C", !s.atRoot, rootMsg));
    {
        std::string base = isDir ? "Paste Into" : "Paste";
        if (s.clipboardCount > 1) base = "Paste " + std::to_string(s.clipboardCount) + " Items" + (isDir ? " Into" : "");
        v.push_back(Cmd("paste", base, tree ? "" : "Ctrl+V", s.hasClipboard && !ro, ro ? noProject : "Nothing was copied or cut yet.",
                        isDir ? "" : "Pastes into the folder being shown."));
    }
    v.push_back(Cmd("moveto", "Move to...", "", changeBlock().empty(), changeBlock(), "Pick the folder to move it to. A name that is taken gets a number, and links to it are updated."));
    v.push_back(Cmd("delete", Counted("Delete", s.count), tree ? "" : "Del", changeBlock().empty(), changeBlock(),
                    "Asks first and lists what uses it. Deleted items go to the project's trash; Ctrl+Z in the Project panel brings them back."));
    // ---- Find, Reimport ----
    v.push_back(Sep());
    if (one && file) {
        v.push_back(Cmd("refs", "Find References", "", s.indexed, "The asset database does not know this file yet.", "List the assets that use this one (from the asset database)."));
        v.push_back(Cmd("deps", "Show Dependencies", "", s.indexed, "The asset database does not know this file yet.", "List what this asset uses, directly or through other assets."));
        v.push_back(Cmd("refs:scene", "Find References in Scene", "", true, "", "Select the objects in the open scene that use this file (matched by file name)."));
    }
    if (one && isDir)
        v.push_back(Cmd("reimport", "Reimport All in Folder", "", true, "", "Read every asset in this folder (and the folders in it) again, e.g. after changing them in another program."));
    else
        v.push_back(Cmd("reimport", Counted("Reimport", s.count), "", true, "", "Read the file again (use after you changed it in another program). Folders are read again with everything in them."));
    v.push_back(Sep());
    // ---- Organise ----
    {
        const std::string favBlock = ro ? noProject : (s.atRoot ? "The Assets folder is always one click away." : "");
        v.push_back(Cmd("favourite", s.favourite ? (many ? "Remove " + std::to_string(s.count) + " Items from Favourites" : std::string("Remove from Favourites"))
                                                 : (many ? "Add " + std::to_string(s.count) + " Items to Favourites" : std::string("Add to Favourites")),
                        "", favBlock.empty(), favBlock));
        if (one && file) {
            std::vector<Item> k;
            k.push_back(Cmd("tag:add", "Add Tag...", "", !ro, noProject, "Search for it with l:tag. It is saved in the asset's .okaymeta."));
            for (const std::string& t : s.tags) k.push_back(Cmd("tag:remove:" + t, "Remove \"" + t + "\"", "", !ro, noProject));
            v.push_back(Sub("tags", "Tags", std::move(k)));
        }
        if (!s.labelNames.empty() && !s.atRoot) {
            std::vector<Item> k;
            for (std::size_t i = 0; i < s.labelNames.size(); ++i) {
                Item l = Cmd("label:" + std::to_string(i), s.labelNames[i], "", !ro, noProject);
                l.checked = static_cast<int>(i) == s.label;
                k.push_back(l);
            }
            v.push_back(Sub("label", "Label", std::move(k)));
        }
        v.push_back(CopyTextMenu(s));
    }
    // ---- Tree and folder view ----
    if (one && isDir) {
        v.push_back(Sep());
        v.push_back(Cmd("tree:expand", "Expand All Subfolders", "", s.hasSubfolders, "This folder has no folders in it.", "Opens this folder and every folder inside it in the tree on the left."));
        v.push_back(Cmd("tree:collapse", "Collapse All Subfolders", "", s.hasSubfolders, "This folder has no folders in it.", "Closes every folder inside this one in the tree on the left."));
        v.push_back(Cmd("tree:others", "Collapse Other Folders", "", true, "", "Closes every folder in the tree except this one and the folders it is in."));
    }
    v.push_back(Sep());
    // ---- Show ----
    v.push_back(Cmd("reveal", "Show in " + s.fileManager, "", true, "",
                    file ? "Shows the file in a window of your file manager (on Windows it is selected there)." : "Opens the folder in your file manager."));
    if (one) v.push_back(Cmd("properties", "Properties...", "", true, "", "Type, size, date, path, GUID and how many assets use it."));
    Tidy(v);
    return v;
}

/// Right-click a folder in the tree: the same rules, for one folder (the tree has no shortcuts and no selection).
inline std::vector<Item> FolderTreeMenu(State s) {
    s.where = Where::Tree;
    s.count = 1; s.kind = Kind::Folder;
    return ItemMenu(s);
}

/// Right-click a starred item in FAVOURITES.
inline std::vector<Item> FavouriteMenu(const State& s) {
    using namespace detail;
    const bool isDir = s.kind == Kind::Folder;
    std::vector<Item> v;
    v.push_back(Cmd("show", isDir ? "Open Folder" : "Show in Project", "", true, "", isDir ? "" : "Goes to the file's folder and selects it."));
    v.push_back(Sep());
    v.push_back(Cmd("favourite", "Remove from Favourites", "", !s.readOnly, "Open a project first."));
    State c = s; c.count = 1;
    v.push_back(CopyTextMenu(c));
    v.push_back(Sep());
    v.push_back(Cmd("reveal", "Show in " + s.fileManager, "", true, "", isDir ? "Opens the folder in your file manager." : "Shows the file in a window of your file manager."));
    v.push_back(Cmd("properties", "Properties...", "", true, "", "Type, size, date, path, GUID and how many assets use it."));
    Tidy(v);
    return v;
}

/// Right-click the empty space of the folder being shown. `kind` is Folder; `atRoot` says it is Assets.
inline std::vector<Item> BackgroundMenu(const State& s) {
    using namespace detail;
    const std::string noProject = "Open a project first.";
    const std::string smart = kSmartViewReason;
    const bool here = !s.readOnly && !s.smartView;
    const std::string hereWhy = s.readOnly ? noProject : smart;
    std::vector<Item> v;
    v.push_back(Sub("create", "Create", CreateItems(s), here, hereWhy));
    {
        const std::string label = s.clipboardCount > 1 ? "Paste " + std::to_string(s.clipboardCount) + " Items" : std::string("Paste");
        v.push_back(Cmd("paste", label, "Ctrl+V", here && s.hasClipboard, !here ? hereWhy : "Nothing was copied or cut yet."));
    }
    {
        const std::string label = s.canUndo && !s.undoLabel.empty() ? "Undo " + s.undoLabel : std::string("Undo");
        v.push_back(Cmd("undo", label, "Ctrl+Z", s.canUndo, "Nothing to undo in the Project panel.",
                        "Puts back the last create, rename, move, paste, duplicate or delete."));
    }
    v.push_back(Sep());
    v.push_back(Cmd("import", "Import New Asset...", "", here, hereWhy, "Copy files from your computer into this folder. You can also drag them onto the editor window."));
    v.push_back(Cmd("refresh", "Refresh", "F5", !s.readOnly, noProject, "Look at the disk again now. The panel also does this by itself every few seconds."));
    v.push_back(Cmd("selectall", "Select All", "Ctrl+A", s.shownCount > 0, "There is nothing here to select."));
    v.push_back(Sep());
    v.push_back(ViewMenu(s));
    v.push_back(SortMenu(s));
    v.push_back(Sep());
    v.push_back(Cmd("up", "Up One Folder", "Backspace", !s.atRoot && !s.smartView, s.smartView ? smart : "This is the Assets folder."));
    {
        const std::string why = s.readOnly ? noProject : (s.atRoot ? "The Assets folder is always one click away." : "");
        v.push_back(Cmd("favourite", s.favourite ? "Remove Folder from Favourites" : "Add Folder to Favourites", "", why.empty() && !s.smartView, s.smartView ? smart : why));
    }
    v.push_back(CopyTextMenu(s));
    v.push_back(Cmd("reveal", "Show in " + s.fileManager, "", !s.readOnly, noProject, "Opens this folder in your file manager."));
    v.push_back(Cmd("open:project", "Open Project Folder in " + s.fileManager, "", !s.readOnly, noProject, "Opens the folder that holds Assets and project.okayproj."));
    v.push_back(Cmd("properties", "Properties...", "", !s.readOnly && !s.smartView, s.readOnly ? noProject : smart, "How many files and folders this folder holds, and how big it is."));
    Tidy(v);
    return v;
}

// ---- looking up an item (the panel's dispatcher and the tests) -------------------------------------

/// The item with this id (a submenu's own item too), looking inside submenus; null when there is none.
inline const Item* Find(const std::vector<Item>& v, const std::string& id) {
    for (const Item& it : v) {
        if (it.separator || it.heading) continue;
        if (it.id == id) return &it;
        if (const Item* c = Find(it.children, id)) return c;
    }
    return nullptr;
}
/// Every clickable id in order (separators, headings and the submenu items themselves left out).
inline void Collect(const std::vector<Item>& v, std::vector<std::string>& out) {
    for (const Item& it : v) {
        if (it.separator || it.heading) continue;
        if (!it.submenu.empty()) { Collect(it.children, out); continue; }
        out.push_back(it.id);
    }
}
inline std::vector<std::string> Ids(const std::vector<Item>& v) { std::vector<std::string> o; Collect(v, o); return o; }

// ---- paths and text --------------------------------------------------------------------------------

/// One per line, no trailing newline.
inline std::string JoinLines(const std::vector<std::string>& lines) {
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) { if (i) out += '\n'; out += lines[i]; }
    return out;
}

inline std::string NameOf(const std::string& path) {
    std::string p = path;
    while (p.size() > 1 && (p.back() == '/' || p.back() == '\\')) p.pop_back();
    const auto sl = p.find_last_of("/\\");
    return sl == std::string::npos ? p : p.substr(sl + 1);
}

/// "Assets/Models/Hero.fbx" for a path inside <projectDir>/Assets (the Assets folder itself gives "Assets"); "" for a
/// path that is outside it (or when either path is empty). Links that point out of the project are refused too.
inline std::string AssetsRelativeText(const std::string& projectDir, const std::string& path) {
    if (projectDir.empty() || path.empty()) return {};
    namespace fs = std::filesystem;
    const fs::path assets = okayproj::Normalized(fs::u8path(projectDir) / "Assets");
    const fs::path p = okayproj::Normalized(fs::u8path(path));
    if (!okayproj::IsSameOrInside(p, assets)) return {};
    std::error_code ec;
    const fs::path rel = p.lexically_relative(assets);
    if (rel.empty() || rel == ".") return "Assets";
    return "Assets/" + rel.generic_u8string();
}

/// The lines Copy Text puts on the clipboard for `paths`: names, Assets-relative paths or full paths ('/' separators).
/// A path outside the project gives its full path in "rel" mode, so nothing is silently dropped.
inline std::string CopyText(const std::string& what, const std::string& projectDir, const std::vector<std::string>& paths) {
    std::vector<std::string> lines;
    for (const std::string& p : paths) {
        if (what == "name") lines.push_back(NameOf(p));
        else if (what == "rel") { std::string r = AssetsRelativeText(projectDir, p); lines.push_back(r.empty() ? std::string(p) : r); }
        else { std::string f = p; for (char& c : f) if (c == '\\') c = '/'; lines.push_back(f); }
    }
    return JoinLines(lines);
}

/// True when the path can be put between double quotes in a command line for another program without the shell doing
/// something with it: no control characters, quote, $, % or backtick (and no backslash off Windows, where it escapes).
/// A name with one of these can be opened from the file manager; the panel just will not build a command from it.
inline bool IsShellSafe(const std::string& path) {
    if (path.empty()) return false;
    for (unsigned char c : path) {
        if (c < 0x20 || c == 0x7f || c == '"' || c == '$' || c == '%' || c == '`') return false;
#if !defined(_WIN32)
        if (c == '\\') return false;
#endif
    }
    return true;
}

/// Why the Move to... picker cannot offer `destDir` for these items, "" when it can. `assetsRoot` is the project's
/// Assets folder. Checked: the folder is inside Assets, none of the items is the Assets folder, a folder is not moved into
/// itself or a folder inside it, and not every item is already there (moving nothing is not a move).
inline std::string MoveBlockedReason(const std::string& assetsRoot, const std::vector<std::string>& items, const std::string& destDir) {
    namespace fs = std::filesystem;
    if (items.empty()) return "Nothing is selected.";
    const fs::path root = okayproj::Normalized(fs::u8path(assetsRoot));
    const fs::path dest = okayproj::Normalized(fs::u8path(destDir));
    if (!okayproj::IsSameOrInside(dest, root)) return "That folder is outside the project's Assets folder.";
    int alreadyThere = 0;
    for (const std::string& it : items) {
        const fs::path p = okayproj::Normalized(fs::u8path(it));
        if (!okayproj::IsSameOrInside(p, root)) return "That item is outside the project's Assets folder.";
        if (okayproj::IsSameOrInside(root, p)) return "The Assets folder itself can't be moved.";
        std::error_code ec;
        if (okayproj::IsSameOrInside(dest, p) && fs::is_directory(p, ec)) return "A folder can't be moved into itself.";
        const fs::path parent = okayproj::Normalized(p.parent_path());
        if (okayproj::IsSameOrInside(dest, parent) && okayproj::IsSameOrInside(parent, dest)) ++alreadyThere;
    }
    if (alreadyThere == static_cast<int>(items.size())) return items.size() == 1 ? "It is already in that folder." : "They are already in that folder.";
    return {};
}

// ---- Scenes In Build: Set as Start Scene -------------------------------------------------------------

/// True when `scenePath` is the first ticked entry (the scene a built game starts on). `E` has `.path` and `.on`;
/// `same(a, b)` says whether two paths name one file (the editor passes a canonical-path compare).
template <class E, class Same>
bool IsStartScene(const std::vector<E>& list, const std::string& scenePath, Same same) {
    for (const E& e : list) if (e.on) return same(e.path, scenePath);
    return false;
}
/// Put `scenePath` first and ticked, adding it when it is not in the list. Everything else keeps its order and its tick.
/// Returns false (nothing changed) when it already is the start scene.
template <class E, class Same>
bool MakeStartScene(std::vector<E>& list, const std::string& scenePath, Same same) {
    if (IsStartScene(list, scenePath, same)) return false;
    E entry{};
    for (std::size_t i = 0; i < list.size(); ++i)
        if (same(list[i].path, scenePath)) { entry = list[i]; list.erase(list.begin() + static_cast<std::ptrdiff_t>(i)); break; }
    entry.path = scenePath;
    entry.on = true;
    list.insert(list.begin(), entry);
    return true;
}

// ---- time text for Properties --------------------------------------------------------------------------

/// "2026-11-15 14:03 UTC" for seconds since the Unix epoch (days counted with the usual civil-calendar arithmetic:
/// 400-year eras, March-based years). "" for 0 or a negative value.
inline std::string FormatUtc(std::int64_t secs) {
    if (secs <= 0) return {};
    const std::int64_t days = secs / 86400, rem = secs % 86400;
    const std::int64_t z = days + 719468;                         // days since 0000-03-01
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const std::int64_t doe = z - era * 146097;                    // day of era, 0..146096
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);   // day of the March-based year, 0..365
    const std::int64_t mp = (5 * doy + 2) / 153;                  // month, March = 0
    const int day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
    const int month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
    const std::int64_t year = yoe + era * 400 + (month <= 2 ? 1 : 0);
    char buf[40];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d UTC", static_cast<int>(year), month, day,
                  static_cast<int>(rem / 3600), static_cast<int>((rem % 3600) / 60));
    return buf;
}

/// "just now", "5 minutes ago", "3 hours ago", "2 days ago", "4 months ago", "2 years ago". "" when `then` is 0.
inline std::string AgoText(std::int64_t now, std::int64_t then) {
    using detail::Plural;
    if (then <= 0 || now <= 0) return {};
    std::int64_t d = now - then;
    if (d < 0) d = 0;
    if (d < 60) return "just now";
    if (d < 3600) return Plural(static_cast<int>(d / 60), "minute", "minutes") + " ago";
    if (d < 86400) return Plural(static_cast<int>(d / 3600), "hour", "hours") + " ago";
    if (d < 86400 * 60) return Plural(static_cast<int>(d / 86400), "day", "days") + " ago";
    if (d < 86400 * 730) return Plural(static_cast<int>(d / (86400 * 30)), "month", "months") + " ago";
    return Plural(static_cast<int>(d / (86400 * 365)), "year", "years") + " ago";
}

// ---- the Properties window --------------------------------------------------------------------------------

struct PropertyInput {
    std::string name, typeName, projectPath, fullPath;   ///< projectPath: "Assets/Models/Hero.fbx"
    bool isFolder = false;
    bool indexed = true;
    std::string sizeText;                ///< "12 KB" (okaypb::HumanSize), files and folders
    std::uint64_t bytes = 0;
    std::int64_t modified = 0, now = 0;  ///< seconds since the epoch
    std::string guid;                    ///< whole GUID text (files)
    bool hasMeta = false;
    int uses = 0, usesAll = 0, usedBy = 0, missing = 0;
    std::vector<std::string> tags;
    std::string colourLabel;             ///< "" for none
    bool favourite = false;
    bool startScene = false;
    int files = 0, folders = 0;          ///< folders: what is inside, below too
};
struct Property { std::string label, value; bool warn = false; };

inline std::vector<Property> PropertyRows(const PropertyInput& in) {
    using detail::Plural;
    std::vector<Property> r;
    auto add = [&](const char* l, std::string v, bool warn = false) { r.push_back({l, std::move(v), warn}); };
    add("Name", in.name);
    add("Type", in.typeName);
    {   // where it is: the project path without its own name
    std::string where = in.projectPath;
    const auto sl = where.find_last_of('/');
    add("Location", sl == std::string::npos ? where : where.substr(0, sl));
    }
    if (in.isFolder) {
        add("Holds", Plural(in.files, "file", "files") + ", " + Plural(in.folders, "folder", "folders") + (in.sizeText.empty() ? std::string() : ", " + in.sizeText));
    } else {
        add("Size", in.sizeText.empty() ? std::to_string(in.bytes) + " bytes" : in.sizeText + " (" + std::to_string(in.bytes) + " bytes)");
    }
    if (in.modified > 0) {
        const std::string ago = AgoText(in.now, in.modified);
        add("Modified", FormatUtc(in.modified) + (ago.empty() ? std::string() : " (" + ago + ")"));
    }
    if (!in.isFolder) {
        if (!in.indexed) add("Database", "The asset database does not know this file yet.");
        else {
            add("GUID", in.guid.empty() ? std::string("-") : in.guid);
            add("Meta file", in.hasMeta ? "Yes: the GUID is kept in the .okaymeta next to it, so renames and moves keep it."
                                        : "No .okaymeta yet: the GUID lasts for this session. One is made when you give it a tag or import settings.");
            add("Uses", in.uses == 0 ? std::string("nothing") : Plural(in.uses, "asset", "assets") + (in.usesAll > in.uses ? " (" + std::to_string(in.usesAll) + " through the whole graph)" : std::string()));
            add("Used by", in.usedBy == 0 ? std::string("nothing") : Plural(in.usedBy, "asset", "assets"));
            if (in.missing > 0) add("Broken references", Plural(in.missing, "reference to a file that is not there", "references to files that are not there"), true);
        }
        if (!in.tags.empty()) { std::string t; for (const std::string& x : in.tags) t += (t.empty() ? "" : ", ") + x; add("Tags", t); }
    }
    if (!in.colourLabel.empty()) add("Label", in.colourLabel);
    if (in.favourite) add("Favourite", "Yes");
    if (in.startScene) add("Start scene", "Yes: a built game starts on this scene.");
    add("Path in project", in.projectPath);
    add("Full path", in.fullPath);
    return r;
}

} // namespace ctx
} // namespace okaypb
