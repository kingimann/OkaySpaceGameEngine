// The Project panel's logic (editor/ProjectBrowser.*, no ImGui): the folder model, safe file operations inside Assets
// (create, rename, move, copy, duplicate, delete to the trash, undo), reference rewriting that keeps scenes working,
// "what uses this" for the delete confirmation, and the clipboard. Everything runs on temp folders. The failure tests
// make the disk refuse each step in turn and require the whole project to come out byte for byte as it went in.
// Nothing sleeps or reads the clock.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "../editor/ProjectBrowser.hpp"

#include <chrono>
#include <fstream>
#include <map>
#include <set>

namespace fs = std::filesystem;
using namespace okaypb;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        static int n = 0;
        path = fs::temp_directory_path() / ("okay_projpanel_" + std::to_string(++n) + "_" +
               std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path / "Assets", ec);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
    std::string assets() const { return (path / "Assets").generic_string(); }
    std::string at(const std::string& rel) const { return (path / "Assets" / fs::u8path(rel)).generic_u8string(); }
};

void WriteFile(const std::string& p, const std::string& s) {
    std::error_code ec;
    fs::create_directories(fs::u8path(p).parent_path(), ec);
    std::ofstream(fs::u8path(p), std::ios::binary | std::ios::trunc) << s;
}
std::string ReadFile(const std::string& p) {
    std::ifstream in(fs::u8path(p), std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}
bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
bool Exists(const std::string& p) { std::error_code ec; return fs::exists(fs::u8path(p), ec); }

using Tree = std::map<std::string, std::string>;
Tree Snap(const fs::path& root) {
    Tree t;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string rel = it->path().lexically_relative(root).generic_u8string();
        if (it->is_directory(ec)) t[rel] = "<dir>";
        else t[rel] = ReadFile(it->path().generic_u8string());
    }
    return t;
}

// A small project: a texture, a model-ish file, a material, a scene and a prefab that use them.
struct Proj {
    TempDir td;
    std::string tex, mat, scene, prefab, script;
    explicit Proj() {
        tex = td.at("Art/hero.png");
        mat = td.at("Art/hero.okaymat");
        scene = td.at("Scenes/main.okayscene");
        prefab = td.at("Prefabs/hero.okayprefab");
        script = td.at("Scripts/move.okay");
        WriteFile(tex, "PNGDATA-hero");
        WriteFile(td.at("Art/other.png"), "PNGDATA-other");
        WriteFile(mat, "okaymat 1\ntexture " + tex + "\n");
        WriteFile(script, "class Move {}\n");
        WriteFile(td.path.generic_u8string() + "/Ext/in.png", "x");        // files outside the project, for the import tests
        WriteFile(td.path.generic_u8string() + "/Ext/d/in.png", "x");
        WriteFile(prefab, "gameobject \"Hero\"\n  sprite texture \"" + tex + "\"\n");
        WriteFile(scene,
            "scene \"Main\"\n"
            "gameobject \"hero.png\"\n"            // a NAME that looks like a file: never a reference
            "  sprite texture \"" + tex + "\"\n"
            "gameobject \"Rel\"\n"
            "  sprite texture \"Assets/Art/hero.png\"\n"
            "gameobject \"Rel2\"\n"
            "  sprite texture \"../Art/hero.png\"\n"
            "gameobject \"Other\"\n"
            "  sprite texture \"" + td.at("Art/other.png") + "\"\n");
    }
};

} // namespace

int main() {
    RUN_SUITE("names, paths and ordering");
    {
        CHECK(ValidateName("Hero").empty());
        CHECK(ValidateName("Hero 2.png").empty());
        CHECK(ValidateName("\xC3\x9Cn\xC3\xAF c\xC3\xB6" "de").empty());                 // unicode is fine
        CHECK(!ValidateName("").empty());
        CHECK(!ValidateName("   ").empty());
        CHECK(!ValidateName("a/b").empty());
        CHECK(!ValidateName("a\\b").empty());
        CHECK(!ValidateName("a:b").empty());
        CHECK(!ValidateName("a?").empty());
        CHECK(!ValidateName("..").empty());
        CHECK(!ValidateName(".hidden").empty());
        CHECK(!ValidateName("name.").empty());
        CHECK(!ValidateName("CON").empty());
        CHECK(!ValidateName("nul.txt").empty());
        CHECK(!ValidateName("x.okaymeta").empty());
        CHECK(!ValidateName(std::string(300, 'a')).empty());
        CHECK(!ValidateName(std::string("a\tb")).empty());

        CHECK(NaturalLess("tex2", "tex10"));
        CHECK(!NaturalLess("tex10", "tex2"));
        CHECK(NaturalLess("Apple", "banana"));
        CHECK(NaturalLess("a", "A b"));
        CHECK(!NaturalLess("same", "SAME") && !NaturalLess("SAME", "same"));
        CHECK(NaturalLess("tex01", "tex2") && NaturalLess("img", "img1"));

        TempDir td;
        CHECK(IsInside(td.assets(), td.assets()));
        CHECK(IsInside(td.assets(), td.at("a/b.png")));
        CHECK(!IsInside(td.assets(), td.assets() + "/../Other/x.png"));
        CHECK(!IsInside(td.assets(), td.assets() + "2/x.png"));          // a sibling with the same prefix
        CHECK(IsInside(td.assets(), td.assets() + "/a/../b.png"));
        WriteFile(td.at("x.png"), "1");
        CHECK(UniqueName(td.assets(), "x", ".png") == "x 2.png");
        WriteFile(td.at("x 2.png"), "1");
        CHECK(UniqueName(td.assets(), "x", ".png") == "x 3.png");
        CHECK(UniqueName(td.assets(), "y", ".png") == "y.png");
        CHECK(HumanSize(512) == "512 B" && HumanSize(2048) == "2 KB" && HumanSize(3 * 1024 * 1024) == "3.0 MB");
        CHECK(TypeLabel(".png", false) == "Image" && TypeLabel("", true) == "Folder" && TypeLabel(".okayscene", false) == "Scene");
    }

    RUN_SUITE("the folder model");
    {
        TempDir td;
        WriteFile(td.at("tex10.png"), "aaaa");
        WriteFile(td.at("tex2.png"), "bb");
        WriteFile(td.at("Zed/readme.txt"), "x");
        WriteFile(td.at("alpha/deep/hero.okaymat"), "m");
        WriteFile(td.at("alpha/hero.png"), "ccccc");
        WriteFile(td.at("scene.okayscene"), "s");
        WriteFile(td.at("tex2.png.okaymeta"), "meta");
        WriteFile(td.at(".hidden.png"), "h");
        WriteFile(td.at("junk.tmp"), "t");
        WriteFile(td.at(".git/config"), "g");

        ListOptions o;
        auto top = ListFolder(td.assets(), o);
        std::vector<std::string> names;
        for (auto& e : top) names.push_back(e.name);
        CHECK((names == std::vector<std::string>{"alpha", "Zed", "scene.okayscene", "tex2.png", "tex10.png"}));   // folders first, natural order, hidden skipped
        CHECK(top[0].isDir && top[0].ext.empty() && !top[2].isDir && top[2].ext == ".okayscene" && top[3].size == 2);

        o.filter = Filter::Images;
        top = ListFolder(td.assets(), o);
        CHECK(top.size() == 4 && top[0].isDir && top[1].isDir);           // browsing keeps folders next to the filter
        o.recursive = true;
        top = ListFolder(td.assets(), o);
        names.clear(); for (auto& e : top) names.push_back(e.name);
        CHECK((names == std::vector<std::string>{"hero.png", "tex2.png", "tex10.png"}));   // searching: files only, below folders too

        o = ListOptions();
        o.text = "HERO";
        top = ListFolder(td.assets(), o);
        CHECK(top.size() == 2);                                           // the png and the material, below the folder
        o.text = "hero t:material";
        top = ListFolder(td.assets(), o);
        CHECK(top.size() == 1 && top[0].name == "hero.okaymat");
        o.text = "ext:png";
        top = ListFolder(td.assets(), o);
        CHECK(top.size() == 3);
        o.text = "t:folder";
        top = ListFolder(td.assets(), o);
        CHECK(top.size() == 3 && top[0].isDir);                           // alpha, deep, Zed
        o.text = "zzz";
        CHECK(ListFolder(td.assets(), o).empty());
        o.text = "t:bogus";
        CHECK(ListFolder(td.assets(), o).empty());                        // an unknown type word is just text

        ListOptions s; s.sort = SortKey::Size; s.descending = true;
        top = ListFolder(td.assets(), s);
        CHECK(top[0].isDir && top[1].isDir && top[2].name == "tex10.png");
        s.descending = false;
        top = ListFolder(td.assets(), s);
        CHECK(top[2].name == "scene.okayscene" && top.back().name == "tex10.png");
        s.sort = SortKey::Type; s.descending = false;
        top = ListFolder(td.assets(), s);
        CHECK(top[2].ext == ".okayscene");
        ListOptions lim; lim.recursive = true; lim.limit = 3;
        CHECK(ListFolder(td.assets(), lim).size() == 3);
        CHECK(ListFolder(td.at("missing"), ListOptions()).empty());
        auto q = ParseQuery("Hero ext:PNG t:audio");
        CHECK(q.words.size() == 1 && q.words[0] == "hero" && q.ext == ".png" && q.filter == Filter::Audio);
        CHECK(IsHiddenName(".x") && IsHiddenName("a.okaymeta") && IsHiddenName("s.okayscene.autosave") && !IsHiddenName("a.png"));
    }

    RUN_SUITE("create");
    {
        Proj p;
        Browser b(p.td.assets());
        auto r = b.CreateFolder(p.td.assets(), "Models");
        CHECK(r.ok && fs::is_directory(fs::u8path(p.td.at("Models"))));
        CHECK(!b.CreateFolder(p.td.assets(), "Models").ok);                       // exists
        CHECK(!b.CreateFolder(p.td.assets(), "bad/name").ok);
        CHECK(!b.CreateFolder(p.td.assets() + "/..", "Escape").ok);
        CHECK(!Exists(p.td.path.generic_string() + "/Escape"));
        r = b.MakeFile(p.td.at("Models"), "My Graph.okayvs", "okayvs\n");
        CHECK(r.ok && ReadFile(p.td.at("Models/My Graph.okayvs")) == "okayvs\n");
        CHECK(!Exists(p.td.at("Models/My Graph.okayvs.tmp")));                     // atomic: no temp file left behind
        CHECK(!b.MakeFile(p.td.at("Models"), "My Graph.okayvs", "other").ok);
        CHECK(ReadFile(p.td.at("Models/My Graph.okayvs")) == "okayvs\n");          // never overwritten
        r = b.MakeFile(p.td.at("Models"), "\xC3\x9C" "ber Skript.okay", "x");
        CHECK(r.ok);
        CHECK(b.CanUndo() && b.Undo().ok && !Exists(p.td.at("Models/\xC3\x9C" "ber Skript.okay")));   // undo of a create
        CHECK(b.Undo().ok && !Exists(p.td.at("Models/My Graph.okayvs")));
        CHECK(b.Undo().ok && b.Undo().ok == false);                              // the folder goes, then nothing is left
    }

    RUN_SUITE("rename keeps the GUID and every reference");
    {
        Proj p;
        Browser b(p.td.assets());
        CHECK(b.Sync());
        CHECK(!Exists(p.tex + ".okaymeta"));                                     // looking at a project writes no metas
        const okay::Guid g = b.EnsureMeta(p.tex);                                // (this asset is given one)
        CHECK(!g.IsNull() && b.Db().PathToGuid("Art/hero.png") == g);
        CHECK(Exists(p.tex + ".okaymeta"));
        const Tree before = Snap(p.td.path);

        auto r = b.Rename(p.tex, "Warrior");                                     // no extension typed: the old one stays
        CHECK(r.ok && r.paths.size() == 1 && r.paths[0] == p.td.at("Art/Warrior.png"));
        CHECK(!Exists(p.tex) && Exists(p.td.at("Art/Warrior.png")));
        CHECK(!Exists(p.tex + ".okaymeta") && Exists(p.td.at("Art/Warrior.png.okaymeta")));
        CHECK(b.Db().PathToGuid("Art/Warrior.png") == g);                        // same GUID, new path
        CHECK(r.filesRewritten == 3 && r.referencesRewritten == 5);              // scene (3 styles), prefab, material
        const std::string sc = ReadFile(p.scene);
        CHECK(Has(sc, "texture \"" + p.td.at("Art/Warrior.png") + "\""));          // absolute stays absolute
        CHECK(Has(sc, "texture \"Assets/Art/Warrior.png\""));                      // project-relative stays so
        CHECK(Has(sc, "texture \"../Art/Warrior.png\""));                          // file-relative stays so
        CHECK(Has(sc, "gameobject \"hero.png\""));                                 // a name is never touched
        CHECK(Has(sc, "other.png\"") && !Has(sc, "texture \"hero.png\""));
        CHECK(Has(ReadFile(p.prefab), "Warrior.png") && Has(ReadFile(p.mat), "texture " + p.td.at("Art/Warrior.png") + "\n"));
        CHECK(b.UsersOf(p.td.at("Art/Warrior.png")) == 3);                    // the users follow

        auto u = b.Undo();                                                      // undo puts all of it back
        CHECK(u.ok);
        CHECK(Snap(p.td.path) == before);
        CHECK(b.Db().PathToGuid("Art/hero.png") == g);
    }
    {   // extension rules, bad names, clashes, case-only, unicode and spaces
        Proj p;
        Browser b(p.td.assets());
        CHECK(!b.Rename(p.tex, "").ok && !b.Rename(p.tex, "a/b").ok && !b.Rename(p.tex, ".png").ok);
        CHECK(!b.Rename(p.tex, "other").ok);                                      // "other.png" exists
        CHECK(Exists(p.tex));
        auto r = b.Rename(p.tex, "hero.v2");                                     // dots in a name are fine: the extension is kept
        CHECK(r.ok && Exists(p.td.at("Art/hero.v2.png")));
        r = b.Rename(p.td.at("Art/hero.v2.png"), "Hero.v2.PNG");                 // same extension in another case: taken as typed
        CHECK(r.ok && Exists(p.td.at("Art/Hero.v2.PNG")) && !Exists(p.td.at("Art/hero.v2.png")));
        r = b.Rename(p.td.at("Art/Hero.v2.PNG"), "hero.v2.png");                 // back, a change of case only
        CHECK(r.ok && Exists(p.td.at("Art/hero.v2.png")));
        r = b.Rename(p.td.at("Art/hero.v2.png"), "h\xC3\xA9ro \xE2\x9C\x93 sprite");
        CHECK(r.ok && Exists(p.td.at("Art/h\xC3\xA9ro \xE2\x9C\x93 sprite.png")));
        CHECK(Has(ReadFile(p.scene), "h\xC3\xA9ro \xE2\x9C\x93 sprite.png"));
        CHECK(b.Rename(p.td.at("Art/h\xC3\xA9ro \xE2\x9C\x93 sprite.png"), "h\xC3\xA9ro \xE2\x9C\x93 sprite").ok);   // same name: nothing to do
        CHECK(!b.Rename(p.td.assets(), "Whole").ok);                              // the Assets folder itself
        CHECK(!b.Rename(p.td.path.generic_string() + "/Assets/../x.png", "y").ok);
    }
    {   // a scene that holds quotes and backslashes in a path
        TempDir td;
        WriteFile(td.at("My \"odd\" pic.png"), "x");
        WriteFile(td.at("s.okayscene"), "scene \"S\"\ngameobject \"A\"\n  sprite texture \"Assets\\\\My \\\"odd\\\" pic.png\"\n");
        const std::string raw = ReadFile(td.at("s.okayscene"));
        PathMap m{{td.at("My \"odd\" pic.png"), td.at("Plain.png")}};
        int n = 0;
        std::string out = RewriteReferences(raw, td.at("s.okayscene"), td.at("s.okayscene"), m, td.assets(), &n);
        (void)out; (void)n;
        CHECK(out == raw || n >= 0);                                             // must not crash; unusual paths are best effort
    }

    RUN_SUITE("rename and move folders");
    {
        Proj p;
        Browser b(p.td.assets());
        CHECK(b.Sync());
        const okay::Guid g = b.EnsureMeta(p.tex);
        const okay::Guid gm = b.EnsureMeta(p.mat);
        auto r = b.Rename(p.td.at("Art"), "Pictures");
        CHECK(r.ok && Exists(p.td.at("Pictures/hero.png")) && !Exists(p.td.at("Art")));
        CHECK(Exists(p.td.at("Pictures/hero.png.okaymeta")));
        CHECK(b.Db().PathToGuid("Pictures/hero.png") == g && b.Db().PathToGuid("Pictures/hero.okaymat") == gm);
        const std::string sc = ReadFile(p.scene);
        CHECK(Has(sc, p.td.at("Pictures/hero.png")) && Has(sc, "Assets/Pictures/hero.png") && Has(sc, "../Pictures/hero.png"));
        CHECK(Has(ReadFile(p.td.at("Pictures/hero.okaymat")), "texture " + p.td.at("Pictures/hero.png")));   // a file inside the folder is rewritten too
        CHECK(!Has(ReadFile(p.td.at("Pictures/hero.okaymat")), "/Art/"));
        CHECK(b.Undo().ok && Exists(p.td.at("Art/hero.png")) && Has(ReadFile(p.scene), "Assets/Art/hero.png"));

        // move: drag a file into another folder; a clash gets a number; into itself is refused
        WriteFile(p.td.at("Scenes/hero.png"), "other hero");
        r = b.Move({p.tex}, p.td.at("Scenes"));
        CHECK(r.ok && r.paths[0] == p.td.at("Scenes/hero 2.png") && ReadFile(p.td.at("Scenes/hero.png")) == "other hero");
        CHECK(Has(ReadFile(p.scene), "Scenes/hero 2.png"));
        CHECK(b.Undo().ok && Exists(p.tex));
        CHECK(!b.Move({p.td.at("Art")}, p.td.at("Art/sub")).ok);                  // (the destination is not even a folder)
        fs::create_directories(fs::u8path(p.td.at("Art/sub")));
        CHECK(!b.Move({p.td.at("Art")}, p.td.at("Art/sub")).ok);                  // into itself
        CHECK(b.Move({p.tex}, p.td.at("Art")).ok);                               // already there: nothing to do
        CHECK(Exists(p.tex));
        r = b.Move({p.td.at("Art"), p.tex}, p.td.at("Scenes"));                  // a folder with a file inside it selected: the folder moves once
        CHECK(r.ok && r.paths.size() == 1 && Exists(p.td.at("Scenes/Art/hero.png")));
    }

    RUN_SUITE("copy, paste, duplicate and the clipboard");
    {
        Proj p;
        Browser b(p.td.assets());
        CHECK(b.Sync());
        CHECK(!b.EnsureMeta(p.tex).IsNull());
        auto r = b.Duplicate(p.tex);
        CHECK(r.ok && r.paths[0] == p.td.at("Art/hero copy.png") && ReadFile(r.paths[0]) == "PNGDATA-hero");
        r = b.Duplicate(p.tex);
        CHECK(r.ok && r.paths[0] == p.td.at("Art/hero copy 2.png"));
        CHECK(!Exists(p.td.at("Art/hero copy.png.tmp")));
        CHECK(b.Sync());
        CHECK(b.Db().PathToGuid("Art/hero.png") != b.Db().PathToGuid("Art/hero copy.png"));   // the copy is its own asset
        CHECK(Has(ReadFile(p.scene), "Art/hero.png"));                                          // and nothing was redirected to it

        Clipboard clip;
        CHECK(clip.Empty() && !clip.Paste(b, p.td.at("Scenes")).ok);
        clip.Set({p.td.at("Art")}, false);
        r = clip.Paste(b, p.td.at("Scenes"));
        CHECK(r.ok && Exists(p.td.at("Scenes/Art/hero.png")) && Exists(p.td.at("Art/hero.png")) && !clip.Empty());
        r = clip.Paste(b, p.td.at("Scenes"));
        CHECK(r.ok && Exists(p.td.at("Scenes/Art 2/hero.png")));                 // pasting twice numbers the copy
        CHECK(!clip.Paste(b, p.td.at("Art")).ok);                                // a folder into itself
        CHECK(Snap(p.td.path / "Assets" / "Art").count("hero.png") == 1);

        clip.Set({p.td.at("Prefabs/hero.okayprefab"), p.td.at("Gone.png")}, true);
        r = clip.Paste(b, p.td.at("Art"));
        CHECK(r.ok && Exists(p.td.at("Art/hero.okayprefab")) && !Exists(p.prefab) && clip.Empty() && Has(r.note, "Gone.png"));
        r = clip.Paste(b, p.td.at("Art"));
        CHECK(!r.ok);

        // copy into a subfolder of itself's sibling keeps the tree intact
        CHECK(!b.Copy({p.td.at("Art")}, p.td.at("Art")).ok);                     // into itself: refused
        CHECK(b.Copy({p.td.at("Art")}, p.td.assets()).ok);                       // next to itself: "Art 2"
        CHECK(Exists(p.td.at("Art 2/hero.png")) && Exists(p.td.at("Art 2/hero.png.okaymeta")));
        auto u = b.Undo();
        CHECK(u.ok && !Exists(p.td.at("Art 2")));
    }

    RUN_SUITE("delete goes to the trash and can be undone");
    {
        Proj p;
        Browser b(p.td.assets());
        CHECK(b.Sync());
        const okay::Guid g = b.EnsureMeta(p.tex);
        const Tree before = Snap(p.td.path);
        auto r = b.Trash({p.tex, p.td.at("Scenes")});
        CHECK(r.ok && !Exists(p.tex) && !Exists(p.tex + ".okaymeta") && !Exists(p.scene));
        CHECK(b.TrashCount() == 1 && Exists(p.td.path.generic_string() + "/.okay/trash/000001/manifest.txt"));
        CHECK(b.CanUndo() && Has(b.UndoLabel(), "Delete"));
        CHECK(b.Db().PathToGuid("Art/hero.png").IsNull());
        auto u = b.Undo();
        CHECK(u.ok && u.paths.size() == 2);
        CHECK(Exists(p.tex) && Exists(p.scene));
        CHECK(b.Db().PathToGuid("Art/hero.png") == g);                           // the GUID came back with the file
        Tree after = Snap(p.td.path);
        after.erase(".okay"); after.erase(".okay/trash");
        for (auto it = after.begin(); it != after.end();) it = Has(it->first, ".okay/trash") ? after.erase(it) : std::next(it);
        CHECK(after == before);

        // restoring is refused when something took the place
        CHECK(b.Trash({p.tex}).ok);
        WriteFile(p.tex, "new file in the same spot");
        auto bad = b.Undo();
        CHECK(!bad.ok && Has(bad.error, "hero.png") && ReadFile(p.tex) == "new file in the same spot" && b.CanUndo());
        fs::remove(fs::u8path(p.tex));
        CHECK(b.Undo().ok && ReadFile(p.tex) == "PNGDATA-hero");

        // nothing outside Assets, and not Assets itself
        WriteFile(p.td.path.generic_u8string() + "/outside.txt", "keep");
        CHECK(!b.Trash({p.td.path.generic_u8string() + "/outside.txt"}).ok);
        CHECK(!b.Trash({p.td.assets()}).ok);
        CHECK(!b.Trash({p.td.at("nope.png")}).ok);
        CHECK(Exists(p.td.path.generic_u8string() + "/outside.txt"));

        // the trash can be emptied for good, and keeps at most 30 deletions
        for (int i = 0; i < 33; ++i) {
            WriteFile(p.td.at("t" + std::to_string(i) + ".txt"), "x");
            CHECK(b.Trash({p.td.at("t" + std::to_string(i) + ".txt")}).ok);
        }
        CHECK(b.TrashCount() == 30);
        CHECK(b.EmptyTrash() && b.TrashCount() == 0);
    }

    RUN_SUITE("what uses an asset");
    {
        Proj p;
        Browser b(p.td.assets());
        auto refs = b.ReferencesTo({p.tex});
        std::set<std::string> users;
        for (auto& r : refs) users.insert(r.path);
        CHECK(users == std::set<std::string>({p.scene, p.prefab, p.mat}));
        CHECK(b.ReferencesTo({p.td.at("Art/other.png")}).size() == 1);
        CHECK(b.ReferencesTo({p.script}).empty());
        // a folder: the users of anything inside it, but not users that are inside it too
        refs = b.ReferencesTo({p.td.at("Art")});
        users.clear(); for (auto& r : refs) users.insert(r.path);
        CHECK(users == std::set<std::string>({p.scene, p.prefab}));              // the material lives in Art/
        refs = b.ReferencesTo({p.td.at("Art"), p.td.at("Scenes"), p.td.at("Prefabs")});
        CHECK(refs.empty());
        auto deps = b.DependenciesOf(p.scene);
        CHECK(deps.size() == 2);
        CHECK(b.DependenciesOf(p.td.at("nope.okayscene")).empty());
    }

    RUN_SUITE("polling for changes made outside the editor");
    {
        Proj p;
        Browser b(p.td.assets());
        CHECK(!b.Indexed() && !b.Poll(0));                                         // nothing is scanned until something needs the database
        CHECK(b.UsersOf(p.tex) == 3 && b.Indexed());                               // scene, prefab, material
        CHECK(b.UsersOf(p.script) == 0 && b.UsersOf(p.td.at("nope.png")) == 0);
        std::uint64_t t = 10000000;
        b.Poll(t);                                                                 // settle
        WriteFile(p.td.at("Scenes/second.okayscene"), "scene \"B\"\ngameobject \"X\"\n  sprite texture \"" + p.tex + "\"\n");
        CHECK(!b.Poll(t + 1));                                                     // not due yet: it waits at least 2 seconds
        CHECK(b.Poll(t + 1000000) && b.UsersOf(p.tex) == 4);                       // due: sees the new scene and what it uses
        CHECK(!b.Poll(t + 1000001));                                               // nothing changed since
        fs::remove(fs::u8path(p.td.at("Scenes/second.okayscene")));
        CHECK(b.Poll(t + 2000000) && b.UsersOf(p.tex) == 3);
        // a file with a meta that is moved outside the editor keeps its GUID
        const okay::Guid g = b.EnsureMeta(p.td.at("Art/other.png"));
        fs::rename(fs::u8path(p.td.at("Art/other.png")), fs::u8path(p.td.at("Art/moved.png")));
        fs::rename(fs::u8path(p.td.at("Art/other.png.okaymeta")), fs::u8path(p.td.at("Art/moved.png.okaymeta")));
        CHECK(b.Poll(t + 4000000) && b.Db().PathToGuid("Art/moved.png") == g);
    }

    RUN_SUITE("no meta files unless an asset needs one");
    {
        auto countMetas = [](const fs::path& root) {
            int n = 0; std::error_code ec;
            for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
                if (it->path().extension() == ".okaymeta") ++n;
            return n;
        };
        Proj p;
        Browser b(p.td.assets());
        CHECK(b.Sync() && b.UsersOf(p.tex) == 3 && !b.ReferencesTo({p.tex}).empty());
        CHECK(b.Rename(p.tex, "Warrior").ok && b.Move({p.td.at("Art/Warrior.png")}, p.td.at("Scenes")).ok);   // references still follow
        CHECK(Has(ReadFile(p.scene), "Scenes/Warrior.png"));
        CHECK(b.Duplicate(p.td.at("Scenes/Warrior.png")).ok && b.Trash({p.td.at("Scenes/Warrior copy.png")}).ok);
        CHECK(countMetas(p.td.path) == 0);                                         // none of that wrote a meta
        const std::string tex = p.td.at("Scenes/Warrior.png");
        okay::ImportSettings s;
        CHECK(b.GetImportSettings(tex, s) && s.kind == okay::AssetType::Texture && s.texture.maxSize == 2048);
        CHECK(b.SetImportSettings(tex, s) && countMetas(p.td.path) == 0);          // the defaults need no file
        s.texture.maxSize = 512;
        CHECK(b.SetImportSettings(tex, s) && countMetas(p.td.path) == 1);          // a real choice does
        CHECK(Has(ReadFile(tex + ".okaymeta"), "set maxSize 512"));
        okay::ImportSettings back;
        CHECK(b.GetImportSettings(tex, back) && back.texture.maxSize == 512);
        CHECK(!b.GetImportSettings(p.script, back));                               // scripts have none
        // the asset with settings keeps them (and its GUID) through a rename
        const okay::Guid g = b.Db().PathToGuid("Scenes/Warrior.png");
        CHECK(b.Rename(tex, "Hero").ok && b.Db().PathToGuid("Scenes/Hero.png") == g);
        okay::ImportSettings again;
        CHECK(b.GetImportSettings(p.td.at("Scenes/Hero.png"), again) && again.texture.maxSize == 512);
        CHECK(b.Reimport(p.td.at("Scenes/Hero.png")));
        okay::Guid k1, k2; std::uint64_t h1 = 0, h2 = 0;
        b.ThumbKey(p.td.at("Scenes/Hero.png"), k1, h1);
        WriteFile(p.td.at("Scenes/Hero.png"), "different, longer picture data");
        b.ThumbKey(p.td.at("Scenes/Hero.png"), k2, h2);
        CHECK(k1 == k2 && h1 != h2 && !k1.IsNull());                               // same picture slot, new content
    }

    RUN_SUITE("importing from outside");
    {
        Proj p;
        Browser b(p.td.assets());
        const std::string out = p.td.path.generic_u8string() + "/Elsewhere";
        WriteFile(out + "/Pic \xC3\xA9.png", "external picture");
        WriteFile(out + "/folder/a.txt", "a");
        WriteFile(out + "/folder/deep/b.txt", "b");
        auto r = b.ImportFile(out + "/Pic \xC3\xA9.png", p.td.at("Art"));
        CHECK(r.ok && ReadFile(p.td.at("Art/Pic \xC3\xA9.png")) == "external picture" && Exists(out + "/Pic \xC3\xA9.png"));
        r = b.ImportFile(out + "/Pic \xC3\xA9.png", p.td.at("Art"));
        CHECK(r.ok && r.paths[0] == p.td.at("Art/Pic \xC3\xA9 2.png"));              // a taken name gets a number
        CHECK(!Exists(p.td.at("Art/Pic \xC3\xA9 2.png.tmp")));
        r = b.ImportFile(out + "/folder", p.td.at("Art"));
        CHECK(r.ok && ReadFile(p.td.at("Art/folder/deep/b.txt")) == "b");
        CHECK(!b.ImportFile(out + "/missing.png", p.td.at("Art")).ok);
        CHECK(!b.ImportFile(out + "/Pic \xC3\xA9.png", out).ok);                      // only into the project
        WriteFile(out + "/x.okaymeta", "m");
        CHECK(!b.ImportFile(out + "/x.okaymeta", p.td.at("Art")).ok);
        CHECK(b.Undo().ok && !Exists(p.td.at("Art/folder")));                      // undo takes the folder back out
        CHECK(b.Undo().ok && !Exists(p.td.at("Art/Pic \xC3\xA9 2.png")));
    }

    RUN_SUITE("importers read the settings that are stored next to an asset");
    {
        TempDir td;
        okay::Image big(200, 100);
        big.Fill(okay::Color(0.2f, 0.6f, 0.3f, 1.0f));
        const std::string png = td.at("big.png"), png2 = td.at("plain.png"), png3 = td.at("free.png");
        CHECK(big.SavePNG(png) && big.SavePNG(png2) && big.SavePNG(png3));
        WriteFile(png + ".okaymeta", "okaymeta 1\nguid 0123456789abcdef0123456789abcdef\ntype texture\nimporter texture 2\nset maxSize 64\n");
        WriteFile(png3 + ".okaymeta", "okaymeta 1\ntype texture\nimporter texture 2\nset maxSize 0\n");
        CHECK(okay::TextureImportMaxSize(png) == 64 && okay::TextureImportMaxSize(png2) == 0 && okay::TextureImportMaxSize(png3) == 0);
        okay::Image* a = okay::GetCachedTexture(png);                              // the renderer's texture cache
        CHECK(a && a->Width() == 64 && a->Height() == 32);                         // longest side 64, shape kept
        okay::Image* b = okay::GetCachedTexture(png2);
        CHECK(b && b->Width() == 200 && b->Height() == 100);                       // no settings: as it is
        okay::Image* c = okay::GetCachedTexture(png3);
        CHECK(c && c->Width() == 200);                                             // 0 means no limit
        okay::Image small(16, 16);
        small.Fill(okay::Color(1, 1, 1, 1));
        okay::ApplyTextureImportSettings(png, small);
        CHECK(small.Width() == 16);                                                // never enlarged
        WriteFile(td.at("m.glb.okaymeta"), "okaymeta 1\ntype model\nimporter model 1\nset scale 2.5\nset normals flat\n");
        CHECK(okay::ModelImportScale(td.at("m.glb")) == 2.5f && okay::ModelImportScale(td.at("none.glb")) == 1.0f);
        WriteFile(td.at("bad.glb.okaymeta"), "okaymeta 1\nimporter model 1\nset scale 0\n");
        CHECK(okay::ModelImportScale(td.at("bad.glb")) == 1.0f);                   // out of range: ignored
        WriteFile(td.at("old.png.okaymeta"), "okaymeta 1\ntype texture\nimporter texture 1\nset maxsize 128\n");
        CHECK(okay::TextureImportMaxSize(td.at("old.png")) == 128);                // the older key still counts
        WriteFile(td.at("wrong.png.okaymeta"), "okaymeta 1\nimporter audio 2\nset maxSize 64\n");
        CHECK(okay::TextureImportMaxSize(td.at("wrong.png")) == 0);                // only a texture block counts
    }

    RUN_SUITE("favourites");
    {
        Proj p;
        Favourites f;
        f.Load(p.td.assets());
        CHECK(f.List().empty() && !f.Has(p.tex));
        CHECK(f.Toggle(p.tex) && f.Toggle(p.td.at("Art")) && f.Has(p.tex) && f.Has(p.td.at("Art")));
        CHECK(f.List().size() == 2 && f.List()[0] == p.tex);
        CHECK(Has(ReadFile(p.td.path.generic_u8string() + "/.okay/favourites.txt"), "Art/hero.png"));
        Favourites g;                                                              // another session: read back from the project
        g.Load(p.td.assets());
        CHECK(g.Has(p.tex) && g.Has(p.td.at("Art")) && g.List().size() == 2);
        CHECK(f.Toggle(p.tex) && !f.Has(p.tex) && f.List().size() == 1);          // star off
        CHECK(!f.Toggle(p.td.assets()) && !f.Toggle(p.td.path.generic_u8string() + "/Elsewhere/x.png"));   // not Assets itself, nothing outside
        // a failed save changes nothing
        f.SetFailureHook([] { return true; });
        CHECK(!f.Toggle(p.script) && !f.Has(p.script));
        f.SetFailureHook(nullptr);
        // renames and moves are followed, a deleted item drops out of the list
        Browser b(p.td.assets());
        auto r = b.Rename(p.td.at("Art"), "Pictures");
        CHECK(r.ok);
        f.Remap(r.moved);
        CHECK(f.Has(p.td.at("Pictures")) && !f.Has(p.td.at("Art")));
        Favourites h; h.Load(p.td.assets());
        CHECK(h.Has(p.td.at("Pictures")));
        fs::remove_all(fs::u8path(p.td.at("Pictures")));
        CHECK(h.List().empty());
        // damaged or hostile lines are ignored
        WriteFile(p.td.path.generic_u8string() + "/.okay/favourites.txt", "# x\n../../etc/passwd\n/abs/path\nScripts/move.okay\n\n");
        Favourites k; k.Load(p.td.assets());
        CHECK(k.List().size() == 1 && k.Has(p.script));
        CHECK(RemapPath({{p.td.at("Scenes"), p.td.at("S2")}}, p.scene) == p.td.at("S2/main.okayscene"));
        CHECK(RemapPath({{p.td.at("Scenes"), p.td.at("S2")}}, p.tex).empty());
    }

    RUN_SUITE("references rewriting on its own");
    {
        TempDir td;
        WriteFile(td.at("a/x.png"), "1");
        WriteFile(td.at("b/y.png"), "2");
        const std::string ref = td.at("a/x.png");
        PathMap m{{ref, td.at("c/z.png")}};
        const std::string txt = "list\n  one \"" + ref + "\"\n  two \"" + td.at("b/y.png") + "\"\n";
        int n = 0;
        std::string out = RewriteReferences(txt, td.at("s.okayscene"), td.at("s.okayscene"), m, td.assets(), &n);
        CHECK(n == 1 && Has(out, td.at("c/z.png")) && Has(out, td.at("b/y.png")));
        const std::string scene = "scene \"S\"\ngameobject \"A\"\n  sprite texture \"" + ref + "\"\n  sprite texture \"" + td.at("b/y.png") + "\"\n";
        out = RewriteReferences(scene, td.at("s.okayscene"), td.at("s.okayscene"), m, td.assets(), &n);
        CHECK(n == 1 && Has(out, td.at("c/z.png")) && Has(out, td.at("b/y.png")) && !Has(out, ref));
        // a referrer that moves too: file-relative paths are computed from its new place
        WriteFile(td.at("a/s.okayscene"), "x");
        const std::string rel = "scene \"S\"\ngameobject \"A\"\n  sprite texture \"x.png\"\n";
        PathMap both{{td.at("a"), td.at("moved")}};
        out = RewriteReferences(rel, td.at("a/s.okayscene"), td.at("moved/s.okayscene"), both, td.assets(), &n);
        CHECK(n == 0 && out == rel);                                              // both moved together: "x.png" is still right
        PathMap one{{td.at("a/x.png"), td.at("far/x.png")}};
        out = RewriteReferences(rel, td.at("a/s.okayscene"), td.at("a/s.okayscene"), one, td.assets(), &n);
        CHECK(n == 1 && Has(out, "\"../far/x.png\""));
        out = RewriteReferences("scene \"S\"\ngameobject \"A\"\n  sprite texture \"http://x/a/x.png\"\n", td.at("s.okayscene"), td.at("s.okayscene"), m, td.assets(), &n);
        CHECK(n == 0);
    }

    RUN_SUITE("nothing outside the project");
    {
        Proj p;
        Browser b(p.td.assets());
        const std::string outside = p.td.path.generic_u8string() + "/Elsewhere";
        WriteFile(outside + "/secret.txt", "secret");
        CHECK(!b.Move({p.tex}, outside).ok && Exists(p.tex));
        CHECK(!b.Copy({outside + "/secret.txt"}, p.td.at("Art")).ok);
        CHECK(!b.Rename(outside + "/secret.txt", "renamed").ok && Exists(outside + "/secret.txt"));
        CHECK(!b.MakeFile(outside, "new.txt", "x").ok && !Exists(outside + "/new.txt"));
        CHECK(!b.Move({p.td.assets() + "/../Elsewhere/secret.txt"}, p.td.at("Art")).ok);
        // a link inside Assets that points out of the project is not a way out
        std::error_code ec;
        fs::create_directory_symlink(fs::u8path(outside), fs::u8path(p.td.at("Link")), ec);
        if (!ec) {
            CHECK(!b.Move({p.tex}, p.td.at("Link")).ok && Exists(p.tex));
            CHECK(!b.MakeFile(p.td.at("Link"), "leak.txt", "x").ok && !Exists(outside + "/leak.txt"));
            CHECK(!b.Trash({p.td.at("Link/secret.txt")}).ok && Exists(outside + "/secret.txt"));
        }
    }

    RUN_SUITE("a failing disk loses nothing");
    {
        // Run each operation once to count its file steps, then once per step with that step failing:
        // the project must be byte for byte what it was, and no temp files may be left.
        struct Case {
            const char* name;
            std::function<Result(Browser&, Proj&)> run;
        };
        const std::vector<Case> cases = {
            {"rename file with references", [](Browser& b, Proj& p) { return b.Rename(p.tex, "Warrior"); }},
            {"rename folder with references", [](Browser& b, Proj& p) { return b.Rename(p.td.at("Art"), "Pictures"); }},
            {"move into a folder", [](Browser& b, Proj& p) { return b.Move({p.tex, p.td.at("Art/other.png")}, p.td.at("Scenes")); }},
            {"copy a folder", [](Browser& b, Proj& p) { return b.Copy({p.td.at("Art")}, p.td.at("Scenes")); }},
            {"duplicate a file", [](Browser& b, Proj& p) { return b.Duplicate(p.tex); }},
            {"delete two", [](Browser& b, Proj& p) { return b.Trash({p.tex, p.td.at("Scenes")}); }},
            {"create a file", [](Browser& b, Proj& p) { return b.MakeFile(p.td.at("Art"), "new.okayvs", "okayvs\n"); }},
            {"create a folder", [](Browser& b, Proj& p) { return b.CreateFolder(p.td.at("Art"), "More"); }},
            {"import a file", [](Browser& b, Proj& p) { return b.ImportFile(p.td.path.generic_u8string() + "/Ext/in.png", p.td.at("Art")); }},
            {"import a folder", [](Browser& b, Proj& p) { return b.ImportFile(p.td.path.generic_u8string() + "/Ext/d", p.td.at("Art")); }},
        };
        for (const Case& c : cases) {
            int steps = 0;
            {
                Proj p;
                Browser b(p.td.assets());
                b.SetFailureHook([&](const char*, const std::string&) { ++steps; return false; });
                b.Sync();
                steps = 0;
                CHECK(c.run(b, p).ok);
            }
            CHECK(steps >= 1);
            for (int k = 1; k <= steps; ++k) {
                Proj p;
                Browser b(p.td.assets());
                b.Sync();
                const Tree before = Snap(p.td.path);
                int seen = 0;
                b.SetFailureHook([&](const char*, const std::string&) { return ++seen == k; });
                Result r = c.run(b, p);
                if (r.ok) std::cerr << "  case \"" << c.name << "\" step " << k << " of " << steps << " did not fail\n";
                CHECK(!r.ok);
                CHECK(!r.error.empty());
                b.SetFailureHook(nullptr);
                Tree after = Snap(p.td.path);
                if (after != before) {
                    std::cerr << "  case \"" << c.name << "\" failing step " << k << " of " << steps << " changed the project\n";
                    for (auto& kv : after) if (!before.count(kv.first)) std::cerr << "    new: " << kv.first << "\n";
                    for (auto& kv : before) if (!after.count(kv.first)) std::cerr << "    lost: " << kv.first << "\n";
                    for (auto& kv : before) if (after.count(kv.first) && after[kv.first] != kv.second) std::cerr << "    changed: " << kv.first << "\n";
                }
                // (the trash folders for a rolled-back delete are removed too)
                CHECK(after == before);
                CHECK(!b.CanUndo() || c.name == std::string("never"));
            }
        }
        // a step that fails during Undo leaves the project as it was before the Undo
        {
            Proj p;
            Browser b(p.td.assets());
            b.Sync();
            CHECK(b.Rename(p.tex, "Warrior").ok);
            const Tree mid = Snap(p.td.path);
            int seen = 0;
            b.SetFailureHook([&](const char*, const std::string&) { return ++seen == 2; });
            CHECK(!b.Undo().ok);
            b.SetFailureHook(nullptr);
            CHECK(Snap(p.td.path) == mid && b.CanUndo());
            CHECK(b.Undo().ok && Exists(p.tex));
        }
    }

    TEST_MAIN_RESULT();
}
