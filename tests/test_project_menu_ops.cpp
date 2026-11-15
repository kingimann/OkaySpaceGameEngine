// What the Project panel's new right-click items do on a real folder (temp folders, no ImGui):
//   * the files the Create menu makes for the story and AI windows (a dialogue, a quest file, a behavior tree, a cutscene):
//     the text reads back with the engine's own parsers and checks clean, is written atomically and never over a file,
//   * Reimport All in Folder (Browser::ReimportFolder) and the numbers the Properties window shows (Browser::StatsOf,
//     AssetInfo::modified),
//   * the safety of the file operations the menu reaches: Duplicate on a whole selection, Paste twice, Move to... with a
//     name that is taken, and that the picker's rule (ProjectContextModel.hpp) says "no" exactly where Browser::Move refuses.
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
        };
        for (const auto& t : table) {
            if (MenuKind(t.path, false) != t.kind) std::cerr << "  MenuKind(" << t.path << ") gave " << (int)MenuKind(t.path, false) << ", wanted " << (int)t.kind << "\n";
            CHECK(MenuKind(t.path, false) == t.kind);
        }
        CHECK(MenuKind("a/Textures", true) == K::Folder && MenuKind("a/Assets.v2", true) == K::Folder && MenuKind("a/x.png", true) == K::Folder);
        // text kinds are the ones "Open in External Editor" is offered for
        ctx::State st;
        for (const auto& t : table) {
            st.kind = MenuKind(t.path, false);
            const bool offered = ctx::Find(ctx::ItemMenu(st), "open:external") != nullptr;
            const bool text = st.kind == K::Script || st.kind == K::VisualScript || st.kind == K::Material || st.kind == K::Data || st.kind == K::Scene || st.kind == K::Prefab;
            CHECK(offered == text);
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
