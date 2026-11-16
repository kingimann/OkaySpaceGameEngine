// What the Project panel's new right-click items do on a real folder (temp folders, no ImGui):
//   * the files the Create menu makes for the story and AI windows (a dialogue, a quest file, a behavior tree, a cutscene):
//     the text reads back with the engine's own parsers and checks clean, is written atomically and never over a file,
//   * Reimport All in Folder (Browser::ReimportFolder) and the numbers the Properties window shows (Browser::StatsOf,
//     AssetInfo::modified),
//   * the safety of the file operations the menu reaches: Duplicate on a whole selection (all or nothing, one undo, an item
//     inside another selected one is not copied twice), Standard Folders (CreateFolders: inside Assets, all or nothing, one
//     undo), Paste twice, Move to... with a name that is taken, and that the picker's rule (ProjectContextModel.hpp) says
//     "no" exactly where Browser::Move refuses,
//   * which types are text (a script file is not: .js, .html), and the tags LabelsOf reads for a menu.
// Nothing sleeps and no threads are used.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "../editor/ProjectBrowser.hpp"
#include "../editor/ProjectContextModel.hpp"
#include "../editor/ProjectNewAssets.hpp"

#include <chrono>
#include <fstream>
#include <map>

namespace fs = std::filesystem;
using namespace okaypb;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        static int n = 0;
        path = fs::temp_directory_path() / ("okay_ctxops_" + std::to_string(++n) + "_" +
               std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path / "Assets", ec);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
    std::string assets() const { return (path / "Assets").generic_u8string(); }
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
bool Exists(const std::string& p) { std::error_code ec; return fs::exists(fs::u8path(p), ec); }
bool Contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
using Tree = std::map<std::string, std::string>;
Tree Snap(const fs::path& root) {
    Tree t;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string rel = it->path().lexically_relative(root).generic_u8string();
        t[rel] = it->is_directory(ec) ? std::string("<dir>") : ReadFile(it->path().generic_u8string());
    }
    return t;
}

} // namespace

int main() {
    RUN_SUITE("project_menu_ops");

    // ---- which menu rules apply to a type ----
    {
        using K = ctx::Kind;
        const struct { const char* path; K kind; } table[] = {
            {"a/Hero.png", K::Image}, {"a/Hero.PNG", K::Image}, {"a/p.jpeg", K::Image}, {"a/s.wav", K::Audio}, {"a/m.ogg", K::Audio},
            {"a/m.fbx", K::Model}, {"a/m.obj", K::Model}, {"a/m.glb", K::Model}, {"a/m.okaymesh", K::Model},
            {"a/l.okayscene", K::Scene}, {"a/p.okayprefab", K::Prefab}, {"a/m.okaymat", K::Material},
            {"a/s.cs", K::Script}, {"a/s.lua", K::Script}, {"a/s.okay", K::Script}, {"a/g.okayvs", K::VisualScript},
            {"a/d.okaydata", K::Data}, {"a/d.okaydialog", K::Data}, {"a/q.okayquest", K::Data}, {"a/b.okaybt", K::Data},
            {"a/c.okayseq", K::Data}, {"a/c.okayclip", K::Data}, {"a/x.json", K::Data}, {"a/x.txt", K::Data}, {"a/x.md", K::Data},
            {"a/blob.bin", K::Other}, {"a/f.ttf", K::Other}, {"a/z.zip", K::Other}, {"a/noext", K::Other}, {"a/.hidden", K::Other},
            // a script or web page is not a text asset: Windows runs a .js, and a page is not edited here
            {"a/x.js", K::Other}, {"a/x.JS", K::Other}, {"a/x.html", K::Other}, {"a/x.htm", K::Other}, {"a/x.bat", K::Other}, {"a/x.ps1", K::Other},
            {"a/x.vbs", K::Other}, {"a/x.hta", K::Other}, {"a/x.exe", K::Other}, {"a/x.sh", K::Other}, {"a/x.css", K::Data}, {"a/x.csv", K::Data},
        };
        for (const auto& t : table) {
            if (MenuKind(t.path, false) != t.kind) std::cerr << "  MenuKind(" << t.path << ") gave " << (int)MenuKind(t.path, false) << ", wanted " << (int)t.kind << "\n";
            CHECK(MenuKind(t.path, false) == t.kind);
        }
        CHECK(MenuKind("a/Textures", true) == K::Folder && MenuKind("a/Assets.v2", true) == K::Folder && MenuKind("a/x.png", true) == K::Folder);
        // text kinds are the ones "Open in External Editor" is offered for, and never a type that runs code
        ctx::State st;
        for (const auto& t : table) {
            st.kind = MenuKind(t.path, false);
            st.runsCode = ctx::RunsCode(t.path);
            const bool offered = ctx::Find(ctx::ItemMenu(st), "open:external") != nullptr;
            const bool text = st.kind == K::Script || st.kind == K::VisualScript || st.kind == K::Material || st.kind == K::Data || st.kind == K::Scene || st.kind == K::Prefab;
            CHECK(offered == (text && !st.runsCode));
            if (st.runsCode) { const ctx::Item* d = ctx::Find(ctx::ItemMenu(st), "open:default"); CHECK(d && !d->enabled); }   // a program or script is never handed to the system
        }
        // .js is not text-openable, whichever way the kind is worked out
        {
            ctx::State js; js.kind = MenuKind("a/b.js", false); js.runsCode = ctx::RunsCode("a/b.js");
            CHECK(!ctx::Find(ctx::ItemMenu(js), "open:external"));
            ctx::State forced; forced.kind = K::Data; forced.runsCode = ctx::RunsCode("a/b.js");   // (even if a table said Data)
            CHECK(!ctx::Find(ctx::ItemMenu(forced), "open:external"));
        }
    }

    // ---- the files the Create menu makes read back and check clean ----
    {
        std::string err;
        okay::DialogueGraph g;
        const std::string d = NewDialogueText("Greeting");
        CHECK(g.Parse(d, &err));
        CHECK(g.name == "Greeting" && g.nodes.size() == 1 && g.Find(g.start) != nullptr);
        CHECK(g.Find(g.start)->speaker == "NPC" && g.Find(g.start)->text == "Hello there.");
        const std::vector<okay::DialogueGraph::Issue> di = g.Validate();
        for (const auto& i : di) std::cerr << "  dialogue issue: " << i.text << "\n";
        CHECK(di.empty());
        CHECK(g.Write() == d);   // writes back byte for byte: nothing is lost when the window saves it
        okay::DialogueGraph unnamed;
        CHECK(unnamed.Parse(NewDialogueText(""), &err) && unnamed.name == "New Dialogue");

        std::vector<okay::QuestDef> qs;
        const std::string q = NewQuestText();
        CHECK(okay::QuestFile::Parse(q, qs, &err));
        CHECK(qs.size() == 1 && qs[0].id == "quest1" && qs[0].objectives.size() == 1);
        const auto qi = okay::QuestFile::Validate(qs);
        for (const auto& i : qi) std::cerr << "  quest issue: " << i.text << "\n";
        CHECK(qi.empty());
        CHECK(okay::QuestFile::Write(qs) == q);

        okay::BehaviorTree t;
        const std::string b = NewBehaviorTreeText("Guard Brain");
        CHECK(t.Deserialize(b, &err));
        CHECK(t.name == "Guard Brain" && t.nodes.size() == 2 && t.root >= 0);
        CHECK(t.Find(t.root)->type == okay::BtNodeType::Selector);
        const auto bi = t.Validate();
        for (const auto& i : bi) std::cerr << "  tree issue: " << i.text << "\n";
        CHECK(bi.empty());
        CHECK(t.Serialize() == b);
        okay::BehaviorTree unnamedTree;
        CHECK(unnamedTree.Deserialize(NewBehaviorTreeText(""), &err) && unnamedTree.name == "Tree");

        okay::Sequence s;
        const std::string sq = NewSequenceText("Intro");
        CHECK(okay::Sequence::Parse(sq, s, &err));
        CHECK(s.name == "Intro" && s.shots.empty() && s.Write() == sq);
        okay::Sequence unnamedSeq;
        CHECK(okay::Sequence::Parse(NewSequenceText(""), unnamedSeq, &err) && unnamedSeq.name == "Sequence");
    }

    // ---- ... and are written through the panel's safe file operation ----
    {
        TempDir td;
        Browser b(td.assets());
        b.Sync();
        const struct { const char* file; std::string text; } made[] = {
            {"Greeting.okaydialog", NewDialogueText("Greeting")}, {"Quests.okayquest", NewQuestText()},
            {"Guard.okaybt", NewBehaviorTreeText("Guard")}, {"Intro.okayseq", NewSequenceText("Intro")}};
        for (const auto& m : made) {
            const Result r = b.MakeFile(td.assets(), m.file, m.text);
            CHECK(r.ok && r.paths.size() == 1 && ReadFile(td.at(m.file)) == m.text);
            // a second Create with the same name is refused and the file is untouched
            const Result again = b.MakeFile(td.assets(), m.file, "other");
            CHECK(!again.ok && ReadFile(td.at(m.file)) == m.text);
        }
        // the prompt starts from a free name
        CHECK(UniqueName(td.assets(), "Greeting", ".okaydialog") == "Greeting 2.okaydialog");
        CHECK(UniqueName(td.assets(), "Fresh", ".okayquest") == "Fresh.okayquest");
        // a name with a path in it is refused (the prompt shows the reason)
        CHECK(!ValidateName("../Evil").empty() && !ValidateName("a/b").empty());
        CHECK(!b.MakeFile(td.assets(), "../Evil.okaydialog", "x").ok);
        CHECK(!Exists((td.path / "Evil.okaydialog").generic_u8string()));
        // undo takes the last created file away again
        CHECK(b.CanUndo() && b.Undo().ok && !Exists(td.at("Intro.okayseq")));
        // the database sees what was made, as text assets
        CHECK(b.Sync());
        const std::vector<Entry> list = b.List(td.assets(), ListOptions{});
        CHECK(list.size() == 3);
        for (const Entry& e : list) CHECK(e.size > 0 && !e.isDir);
    }

    // ---- Reimport All in Folder, folder numbers and the modified time ----
    {
        TempDir td;
        WriteFile(td.at("Textures/a.png"), "aaa");
        WriteFile(td.at("Textures/Deep/b.png"), "bbbbbb");
        WriteFile(td.at("Other/c.png"), "c");
        WriteFile(td.at("Textures/Empty/.keep"), "");   // a hidden file: the folder shows as empty
        Browser b(td.assets());
        b.Sync();
        const std::uint64_t rev0 = b.Revision();
        CHECK(b.Info(td.at("Textures/a.png")).size == 3);
        // changed outside; nothing polled yet, so the database still has the old size
        WriteFile(td.at("Textures/a.png"), "aaaaaaaaaa");
        WriteFile(td.at("Textures/Deep/b.png"), "bbbbbbbbbbbbbb");
        CHECK(b.Info(td.at("Textures/a.png")).size == 3);
        CHECK(b.ReimportFolder(td.at("Textures")) == 2);        // both files, the one in the folder below too
        CHECK(b.Info(td.at("Textures/a.png")).size == 10 && b.Info(td.at("Textures/Deep/b.png")).size == 14);
        CHECK(b.Info(td.at("Other/c.png")).size == 1);          // another folder is not touched
        CHECK(b.Revision() > rev0);
        // an empty folder and a missing one read nothing and change nothing
        const std::uint64_t rev1 = b.Revision();
        CHECK(b.ReimportFolder(td.at("Textures/Empty")) == 0 && b.Revision() == rev1);
        CHECK(b.ReimportFolder(td.at("Nope")) == 0 && b.ReimportFolder(td.at("Nope/x.png")) == 0);
        // one file
        WriteFile(td.at("Other/c.png"), "cc");
        CHECK(b.ReimportFolder(td.at("Other/c.png")) == 1 && b.Info(td.at("Other/c.png")).size == 2);
        // outside Assets is refused
        WriteFile((td.path / "outside.png").generic_u8string(), "o");
        CHECK(b.ReimportFolder(td.path.generic_u8string()) == 0 && b.ReimportFolder((td.path / "outside.png").generic_u8string()) == 0);
        CHECK(b.ReimportFolder(td.assets() + "/../outside.png") == 0);
        // folder numbers: files and folders below, bytes
        const Browser::FolderStats st = b.StatsOf(td.at("Textures"));
        CHECK(st.files == 2 && st.folders == 2 && st.bytes == 24);   // a.png, Deep/b.png; Deep and Empty
        const Browser::FolderStats all = b.StatsOf(td.assets());
        CHECK(all.files == 3 && all.folders == 4 && all.bytes == 26);
        CHECK(b.StatsOf(td.at("Nope")).files == 0 && b.StatsOf(td.path.generic_u8string()).files == 0);
        CHECK(b.Info(td.at("Other/c.png")).modified > 0);
    }

    // ---- Standard Folders: inside Assets, all or nothing, one undo ----
    {
        TempDir td;
        WriteFile(td.at("Existing/keep.txt"), "k");
        Browser b(td.assets());
        b.Sync();
        const std::vector<std::string> seven = {"Scenes", "Scripts", "Prefabs", "Materials", "Models", "Textures", "Audio"};
        const Result r = b.CreateFolders(td.assets(), seven);
        CHECK(r.ok && r.paths.size() == 7 && r.note.empty());
        for (const std::string& n : seven) CHECK(fs::is_directory(fs::u8path(td.at(n))));
        CHECK(b.UndoLabel() == "Create folders");
        CHECK(b.Undo().ok);                                                   // one Ctrl+Z takes all seven away
        for (const std::string& n : seven) CHECK(!Exists(td.at(n)));
        CHECK(Exists(td.at("Existing/keep.txt")));
        // some are there already: they are left alone and named, the rest are made
        fs::create_directories(fs::u8path(td.at("Models")));
        WriteFile(td.at("Models/model.fbx"), "m");
        const Result again = b.CreateFolders(td.assets(), seven);
        CHECK(again.ok && again.paths.size() == 6 && Contains(again.note, "1 folder was already there"));
        CHECK(ReadFile(td.at("Models/model.fbx")) == "m");
        CHECK(b.Undo().ok && Exists(td.at("Models/model.fbx")) && !Exists(td.at("Scenes")));   // (the folder that was there stays, with its file)
        // everything there already: nothing to do, nothing to undo
        const Result none = b.CreateFolders(td.assets(), {"Models"});
        CHECK(none.ok && none.paths.empty());
        // in a sub folder
        const Result sub = b.CreateFolders(td.at("Existing"), {"A", "B"});
        CHECK(sub.ok && Exists(td.at("Existing/A")) && Exists(td.at("Existing/B")));
        CHECK(b.Undo().ok && !Exists(td.at("Existing/A")));
        // refused: a bad name makes nothing (not even the good ones before it), outside Assets, not a folder
        const Tree before = Snap(td.path);
        CHECK(!b.CreateFolders(td.assets(), {"Good", "../Evil"}).ok && Snap(td.path) == before);
        CHECK(!b.CreateFolders(td.assets(), {"Good", "bad:name"}).ok && Snap(td.path) == before);
        CHECK(!b.CreateFolders(td.path.generic_u8string(), {"Outside"}).ok && !Exists((td.path / "Outside").generic_u8string()));
        CHECK(!b.CreateFolders(td.assets() + "/../Elsewhere", {"Outside"}).ok);
        CHECK(!b.CreateFolders(td.at("Existing/keep.txt"), {"X"}).ok && Snap(td.path) == before);
        CHECK(!b.CreateFolders(td.at("Missing"), {"X"}).ok && Snap(td.path) == before);
        CHECK(b.CreateFolders(td.assets(), {}).ok);
        // a link in Assets that points out: nothing is made through it
        fs::create_directories(td.path / "Outside");
        std::error_code lec;
        fs::create_directory_symlink(td.path / "Outside", td.path / "Assets" / "Link", lec);
        if (!lec) {
            CHECK(!b.CreateFolders(td.at("Link"), {"Scenes", "Scripts"}).ok);
            CHECK(!Exists((td.path / "Outside" / "Scenes").generic_u8string()) && !Exists((td.path / "Outside" / "Scripts").generic_u8string()));
        } else std::cout << "  (symlinks not available here: link case skipped)\n";
        // the disk refuses the third folder: nothing is left behind and there is nothing to undo
        {
            Browser fb(td.assets());
            fb.Sync();
            const Tree snap = Snap(td.path);
            int seen = 0;
            fb.SetFailureHook([&](const char* op, const std::string&) { return std::string(op) == "mkdir" && ++seen == 3; });
            const Result failed = fb.CreateFolders(td.assets(), seven);
            fb.SetFailureHook(nullptr);
            CHECK(!failed.ok && Contains(failed.error, "Nothing was created"));
            CHECK(Snap(td.path) == snap && !fb.CanUndo());
        }
    }

    // ---- DuplicateMany: a whole selection, all or nothing, one undo, nothing copied twice ----
    {
        TempDir td;
        WriteFile(td.at("A/x.png"), "x"); WriteFile(td.at("A/y.png"), "y"); WriteFile(td.at("b.png"), "B"); WriteFile(td.at("C/c.png"), "c");
        Browser b(td.assets());
        b.Sync();
        // {A, A/x}: A is copied (with its own x); A/x is not copied again next to itself
        const Result nested = b.DuplicateMany({td.at("A"), td.at("A/x.png")});
        CHECK(nested.ok && nested.paths.size() == 1 && nested.paths[0] == td.at("A copy"));
        CHECK(ReadFile(td.at("A copy/x.png")) == "x" && ReadFile(td.at("A copy/y.png")) == "y");
        CHECK(!Exists(td.at("A/x copy.png")) && !Exists(td.at("A/y copy.png")));
        CHECK(b.UndoLabel() == "Duplicate A" && b.Undo().ok && !Exists(td.at("A copy")));
        // the same selection the other way round, and twice the same path
        const Result rev = b.DuplicateMany({td.at("A/x.png"), td.at("A"), td.at("A")});
        CHECK(rev.ok && rev.paths.size() == 1 && Exists(td.at("A copy/x.png")) && !Exists(td.at("A/x copy.png")));
        CHECK(b.Undo().ok);
        // several: each next to its own original, one Ctrl+Z for all
        const Result many = b.DuplicateMany({td.at("b.png"), td.at("C/c.png"), td.at("A/y.png")});
        CHECK(many.ok && many.paths.size() == 3);
        CHECK(ReadFile(td.at("b copy.png")) == "B" && ReadFile(td.at("C/c copy.png")) == "c" && ReadFile(td.at("A/y copy.png")) == "y");
        CHECK(b.UndoLabel() == "Duplicate 3 items");
        CHECK(b.Undo().ok && !Exists(td.at("b copy.png")) && !Exists(td.at("C/c copy.png")) && !Exists(td.at("A/y copy.png")));
        // two selected files where one is the copy-name of the other
        WriteFile(td.at("d.png"), "d");
        CHECK(b.Duplicate(td.at("d.png")).ok);                                                    // "d copy.png"
        const Result both = b.DuplicateMany({td.at("d.png"), td.at("d copy.png")});
        CHECK(both.ok && both.paths.size() == 2 && Exists(td.at("d copy 2.png")) && Exists(td.at("d copy copy.png")));
        CHECK(ReadFile(td.at("d copy 2.png")) == "d" && ReadFile(td.at("d copy copy.png")) == "d");
        // Duplicate of one is DuplicateMany of one
        const Result one = b.Duplicate(td.at("b.png"));
        CHECK(one.ok && one.paths.size() == 1 && one.paths[0] == td.at("b copy.png") && b.UndoLabel() == "Duplicate b.png");
        // refused: the Assets folder, outside Assets, a file that is gone. Nothing is made, not even for the good ones in the selection.
        const Tree before = Snap(td.path);
        CHECK(!b.DuplicateMany({td.at("b.png"), td.assets()}).ok && Snap(td.path) == before);
        CHECK(!b.DuplicateMany({td.at("b.png"), (td.path / "outside.png").generic_u8string()}).ok && Snap(td.path) == before);
        CHECK(!b.DuplicateMany({td.at("b.png"), td.at("gone.png")}).ok && Snap(td.path) == before);
        CHECK(b.DuplicateMany({}).ok);
        // the disk refuses the second copy: the first one is taken away again
        {
            Browser fb(td.assets());
            fb.Sync();
            int seen = 0;
            fb.SetFailureHook([&](const char* op, const std::string&) { return std::string(op) == "copy" && ++seen == 2; });
            const Result failed = fb.DuplicateMany({td.at("b.png"), td.at("C/c.png")});
            fb.SetFailureHook(nullptr);
            CHECK(!failed.ok && Snap(td.path) == before && !fb.CanUndo());
        }
    }

    // ---- the tags a menu lists ----
    {
        TempDir td;
        WriteFile(td.at("Textures/hero.png"), "h"); WriteFile(td.at("plain.png"), "p");
        Browser b(td.assets());
        b.Sync();
        CHECK(b.LabelsOf(td.at("Textures/hero.png")).empty() && b.LabelsOf(td.at("plain.png")).empty());
        CHECK(b.AddLabel(td.at("Textures/hero.png"), "hero") && b.AddLabel(td.at("Textures/hero.png"), "wip"));
        const std::vector<std::string> tags = b.LabelsOf(td.at("Textures/hero.png"));
        CHECK(tags.size() == 2 && ((tags[0] == "hero" && tags[1] == "wip") || (tags[0] == "wip" && tags[1] == "hero")));
        CHECK(b.LabelsOf(td.at("plain.png")).empty());
        CHECK(b.LabelsOf(td.at("Textures")).empty());                            // a folder has none
        CHECK(b.LabelsOf(td.at("missing.png")).empty());
        CHECK(b.LabelsOf((td.path / "outside.png").generic_u8string()).empty());
        CHECK(b.LabelsOf(td.at("Textures/hero.png")) == b.Info(td.at("Textures/hero.png")).labels);   // the same list Info gives
        CHECK(b.RemoveLabel(td.at("Textures/hero.png"), "wip") && b.LabelsOf(td.at("Textures/hero.png")).size() == 1);
    }

    // ---- Duplicate on a whole selection, Paste twice ----
    {
        TempDir td;
        WriteFile(td.at("a.png"), "A"); WriteFile(td.at("b.png"), "B"); WriteFile(td.at("Folder/inner.txt"), "I");
        Browser b(td.assets());
        b.Sync();
        const std::vector<std::string> sel = {td.at("a.png"), td.at("b.png"), td.at("Folder")};
        for (const std::string& p : sel) CHECK(b.Duplicate(p).ok);
        CHECK(ReadFile(td.at("a copy.png")) == "A" && ReadFile(td.at("b copy.png")) == "B" && ReadFile(td.at("Folder copy/inner.txt")) == "I");
        for (const std::string& p : sel) CHECK(b.Duplicate(p).ok);           // again: numbered, nothing overwritten
        CHECK(Exists(td.at("a copy 2.png")) && Exists(td.at("b copy 2.png")) && Exists(td.at("Folder copy 2/inner.txt")));
        CHECK(ReadFile(td.at("a.png")) == "A" && ReadFile(td.at("a copy.png")) == "A");
        // paste the same clipboard twice into one folder
        Clipboard cb;
        cb.Set({td.at("a.png"), td.at("b.png")}, false);
        CHECK(cb.Paste(b, td.at("Folder")).ok);
        CHECK(cb.Paste(b, td.at("Folder")).ok);
        CHECK(Exists(td.at("Folder/a.png")) && Exists(td.at("Folder/a 2.png")) && Exists(td.at("Folder/b.png")) && Exists(td.at("Folder/b 2.png")));
        CHECK(ReadFile(td.at("a.png")) == "A" && ReadFile(td.at("Folder/a 2.png")) == "A");
        // a duplicate of the Assets folder itself is refused
        const Tree before = Snap(td.path);
        CHECK(!b.Duplicate(td.assets()).ok && Snap(td.path) == before);
        CHECK(!b.Duplicate((td.path / "outside").generic_u8string()).ok);
    }

    // ---- Move to...: the picker's rule and Browser::Move agree, and a taken name gets a number ----
    {
        TempDir td;
        WriteFile(td.at("A/one.png"), "1"); WriteFile(td.at("A/B/deep.png"), "d"); WriteFile(td.at("C/one.png"), "other one"); WriteFile(td.at("top.png"), "t");
        WriteFile(td.at("A2/x.png"), "x");
        Browser b(td.assets());
        b.Sync();
        struct Case { std::vector<std::string> items; std::string dest; };
        const std::vector<Case> cases = {
            {{td.at("A")}, td.at("A")}, {{td.at("A")}, td.at("A/B")}, {{td.at("A"), td.at("C")}, td.at("A/B")},
            {{td.at("top.png")}, td.assets()}, {{td.at("A/one.png")}, td.at("A")},
            {{td.at("top.png")}, td.path.generic_u8string()}, {{td.assets()}, td.at("C")},
            {{td.at("top.png")}, td.at("C")}, {{td.at("A")}, td.at("A2")}, {{td.at("A/B")}, td.at("C")},
        };
        for (const Case& c : cases) {
            const std::string why = ctx::MoveBlockedReason(td.assets(), c.items, c.dest);
            Browser fresh(td.assets());
            fresh.Sync();
            const Tree before = Snap(td.path);
            const Result r = fresh.Move(c.items, c.dest);
            const bool changed = Snap(td.path) != before;
            if (!why.empty() && why.find("already in") == std::string::npos) {
                // the picker says no: the operation refuses and nothing moves
                if (r.ok || changed) std::cerr << "  picker said \"" << why << "\" but Move ok=" << r.ok << " changed=" << changed << "\n";
                CHECK(!r.ok && !changed);
            } else if (!why.empty()) {
                CHECK(!changed);   // "already there": a no-op
            } else {
                if (!r.ok) std::cerr << "  picker allowed it but Move said: " << r.error << "\n";
                CHECK(r.ok && changed);
                CHECK(fresh.Undo().ok && Snap(td.path) == before);   // and Ctrl+Z puts it back exactly
            }
        }
        // the taken name: C/one.png exists, so A/one.png arrives as "one 2.png" and C/one.png is untouched
        const Result r = b.Move({td.at("A/one.png")}, td.at("C"));
        CHECK(r.ok && ReadFile(td.at("C/one.png")) == "other one" && ReadFile(td.at("C/one 2.png")) == "1" && !Exists(td.at("A/one.png")));
        CHECK(r.paths.size() == 1 && r.paths[0] == td.at("C/one 2.png"));
        CHECK(b.Undo().ok && ReadFile(td.at("A/one.png")) == "1" && !Exists(td.at("C/one 2.png")));
    }

    TEST_MAIN_RESULT();
}
