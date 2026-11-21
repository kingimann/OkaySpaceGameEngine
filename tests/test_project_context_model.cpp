// The Project panel's right-click menus (editor/ProjectContextModel.hpp): header-only, no ImGui, no engine. Checks the
// order and labels of every menu, plurals for a selection ("Delete 3 Items"), what is greyed and the reason it says,
// that no menu starts, ends or doubles a separator, and the pure helpers behind the new items: Copy Text, the
// Assets-relative path, which file types run code (never opened with the system's open command), the path text helpers
// (Outermost of a selection, the nearest folder that exists), the Move to... rule, Set as Start Scene and the Properties rows.
// Paths are checked on temp folders. Nothing sleeps, no threads.
#include "test_framework.hpp"
#include "../editor/ProjectContextModel.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <random>
#include <set>
#include <sys/stat.h>

namespace fs = std::filesystem;
using namespace okaypb::ctx;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        static int n = 0;
        path = fs::temp_directory_path() / ("okay_ctxmodel_" + std::to_string(++n) + "_" +
               std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path / "Assets", ec);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
    std::string proj() const { return path.generic_u8string(); }
    std::string assets() const { return (path / "Assets").generic_u8string(); }
    std::string at(const std::string& rel) const { return (path / "Assets" / fs::u8path(rel)).generic_u8string(); }
    void dir(const std::string& rel) const { std::error_code ec; fs::create_directories(path / "Assets" / fs::u8path(rel), ec); }
    void file(const std::string& rel, const std::string& text = "x") const {
        std::error_code ec; fs::create_directories((path / "Assets" / fs::u8path(rel)).parent_path(), ec);
        std::ofstream(path / "Assets" / fs::u8path(rel), std::ios::binary) << text;
    }
};

bool Has(const std::vector<Item>& v, const std::string& id) { return Find(v, id) != nullptr; }
bool On(const std::vector<Item>& v, const std::string& id) { const Item* i = Find(v, id); return i && i->enabled; }
bool Off(const std::vector<Item>& v, const std::string& id) { const Item* i = Find(v, id); return i && !i->enabled; }
std::string Label(const std::vector<Item>& v, const std::string& id) { const Item* i = Find(v, id); return i ? i->label : std::string("<missing ") + id + ">"; }
std::string Why(const std::vector<Item>& v, const std::string& id) { const Item* i = Find(v, id); return i ? i->reason : std::string("<missing>"); }
bool Contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

State File(Kind k) { State s; s.kind = k; s.count = 1; s.labelNames = {"None", "Red", "Green"}; return s; }
State Folder() { State s; s.kind = Kind::Folder; s.count = 1; s.labelNames = {"None", "Red", "Green"}; return s; }

// No separator first, last or doubled, no empty submenu, anywhere in the tree of items.
bool Tidy(const std::vector<Item>& v) {
    if (v.empty()) return true;
    if (v.front().separator || v.back().separator) return false;
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i && v[i].separator && v[i - 1].separator) return false;
        if (!v[i].submenu.empty() && v[i].children.empty()) return false;
        if (!Tidy(v[i].children)) return false;
        if (v[i].separator && (v[i].enabled == false && !v[i].id.empty())) return false;
    }
    return true;
}

struct SceneEntry { std::string path; bool on = true; };
bool SameText(const std::string& a, const std::string& b) { return a == b; }

} // namespace

int main() {
    RUN_SUITE("project_context_model");

    // ---- a single item: order and labels ----
    {
        State s = File(Kind::Script);
        const std::vector<Item> m = ItemMenu(s);
        const std::vector<std::string> want = {"open", "script:edit", "open:external", "open:default", "build:exclude", "rename", "duplicate", "cut", "copy", "paste", "moveto", "delete",
                                               "refs", "deps", "refs:scene", "reimport", "favourite", "tag:add", "label:0", "label:1", "label:2",
                                               "copy:name", "copy:rel", "copy:full", "reveal", "properties"};
        CHECK(Ids(m) == want);
        CHECK(Label(m, "open") == "Open");
        CHECK(Label(m, "build:exclude") == "Exclude from Build");
        { State x = File(Kind::Script); x.excludedFromBuild = true; CHECK(Label(ItemMenu(x), "build:exclude") == "Include in Build"); }
        { State x = File(Kind::Scene); CHECK(Find(ItemMenu(x), "build:exclude") == nullptr); }            // scenes are chosen in Scenes In Build
        { State x = File(Kind::Script); x.readOnly = true; const Item* e = Find(ItemMenu(x), "build:exclude"); CHECK(e && !e->enabled); }
        CHECK(Label(m, "delete") == "Delete" && Label(m, "duplicate") == "Duplicate" && Label(m, "cut") == "Cut" && Label(m, "copy") == "Copy");
        CHECK(Find(m, "rename")->shortcut == "F2" && Find(m, "delete")->shortcut == "Del" && Find(m, "duplicate")->shortcut == "Ctrl+D");
        CHECK(Find(m, "cut")->shortcut == "Ctrl+X" && Find(m, "copy")->shortcut == "Ctrl+C" && Find(m, "paste")->shortcut == "Ctrl+V");
        CHECK(Label(m, "reveal") == "Show in File Manager");
        CHECK(Tidy(m));
        // the very same item on Windows says Explorer
        State w = s; w.fileManager = "Explorer";
        CHECK(Label(ItemMenu(w), "reveal") == "Show in Explorer");
    }

    // ---- kind-specific items ----
    {
        State pf = File(Kind::Prefab);
        std::vector<Item> m = ItemMenu(pf);
        CHECK(Label(m, "open") == "Open Prefab");
        CHECK(On(m, "prefab:place") && On(m, "prefab:variant") && On(m, "prefab:validate"));
        CHECK(Off(m, "prefab:child") && Contains(Why(m, "prefab:child"), "Hierarchy"));
        pf.hierarchySelection = true;
        CHECK(On(ItemMenu(pf), "prefab:child"));

        State sc = File(Kind::Scene);
        m = ItemMenu(sc);
        CHECK(On(m, "scene:add") && On(m, "scene:start") && !Has(m, "prefab:place"));
        sc.isStartScene = true;
        m = ItemMenu(sc);
        CHECK(Off(m, "scene:start") && Contains(Why(m, "scene:start"), "already starts the game"));
        State md = File(Kind::Model);
        CHECK(Has(ItemMenu(md), "model:player") && !Has(ItemMenu(File(Kind::Image)), "model:player"));
        CHECK(Has(ItemMenu(File(Kind::Script)), "script:edit") && !Has(ItemMenu(File(Kind::Data)), "script:edit"));
    }

    // ---- open outside the editor: text kinds get both, pictures and models only the default app ----
    {
        for (Kind k : {Kind::Script, Kind::VisualScript, Kind::Material, Kind::Data, Kind::Scene, Kind::Prefab}) {
            const std::vector<Item> m = ItemMenu(File(k));
            CHECK(Has(m, "open:external") && Has(m, "open:default"));
            CHECK(On(m, "open:external") && On(m, "open:default"));
        }
        for (Kind k : {Kind::Image, Kind::Audio, Kind::Model, Kind::Other}) {
            const std::vector<Item> m = ItemMenu(File(k));
            CHECK(!Has(m, "open:external") && Has(m, "open:default"));
        }
        CHECK(!Has(ItemMenu(Folder()), "open:default") && !Has(ItemMenu(Folder()), "open:external"));
        // a type that runs code is never handed to the system's open command, and never goes to an editor from this menu either
        State s = File(Kind::Data); s.runsCode = true;
        std::vector<Item> m = ItemMenu(s);
        CHECK(!Has(m, "open:external"));                                  // ".js" is not text-openable
        CHECK(Off(m, "open:default"));
        CHECK(Why(m, "open:default") == "This file type can run code; open it from the file manager if you trust it.");
        // the other items do not care about it: it can still be renamed and shown in the file manager
        CHECK(On(m, "rename") && On(m, "reveal") && On(m, "properties"));
        State other = File(Kind::Other); other.runsCode = true;
        CHECK(Off(ItemMenu(other), "open:default") && !Has(ItemMenu(other), "open:external"));
        // a file that is not there any more cannot be opened or shown
        State g = File(Kind::Script); g.gone = true;
        m = ItemMenu(g);
        CHECK(Off(m, "open:external") && Off(m, "open:default") && Off(m, "open") && Off(m, "reveal") && Off(m, "properties"));
        CHECK(Why(m, "reveal") == "This file is not there any more.");
        State gf = Folder(); gf.gone = true; gf.where = Where::Tree;
        m = FolderTreeMenu(gf);
        CHECK(Off(m, "create") && Why(m, "create") == "This folder is not there any more." && Off(m, "paste") && Off(m, "reveal") && Off(m, "properties"));
        CHECK(Off(m, "create:folder"));   // (the items in the submenu agree)
    }

    // ---- Open is greyed for a type no editor window opens, and says what to do instead ----
    {
        State s = File(Kind::Image); s.canOpen = false;
        std::vector<Item> m = ItemMenu(s);
        CHECK(Off(m, "open") && Contains(Why(m, "open"), "Open with Default App"));
        CHECK(On(m, "open:default"));
        s.count = 2;
        CHECK(Off(ItemMenu(s), "open") && Contains(Why(ItemMenu(s), "open"), "one item"));   // (several items: the one-item reason first)
        CHECK(On(ItemMenu(File(Kind::Scene)), "open"));
        CHECK(State().canOpen);   // the default is "can open", so a caller that does not know gets a working item
    }

    // ---- several items selected ----
    {
        State s = File(Kind::Image); s.count = 3;
        const std::vector<Item> m = ItemMenu(s);
        CHECK(m.front().heading && m.front().label == "3 items selected");
        CHECK(Label(m, "delete") == "Delete 3 Items" && Label(m, "duplicate") == "Duplicate 3 Items");
        CHECK(Label(m, "cut") == "Cut 3 Items" && Label(m, "copy") == "Copy 3 Items");
        CHECK(Label(m, "reimport") == "Reimport 3 Items");
        CHECK(Off(m, "rename") && Contains(Why(m, "rename"), "one item"));
        CHECK(Off(m, "open") && Contains(Why(m, "open"), "one item"));
        // single-item things are gone, not greyed
        CHECK(!Has(m, "properties") && !Has(m, "refs") && !Has(m, "deps") && !Has(m, "refs:scene") && !Has(m, "tag:add") && !Has(m, "open:default"));
        // Copy Text says one per line
        const Item* c = Find(m, "copy");   // (the Copy command, not the submenu)
        CHECK(c && c->submenu.empty());
        CHECK(Label(m, "copy:name") == "Names (one per line)" && Label(m, "copy:rel") == "Relative Paths (one per line)" && Label(m, "copy:full") == "Full Paths (one per line)");
        CHECK(Contains(Label(m, "favourite"), "Add 3 Items to Favourites"));
        s.favourite = true;
        CHECK(Label(ItemMenu(s), "favourite") == "Remove 3 Items from Favourites");
        CHECK(Tidy(m));
        // one item: no count in the labels
        s.count = 1; s.favourite = false;
        CHECK(Label(ItemMenu(s), "delete") == "Delete" && Label(ItemMenu(s), "favourite") == "Add to Favourites");
    }

    // ---- greyed items say why ----
    {
        State s = File(Kind::Image); s.readOnly = true;
        std::vector<Item> m = ItemMenu(s);
        for (const char* id : {"rename", "duplicate", "cut", "delete", "moveto", "favourite"}) {
            CHECK(Off(m, id));
            CHECK(Why(m, id) == "Open a project first.");
        }
        CHECK(Off(m, "paste") && Why(m, "paste") == "Open a project first.");
        s = File(Kind::Image);
        m = ItemMenu(s);
        CHECK(Off(m, "paste") && Why(m, "paste") == "Nothing was copied or cut yet.");
        s.hasClipboard = true; s.clipboardCount = 1;
        m = ItemMenu(s);
        CHECK(On(m, "paste") && Label(m, "paste") == "Paste");
        s.clipboardCount = 4;
        CHECK(Label(ItemMenu(s), "paste") == "Paste 4 Items");
        // every greyed command has a reason, every other item has none
        for (Kind k : {Kind::Folder, Kind::Script, Kind::Scene, Kind::Prefab, Kind::Image}) {
            for (int ro = 0; ro < 2; ++ro) for (int root = 0; root < 2; ++root) for (int n = 1; n <= 2; ++n) {
                State t = File(k); t.readOnly = ro; t.atRoot = root && k == Kind::Folder; t.count = n; t.runsCode = ro != 0; t.gone = root == 0 && ro != 0;
                for (Where w : {Where::Item, Where::Tree}) {
                    t.where = w;
                    const std::vector<Item> mm = w == Where::Tree ? FolderTreeMenu(t) : ItemMenu(t);
                    std::vector<const Item*> all;
                    std::vector<std::string> ids = Ids(mm);
                    for (const std::string& id : ids) { const Item* it = Find(mm, id); CHECK(it != nullptr); if (it) { CHECK(it->enabled || !it->reason.empty()); CHECK(!it->enabled || it->reason.empty()); } }
                    CHECK(Tidy(mm));
                }
            }
        }
    }

    // ---- folders ----
    {
        State s = Folder();
        std::vector<Item> m = ItemMenu(s);
        CHECK(Label(m, "open") == "Open Folder");
        const Item* create = Find(m, "create");
        CHECK(create && create->submenu == "create" && !create->children.empty() && create->enabled);
        CHECK(Has(m, "create:folder") && Has(m, "create:dialogue"));
        CHECK(Label(m, "paste") == "Paste Into");
        s.clipboardCount = 3; s.hasClipboard = true;
        CHECK(Label(ItemMenu(s), "paste") == "Paste 3 Items Into");
        CHECK(Label(m, "reimport") == "Reimport All in Folder");
        CHECK(!Has(m, "refs") && !Has(m, "deps") && !Has(m, "refs:scene") && !Has(m, "tag:add"));
        CHECK(Off(m, "tree:expand") && Off(m, "tree:collapse") && Contains(Why(m, "tree:expand"), "no folders"));
        CHECK(On(m, "tree:others"));
        s.hasSubfolders = true;
        m = ItemMenu(s);
        CHECK(On(m, "tree:expand") && On(m, "tree:collapse"));
        CHECK(Label(m, "reveal") == "Show in File Manager" && Find(m, "reveal")->tip.find("folder") != std::string::npos);
        // a file never gets the tree items or the Create submenu
        CHECK(!Has(ItemMenu(File(Kind::Image)), "tree:expand") && !Has(ItemMenu(File(Kind::Image)), "create"));
        // several selected: no Create, no tree items
        State many = Folder(); many.count = 2;
        CHECK(!Has(ItemMenu(many), "create") && !Has(ItemMenu(many), "tree:expand"));
    }

    // ---- the folder tree menu and the Assets root ----
    {
        State s = Folder(); s.where = Where::Tree; s.atRoot = true;
        std::vector<Item> m = FolderTreeMenu(s);
        CHECK(Find(m, "rename")->shortcut.empty() && Find(m, "delete")->shortcut.empty());   // the tree has no shortcuts
        for (const char* id : {"rename", "duplicate", "cut", "moveto", "delete", "favourite"}) {
            CHECK(Off(m, id));
            CHECK(Contains(Why(m, id), "Assets folder"));
        }
        CHECK(On(m, "create") && On(m, "reveal") && On(m, "properties") && On(m, "tree:others"));
        CHECK(Off(m, "copy"));   // the root cannot be copied either
        CHECK(!Has(m, "label:1"));   // no colour label for the root
        s.atRoot = false;
        m = FolderTreeMenu(s);
        CHECK(On(m, "rename") && On(m, "delete") && On(m, "moveto") && Has(m, "label:1"));
        // the tree has no selection header even if the caller left a count
        s.count = 5;
        CHECK(!FolderTreeMenu(s).front().heading && Label(FolderTreeMenu(s), "delete") == "Delete");
    }

    // ---- tags and labels ----
    {
        State s = File(Kind::Image); s.tags = {"hero", "wip"}; s.label = 2;
        const std::vector<Item> m = ItemMenu(s);
        const Item* tags = Find(m, "tags");
        CHECK(tags && tags->children.size() == 3 && tags->children[0].id == "tag:add");
        CHECK(Has(m, "tag:remove:hero") && Label(m, "tag:remove:wip") == "Remove \"wip\"");
        const Item* lab = Find(m, "label");
        CHECK(lab && lab->children.size() == 3);
        CHECK(!lab->children[0].checked && !lab->children[1].checked && lab->children[2].checked);
        State none = File(Kind::Image); none.labelNames.clear();
        CHECK(!Has(ItemMenu(none), "label"));
    }

    // ---- the Create submenu ----
    {
        State s;
        std::vector<Item> c = CreateItems(s);
        std::vector<std::string> want = {"create:folder", "create:script", "create:visualscript", "create:scene", "create:prefab", "create:dialogue", "create:quest",
                                         "create:behaviortree", "create:sequence", "create:material", "create:data", "create:item", "create:loot", "create:recipe", "create:stdfolders"};
        CHECK(Ids(c) == want);
        CHECK(Label(c, "create:prefab") == "Prefab");
        s.hierarchySelection = true;
        CHECK(Label(CreateItems(s), "create:prefab") == "Prefab from Selection");
        s.readOnly = true;
        for (const std::string& id : Ids(CreateItems(s))) { CHECK(Off(CreateItems(s), id)); CHECK(Why(CreateItems(s), id) == "Open a project first."); }
        CHECK(Tidy(CreateItems(State())));
        CHECK(c[0].id == "create:folder");
    }

    // ---- the empty-space menu ----
    {
        State s; s.kind = Kind::Folder; s.where = Where::Background; s.shownCount = 4; s.sort = 2; s.view = 1;
        std::vector<Item> m = BackgroundMenu(s);
        const std::vector<std::string> want = {"create:folder", "create:script", "create:visualscript", "create:scene", "create:prefab", "create:dialogue", "create:quest",
                                               "create:behaviortree", "create:sequence", "create:material", "create:data", "create:item", "create:loot", "create:recipe", "create:stdfolders",
                                               "paste", "undo", "import", "refresh", "selectall", "view:grid", "view:list", "tiles:1", "tiles:2", "tiles:3",
                                               "sort:0", "sort:1", "sort:2", "sort:3", "up", "favourite", "copy:name", "copy:rel", "copy:full", "reveal", "open:project", "properties"};
        CHECK(Ids(m) == want);
        CHECK(Tidy(m));
        CHECK(Label(m, "import") == "Import New Asset..." && Label(m, "refresh") == "Refresh" && Find(m, "refresh")->shortcut == "F5");
        CHECK(Label(m, "copy:name") == "Folder Name");
        CHECK(Label(m, "reveal") == "Show in File Manager" && Label(m, "open:project") == "Open Project Folder in File Manager");
        State w = s; w.fileManager = "Explorer";
        CHECK(Label(BackgroundMenu(w), "open:project") == "Open Project Folder in Explorer");
        // checked view and sort; tile sizes are for the grid only
        CHECK(Find(m, "view:list")->checked && !Find(m, "view:grid")->checked && Find(m, "sort:2")->checked && !Find(m, "sort:0")->checked);
        CHECK(Off(m, "tiles:2") && Why(m, "tiles:2") == "Tile size only applies to the grid.");
        s.view = 0; s.tilePreset = 3;
        m = BackgroundMenu(s);
        CHECK(On(m, "tiles:2") && Find(m, "tiles:3")->checked && !Find(m, "tiles:1")->checked);
        // nothing copied, nothing to undo
        CHECK(Off(m, "paste") && Why(m, "paste") == "Nothing was copied or cut yet." && Label(m, "paste") == "Paste");
        CHECK(Off(m, "undo") && Label(m, "undo") == "Undo" && Why(m, "undo") == "Nothing to undo in the Project panel.");
        s.hasClipboard = true; s.clipboardCount = 2; s.canUndo = true; s.undoLabel = "Rename Hero.png";
        m = BackgroundMenu(s);
        CHECK(On(m, "paste") && Label(m, "paste") == "Paste 2 Items");
        CHECK(On(m, "undo") && Label(m, "undo") == "Undo Rename Hero.png" && Find(m, "undo")->shortcut == "Ctrl+Z");
        // at the Assets folder: no Up, no star
        s.atRoot = true;
        m = BackgroundMenu(s);
        CHECK(Off(m, "up") && Why(m, "up") == "This is the Assets folder." && Off(m, "favourite"));
        s.atRoot = false;
        // an empty folder cannot Select All
        s.shownCount = 0;
        CHECK(Off(BackgroundMenu(s), "selectall") && Contains(Why(BackgroundMenu(s), "selectall"), "nothing"));
        // a smart view is not a folder: no creating, pasting, importing here, and the reason says how to leave
        s.smartView = true; s.shownCount = 3;
        m = BackgroundMenu(s);
        for (const char* id : {"create:folder", "create:dialogue", "paste", "import", "up", "favourite", "properties"}) {
            CHECK(Off(m, id));
            CHECK(Contains(Why(m, id), "smart view"));
        }
        CHECK(On(m, "refresh") && On(m, "selectall") && On(m, "reveal") && On(m, "view:grid"));
        // the folder on screen is gone (deleted or renamed behind the panel's back): nothing can be made or pasted in it
        State gone; gone.kind = Kind::Folder; gone.where = Where::Background; gone.gone = true; gone.shownCount = 2; gone.hasClipboard = true; gone.clipboardCount = 1;
        m = BackgroundMenu(gone);
        for (const char* id : {"create:folder", "create:dialogue", "create:stdfolders", "paste", "import", "reveal", "properties"}) {
            CHECK(Off(m, id));
            CHECK(Why(m, id) == "This folder is not there any more.");
        }
        CHECK(On(m, "refresh") && On(m, "open:project") && On(m, "view:grid"));
        CHECK(Off(CreateItems(gone), "create:scene") && Why(CreateItems(gone), "create:scene") == "This folder is not there any more.");
        // with no project open that reason comes first
        State both = gone; both.readOnly = true;
        CHECK(Why(BackgroundMenu(both), "create:folder") == "Open a project first.");
        // no project open
        State none; none.kind = Kind::Folder; none.where = Where::Background; none.readOnly = true;
        m = BackgroundMenu(none);
        for (const char* id : {"create:folder", "import", "refresh", "reveal", "open:project", "properties"}) { CHECK(Off(m, id)); CHECK(Why(m, id) == "Open a project first."); }
        CHECK(Tidy(m));
    }

    // ---- the favourites menu ----
    {
        State s = Folder(); s.where = Where::Favourite;
        std::vector<Item> m = FavouriteMenu(s);
        CHECK(Ids(m) == (std::vector<std::string>{"show", "favourite", "copy:name", "copy:rel", "copy:full", "reveal", "properties"}));
        CHECK(Label(m, "show") == "Open Folder" && Label(m, "favourite") == "Remove from Favourites");
        State f = File(Kind::Image); f.where = Where::Favourite;
        CHECK(Label(FavouriteMenu(f), "show") == "Show in Project");
        f.count = 7;   // a count left over from the grid selection must not leak into the star menu
        CHECK(Label(FavouriteMenu(f), "copy:name") == "Name" && Tidy(FavouriteMenu(f)));
    }

    // ---- Tidy, the guard behind "no separator first, last or doubled" ----
    {
        using namespace okaypb::ctx::detail;
        std::vector<Item> v = {Sep(), Cmd("a", "A"), Sep(), Sep(), Cmd("b", "B"), Sep()};
        Tidy(v);
        CHECK(v.size() == 3 && v[0].id == "a" && v[1].separator && v[2].id == "b");
        std::vector<Item> w = {Cmd("a", "A"), Sep(), Sub("x", "X", {}), Sep(), Cmd("b", "B")};   // an empty submenu goes, and the separators around it join
        Tidy(w);
        CHECK(w.size() == 3 && w[0].id == "a" && w[1].separator && w[2].id == "b");
        std::vector<Item> u = {Sub("s", "S", {Sep(), Cmd("k", "K"), Sep(), Sep()})};          // children are tidied too
        Tidy(u);
        CHECK(u.size() == 1 && u[0].children.size() == 1 && u[0].children[0].id == "k");
        std::vector<Item> e = {Sep(), Sep()};
        Tidy(e);
        CHECK(e.empty());
        std::vector<Item> h = {Head("Group"), Cmd("a", "A")};                                // a heading is not a separator
        Tidy(h);
        CHECK(h.size() == 2 && h[0].heading);
    }

    // ---- every menu in every state is tidy ----
    {
        int n = 0;
        for (Kind k : {Kind::Folder, Kind::Scene, Kind::Prefab, Kind::Script, Kind::VisualScript, Kind::Model, Kind::Image, Kind::Audio, Kind::Material, Kind::Data, Kind::Other})
            for (int bits = 0; bits < 256; ++bits) for (int cnt = 1; cnt <= 3; cnt += 2) {
                State s = File(k); s.count = cnt;
                s.readOnly = bits & 1; s.hasClipboard = bits & 2; s.clipboardCount = (bits & 2) ? 2 : 0; s.favourite = bits & 4; s.hierarchySelection = bits & 8;
                s.isStartScene = bits & 16; s.runsCode = bits & 32; s.gone = (bits & 3) == 1 && (bits & 128); s.atRoot = (bits & 64) && k == Kind::Folder; s.hasSubfolders = bits & 128;
                s.tags = (bits & 4) ? std::vector<std::string>{"a"} : std::vector<std::string>{};
                for (Where w : {Where::Item, Where::Tree, Where::Favourite, Where::Background}) {
                    s.where = w;
                    const std::vector<Item> m = w == Where::Item ? ItemMenu(s) : w == Where::Tree ? FolderTreeMenu(s) : w == Where::Favourite ? FavouriteMenu(s) : BackgroundMenu(s);
                    CHECK(Tidy(m));
                    s.smartView = (bits & 3) == 3;
                    CHECK(Tidy(w == Where::Background ? BackgroundMenu(s) : m));
                    s.smartView = false;
                    ++n;
                }
            }
        CHECK(n == 11 * 256 * 2 * 4);
    }

    // ---- Copy Text ----
    {
        TempDir td;
        td.file("Models/Hero.fbx");
        td.file("Hero Pic.png");
        const std::string a = td.at("Models/Hero.fbx"), b = td.at("Hero Pic.png");
        CHECK(CopyText("name", td.proj(), {a}) == "Hero.fbx");
        CHECK(CopyText("name", td.proj(), {a, b}) == "Hero.fbx\nHero Pic.png");
        CHECK(CopyText("rel", td.proj(), {a}) == "Assets/Models/Hero.fbx");
        CHECK(CopyText("rel", td.proj(), {a, b}) == "Assets/Models/Hero.fbx\nAssets/Hero Pic.png");
        CHECK(CopyText("full", td.proj(), {a}) == a);
        CHECK(CopyText("full", td.proj(), {}) == "");
        CHECK(CopyText("rel", td.proj(), {td.assets()}) == "Assets");
        // a backslash path (Windows style) is written with '/'
        std::string bs = a; for (char& c : bs) if (c == '/') c = '\\';
        CHECK(CopyText("full", td.proj(), {bs}) == a);
        CHECK(CopyText("name", td.proj(), {bs}) == "Hero.fbx");
        // trailing slash on a folder
        CHECK(CopyText("name", td.proj(), {td.at("Models") + "/"}) == "Models");
    }

    // ---- the Assets-relative path refuses anything outside ----
    {
        TempDir td;
        td.file("Sub/x.png");
        td.dir("Sub/Deeper");
        CHECK(AssetsRelativeText(td.proj(), td.at("Sub/x.png")) == "Assets/Sub/x.png");
        CHECK(AssetsRelativeText(td.proj(), td.at("Sub/Deeper")) == "Assets/Sub/Deeper");
        CHECK(AssetsRelativeText(td.proj(), td.assets()) == "Assets");
        CHECK(AssetsRelativeText(td.proj(), td.assets() + "/") == "Assets");
        CHECK(AssetsRelativeText(td.proj(), td.proj()) == "");                                  // the project folder is not inside Assets
        CHECK(AssetsRelativeText(td.proj(), (td.path / "Other" / "y.png").generic_u8string()) == "");
        CHECK(AssetsRelativeText(td.proj(), td.at("Sub/../../escape.png")) == "");              // .. out of the project
        CHECK(AssetsRelativeText(td.proj(), td.at("Sub/../Sub/x.png")) == "Assets/Sub/x.png"); // .. that stays inside is fine
        CHECK(AssetsRelativeText("", td.at("Sub/x.png")) == "" && AssetsRelativeText(td.proj(), "") == "");
        // "Assets2" next to Assets is not inside it
        std::error_code ec;
        fs::create_directories(td.path / "Assets2", ec);
        CHECK(AssetsRelativeText(td.proj(), (td.path / "Assets2" / "z.png").generic_u8string()) == "");
        // text only: a link inside Assets is written the way scenes write it, "Assets/Link/..." (the file operations resolve links
        // before they touch anything; this text is only for the clipboard)
        fs::create_directories(td.path / "Outside", ec);
        std::ofstream((td.path / "Outside" / "secret.txt")) << "s";
        std::error_code lec;
        fs::create_directory_symlink(td.path / "Outside", td.path / "Assets" / "Link", lec);
        if (!lec) CHECK(AssetsRelativeText(td.proj(), td.at("Link/secret.txt")) == "Assets/Link/secret.txt");
        else std::cout << "  (symlinks not available here: link case skipped)\n";
        CHECK(AssetsRelativeText(td.proj(), (td.path / "Outside" / "secret.txt").generic_u8string()) == "");   // the real path of that file is outside
    }

    // ---- file types that run code ----
    {
        // the types the owner's reviewer named, every one of them
        for (const char* e : {"exe", "bat", "cmd", "com", "scr", "lnk", "msi", "ps1", "vbs", "js", "jse", "wsf", "hta", "reg", "jar", "sh"}) {
            CHECK(RunsCode(std::string("C:/p/Assets/tool.") + e));
            std::string upper = e;
            for (char& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            CHECK(RunsCode("C:\\p\\Assets\\TOOL." + upper));   // (a Windows path, in capitals)
            CHECK(!LaunchRefusal(std::string("/nonexistent/dir/tool.") + e).empty());
            State st = File(Kind::Other); st.runsCode = RunsCode(std::string("x.") + e);
            CHECK(Off(ItemMenu(st), "open:default"));
        }
        // more of them, and the spellings that fool a plain check
        for (const char* e : {"vbe", "wsh", "psm1", "pif", "msp", "cpl", "msc", "url", "chm", "appx", "msix", "docm", "xlsm", "command", "appimage", "desktop", "py", "app", "pkg",
                              "ws", "vb", "bas", "sct", "msh1", "ahk", "au3", "mdb", "mde", "msu", "rdp", "theme", "shs", "job"}) CHECK(RunsCode(std::string("a/b.") + e));
        CHECK(RunsCode("a/Setup.EXE") && RunsCode("a/Setup.Exe") && RunsCode("a/photo.png.exe"));
        CHECK(RunsCode("a/setup.exe.") && RunsCode("a/setup.exe ") && RunsCode("a/setup.exe. . "));      // Windows drops trailing dots and spaces
        CHECK(RunsCode("a.b/c/run.bat") && RunsCode("C:\\x\\y\\run.cmd") && RunsCode("run.sh"));
        CHECK(RunsCode("a/.sh"));                                                                          // a file named ".sh" is a .sh
        // things that are only looked at
        for (const char* e : {"png", "jpg", "wav", "ogg", "fbx", "obj", "glb", "txt", "json", "md", "pdf", "okayscene", "okayprefab", "okaymat", "okay", "cs", "lua",
                              "okayvs", "okaydata", "okaydialog", "okaybt", "okayseq", "ttf", "zip", "html", "csv", "xml", "okaymeta"})
            CHECK(!RunsCode(std::string("a/b.") + e));
        CHECK(!RunsCode("a/setup.exe.png") && !RunsCode("a/exe") && !RunsCode("a/noext") && !RunsCode("") && !RunsCode("a/dir.exe/file.txt"));
        CHECK(ExtensionOf("a/b/Hero.PNG") == "png" && ExtensionOf("a/b/noext") == "" && ExtensionOf("a.b/noext") == "" && ExtensionOf("x.tar.gz") == "gz");
        CHECK(ExtensionOf("a/setup.exe...") == "exe" && ExtensionOf("") == "");
        // the list has no blanks, no dots, no upper case, no repeats
        std::set<std::string> seen;
        for (const std::string& e : RunnableExtensions()) {
            CHECK(!e.empty() && e.find('.') == std::string::npos && e == okaypb::paths::Lower(e));
            CHECK(seen.insert(e).second);
        }
        // a program with no extension (Linux, macOS) is recognised by its execute bit, on systems that have one
        TempDir td;
        td.file("bin/tool"); td.file("bin/data");
#if !defined(_WIN32)
        ::chmod(td.at("bin/tool").c_str(), 0755);
        ::chmod(td.at("bin/data").c_str(), 0644);
        CHECK(HasExecutePermission(td.at("bin/tool")) && !HasExecutePermission(td.at("bin/data")));
        CHECK(LaunchRefusal(td.at("bin/tool")) == "This file type can run code; open it from the file manager if you trust it.");
        CHECK(LaunchRefusal(td.at("bin/data")).empty());
        // a picture with the execute bit set (a zip unpacked on a Windows disk) is still just a picture
        td.file("bin/pic.png");
        ::chmod(td.at("bin/pic.png").c_str(), 0755);
        CHECK(LaunchRefusal(td.at("bin/pic.png")).empty());
        CHECK(!HasExecutePermission(td.at("bin")));        // a folder is not a program
#endif
        CHECK(!HasExecutePermission(td.at("bin/missing")) && LaunchRefusal(td.at("bin/missing")).empty());
        CHECK(LaunchRefusal("a/b.txt").empty() && !LaunchRefusal("a/b.EXE").empty());
    }

    // ---- path text: Normalize, SameOrInside, Outermost, NearestFolder ----
    {
        // On Windows "/a/b" has no drive, so Normalize makes it "D:/a/b" (the current drive): spell the paths with it.
        const auto D = [](const char* s) {
#ifdef _WIN32
            return std::filesystem::current_path().root_name().generic_u8string() + s;
#else
            return std::string(s);
#endif
        };
        using namespace okaypb::paths;
        CHECK(Normalize("") == "" && Normalize(D("/a/b/../c/./d/")) == D("/a/c/d") && Normalize(D("/a//b")) == D("/a/b") && Normalize(D("/a\\b\\c")) == D("/a/b/c"));
        CHECK(StripSlash("/a/b///") == "/a/b" && StripSlash("/") == "/");
        // the root keeps its slash: "/" here, "D:/" on Windows (D("/")); "D:" alone would mean the current folder on drive D
        CHECK(Normalize(D("/")) == D("/") && SameOrInside(D("/"), D("/a")) && Normalize(D("/a/..")) == D("/"));
        CHECK(Normalize(D("//")) == D("/") && Normalize(D("/a/../..")) == D("/"));
        // the text part is the same on every system, so the drive spellings are checked here on Linux too
        CHECK(StripSlash("D:/") == "D:/" && StripSlash("d:/") == "d:/" && StripSlash("D://") == "D:/" && StripSlash("D:\\") == "D:\\");
        CHECK(StripSlash("D:/a/") == "D:/a" && StripSlash("D:") == "D:");   // "D:" (drive-relative) is left as written: no slash is added
        CHECK(StripSlash("") == "" && StripSlash("//") == "/" && StripSlash("a/") == "a" && StripSlash("1:/") == "1:");   // "1:" is not a drive
        CHECK(Clean("D:/a/..") == "D:/" && Clean("D:\\a\\..") == "D:/" && Clean("d:/a/b/../") == "d:/a" && Clean("D:/") == "D:/");
        CHECK(Clean("/a/b/../c/") == "/a/c" && Clean("/") == "/" && Clean("") == "");
        CHECK(SameOrInside("D:/", "D:/a") && SameOrInside("D:/", "D:/") && !SameOrInside("D:/", "E:/a") && !SameOrInside("D:/", "D:"));
        CHECK(Parent("D:/a") == "D:/" && Parent("D:/") == "" && Parent("/a") == "/" && Parent("/") == "" && Parent("/a/b") == "/a" && Parent("D:/a/b") == "D:/a" && Parent("a") == "");
        CHECK(Outermost({"D:/", "D:/p/A"}).size() == 1);
        CHECK(SameOrInside(D("/a/b"), D("/a/b")) && SameOrInside(D("/a/b"), D("/a/b/c")) && SameOrInside(D("/a/b/"), D("/a/b/c/")));
        CHECK(!SameOrInside(D("/a/b"), D("/a/bc")) && !SameOrInside(D("/a/b"), D("/a")) && !SameOrInside(D("/a/b/c"), D("/a/b")));
        // Outermost: a folder and something inside it give the folder
        auto out = [](std::vector<std::string> v) { return Outermost(v); };
        CHECK(out({D("/p/A"), D("/p/A/x")}) == (std::vector<std::string>{D("/p/A")}));
        CHECK(out({D("/p/A/x"), D("/p/A")}) == (std::vector<std::string>{D("/p/A")}));                    // the order of the selection does not matter
        CHECK(out({D("/p/A"), D("/p/A/x"), D("/p/A/x/y.png"), D("/p/B/z.png")}) == (std::vector<std::string>{D("/p/A"), D("/p/B/z.png")}));
        CHECK(out({D("/p/A"), D("/p/A b"), D("/p/A/x")}) == (std::vector<std::string>{D("/p/A"), D("/p/A b")}));   // "A b" sorts between "A" and "A/x" in plain text: not inside A
        CHECK(out({D("/p/A"), D("/p/A-1"), D("/p/A.png"), D("/p/A/x")}) == (std::vector<std::string>{D("/p/A"), D("/p/A-1"), D("/p/A.png")}));
        CHECK(out({D("/p/A"), D("/p/A")}) == (std::vector<std::string>{D("/p/A")}));                      // twice the same path
        CHECK(out({D("/p/A/"), D("/p/A")}) == (std::vector<std::string>{D("/p/A")}));                     // two spellings
        CHECK(out({D("/p/A/../A/x"), D("/p/A")}) == (std::vector<std::string>{D("/p/A")}));
        CHECK(out({D("/p/b.png"), D("/p/a.png")}) == (std::vector<std::string>{D("/p/a.png"), D("/p/b.png")}));   // sorted
        CHECK(out({}).empty());
        CHECK(out({D("/")}) == (std::vector<std::string>{D("/")}));
        CHECK(out({D("/"), D("/p/A")}) == (std::vector<std::string>{D("/")}));                     // everything is inside the root
        CHECK(out({D("/p/AB"), D("/p/A"), D("/p/A/B")}) == (std::vector<std::string>{D("/p/A"), D("/p/AB")}));
        // against the slow way (every item against every other) on a few thousand random paths
        std::mt19937 rng(12345);
        const char* parts[] = {"A", "A b", "A-1", "a", "B", "B.c", "x", "y z", "_"};
        for (int round = 0; round < 40; ++round) {
            std::vector<std::string> in;
            const int n = 1 + static_cast<int>(rng() % 60);
            for (int i = 0; i < n; ++i) {
                std::string p = "/p";
                const int depth = 1 + static_cast<int>(rng() % 4);
                for (int d = 0; d < depth; ++d) p += std::string("/") + parts[rng() % 9];
                if (rng() % 7 == 0) p += "/";
                in.push_back(p);
            }
            std::set<std::string> uniq;
            for (const std::string& p : in) uniq.insert(Normalize(p));
            std::vector<std::string> slow;
            for (const std::string& p : uniq) {
                bool nested = false;
                for (const std::string& o : uniq) if (o != p && SameOrInside(o, p)) { nested = true; break; }
                if (!nested) slow.push_back(p);
            }
            const std::vector<std::string> fast = Outermost(in);
#ifdef _WIN32
            (void)slow;   // (Windows folds case, so the slow way above would keep both spellings: compared on the other systems)
#else
            if (fast != slow) { std::cerr << "  Outermost differs in round " << round << "\n"; }
            CHECK(fast == slow);
#endif
            for (const std::string& p : fast) for (const std::string& q : fast) CHECK(p == q || !SameOrInside(p, q));   // nothing left is inside another
        }
        // a big selection is quick (this is only a loop count, the real guard is that it finishes)
        std::vector<std::string> big;
        for (int i = 0; i < 20000; ++i) big.push_back(D("/p/Folder/file") + std::to_string(i) + ".png");
        big.push_back(D("/p/Folder"));
        CHECK(Outermost(big) == (std::vector<std::string>{D("/p/Folder")}));

        // NearestFolder: the shown folder, or the nearest parent that is still there
        TempDir td;
        td.dir("A/B/C"); td.file("A/f.txt");
        const std::string root = td.assets();
        CHECK(NearestFolder(root, td.at("A/B/C")) == td.at("A/B/C"));
        CHECK(NearestFolder(root, td.at("A/B/C/")) == td.at("A/B/C"));
        std::error_code ec;
        fs::remove_all(td.path / "Assets" / "A" / "B" / "C", ec);
        CHECK(NearestFolder(root, td.at("A/B/C")) == td.at("A/B"));
        fs::remove_all(td.path / "Assets" / "A" / "B", ec);
        CHECK(NearestFolder(root, td.at("A/B/C")) == td.at("A"));
        CHECK(NearestFolder(root, td.at("A/f.txt")) == td.at("A"));              // a file is not a folder
        fs::remove_all(td.path / "Assets" / "A", ec);
        CHECK(NearestFolder(root, td.at("A/B/C")) == root);
        CHECK(NearestFolder(root, root) == root && NearestFolder(root, "") == root);
        CHECK(NearestFolder(root, td.proj()) == root);                           // outside Assets: Assets
        CHECK(NearestFolder(root, (td.path / "Elsewhere").generic_u8string()) == root);
        CHECK(NearestFolder(root, td.at("../Assets2/x")) == root);
    }

    // ---- the Move to... rule ----
    {
        TempDir td;
        td.dir("A/B"); td.dir("C"); td.file("A/one.png"); td.file("C/two.png"); td.file("top.png");
        const std::string root = td.assets();
        const std::string A = td.at("A"), B = td.at("A/B"), C = td.at("C");
        CHECK(MoveBlockedReason(root, {td.at("top.png")}, C) == "");
        CHECK(MoveBlockedReason(root, {A}, C) == "");
        CHECK(MoveBlockedReason(root, {td.at("A/one.png"), td.at("C/two.png")}, B) == "");   // one of them is not there yet: fine
        CHECK(MoveBlockedReason(root, {A}, A) == "A folder can't be moved into itself.");
        CHECK(MoveBlockedReason(root, {A}, B) == "A folder can't be moved into itself.");
        CHECK(MoveBlockedReason(root, {A, C}, B) == "A folder can't be moved into itself.");
        CHECK(MoveBlockedReason(root, {td.at("A/one.png")}, A) == "It is already in that folder.");
        CHECK(MoveBlockedReason(root, {td.at("A/one.png"), td.at("A/B")}, A) == "They are already in that folder.");
        CHECK(MoveBlockedReason(root, {td.at("top.png")}, root) == "It is already in that folder.");
        CHECK(MoveBlockedReason(root, {root}, C) == "The Assets folder itself can't be moved.");
        CHECK(MoveBlockedReason(root, {td.at("top.png")}, td.proj()) == "That folder is outside the project's Assets folder.");
        CHECK(MoveBlockedReason(root, {td.at("top.png")}, td.at("C/../../Elsewhere")) == "That folder is outside the project's Assets folder.");
        CHECK(MoveBlockedReason(root, {(td.path / "outside.png").generic_u8string()}, C) == "That item is outside the project's Assets folder.");
        CHECK(MoveBlockedReason(root, {}, C) == "Nothing is selected.");
        // a file named like a folder prefix does not confuse it: "A" vs "A2"
        td.dir("A2");
        CHECK(MoveBlockedReason(root, {A}, td.at("A2")) == "");
        // {A, A/B}: only A moves (B is inside it), so moving them next to A is "already there"
        CHECK(MoveBlockedReason(root, {A, B}, root) == "It is already in that folder.");
        CHECK(MoveBlockedReason(root, {A, B}, C) == "");
        // the picker asks about many folders: the items are looked at once, and a long selection is no slower per folder
        const MoveSources one = PrepareMove(root, {td.at("top.png")});
        CHECK(one.count == 1 && one.never.empty() && one.folders.empty());
        std::vector<std::string> many;
        for (int i = 0; i < 20000; ++i) many.push_back(td.at("Many/file" + std::to_string(i) + ".png"));
        many.push_back(td.at("Many"));                              // (the folder is not there: nothing to look at, and every file is inside it)
        const MoveSources big = PrepareMove(root, many);
        CHECK(big.count == 1);
        CHECK(MoveBlockedReason(big, C) == "");
        std::vector<std::string> files;
        for (int i = 0; i < 20000; ++i) files.push_back(td.at("Spread/f" + std::to_string(i) + ".png"));
        const MoveSources spread = PrepareMove(root, files);
        CHECK(spread.count == 20000 && spread.sameParent && MoveBlockedReason(spread, td.at("Spread")) == "They are already in that folder.");
        CHECK(MoveBlockedReason(spread, C) == "" && MoveBlockedReason(spread, td.proj()) == "That folder is outside the project's Assets folder.");
        files.push_back(td.at("C/two.png"));
        const MoveSources mixed = PrepareMove(root, files);
        CHECK(!mixed.sameParent && MoveBlockedReason(mixed, td.at("Spread")) == "");   // not all of them are there already
    }

    // ---- Set as Start Scene ----
    {
        std::vector<SceneEntry> l = {{"/p/A.okayscene", false}, {"/p/B.okayscene", true}, {"/p/C.okayscene", true}};
        CHECK(IsStartScene(l, "/p/B.okayscene", SameText) && !IsStartScene(l, "/p/A.okayscene", SameText) && !IsStartScene(l, "/p/C.okayscene", SameText));
        CHECK(!MakeStartScene(l, "/p/B.okayscene", SameText));                         // already first: nothing changes
        CHECK(l.size() == 3 && l[0].path == "/p/A.okayscene");
        CHECK(MakeStartScene(l, "/p/C.okayscene", SameText));                          // moved to the front, ticked
        CHECK(l.size() == 3 && l[0].path == "/p/C.okayscene" && l[0].on);
        CHECK(l[1].path == "/p/A.okayscene" && !l[1].on && l[2].path == "/p/B.okayscene" && l[2].on);   // the others keep order and ticks
        CHECK(IsStartScene(l, "/p/C.okayscene", SameText));
        CHECK(MakeStartScene(l, "/p/A.okayscene", SameText));                          // an unticked scene becomes ticked and first
        CHECK(l[0].path == "/p/A.okayscene" && l[0].on && l.size() == 3);
        CHECK(MakeStartScene(l, "/p/New.okayscene", SameText));                        // not in the list yet: added
        CHECK(l.size() == 4 && l[0].path == "/p/New.okayscene" && l[0].on);
        std::vector<SceneEntry> empty;
        CHECK(!IsStartScene(empty, "/p/A.okayscene", SameText));
        CHECK(MakeStartScene(empty, "/p/A.okayscene", SameText) && empty.size() == 1 && empty[0].on);
        std::vector<SceneEntry> allOff = {{"/p/A.okayscene", false}, {"/p/B.okayscene", false}};
        CHECK(!IsStartScene(allOff, "/p/A.okayscene", SameText));                      // nothing ticked: no start scene
        CHECK(MakeStartScene(allOff, "/p/B.okayscene", SameText) && allOff[0].path == "/p/B.okayscene" && allOff[0].on && !allOff[1].on);
        // the comparison is the caller's: a case-insensitive one finds the same file
        auto ci = [](const std::string& a, const std::string& b) {
            if (a.size() != b.size()) return false;
            for (std::size_t i = 0; i < a.size(); ++i) if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
            return true;
        };
        std::vector<SceneEntry> w = {{"/P/A.okayscene", true}, {"/P/B.okayscene", true}};
        CHECK(MakeStartScene(w, "/p/b.okayscene", ci) && w.size() == 2 && w[0].path == "/p/b.okayscene");
    }

    // ---- dates ----
    {
        CHECK(FormatUtc(0) == "" && FormatUtc(-5) == "");
        CHECK(FormatUtc(1) == "1970-01-01 00:00 UTC");
        CHECK(FormatUtc(86400LL * 365) == "1971-01-01 00:00 UTC");
        CHECK(FormatUtc(951782400LL) == "2000-02-29 00:00 UTC");            // a leap day in a year divisible by 400
        CHECK(FormatUtc(951782400LL + 86400) == "2000-03-01 00:00 UTC");
        CHECK(FormatUtc(4107542400LL) == "2100-03-01 00:00 UTC");           // 2100 is not a leap year
        CHECK(FormatUtc(1700000000LL) == "2023-11-14 22:13 UTC");
        CHECK(FormatUtc(1234567890LL) == "2009-02-13 23:31 UTC");          // the famous one
        CHECK(FormatUtc(1609459199LL) == "2020-12-31 23:59 UTC" && FormatUtc(1609459200LL) == "2021-01-01 00:00 UTC");
        CHECK(FormatUtc(4102444800LL) == "2100-01-01 00:00 UTC");
        CHECK(FormatUtc(1709164800LL) == "2024-02-29 00:00 UTC");         // a leap day
        CHECK(AgoText(1000, 0) == "" && AgoText(0, 5) == "");
        CHECK(AgoText(1000, 990) == "just now" && AgoText(1000, 2000) == "just now");
        CHECK(AgoText(1000, 940) == "1 minute ago" && AgoText(1000 + 120, 1000) == "2 minutes ago");
        CHECK(AgoText(10000, 10000 - 3600) == "1 hour ago" && AgoText(100000, 100000 - 7200) == "2 hours ago");
        CHECK(AgoText(1000000, 1000000 - 86400) == "1 day ago" && AgoText(1000000, 1000000 - 86400 * 5) == "5 days ago");
        CHECK(AgoText(90000000, 90000000 - 86400LL * 90) == "3 months ago");
        CHECK(AgoText(300000000, 300000000 - 86400LL * 365 * 3) == "3 years ago");
    }

    // ---- the Properties rows ----
    {
        PropertyInput in;
        in.name = "Hero.png"; in.typeName = "Texture"; in.projectPath = "Assets/Textures/Hero.png"; in.fullPath = "/p/Assets/Textures/Hero.png";
        in.sizeText = "12 KB"; in.bytes = 12345; in.modified = 951782400; in.now = 951782400 + 86400 * 2;
        in.guid = "0123456789abcdef0123456789abcdef"; in.hasMeta = true; in.uses = 0; in.usedBy = 1;
        std::vector<Property> r = PropertyRows(in);
        std::vector<std::string> labels;
        for (const Property& p : r) labels.push_back(p.label);
        CHECK(labels == (std::vector<std::string>{"Name", "Type", "Location", "Size", "Modified", "GUID", "Meta file", "Uses", "Used by", "Path in project", "Full path"}));
        auto val = [&](const char* l) { for (const Property& p : r) if (p.label == l) return p.value; return std::string("<missing>"); };
        CHECK(val("Location") == "Assets/Textures");
        CHECK(val("Size") == "12 KB (12345 bytes)");
        CHECK(val("Modified") == "2000-02-29 00:00 UTC (2 days ago)");
        CHECK(val("Uses") == "nothing" && val("Used by") == "1 asset");
        in.uses = 2; in.usesAll = 5; in.usedBy = 3; in.missing = 1; in.tags = {"hero", "wip"}; in.colourLabel = "Red"; in.favourite = true; in.startScene = true;
        r = PropertyRows(in);
        CHECK(val("Uses") == "2 assets (5 through the whole graph)" && val("Used by") == "3 assets");
        CHECK(val("Tags") == "hero, wip" && val("Label") == "Red" && val("Favourite") == "Yes" && Contains(val("Start scene"), "starts"));
        bool warn = false; for (const Property& p : r) if (p.label == "Broken references") warn = p.warn && p.value == "1 reference to a file that is not there";
        CHECK(warn);
        in.missing = 2;
        r = PropertyRows(in);
        CHECK(val("Broken references") == "2 references to files that are not there");
        // the database does not know the file: no GUID rows, one line saying so
        in.indexed = false;
        r = PropertyRows(in);
        CHECK(val("Database") != "<missing>" && val("GUID") == "<missing>" && val("Uses") == "<missing>");
        // a folder
        PropertyInput fo;
        fo.name = "Textures"; fo.typeName = "Folder"; fo.isFolder = true; fo.projectPath = "Assets/Textures"; fo.fullPath = "/p/Assets/Textures";
        fo.files = 1; fo.folders = 2; fo.sizeText = "3 MB";
        r = PropertyRows(fo);
        CHECK(val("Holds") == "1 file, 2 folders, 3 MB" && val("GUID") == "<missing>" && val("Size") == "<missing>" && val("Modified") == "<missing>");
        CHECK(val("Location") == "Assets");
        fo.files = 4; fo.folders = 1;
        r = PropertyRows(fo);
        CHECK(val("Holds") == "4 files, 1 folder, 3 MB");
        // a path with no folder part
        PropertyInput top; top.name = "Assets"; top.typeName = "Folder"; top.isFolder = true; top.projectPath = "Assets"; top.fullPath = "/p/Assets";
        r = PropertyRows(top);
        CHECK(val("Location") == "Assets");
    }

    TEST_MAIN_RESULT();
}
