// The okay command line (tools/okay-cli): argument handling, help, the .gitignore / .gitattributes lines, reading
// `git status`, the Windows command-line quoting, empty project creation and `okay test`, then (when git is installed)
// version control on temporary repositories: init, who, status, save, history, undo, branch, switch, remote, sync,
// and the scene merge driver that git calls: two edits of one scene that do not overlap merge with no hand editing, and
// a real clash is reported in plain words while the file still loads.
//
// This program is also git's merge and diff driver during the test: `okay vc init` registers the running program, so
// `test_okay_cli vc merge-driver ...` forwards to the CLI. Exit 77 (skipped) when every check that does not need git
// passed and git is not installed. Written for this repository.
#include "test_framework.hpp"

#include "Cli.hpp"
#include "Git.hpp"
#include "Process.hpp"

#include <Okay.hpp>
#include "okay/Core/StringUtil.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;
using okaycli::Io;
using Strs = std::vector<std::string>;

namespace {

bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

std::string Read(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}
void Write(const fs::path& p, const std::string& t) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f << t;
}

struct Said {
    std::string out, err;
    Io io;
    Said() { io.out = &out; io.err = &err; }
    std::string all() const { return out + err; }
};

int Cli(const Strs& args, Said& s) { s.out.clear(); s.err.clear(); return okaycli::Main(args, s.io); }

void SetEnv(const char* k, const std::string& v) {
#if defined(_WIN32)
    _putenv_s(k, v.c_str());
#else
    setenv(k, v.c_str(), 1);
#endif
}

/// A scene with three objects with ids, A's x position and B's y position given.
std::string LevelText(const std::string& ax, const std::string& by, const std::string& cName = "C") {
    std::string t = "okayscene 3\nname \"Level\"\n";
    auto obj = [&](int i, const char* guid, const std::string& name, const std::string& transform) {
        t += "gameobject " + std::to_string(i) + " \"" + name + "\"\n  guid \"" + guid + "\"\n  active 1\n  parent -1\n  " + transform + "\nend\n";
    };
    obj(0, "a1", "A", "transform " + ax + " 0 0 0 0 0 1 1 1 1");
    obj(1, "b2", "B", "transform 0 " + by + " 0 0 0 0 1 1 1 1");
    obj(2, "c3", cName, "transform 0 0 0 0 0 0 1 1 1 1");
    return t;
}

/// Load scene text and read an object's position.
bool PositionOf(const std::string& text, const std::string& name, okay::Vec3& out) {
    okay::Scene s("probe");
    std::string err;
    if (!okay::SceneSerializer::Deserialize(s, text, &err)) return false;
    okay::GameObject* o = s.Find(name);
    if (!o) return false;
    out = o->transform->localPosition;
    return true;
}

void TestArguments() {
    RUN_SUITE("okay cli: commands, help, arguments");
    Said s;
    CHECK(Cli({}, s) == 0 && Has(s.out, "Commands:") && Has(s.out, "vc save"));
    CHECK(Cli({"help"}, s) == 0 && Has(s.out, "Make a new project.") && Has(s.out, "okay help <command>"));
    CHECK(Cli({"help", "vc", "save"}, s) == 0 && Has(s.out, "Usage: okay vc save"));
    CHECK(Cli({"help", "vc"}, s) == 0 && Has(s.out, "history"));
    CHECK(Cli({"help", "nosuch"}, s) == 2 && Has(s.err, "no command called \"nosuch\""));
    CHECK(Cli({"nosuch"}, s) == 2 && Has(s.err, "no command called"));
    CHECK(Cli({"vc", "nosuch"}, s) == 2 && Has(s.err, "no okay vc command"));
    CHECK(Cli({"--version"}, s) == 0 && Has(s.out, okaycli::EngineVersion()));
    CHECK(Cli({"new", "--bogus"}, s) == 2);                        // an unknown option is a bad command line
    CHECK(Cli({"new"}, s) == 2 && Has(s.err, "okay new MyGame"));    // the name is missing
    CHECK(Cli({"new", "--template", "list"}, s) == 0 && Has(s.out, "\"3D Scene\""));
    CHECK(Cli({"new", "X", "--template", "No Such Template"}, s) == 2 && Has(s.err, "no template"));
    CHECK(Cli({"vc", "history", "-n", "0", "--project", "/nonexistent-okay"}, s) != 0);
    CHECK(Cli({"build", "--target", "playstation"}, s) == 1 && Has(s.err, "has to be made on playstation"));
    CHECK(Cli({"vc", "merge-driver", "a"}, s) == 2);                 // the hidden driver needs three files

    // The shared Trim this CLI added to Core/StringUtil.hpp.
    CHECK(okay::str::Trim("  a b \r\n") == "a b" && okay::str::Trim("\t\n").empty() && okay::str::Trim("x") == "x");

    // The process seam: a program that is not there is "not started", not a crash.
    const okaycli::RunResult r = okaycli::Run({"okay-no-such-program-xyz"}, "", true);
    CHECK(!r.started);
    CHECK(okaycli::FindOnPath("okay-no-such-program-xyz").empty());
    CHECK(!okaycli::SelfPath().empty());
}

void TestIgnoreAndAttributes() {
    RUN_SUITE("okay cli: .gitignore and .gitattributes");
    const Strs ig = okaycli::vc::IgnoreLines();
    auto in = [](const Strs& v, const std::string& l) { for (const auto& x : v) if (x == l) return true; return false; };
    for (const char* l : {"build/", ".okay/backups/", ".okay/build_live.cfg", "*.autosave", "*.log", "*.bak", ".okaycache/",
                          "Library/", "export/", ".okay/projectview.txt", ".okay/favourites.txt"})
        CHECK(in(ig, l));
    CHECK(!in(ig, "Assets/"));
    CHECK(!in(ig, "project.okayproj"));
    int added = 0;
    const std::string first = okaycli::vc::GitIgnoreText("my-own-line\n", &added);
    CHECK(first.rfind("my-own-line\n", 0) == 0);          // the person's lines stay first and untouched
    CHECK(added == (int)ig.size());
    CHECK(Has(first, "# added by okay vc init"));
    int again = -1;
    CHECK(okaycli::vc::GitIgnoreText(first, &again) == first && again == 0);   // running it twice adds nothing
    const Strs at = okaycli::vc::AttributeLines();
    CHECK(in(at, "*.okayscene text eol=lf merge=okayscene diff=okayscene"));
    CHECK(in(at, "*.okayprefab text eol=lf merge=okayscene diff=okayscene"));
    for (const char* b : {"*.png binary", "*.wav binary", "*.fbx binary", "*.ttf binary", "*.glb binary"}) CHECK(in(at, b));
    CHECK(okaycli::vc::MergeDriverCommand("C:\\Program Files\\okay.exe") == "\"C:/Program Files/okay.exe\" vc merge-driver %O %A %B %P");
    CHECK(okaycli::vc::DiffDriverCommand("/opt/my $dir/okay") == "\"/opt/my \\$dir/okay\" vc diff-driver");
}

void TestStatusGrouping() {
    RUN_SUITE("okay cli: status grouped into scenes, scripts and assets");
    using okaycli::vc::Group;
    using okaycli::vc::State;
    std::string z;
    for (const char* e : {" M Assets/Level.okayscene", "?? Assets/Scripts/Player.okayvs", "D  Assets/old.png",
                          "R  Assets/new.okayprefab", "Assets/old.okayprefab", "UU Assets/Clash.okayscene", " M project.okayproj",
                          "A  Assets/Scripts/ai.lua", "MM Assets/Textures/rock.png"}) {
        z += e;
        z += '\0';
    }
    const auto ch = okaycli::vc::ParseStatus(z);
    CHECK(ch.size() == 8);
    if (ch.size() == 8) {
        CHECK(ch[0].state == State::Changed && ch[0].group == Group::Scenes);
        CHECK(ch[1].state == State::New && ch[1].group == Group::Scripts);
        CHECK(ch[2].state == State::Deleted && ch[2].group == Group::Assets);
        CHECK(ch[3].state == State::Renamed && ch[3].from == "Assets/old.okayprefab" && ch[3].group == Group::Scenes);
        CHECK(ch[4].state == State::Conflict);
        CHECK(ch[5].group == Group::Settings);
        CHECK(ch[6].state == State::New && ch[6].group == Group::Scripts);
        CHECK(ch[7].state == State::Changed && ch[7].group == Group::Assets);
    }
    const std::string t = okaycli::vc::StatusText(ch, "main");
    CHECK(Has(t, "Version line: main"));
    CHECK(Has(t, "8 files have changed"));
    CHECK(Has(t, "Scenes and prefabs (3)"));
    CHECK(Has(t, "Scripts (2)"));
    CHECK(Has(t, "Assets (2)"));
    CHECK(Has(t, "Project settings (1)"));
    CHECK(Has(t, "renamed  Assets/new.okayprefab  (was Assets/old.okayprefab)"));
    CHECK(Has(t, "1 file clashes"));
    CHECK(t.find("Scenes and prefabs") < t.find("Scripts (") && t.find("Scripts (") < t.find("Assets ("));
    CHECK(Has(okaycli::vc::StatusText({}, "main"), "Everything is saved"));
    CHECK(okaycli::vc::GroupOf("Assets\\Scripts\\Boss.CS") == Group::Scripts);
    CHECK(okaycli::vc::GroupOf(".okay/scene_build.txt") == Group::Settings);
}

void TestProjects(const fs::path& tmp) {
    RUN_SUITE("okay cli: new (empty), finding the project, okay test");
    Said s;
    const fs::path where = tmp / "projects";
    fs::create_directories(where);
    CHECK(Cli({"new", "My Game", "--in", where.string(), "--no-git", "--version", "0.3.0"}, s) == 0);
    const fs::path root = where / "My Game";
    CHECK(fs::is_regular_file(root / "project.okayproj"));
    CHECK(fs::is_regular_file(root / "Assets" / "My Game.okayscene"));
    CHECK(fs::is_regular_file(root / "Assets" / "input.okayinput"));
    const std::string settings = Read(root / "project.okayproj");
    CHECK(Has(settings, "version=0.3.0"));
    CHECK(Has(settings, "engine_version="));
    CHECK(!fs::exists(root / ".git"));
    // A second project with the same name gets its own folder.
    CHECK(Cli({"new", "My Game", "--in", where.string(), "--no-git"}, s) == 0 && Has(s.out, "My Game-2"));
    CHECK(Cli({"new", "bad/name", "--in", where.string(), "--no-git"}, s) == 1);

    CHECK(fs::path(okaycli::FindProjectRoot((root / "Assets").string())) == root);
    CHECK(fs::path(okaycli::FindProjectRoot((root / ".").string())).filename() == "My Game");   // no trailing slash from "."
    CHECK(fs::path(okaycli::FindProjectRoot((root / "Assets" / "").string())) == root);
    CHECK(fs::path(okaycli::FindProjectRoot((root / "Assets" / "My Game.okayscene").string())) == root);
    CHECK(okaycli::FindProjectRoot(tmp.string()).empty());
    CHECK(fs::path(okaycli::MainScene(root.string())) == root / "Assets" / "My Game.okayscene");
    Write(root / "Assets" / "Levels" / "Two.okayscene", LevelText("1", "2"));
    Write(root / ".okay" / "scene_build.txt", "# list\n0|Assets/My Game.okayscene\n1|Assets/Levels/Two.okayscene\n");
    CHECK(fs::path(okaycli::MainScene(root.string())) == root / "Assets" / "Levels" / "Two.okayscene");

    CHECK(Cli({"info", "--project", root.string()}, s) == 0 && Has(s.out, "Project: My Game") && Has(s.out, "Game version: 0.3.0"));
    CHECK(Cli({"test", "--project", root.string()}, s) == 0 && Has(s.out, "Checked 2 scenes") && Has(s.out, "Everything loads"));
    Write(root / "Assets" / "Broken.okayscene", "okayscene 3\n<<<<<<< ours\nname \"x\"\n=======\nname \"y\"\n>>>>>>> theirs\n");
    CHECK(Cli({"test", "--project", root.string()}, s) == 1 && Has(s.err, "Broken.okayscene: still has merge clash marks"));
    fs::remove(root / "Assets" / "Broken.okayscene");
    CHECK(Cli({"play", "--project", tmp.string()}, s) == 1 && Has(s.err, "not an OkaySpace project"));
}

void TestMergeDriverDirect(const fs::path& tmp) {
    RUN_SUITE("okay cli: the scene merge driver on files");
    Said s;
    const fs::path d = tmp / "merge";
    fs::create_directories(d);
    // Two edits that do not overlap: A moved on one side, B on the other.
    Write(d / "base", LevelText("0", "0"));
    Write(d / "ours", LevelText("5", "0"));
    Write(d / "theirs", LevelText("0", "9"));
    CHECK(okaycli::SceneMergeDriver((d / "base").string(), (d / "ours").string(), (d / "theirs").string(), "Level.okayscene", s.io) == 0);
    okay::Vec3 a, b;
    const std::string merged = Read(d / "ours");
    CHECK(PositionOf(merged, "A", a) && a.x == 5.0f);
    CHECK(PositionOf(merged, "B", b) && b.y == 9.0f);
    CHECK(Has(s.err, "merged both sets of changes"));
    // A real clash: both moved A to different places.
    s.out.clear(); s.err.clear();
    Write(d / "ours", LevelText("5", "0"));
    Write(d / "theirs", LevelText("7", "0"));
    CHECK(okaycli::SceneMergeDriver((d / "base").string(), (d / "ours").string(), (d / "theirs").string(), "Level.okayscene", s.io) == 1);
    CHECK(Has(s.err, "Level.okayscene: 1 change clashes"));
    CHECK(Has(s.err, "A: "));
    CHECK(Has(s.err, "you made it") && Has(s.err, "they made it"));
    CHECK(PositionOf(Read(d / "ours"), "A", a) && a.x == 5.0f);   // still loads, with our side kept
    // Not a scene at all: merged as text by git (only when git is installed).
    // The diff driver: objects and fields, not lines.
    s.out.clear();
    Write(d / "old", LevelText("0", "0"));
    Write(d / "new", LevelText("3", "0", "Crate"));
    CHECK(okaycli::SceneDiffDriver({(d / "old").string(), (d / "new").string()}, s.io) == 0);
    CHECK(Has(s.out, "scene "));
    CHECK(Has(s.out, "Crate"));
    s.out.clear();
    CHECK(okaycli::SceneDiffDriver({"Level.okayscene", "/dev/null", ".", ".", (d / "new").string(), ".", "100644"}, s.io) == 0);
    CHECK(Has(s.out, "scene Level.okayscene"));
}

std::string GitOut(const fs::path& dir, const Strs& args, int* code = nullptr) {
    okaycli::vc::Git g(dir.string());
    const okaycli::RunResult r = g.Capture(args);
    if (code) *code = r.exitCode;
    return r.out + r.err;
}

void TestGit(const fs::path& tmp) {
    RUN_SUITE("okay cli: version control on a temporary repository");
    Said s;
    // A private git setup: no system or user config of this machine is read or changed.
    const fs::path home = tmp / "home";
    fs::create_directories(home);
    SetEnv("HOME", home.string());
    SetEnv("XDG_CONFIG_HOME", (home / ".config").string());
    SetEnv("GIT_CONFIG_NOSYSTEM", "1");
    SetEnv("GIT_CONFIG_GLOBAL", (home / ".gitconfig").string());
    SetEnv("GIT_TERMINAL_PROMPT", "0");

    const fs::path where = tmp / "vc";
    fs::create_directories(where);
    CHECK(Cli({"new", "Game", "--in", where.string()}, s) == 0);   // with git: okay new runs okay vc init
    const fs::path root = where / "Game";
    const std::string P = root.string();
    CHECK(fs::is_directory(root / ".git"));
    CHECK(Has(Read(root / ".gitignore"), ".okay/backups/"));
    CHECK(Has(Read(root / ".gitattributes"), "*.okayscene text eol=lf merge=okayscene diff=okayscene"));
    CHECK(Has(GitOut(root, {"config", "merge.okayscene.driver"}), "vc merge-driver %O %A %B %P"));
    CHECK(Has(GitOut(root, {"config", "diff.okayscene.command"}), "vc diff-driver"));
    CHECK(GitOut(root, {"symbolic-ref", "--short", "HEAD"}).rfind("main", 0) == 0);
    CHECK(Cli({"vc", "init", "--project", P}, s) == 0 && Has(s.out, "already had everything"));   // safe to run again

    CHECK(Cli({"vc", "status", "--project", P}, s) == 0 && Has(s.out, "Scenes and prefabs (1)") && Has(s.out, "Nothing has been saved yet"));
    // Saving needs a name first; vc who stores it (in this test's private config).
    CHECK(Cli({"vc", "who", "Test Person", "test@example.invalid", "--project", P}, s) == 0);
    CHECK(Cli({"vc", "who", "--project", P}, s) == 0 && Has(s.out, "Test Person"));
    CHECK(Cli({"vc", "save", "--project", P}, s) == 2);   // no message
    const fs::path scene = root / "Assets" / "Level.okayscene";
    Write(scene, LevelText("0", "0"));
    CHECK(Cli({"vc", "save", "First version", "--project", P}, s) == 0 && Has(s.out, "Saved") && Has(s.out, "First version"));
    CHECK(Cli({"vc", "status", "--project", P}, s) == 0 && Has(s.out, "Everything is saved"));
    CHECK(Cli({"vc", "save", "again", "--project", P}, s) == 0 && Has(s.out, "nothing to save"));
    // Nothing ignored made it in: build output and backups stay out.
    Write(root / "build" / "Game.exe", "x");
    Write(root / ".okay" / "backups" / "Level.okayscene.1", "x");
    Write(root / "Assets" / "Level.okayscene.bak", "x");
    CHECK(Cli({"vc", "status", "--project", P}, s) == 0 && Has(s.out, "Everything is saved"));

    // Change, history, undo (with a kept copy).
    Write(scene, LevelText("4", "0"));
    CHECK(Cli({"vc", "status", "--project", P}, s) == 0 && Has(s.out, "changed  Assets/Level.okayscene"));
    CHECK(Cli({"vc", "save", "Moved A", "--project", P}, s) == 0);
    Write(scene, LevelText("8", "0"));
    CHECK(Cli({"vc", "history", scene.string(), "--project", P}, s) == 0 && Has(s.out, "Moved A") && Has(s.out, "First version"));
    CHECK(Cli({"vc", "history", "-n", "1", "--project", P}, s) == 0 && Has(s.out, "Moved A") && !Has(s.out, "First version"));
    CHECK(Cli({"vc", "undo", scene.string(), "--project", P}, s) == 0 && Has(s.out, "is back to how it was"));
    okay::Vec3 a, b;
    CHECK(PositionOf(Read(scene), "A", a) && a.x == 4.0f);
    bool kept = false;
    for (const auto& e : fs::directory_iterator(root / ".okay" / "backups" / "undo"))
        if (PositionOf(Read(e.path()), "A", b) && b.x == 8.0f) kept = true;
    CHECK(kept);
    CHECK(Cli({"vc", "undo", scene.string(), "--project", P}, s) == 0 && Has(s.out, "no changes since the last save"));
    const std::string first = GitOut(root, {"rev-list", "--max-parents=0", "HEAD"}).substr(0, 7);
    CHECK(Cli({"vc", "undo", scene.string(), "--to", first, "--project", P}, s) == 0);
    CHECK(PositionOf(Read(scene), "A", a) && a.x == 0.0f);
    GitOut(root, {"checkout", "HEAD", "--", scene.string()});
    Write(root / "Assets" / "Never.okayscene", LevelText("1", "1"));
    CHECK(Cli({"vc", "undo", (root / "Assets" / "Never.okayscene").string(), "--project", P}, s) == 1 && Has(s.err, "was never saved"));
    fs::remove(root / "Assets" / "Never.okayscene");

    // Branches: a clean three-way scene merge through git's merge driver.
    CHECK(Cli({"vc", "branch", "--project", P}, s) == 0 && Has(s.out, "* main"));
    CHECK(Cli({"vc", "branch", "try-idea", "--project", P}, s) == 0 && Has(s.out, "you are on it now"));
    Write(scene, LevelText("4", "6"));   // B moved on the new line
    CHECK(Cli({"vc", "switch", "main", "--project", P}, s) == 1 && Has(s.err, "not saved yet"));
    CHECK(Cli({"vc", "save", "Moved B", "--project", P}, s) == 0);
    CHECK(Cli({"vc", "switch", "main", "--project", P}, s) == 0 && Has(s.out, "You are on main"));
    CHECK(Cli({"vc", "switch", "nope", "--project", P}, s) == 1 && Has(s.err, "no version line called nope"));
    Write(scene, LevelText("4", "0", "Crate"));   // C renamed on main
    CHECK(Cli({"vc", "save", "Renamed C", "--project", P}, s) == 0);
    int code = -1;
    const std::string mergeSaid = GitOut(root, {"merge", "--no-edit", "try-idea"}, &code);
    CHECK(code == 0);
    if (code != 0) std::cerr << "  git merge said: " << mergeSaid << "\n";
    CHECK(!Has(Read(scene), "<<<<<<<"));
    CHECK(PositionOf(Read(scene), "B", b) && b.y == 6.0f);   // both sides' changes, no hand editing
    okay::Scene probe("probe");
    CHECK(okay::SceneSerializer::Deserialize(probe, Read(scene)) && probe.Find("Crate") != nullptr);
    CHECK(Has(GitOut(root, {"diff", "HEAD~1", "--", scene.string()}), "scene "));   // git diff goes through our scene diff

    // A real clash through git: A moved to different places on two lines.
    CHECK(Cli({"vc", "branch", "other", "--project", P}, s) == 0);
    Write(scene, LevelText("11", "6", "Crate"));
    CHECK(Cli({"vc", "save", "A to 11", "--project", P}, s) == 0);
    CHECK(Cli({"vc", "switch", "main", "--project", P}, s) == 0);
    Write(scene, LevelText("22", "6", "Crate"));
    CHECK(Cli({"vc", "save", "A to 22", "--project", P}, s) == 0);
    const std::string clash = GitOut(root, {"merge", "--no-edit", "other"}, &code);
    CHECK(code != 0);
    CHECK(Has(clash, "change clashes") && Has(clash, "you made it"));   // the driver's plain words reach the person
    CHECK(!Has(Read(scene), "<<<<<<<"));
    CHECK(PositionOf(Read(scene), "A", a) && a.x == 22.0f);   // loads, with our side kept
    CHECK(Cli({"vc", "status", "--project", P}, s) == 0 && Has(s.out, "clash") && Has(s.out, "with changes from someone else"));
    CHECK(Cli({"vc", "save", "Merged", "--project", P}, s) == 0);   // finishing the merge is a plain save
    CHECK(Cli({"vc", "status", "--project", P}, s) == 0 && Has(s.out, "Everything is saved"));

    // Sync with an online copy: here a bare repository in a folder (any git host works the same way).
    const fs::path bare = tmp / "server.git";
    GitOut(tmp, {"init", "-q", "--bare", bare.string()});
    GitOut(tmp, {"--git-dir", bare.string(), "symbolic-ref", "HEAD", "refs/heads/main"});   // so a clone checks out main
    CHECK(Cli({"vc", "sync", "--project", P}, s) == 1 && Has(s.err, "no online copy"));
    CHECK(Cli({"vc", "remote", bare.string(), "--project", P}, s) == 0);
    CHECK(Cli({"vc", "remote", "--project", P}, s) == 0 && Has(s.out, bare.string()));
    CHECK(Cli({"vc", "sync", "--project", P}, s) == 0 && Has(s.out, "Synced"));
    CHECK(GitOut(bare, {"--git-dir", bare.string(), "log", "--format=%s", "-n", "1", "main"}).rfind("Merged", 0) == 0);
    // A second person: clone, change B, sync; the first person syncs and gets it.
    const fs::path second = tmp / "second";
    GitOut(tmp, {"clone", "-q", bare.string(), second.string()});
    Write(second / "Assets" / "Level.okayscene", LevelText("22", "33", "Crate"));
    CHECK(Cli({"vc", "save", "B to 33", "--project", second.string()}, s) == 0);
    CHECK(Cli({"vc", "sync", "--project", second.string()}, s) == 0);
    CHECK(Cli({"vc", "sync", "--project", P}, s) == 0 && Has(s.out, "took in 1 new save"));
    CHECK(PositionOf(Read(scene), "B", b) && b.y == 33.0f);

    CHECK(Cli({"doctor", "--project", P}, s) >= 0 && Has(s.out, "Git merges and compares scenes with the engine"));
    CHECK(Cli({"info", "--project", P}, s) == 0 && Has(s.out, "Version line: main") && Has(s.out, "Online copy: " + bare.string()));
}

} // namespace

int main(int argc, char** argv) {
    // During the git tests this program is git's merge and diff driver (okay vc init registered it).
    if (argc > 1 && std::string(argv[1]) == "vc") {
        Strs args;
        for (int i = 1; i < argc; ++i) args.push_back(argv[i]);
        Io io;
        return okaycli::Main(args, io);
    }
    std::error_code ec;
    const fs::path tmp = fs::temp_directory_path(ec) / ("okay_cli_test_" + std::to_string((unsigned long)std::rand()) + "_" +
                                                         std::to_string((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    TestArguments();
    TestIgnoreAndAttributes();
    TestStatusGrouping();
    TestProjects(tmp);
    TestMergeDriverDirect(tmp);
    const bool git = okaycli::vc::Git::Available();
    if (git) TestGit(tmp);
    else std::cout << "  SKIP: git is not installed, so the version control checks did not run\n";
    fs::remove_all(tmp, ec);
    std::cout << ::okaytest::g_checks << " checks, " << ::okaytest::g_failures << " failures\n";
    if (::okaytest::g_failures) return 1;
    return git ? 0 : 77;
}
