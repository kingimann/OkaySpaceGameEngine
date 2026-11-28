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
#include "CliInternal.hpp"   // LongPath, RelInside
#include "Engine.hpp"
#include "Git.hpp"
#include "Process.hpp"
#include "ProjectVersion.hpp"   // shared/: the engine_version stamp

#include <Okay.hpp>
#include "okay/Core/StringUtil.hpp"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>   // GetShortPathNameW, for the short-name check
#endif

namespace fs = std::filesystem;
using okaycli::Io;
using Strs = std::vector<std::string>;
using okay::str::Trim;

namespace {

bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

/// The engine checkout. Not __FILE__: Ninja compiles with a relative path (../tests/test_okay_cli.cpp), which only
/// works from the build folder, and the Windows test job runs the copied .exe from another folder.
fs::path RepoRoot() {
#ifdef OKAY_SOURCE_DIR
    return fs::path(OKAY_SOURCE_DIR);
#else
    return fs::absolute(fs::path(__FILE__)).parent_path().parent_path();
#endif
}

std::string Read(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}
/// The entries of a folder; none (not an exception that ends the test) when it is missing or can't be read.
std::vector<fs::path> FilesIn(const fs::path& dir) {
    std::vector<fs::path> v;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) v.push_back(it->path());
    return v;
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
    CHECK(Cli({}, s) == 0 && Has(s.out, "Projects:") && Has(s.out, "vc save"));
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
    CHECK(Cli({"--project", "/nonexistent-okay", "vc", "history", "-n", "0"}, s) != 0);
    CHECK(Cli({"build", "--target", "playstation"}, s) == 1 && Has(s.err, "has to be made on playstation"));
    CHECK(Cli({"vc", "merge-driver", "a"}, s) == 2);                 // the hidden driver needs three files
    CHECK(Cli({"help", "--project", "x"}, s) == 2);                  // --project is only taken before the command
    CHECK(Cli({"help", "vc", "resolve"}, s) == 0 && Has(s.out, "--theirs"));
    CHECK(Cli({"help", "vc", "clone"}, s) == 0);

    // The shared Trim this CLI added to Core/StringUtil.hpp.
    CHECK(okay::str::Trim("  a b \r\n") == "a b" && okay::str::Trim("\t\n").empty() && okay::str::Trim("x") == "x");

    // The process seam: a program that is not there is "not started", not a crash.
    const okaycli::RunResult r = okaycli::Run({"okay-no-such-program-xyz"}, "", true);
    CHECK(!r.started);
    CHECK(okaycli::FindOnPath("okay-no-such-program-xyz").empty());
    CHECK(!okaycli::SelfPath().empty());
    // .cmd and .bat files are never started (Windows would run them through cmd.exe).
    CHECK(okaycli::ResolveProgram("C:/tools/git.cmd").empty() && okaycli::ResolveProgram("run.BAT").empty());
    CHECK(!okaycli::Run({"tools/okay.bat"}, "", true).started);
}

#if !defined(_WIN32)
/// A program in the current folder is never started by its bare name, even when PATH has "." or a relative entry.
void TestPathLookup(const fs::path& tmp) {
    RUN_SUITE("okay cli: programs are found only in absolute PATH folders");
    const fs::path bin = tmp / "relbin";
    Write(bin / "okayfakeprog", "#!/bin/sh\nexit 0\n");
    fs::permissions(bin / "okayfakeprog", fs::perms::owner_all);
    Write(tmp / "okayfakeprog2", "#!/bin/sh\nexit 0\n");
    fs::permissions(tmp / "okayfakeprog2", fs::perms::owner_all);
    const char* old = std::getenv("PATH");
    const std::string saved = old ? old : "";
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    fs::current_path(tmp, ec);
    SetEnv("PATH", "relbin:.::" + saved);
    CHECK(okaycli::FindOnPath("okayfakeprog").empty());
    CHECK(okaycli::FindOnPath("okayfakeprog2").empty());
    CHECK(!okaycli::Run({"okayfakeprog2"}, "", true).started);
    SetEnv("PATH", bin.string() + ":" + saved);
    CHECK(fs::path(okaycli::FindOnPath("okayfakeprog")) == bin / "okayfakeprog");
    const okaycli::RunResult r = okaycli::Run({"okayfakeprog"}, "", true);
    CHECK(r.started && r.exitCode == 0);
    SetEnv("PATH", saved);
    fs::current_path(cwd, ec);
}
#endif

void TestIgnoreAndAttributes() {
    RUN_SUITE("okay cli: .gitignore and .gitattributes");
    const Strs ig = okaycli::vc::IgnoreLines();
    auto in = [](const Strs& v, const std::string& l) { for (const auto& x : v) if (x == l) return true; return false; };
    for (const char* l : {"/build/", "/.okay/backups/", "/.okay/build_live.cfg", "/.okay/clashes/", "*.autosave", "*.okayscene.bak",
                          "*.okayprefab.bak", ".okaycache/", "/.okay/projectview.txt", "/.okay/favourites.txt",
                          "/Library/", "/Logs/", "/Temp/", "/Builds/"})   // the project layout's folders that are not project data
        CHECK(in(ig, l));
    CHECK(okaycli::vc::GroupOf("ProjectSettings/project.okayproj") == okaycli::vc::Group::Settings);
    // Nothing that could match a real folder deeper in the project, or a person's own files.
    for (const char* l : {"build/", "Build/", "export/", "Export/", "Library/", "Temp/", "*.log", "*.bak"}) CHECK(!in(ig, l));
    for (const auto& l : ig) if (l.find('/') != std::string::npos && l != ".okaycache/") CHECK(l[0] == '/');
    CHECK(in(okaycli::vc::IgnoreLines("Out/Game"), "/Out/Game/"));   // the Build Settings output folder
    CHECK(in(okaycli::vc::IgnoreLines("Out/Game"), "/build/"));
    CHECK(!in(ig, "Assets/"));
    CHECK(!in(ig, "project.okayproj"));
    int added = 0;
    const std::string first = okaycli::vc::GitIgnoreText("my-own-line\n", "", &added);
    CHECK(first.rfind("my-own-line\n", 0) == 0);          // the person's lines stay first and untouched
    CHECK(added == (int)ig.size());
    CHECK(Has(first, "# added by okay vc init"));
    int again = -1;
    CHECK(okaycli::vc::GitIgnoreText(first, "", &again) == first && again == 0);   // running it twice adds nothing
    const Strs at = okaycli::vc::AttributeLines();
    CHECK(in(at, "*.okayscene text eol=lf merge=okayscene diff=okayscene"));
    CHECK(in(at, "*.okayprefab text eol=lf merge=okayscene diff=okayscene"));
    for (const char* b : {"*.png binary", "*.wav binary", "*.fbx binary", "*.ttf binary", "*.glb binary", "*.tif binary", "*.tiff binary",
                          "*.mp4 binary", "*.webm binary", "*.mov binary", "*.m4a binary", "*.opus binary", "*.aiff binary",
                          "*.bin binary", "*.okaypkg binary", "*.okaynav binary"})
        CHECK(in(at, b));
    CHECK(okaycli::vc::MergeDriverCommand("C:\\Program Files\\okay.exe") == "\"C:/Program Files/okay.exe\" vc merge-driver %O %A %B %P");
    CHECK(okaycli::vc::DiffDriverCommand("/opt/my $dir/okay") == "\"/opt/my \\$dir/okay\" vc diff-driver");
}

/// Is a path inside a folder: the vc commands ask this of the file named on the command line and git's top folder.
void TestRelInside(const fs::path& tmp) {
    RUN_SUITE("okay cli: a path inside a folder, however the two are written");
    using okaycli::detail::LongPath;
    using okaycli::detail::RelInside;
    const fs::path top = tmp / "inside" / "Game";
    fs::create_directories(top / "Assets");
    CHECK(RelInside((top / "Assets" / "Level.okayscene").string(), top.string()) == "Assets/Level.okayscene");   // need not exist
    CHECK(RelInside((top / "Assets" / ".." / "a.txt").string(), top.string()) == "a.txt");
    CHECK(RelInside((top / "Assets").string(), top.string() + "/") == "Assets");
    CHECK(RelInside(top.string(), top.string()).empty());                                        // the folder itself
    CHECK(RelInside((tmp / "inside" / "Game2" / "a.txt").string(), top.string()).empty());       // a name that only starts the same
    CHECK(RelInside((tmp / "inside" / "a.txt").string(), top.string()).empty());
    CHECK(RelInside((top / ".." / "Game" / "b.txt").string(), top.string()) == "b.txt");
    CHECK(LongPath((top / "Assets" / "").string()) == LongPath((top / "Assets").string()));     // no trailing slash
    CHECK(LongPath(top.string()).find('\\') == std::string::npos);
#if defined(_WIN32)
    // Windows: case and slashes do not matter, and a folder's short 8.3 name is the same folder as its long one.
    std::string upper = top.string();
    for (char& c : upper) c = (char)std::toupper((unsigned char)c);
    CHECK(RelInside(upper + "\\Assets\\X.png", top.generic_string()) == "Assets/X.png");
    wchar_t shortName[1024] = {0};
    if (GetShortPathNameW(top.wstring().c_str(), shortName, 1024) > 0)
        CHECK(RelInside(fs::path(shortName).string() + "\\Assets\\Y.png", top.string()) == "Assets/Y.png");
#endif
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
    CHECK(okaycli::vc::GroupOf("Assets/Scripts/ai.okay") == Group::Scripts);
}

void TestProjects(const fs::path& tmp) {
    RUN_SUITE("okay cli: new (empty), finding the project, okay test");
    Said s;
    const fs::path where = tmp / "projects";
    fs::create_directories(where);
    CHECK(Cli({"new", "My Game", "--in", where.string(), "--no-git", "--version", "0.3.0"}, s) == 0);
    const fs::path root = where / "My Game";
    CHECK(fs::is_regular_file(root / "ProjectSettings" / "project.okayproj"));
    CHECK(fs::is_regular_file(root / "Assets" / "Scenes" / "My Game.okayscene"));
    CHECK(fs::is_regular_file(root / "Assets" / "input.okayinput"));
    const std::string settings = Read(root / "ProjectSettings" / "project.okayproj");
    CHECK(Has(settings, "version=0.3.0"));
    CHECK(Has(settings, "engine_version="));
    CHECK(!fs::exists(root / ".git"));
    // A second project with the same name gets its own folder.
    CHECK(Cli({"new", "My Game", "--in", where.string(), "--no-git"}, s) == 0 && Has(s.out, "My Game-2"));
    CHECK(Cli({"new", "bad/name", "--in", where.string(), "--no-git"}, s) == 1);

    CHECK(fs::path(okaycli::FindProjectRoot((root / "Assets").string())) == root);
    CHECK(fs::path(okaycli::FindProjectRoot((root / ".").string())).filename() == "My Game");   // no trailing slash from "."
    CHECK(fs::path(okaycli::FindProjectRoot((root / "Assets" / "").string())) == root);
    CHECK(fs::path(okaycli::FindProjectRoot((root / "Assets" / "Scenes" / "My Game.okayscene").string())) == root);
    CHECK(okaycli::FindProjectRoot(tmp.string()).empty());
    CHECK(fs::path(okaycli::MainScene(root.string())) == root / "Assets" / "Scenes" / "My Game.okayscene");
    Write(root / "Assets" / "Levels" / "Two.okayscene", LevelText("1", "2"));
    Write(root / ".okay" / "scene_build.txt", "# list\n0|Assets/My Game.okayscene\n1|Assets/Levels/Two.okayscene\n");
    CHECK(fs::path(okaycli::MainScene(root.string())) == root / "Assets" / "Levels" / "Two.okayscene");

    CHECK(Cli({"--project", root.string(), "info"}, s) == 0 && Has(s.out, "Project: My Game") && Has(s.out, "Game version: 0.3.0"));
    CHECK(Cli({"--project", root.string(), "test"}, s) == 0 && Has(s.out, "Checked 2 scenes") && Has(s.out, "Everything loads"));
    Write(root / "Assets" / "Broken.okayscene", "okayscene 3\n<<<<<<< ours\nname \"x\"\n=======\nname \"y\"\n>>>>>>> theirs\n");
    CHECK(Cli({"--project", root.string(), "test"}, s) == 1 && Has(s.err, "Broken.okayscene: still has merge clash marks"));
    fs::remove(root / "Assets" / "Broken.okayscene");
    CHECK(Cli({"--project", tmp.string(), "play"}, s) == 1 && Has(s.err, "not an OkaySpace project"));
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
    std::error_code cec;
    const fs::path cwd = fs::current_path(cec);
    fs::current_path(d, cec);   // not a repository: the clash report goes into this folder's .okay/clashes
    CHECK(okaycli::SceneMergeDriver((d / "base").string(), (d / "ours").string(), (d / "theirs").string(), "Level.okayscene", s.io) == 1);
    fs::current_path(cwd, cec);
    CHECK(Has(Read(d / ".okay" / "clashes" / "Level.okayscene.txt"), "they made it 7"));
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
    CHECK(Has(Read(root / ".gitignore"), "/.okay/backups/"));
    CHECK(Has(Read(root / ".gitattributes"), "*.okayscene text eol=lf merge=okayscene diff=okayscene"));
    CHECK(Has(GitOut(root, {"config", "merge.okayscene.driver"}), "vc merge-driver %O %A %B %P"));
    CHECK(Has(GitOut(root, {"config", "diff.okayscene.command"}), "vc diff-driver"));
    CHECK(GitOut(root, {"symbolic-ref", "--short", "HEAD"}).rfind("main", 0) == 0);
    CHECK(Cli({"--project", P, "vc", "init"}, s) == 0 && Has(s.out, "already had everything"));   // safe to run again
    // The same project named another way (on Windows the temp folder's short name C:\Users\RUNNER~1 while git prints
    // the long one; here a symbolic link) is still this repository, not "inside another Git folder".
    const fs::path alias = tmp / "vc-alias";
    std::error_code linkEc;
    fs::create_directory_symlink(where, alias, linkEc);
    if (!linkEc) {
        const std::string PA = (alias / "Game").string();
        CHECK(Cli({"--project", PA, "vc", "init"}, s) == 0 && Has(s.out, "already had everything"));
        CHECK(okaycli::detail::RelInside((alias / "Game" / "Assets" / "x.png").string(), root.string()) == "Assets/x.png");
    }
    // A .gitignore with Windows line ends (git's autocrlf, a Windows editor): its lines still count, and lines added
    // to it get the same line ends.
    {
        const std::string lf = Read(root / ".gitignore");
        std::string crlf;
        for (char c : lf) { if (c == '\n') crlf += '\r'; crlf += c; }
        Write(root / ".gitignore", crlf);
        CHECK(Cli({"--project", P, "vc", "init"}, s) == 0 && Has(s.out, ".gitignore already had everything"));
        CHECK(Read(root / ".gitignore") == crlf);
        int added = 0;
        const std::string more = okaycli::vc::GitIgnoreText(crlf, "Out/Win", &added);
        bool allCrlf = true;
        for (std::size_t i = crlf.size(); i < more.size(); ++i) if (more[i] == '\n' && more[i - 1] != '\r') allCrlf = false;
        CHECK(added == 1 && Has(more, "\r\n/Out/Win/\r\n") && allCrlf);
        Write(root / ".gitignore", lf);
    }

    CHECK(Cli({"--project", P, "vc", "status"}, s) == 0 && Has(s.out, "Scenes and prefabs (1)") && Has(s.out, "Nothing has been saved yet"));
    CHECK(Cli({"--project", P, "vc", "status", "--json"}, s) == 0 && Has(s.out, "\"saved\":false") && Has(s.out, "\"state\":\"new\"") &&
          Has(s.out, "\"group\":\"Scenes and prefabs\"") && Has(s.out, "\"clashes\":[]"));
    // Saving needs a name first; vc who stores it in this project's own git config (not for every project).
    CHECK(Cli({"--project", P, "vc", "who", "Test Person", "test@example.invalid"}, s) == 0 && Has(s.out, "in this project"));
    CHECK(Cli({"--project", P, "vc", "who"}, s) == 0 && Has(s.out, "Test Person"));
    CHECK(Has(GitOut(root, {"config", "--local", "user.name"}), "Test Person"));
    CHECK(!fs::exists(home / ".gitconfig") || !Has(Read(home / ".gitconfig"), "Test Person"));
    CHECK(Cli({"--project", tmp.string(), "vc", "who", "X", "x@example.invalid"}, s) == 1);   // no repository and no --global
    CHECK(Cli({"--project", P, "vc", "save"}, s) == 2);   // no message
    const fs::path scene = root / "Assets" / "Level.okayscene";
    Write(scene, LevelText("0", "0"));
    CHECK(Cli({"--project", P, "vc", "save", "First version"}, s) == 0 && Has(s.out, "Saved") && Has(s.out, "First version"));
    CHECK(Cli({"--project", P, "vc", "status"}, s) == 0 && Has(s.out, "Everything is saved"));
    CHECK(Cli({"--project", P, "vc", "save", "again"}, s) == 0 && Has(s.out, "nothing to save"));
    // Nothing ignored made it in: build output and backups stay out.
    Write(root / "build" / "Game.exe", "x");
    Write(root / ".okay" / "backups" / "Level.okayscene.1", "x");
    Write(root / "Assets" / "Level.okayscene.bak", "x");
    CHECK(Cli({"--project", P, "vc", "status"}, s) == 0 && Has(s.out, "Everything is saved"));
    // ...but folders and files of the same names deeper in Assets are the person's own work and are saved.
    for (const char* f : {"Assets/Models/Export/rock.png", "Assets/build/wall.okaymesh", "Assets/Temp/t.png", "Assets/Library/l.png",
                          "Assets/notes.log", "Assets/Old.bak"})
        Write(root / f, "x");
    CHECK(Cli({"--project", P, "vc", "status"}, s) == 0 && Has(s.out, "Assets (6)") && Has(s.out, "Assets/Models/Export/rock.png") &&
          Has(s.out, "Assets/build/wall.okaymesh"));
    CHECK(Cli({"--project", P, "vc", "save", "Own folders"}, s) == 0 && Has(s.out, "Saved 6 changes"));
    // The Build Settings output folder is kept out too.
    Write(root / ".okay" / "build_live.cfg", "# OkaySpace build settings\n@profile live\ndir=Out/Game\n");
    Write(root / "Out" / "Game" / "Game.exe", "x");
    CHECK(Cli({"--project", P, "vc", "init"}, s) == 0);
    CHECK(Has(Read(root / ".gitignore"), "\n/Out/Game/\n"));
    CHECK(Cli({"--project", P, "vc", "status"}, s) == 0 && !Has(s.out, "Out/Game") && Has(s.out, "changed  .gitignore"));
    // --project is taken only before the command: here it is part of the message.
    CHECK(Cli({"--project", P, "vc", "save", "--", "fix --project thing"}, s) == 0);
    CHECK(Cli({"--project", P, "vc", "history", "-n", "1"}, s) == 0 && Has(s.out, "fix --project thing"));

    // Change, history, undo (with a kept copy).
    Write(scene, LevelText("4", "0"));
    CHECK(Cli({"--project", P, "vc", "status"}, s) == 0 && Has(s.out, "changed  Assets/Level.okayscene"));
    CHECK(Cli({"--project", P, "vc", "save", "Moved A"}, s) == 0);
    Write(scene, LevelText("8", "0"));
    CHECK(Cli({"--project", P, "vc", "history", scene.string()}, s) == 0 && Has(s.out, "Moved A") && Has(s.out, "First version"));
    CHECK(Cli({"--project", P, "vc", "history", "-n", "1"}, s) == 0 && Has(s.out, "Moved A") && !Has(s.out, "First version"));
    CHECK(Cli({"--project", P, "vc", "undo", scene.string()}, s) == 0 && Has(s.out, "is back to how it was"));
    okay::Vec3 a, b;
    CHECK(PositionOf(Read(scene), "A", a) && a.x == 4.0f);
    bool kept = false;
    for (const fs::path& e : FilesIn(root / ".okay" / "backups" / "undo"))
        if (PositionOf(Read(e), "A", b) && b.x == 8.0f) kept = true;
    CHECK(kept);
    CHECK(Cli({"--project", P, "vc", "undo", scene.string()}, s) == 0 && Has(s.out, "no changes since the last save"));
    const std::string first = GitOut(root, {"rev-list", "--max-parents=0", "HEAD"}).substr(0, 7);
    CHECK(Cli({"--project", P, "vc", "undo", scene.string(), "--to", first}, s) == 0);
    CHECK(PositionOf(Read(scene), "A", a) && a.x == 0.0f);
    GitOut(root, {"checkout", "HEAD", "--", scene.string()});
    CHECK(Cli({"--project", P, "vc", "undo", scene.string(), "--to", "-q"}, s) == 1 && Has(s.err, "no save called"));
    CHECK(Cli({"--project", P, "vc", "undo", (root / "Assets").string()}, s) == 1 && Has(s.err, "is a folder"));
    // Kept copies never overwrite each other: same file name in two folders, and the same file twice in one second.
    const fs::path xa = root / "Assets" / "A" / "x.okayscene", xb = root / "Assets" / "B" / "x.okayscene";
    Write(xa, LevelText("1", "0"));
    Write(xb, LevelText("2", "0"));
    CHECK(Cli({"--project", P, "vc", "save", "two x"}, s) == 0);
    for (int round = 0; round < 2; ++round) {
        Write(xa, LevelText(std::to_string(10 + round), "0"));
        Write(xb, LevelText(std::to_string(20 + round), "0"));
        CHECK(Cli({"--project", P, "vc", "undo", xa.string()}, s) == 0);
        CHECK(Cli({"--project", P, "vc", "undo", xb.string()}, s) == 0);
    }
    int copiesA = 0, copiesB = 0;
    for (const fs::path& e : FilesIn(root / ".okay" / "backups" / "undo")) {
        const std::string n = e.filename().string();
        if (n.rfind("Assets~A~x.okayscene.", 0) == 0) ++copiesA;
        if (n.rfind("Assets~B~x.okayscene.", 0) == 0) ++copiesB;
    }
    CHECK(copiesA == 2 && copiesB == 2);
    Write(root / "Assets" / "Never.okayscene", LevelText("1", "1"));
    CHECK(Cli({"--project", P, "vc", "undo", (root / "Assets" / "Never.okayscene").string()}, s) == 1 && Has(s.err, "was never saved"));
    fs::remove(root / "Assets" / "Never.okayscene");
    // A file named relative to --project, from another folder (this program runs in the build folder).
    Write(scene, LevelText("6", "0"));
    CHECK(Cli({"--project", P, "vc", "history", "Assets/Level.okayscene"}, s) == 0 && Has(s.out, "Moved A"));
    CHECK(Cli({"--project", P, "vc", "undo", "Assets/Level.okayscene"}, s) == 0 && Has(s.out, "is back to how it was"));
    CHECK(PositionOf(Read(scene), "A", a) && a.x == 4.0f);
    // File names are not patterns: [x] names the file "[x].okayscene", not x.okayscene, and * is just a letter.
    const fs::path gx = root / "Assets" / "G" / "x.okayscene", gb = root / "Assets" / "G" / "[x].okayscene";
    Write(gx, LevelText("1", "0"));
    Write(gb, LevelText("2", "0"));
    CHECK(Cli({"--project", P, "vc", "save", "glob names"}, s) == 0);
    Write(gx, LevelText("3", "0"));   // only x.okayscene changes
    CHECK(Cli({"--project", P, "vc", "undo", "Assets/G/[x].okayscene"}, s) == 0 && Has(s.out, "no changes since the last save"));
    CHECK(PositionOf(Read(gx), "A", a) && a.x == 3.0f);   // the other file was not put back
    CHECK(Cli({"--project", P, "vc", "history", "Assets/G/*.okayscene"}, s) == 0 && Has(s.out, "has never been saved"));
    CHECK(Cli({"--project", P, "vc", "undo", "Assets/G/x.okayscene"}, s) == 0 && Has(s.out, "is back to how it was"));

    // Branches: a clean three-way scene merge through git's merge driver.
    CHECK(Cli({"--project", P, "vc", "branch"}, s) == 0 && Has(s.out, "* main"));
    CHECK(Cli({"--project", P, "vc", "branch", "try-idea"}, s) == 0 && Has(s.out, "you are on it now"));
    Write(scene, LevelText("4", "6"));   // B moved on the new line
    CHECK(Cli({"--project", P, "vc", "switch", "main"}, s) == 1 && Has(s.err, "not saved yet"));
    CHECK(Cli({"--project", P, "vc", "save", "Moved B"}, s) == 0);
    CHECK(Cli({"--project", P, "vc", "switch", "main"}, s) == 0 && Has(s.out, "You are on main"));
    CHECK(Cli({"--project", P, "vc", "switch", "nope"}, s) == 1 && Has(s.err, "no version line called nope"));
    Write(scene, LevelText("4", "0", "Crate"));   // C renamed on main
    CHECK(Cli({"--project", P, "vc", "save", "Renamed C"}, s) == 0);
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
    CHECK(Cli({"--project", P, "vc", "branch", "other"}, s) == 0);
    Write(scene, LevelText("11", "6", "Crate"));
    CHECK(Cli({"--project", P, "vc", "save", "A to 11"}, s) == 0);
    CHECK(Cli({"--project", P, "vc", "switch", "main"}, s) == 0);
    Write(scene, LevelText("22", "6", "Crate"));
    CHECK(Cli({"--project", P, "vc", "save", "A to 22"}, s) == 0);
    const std::string clash = GitOut(root, {"merge", "--no-edit", "other"}, &code);
    CHECK(code != 0);
    CHECK(Has(clash, "change clashes") && Has(clash, "you made it"));   // the driver's plain words reach the person
    CHECK(!Has(Read(scene), "<<<<<<<"));
    CHECK(PositionOf(Read(scene), "A", a) && a.x == 22.0f);   // loads, with our side kept
    // Nothing of the other side is lost silently: a report lists both values, and save refuses until it is settled.
    const fs::path report = root / ".okay" / "clashes" / "Assets" / "Level.okayscene.txt";
    CHECK(fs::is_regular_file(report));
    CHECK(Has(Read(report), "you made it 22") && Has(Read(report), "they made it 11"));
    CHECK(Cli({"--project", P, "vc", "status"}, s) == 0 && Has(s.out, "clash") && Has(s.out, "Clashes to settle (1)"));
    CHECK(Cli({"--project", P, "vc", "save", "Merged"}, s) == 1 && Has(s.err, "not settled"));
    CHECK(Cli({"--project", P, "vc", "resolve", scene.string()}, s) == 2);   // a choice is needed
    CHECK(Cli({"--project", P, "vc", "resolve", scene.string(), "--theirs"}, s) == 0 && Has(s.out, "took theirs"));
    CHECK(PositionOf(Read(scene), "A", a) && a.x == 11.0f);                  // their value for the clash...
    CHECK(PositionOf(Read(scene), "B", b) && b.y == 6.0f);                   // ...and every change that did not clash
    CHECK(!fs::exists(report));
    CHECK(Cli({"--project", P, "vc", "save", "Merged"}, s) == 0);   // finishing the merge is a plain save
    CHECK(Cli({"--project", P, "vc", "status"}, s) == 0 && Has(s.out, "Everything is saved"));

    // --mine keeps your side of the clash and still takes their changes that did not clash.
    CHECK(Cli({"--project", P, "vc", "branch", "other2"}, s) == 0);
    Write(scene, LevelText("31", "6", "Box"));
    CHECK(Cli({"--project", P, "vc", "save", "A to 31, Box"}, s) == 0);
    CHECK(Cli({"--project", P, "vc", "switch", "main"}, s) == 0);
    Write(scene, LevelText("32", "6", "Crate"));
    CHECK(Cli({"--project", P, "vc", "save", "A to 32"}, s) == 0);
    GitOut(root, {"merge", "--no-edit", "other2"}, &code);
    CHECK(code != 0);
    CHECK(Cli({"--project", P, "vc", "resolve", scene.string(), "--mine"}, s) == 0 && Has(s.out, "kept yours"));
    CHECK(PositionOf(Read(scene), "A", a) && a.x == 32.0f);
    { okay::Scene pr("p"); CHECK(okay::SceneSerializer::Deserialize(pr, Read(scene)) && pr.Find("Box") != nullptr); }
    CHECK(Cli({"--project", P, "vc", "save", "Merged 2"}, s) == 0);

    // --as-is keeps the file as the person fixed it by hand.
    CHECK(Cli({"--project", P, "vc", "branch", "other3"}, s) == 0);
    Write(scene, LevelText("41", "6", "Box"));
    CHECK(Cli({"--project", P, "vc", "save", "A to 41"}, s) == 0);
    CHECK(Cli({"--project", P, "vc", "switch", "main"}, s) == 0);
    Write(scene, LevelText("42", "6", "Box"));
    CHECK(Cli({"--project", P, "vc", "save", "A to 42"}, s) == 0);
    GitOut(root, {"merge", "--no-edit", "other3"}, &code);
    CHECK(code != 0);
    Write(scene, LevelText("43", "6", "Box"));
    CHECK(Cli({"--project", P, "vc", "resolve", scene.string(), "--as-is"}, s) == 0);
    CHECK(Cli({"--project", P, "vc", "save", "Merged 3"}, s) == 0);
    CHECK(PositionOf(Read(scene), "A", a) && a.x == 43.0f);

    // In a rebase git swaps "ours" and "theirs": the words and --mine still mean the person's own change.
    CHECK(Cli({"--project", P, "vc", "branch", "rb"}, s) == 0);
    Write(scene, LevelText("51", "6", "Box"));
    CHECK(Cli({"--project", P, "vc", "save", "A to 51"}, s) == 0);
    CHECK(Cli({"--project", P, "vc", "switch", "main"}, s) == 0);
    Write(scene, LevelText("52", "6", "Box"));
    CHECK(Cli({"--project", P, "vc", "save", "A to 52"}, s) == 0);
    CHECK(Cli({"--project", P, "vc", "switch", "rb"}, s) == 0);
    const std::string rebaseSaid = GitOut(root, {"rebase", "main"}, &code);
    CHECK(code != 0);
    CHECK(Has(rebaseSaid, "you made it 51") && Has(rebaseSaid, "they made it 52"));
    CHECK(Cli({"--project", P, "vc", "resolve", scene.string(), "--mine"}, s) == 0);
    CHECK(PositionOf(Read(scene), "A", a) && a.x == 51.0f);
    GitOut(root, {"-c", "core.editor=true", "rebase", "--continue"}, &code);
    CHECK(code == 0);
    CHECK(Cli({"--project", P, "vc", "switch", "main"}, s) == 0);

    // A driver path that went stale (the launcher removed that version) is pointed at the stable okay again.
    GitOut(root, {"config", "--local", "merge.okayscene.driver", "\"/nonexistent/okay\" vc merge-driver %O %A %B %P"});
    CHECK(Cli({"--project", P, "vc", "status"}, s) == 0 && Has(s.err, "is gone"));
    CHECK(!Has(GitOut(root, {"config", "merge.okayscene.driver"}), "/nonexistent/"));
    // The stored command is exactly what vc init writes: the stable okay (an okay on PATH wins, as on the Windows runner,
    // where wt/bin is on PATH), quoted with forward slashes. Comparing with SelfPath() failed there on both counts.
    CHECK(okay::str::Trim(GitOut(root, {"config", "merge.okayscene.driver"})) ==
          okaycli::vc::MergeDriverCommand(okaycli::detail::StableOkayPath()));

    // Sync with an online copy: here a bare repository in a folder (any git host works the same way).
    const fs::path bare = tmp / "server.git";
    GitOut(tmp, {"init", "-q", "--bare", bare.string()});
    GitOut(tmp, {"--git-dir", bare.string(), "symbolic-ref", "HEAD", "refs/heads/main"});   // so a clone checks out main
    CHECK(Cli({"--project", P, "vc", "sync"}, s) == 1 && Has(s.err, "no online copy"));
    CHECK(Cli({"--project", P, "vc", "remote", bare.string()}, s) == 0);
    CHECK(Cli({"--project", P, "vc", "remote"}, s) == 0 && Has(s.out, bare.string()));
    CHECK(Cli({"--project", P, "vc", "sync"}, s) == 0 && Has(s.out, "Synced"));
    CHECK(okay::str::Trim(GitOut(bare, {"--git-dir", bare.string(), "rev-parse", "main"})) == okay::str::Trim(GitOut(root, {"rev-parse", "HEAD"})));
    // A second person: clone, change B, sync; the first person syncs and gets it.
    const fs::path second = tmp / "second";
    CHECK(Cli({"vc", "clone", bare.string(), second.string()}, s) == 0);   // join an existing project: clone and set up
    CHECK(Has(GitOut(second, {"config", "merge.okayscene.driver"}), "vc merge-driver"));
    CHECK(fs::is_regular_file(second / ".gitignore"));
    CHECK(Cli({"--project", second.string(), "vc", "who", "--global", "Second Person", "second@example.invalid"}, s) == 0);
    Write(second / "Assets" / "Level.okayscene", LevelText("22", "33", "Crate"));
    CHECK(Cli({"--project", second.string(), "vc", "save", "B to 33"}, s) == 0);
    CHECK(Cli({"--project", second.string(), "vc", "sync"}, s) == 0);
    CHECK(Cli({"--project", P, "vc", "sync"}, s) == 0 && Has(s.out, "took in 1 new save"));
    CHECK(PositionOf(Read(scene), "B", b) && b.y == 33.0f);
    // A clash through sync: the clash list with both values is printed, then settled and sent.
    Write(second / "Assets" / "Level.okayscene", LevelText("61", "33", "Box"));
    CHECK(Cli({"--project", second.string(), "vc", "save", "A to 61"}, s) == 0);
    CHECK(Cli({"--project", second.string(), "vc", "sync"}, s) == 0);
    Write(scene, LevelText("62", "33", "Box"));
    CHECK(Cli({"--project", P, "vc", "save", "A to 62"}, s) == 0);
    CHECK(Cli({"--project", P, "vc", "sync"}, s) == 1 && Has(s.err, "you made it 62") && Has(s.err, "they made it 61") &&
          Has(s.err, "These files clash"));
    CHECK(Cli({"--project", P, "vc", "resolve", scene.string(), "--mine"}, s) == 0);
    CHECK(Cli({"--project", P, "vc", "save", "Merged sync"}, s) == 0);
    CHECK(Cli({"--project", P, "vc", "sync"}, s) == 0);

    // A project inside a bigger git folder: init refuses to change the outer one unless asked.
    const fs::path outer = tmp / "outer";
    fs::create_directories(outer);
    GitOut(outer, {"init", "-q"});
    CHECK(Cli({"new", "Inner", "--in", outer.string(), "--no-git"}, s) == 0);
    CHECK(Cli({"--project", (outer / "Inner").string(), "vc", "init"}, s) == 1 && Has(s.err, "inside another Git folder"));
    CHECK(!Has(GitOut(outer, {"config", "--local", "--get", "merge.okayscene.driver"}), "merge-driver"));
    CHECK(Cli({"--project", (outer / "Inner").string(), "vc", "init", "--separate"}, s) == 0);
    CHECK(fs::exists(outer / "Inner" / ".git"));

    CHECK(Cli({"--project", P, "doctor"}, s) >= 0 && Has(s.out, "Git merges and compares scenes with the engine"));
    CHECK(Cli({"--project", P, "info"}, s) == 0 && Has(s.out, "Version line: main") && Has(s.out, "Online copy: " + bare.string()));
}

// ---- okay engine build / doctor ----------------------------------------------------------------------------------

void UnsetEnv(const char* k) {
#if defined(_WIN32)
    _putenv_s(k, "");
#else
    unsetenv(k);
#endif
}

void TestEngineArgs() {
    RUN_SUITE("okay engine: build arguments");
    using okaycli::engine::BuildOptions;
    using okaycli::engine::ParseBuildArgs;
    BuildOptions o;
    std::string e;
    CHECK(ParseBuildArgs({}, o, e) && o.what == "all" && !o.debug && o.jobs == 0 && !o.clean && !o.run && o.dir == "build-local");
    o = BuildOptions{};
    CHECK(ParseBuildArgs({"Editor", "--debug", "-j", "4", "--clean", "--run", "--dir", "out", "--source", "src"}, o, e));
    CHECK(o.what == "editor" && o.debug && o.jobs == 4 && o.clean && o.run && o.dir == "out" && o.source == "src");
    CHECK(!ParseBuildArgs({"--sdl2", "sd"}, o, e));   // SDL2 is not needed any more (2026-11-27): the option is gone
    o = BuildOptions{};
    CHECK(ParseBuildArgs({"launcher", "--jobs", "12"}, o, e) && o.what == "launcher" && o.jobs == 12);
    CHECK(!ParseBuildArgs({"sandbox"}, o, e) && Has(e, "editor, launcher, player or all"));
    CHECK(!ParseBuildArgs({"editor", "player"}, o, e));
    CHECK(!ParseBuildArgs({"--jobs", "0"}, o, e) && Has(e, "--jobs"));
    CHECK(!ParseBuildArgs({"--jobs", "x"}, o, e));
    CHECK(!ParseBuildArgs({"--bogus"}, o, e));

    Said s;
    CHECK(Cli({"engine"}, s) == 0 && Has(s.out, "engine build") && Has(s.out, "engine doctor"));
    CHECK(Cli({"help", "engine"}, s) == 0 && Has(s.out, "engine build"));
    CHECK(Cli({"help", "engine", "build"}, s) == 0 && !Has(s.out, "--sdl2") && Has(s.out, "--source"));
    CHECK(Cli({"help"}, s) == 0 && Has(s.out, "engine"));
    CHECK(Cli({"engine", "nosuch"}, s) == 2 && Has(s.err, "no okay engine command"));
    CHECK(Cli({"engine", "build", "--jobs", "0"}, s) == 2);
    CHECK(Cli({"engine", "build", "--help"}, s) == 0 && Has(s.out, "OkaySpacePlayer.exe"));
}

void TestEngineDetect(const fs::path& tmp) {
    RUN_SUITE("okay engine: finding the checkout and the tools");
    using namespace okaycli::engine;
    // The checkout: a CMakeLists.txt with project(OkaySpace.
    const fs::path src = tmp / "engine-src";
    Write(src / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\nproject( OkaySpace VERSION 1.2.3 LANGUAGES C CXX)\n");
    Write(src / "editor" / "deep" / "x.txt", "x");
    Write(tmp / "other" / "CMakeLists.txt", "project(OkaySpaceTools)\n");
    Write(tmp / "other2" / "CMakeLists.txt", "subproject(OkaySpace)\nproject(Other)\n");
    Write(tmp / "nocmake" / "a.txt", "a");
    CHECK(IsEngineSource(src.string()));
    CHECK(!IsEngineSource((tmp / "other").string()) && !IsEngineSource((tmp / "other2").string()));
    CHECK(!IsEngineSource((tmp / "nocmake").string()));
    CHECK(fs::path(FindEngineSource("", (src / "editor" / "deep").string())) == src);
    CHECK(fs::path(FindEngineSource(src.string(), "/")) == src);
    CHECK(FindEngineSource((tmp / "nocmake").string(), src.string()).empty());   // --source wins, and must be real
    // The real repository is one.
    const fs::path repo = RepoRoot();
    CHECK(IsEngineSource(repo.string()));

#if !defined(_WIN32)
    // The tools: full paths from absolute PATH folders only; vswhere's answer is read.
    const fs::path bin = tmp / "fakebin";
    for (const char* n : {"cmake", "ninja", "g++", "gcc"}) {
        Write(bin / n, "#!/bin/sh\nexit 0\n");
        fs::permissions(bin / n, fs::perms::owner_all);
    }
    const fs::path vsDir = tmp / "VS 2022" / "BuildTools";
    fs::create_directories(vsDir);
    Write(bin / "vswhere", "#!/bin/sh\necho '" + vsDir.string() + "'\n");
    fs::permissions(bin / "vswhere", fs::perms::owner_all);
    const char* old = std::getenv("PATH");
    const std::string saved = old ? old : "";
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    fs::current_path(tmp, ec);
    SetEnv("PATH", "fakebin:" + bin.string());
    const Tools t = FindTools((bin / "vswhere").string());
    CHECK(fs::path(t.cmake) == bin / "cmake" && fs::path(t.ninja) == bin / "ninja" && fs::path(t.gxx) == bin / "g++" && fs::path(t.gcc) == bin / "gcc");
    CHECK(fs::path(t.visualStudio) == vsDir);
    SetEnv("PATH", "fakebin");   // a relative PATH entry is never searched
    CHECK(FindTools((tmp / "no-vswhere").string()).cmake.empty());
    SetEnv("PATH", saved);
    fs::current_path(cwd, ec);
#endif
}

void TestEnginePlan(const fs::path& tmp) {
    RUN_SUITE("okay engine: the plan and the cmake command lines (as build-local.ps1 builds them)");
    using namespace okaycli::engine;
    const std::string src = (tmp / "engine-src").string();
    Tools win;
    win.windows = true;
    win.cmake = "C:/CMake/bin/cmake.exe";
    win.ninja = "C:/tools/ninja.exe";
    win.gxx = "C:/mingw64/bin/g++.exe";
    win.gcc = "C:/mingw64/bin/gcc.exe";
    BuildOptions o;
    std::string e;
    CHECK(ParseBuildArgs({"editor", "-j", "4"}, o, e));
    Plan p;
    CHECK(MakePlan(o, win, src, p, e));
    const std::string build = (fs::path(src) / "build-local").string();
    // build-local.ps1 -Target Editor -Config Release -Jobs 4, with ninja on PATH, passes exactly these
    // (in this order), and okay adds the tools by full path at the end.
    const Strs ps1Configure = {"-S", src, "-B", build, "-DOKAY_BUILD_EDITOR=ON", "-DOKAY_BUILD_LAUNCHER=OFF",
                               "-DOKAY_BUILD_PLAYER=OFF", "-DOKAY_BUILD_SANDBOX=OFF", "-DOKAY_BUILD_TESTS=OFF",
                               "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release"};
    Strs want = ps1Configure;
    for (const char* x : {"-DCMAKE_MAKE_PROGRAM=C:/tools/ninja.exe", "-DCMAKE_C_COMPILER=C:/mingw64/bin/gcc.exe",
                          "-DCMAKE_CXX_COMPILER=C:/mingw64/bin/g++.exe"})
        want.push_back(x);
    CHECK(ConfigureArgs(p) == want);
    CHECK(BuildArgs(p) == (Strs{"--build", build, "--config", "Release", "--parallel", "4", "--target", "okay-editor"}));
    CHECK(p.mingw && !p.multiConfig);

    // All (build-local.ps1's default) also builds the sandbox; Debug.
    o = BuildOptions{};
    CHECK(ParseBuildArgs({"--debug", "-j", "2", "--dir", "b2"}, o, e) && MakePlan(o, win, src, p, e));
    const Strs cfg = ConfigureArgs(p);
    CHECK(cfg[4] == "-DOKAY_BUILD_EDITOR=ON" && cfg[5] == "-DOKAY_BUILD_LAUNCHER=ON" && cfg[6] == "-DOKAY_BUILD_PLAYER=ON" &&
          cfg[7] == "-DOKAY_BUILD_SANDBOX=ON" && cfg[11] == "-DCMAKE_BUILD_TYPE=Debug");
    CHECK(BuildArgs(p) == (Strs{"--build", (fs::path(src) / "b2").string(), "--config", "Debug", "--parallel", "2", "--target",
                                "okay-editor", "okayspace-launcher", "okay-player", "sandbox"}));

    // Visual Studio only (no Ninja, no MinGW): no -G and no CMAKE_BUILD_TYPE (as build-local.ps1), the config at build time.
    Tools vs;
    vs.windows = true;
    vs.cmake = win.cmake;
    vs.visualStudio = "C:/Program Files/Microsoft Visual Studio/2022/BuildTools";
    o = BuildOptions{};
    CHECK(ParseBuildArgs({"launcher", "-j", "3"}, o, e) && MakePlan(o, vs, src, p, e));
    CHECK(ConfigureArgs(p) == (Strs{"-S", src, "-B", build, "-DOKAY_BUILD_EDITOR=OFF", "-DOKAY_BUILD_LAUNCHER=ON", "-DOKAY_BUILD_PLAYER=OFF",
                                    "-DOKAY_BUILD_SANDBOX=OFF", "-DOKAY_BUILD_TESTS=OFF"}));
    CHECK(p.multiConfig && !p.mingw);
    fs::create_directories(fs::path(build) / "bin" / "Release");
    CHECK(fs::path(BinDir(p)) == fs::path(build) / "bin" / "Release");

    // What is missing is said with where to get it.
    Tools none;
    none.windows = true;
    CHECK(!MakePlan(o, none, src, p, e) && Has(e, "cmake.org"));
    none.cmake = win.cmake;
    CHECK(!MakePlan(o, none, src, p, e) && Has(e, "visualstudio.microsoft.com") && Has(e, "winlibs.com"));
    Tools noNinja = win;
    noNinja.ninja.clear();
    CHECK(!MakePlan(o, noNinja, src, p, e) && Has(e, "ninja-build"));
    noNinja.mingwMake = "C:/mingw64/bin/mingw32-make.exe";
    CHECK(MakePlan(o, noNinja, src, p, e) && p.generator == "MinGW Makefiles" && p.makeProgram == noNinja.mingwMake);
    CHECK(MakePlan(o, win, src, p, e) && MakePlan(o, vs, src, p, e));   // no third-party library is asked for
    CHECK(!MakePlan(o, win, "", p, e) && Has(e, "--source"));
    o.dir = ".";
    CHECK(!MakePlan(o, win, src, p, e) && Has(e, "checkout itself"));
    o.dir = "..";
    CHECK(!MakePlan(o, win, src, p, e));
    // Off Windows the system compiler is CMake's to find.
    Tools lin;
    lin.windows = false;
    lin.cmake = "/usr/bin/cmake";
    o = BuildOptions{};
    CHECK(MakePlan(o, lin, src, p, e) && p.generator.empty() && !p.multiConfig);
    CHECK(ConfigureArgs(p).back() == "-DCMAKE_BUILD_TYPE=Release");

    int problems = 0;
    const std::string d = DoctorText(none, "", &problems);
    CHECK(problems >= 2 && Has(d, "MISSING") && !Has(d, "SDL2") && Has(d, "--source"));
    const std::string ok = DoctorText(win, src, &problems);
    CHECK(problems == 0 && Has(ok, "Everything needed") && Has(ok, win.cmake));

    // build-local.ps1 still builds the same recipe: its CMake options, targets and defaults are the ones above.
    const fs::path repo = RepoRoot();
    const std::string ps1 = Read(repo / "scripts" / "build-local.ps1");
    std::size_t last = 0;
    bool inOrder = true;
    for (const char* opt : {"-DOKAY_BUILD_EDITOR=", "-DOKAY_BUILD_LAUNCHER=", "-DOKAY_BUILD_PLAYER=", "-DOKAY_BUILD_SANDBOX=",
                            "-DOKAY_BUILD_TESTS=", "'-G', 'Ninja'", "-DCMAKE_BUILD_TYPE=",
                            "'--build'", "'--config'", "'--parallel'", "'okay-editor'", "'okayspace-launcher'", "'okay-player'", "'sandbox'"}) {
        const std::size_t at = ps1.find(opt, last);
        if (at == std::string::npos) { inOrder = false; std::cout << "    build-local.ps1 lacks (in order): " << opt << "\n"; continue; }
        last = at;
    }
    CHECK(inOrder);
    CHECK(Has(ps1, "[Math]::Max(2, [int]([Environment]::ProcessorCount / 2))"));   // the same default job count
    CHECK(Has(ps1, "$BuildDir = 'build-local'") && Has(ps1, "$Config = 'Release'"));
    CHECK(Has(ps1, "okay engine build"));   // and it points at the C++ copy
}

/// What okay engine build --clean may delete, and paths compared with and without case.
void TestEngineClean(const fs::path& tmp) {
    RUN_SUITE("okay engine: --clean deletes only a build folder of this checkout");
    using okaycli::engine::CleanRefusal;
    using okaycli::engine::PathWithin;
    const fs::path src = tmp / "clean-src", b = tmp / "clean-build";
    fs::create_directories(src);
    CHECK(Has(CleanRefusal(b.string(), src.string(), false), "no CMakeCache.txt"));
    Write(b / "CMakeCache.txt", "# a cache\nCMAKE_HOME_DIRECTORY:INTERNAL=" + src.string() + "\nOTHER:STRING=x\n");
    CHECK(CleanRefusal(b.string(), src.string(), false).empty());
    CHECK(CleanRefusal((b / ".").string() + "/", src.string() + "/", false).empty());   // spelled differently, same folders
    Write(b / "CMakeLists.txt", "project(Game)\n");
    CHECK(Has(CleanRefusal(b.string(), src.string(), false), "CMakeLists.txt"));
    fs::remove(b / "CMakeLists.txt");
    Write(b / "project.okayproj", "name Game\n");
    CHECK(Has(CleanRefusal(b.string(), src.string(), false), "project.okayproj"));
    fs::remove(b / "project.okayproj");
    fs::create_directories(b / "Assets");
    CHECK(Has(CleanRefusal(b.string(), src.string(), false), "Assets"));
    fs::remove(b / "Assets");
    Write(b / "CMakeCache.txt", "CMAKE_BUILD_TYPE:STRING=Release\n");   // no source folder named
    CHECK(Has(CleanRefusal(b.string(), src.string(), false), "does not say"));
    Write(b / "CMakeCache.txt", "CMAKE_HOME_DIRECTORY:INTERNAL=" + (tmp / "elsewhere").string() + "\n");
    CHECK(Has(CleanRefusal(b.string(), src.string(), false), "was made from"));
    // Windows compares without case: a cache that names the checkout in other letters is the same checkout there only.
    Write(b / "CMakeCache.txt", "CMAKE_HOME_DIRECTORY:INTERNAL=/No/Such/Engine\n");
    CHECK(CleanRefusal(b.string(), "/no/such/ENGINE", true).empty());
    CHECK(!CleanRefusal(b.string(), "/no/such/ENGINE", false).empty());
    CHECK(PathWithin("/no/Such/engine/x", "/NO/such", true) && !PathWithin("/no/Such/engine/x", "/NO/such", false));
    CHECK(PathWithin("/a/b", "/a/b", false) && PathWithin("/a/b/c", "/a/b/", false));
    CHECK(!PathWithin("/a/bc", "/a/b", false) && !PathWithin("/a", "/a/b", false));
}

#if !defined(_WIN32)
/// The whole of okay engine build against a fake cmake that writes down what it was given.
void TestEngineBuildRun(const fs::path& tmp) {
    RUN_SUITE("okay engine: build runs cmake twice and says where the programs are");
    const fs::path src = tmp / "engine-src";
    const fs::path bin = tmp / "fakebin2";
    const fs::path log = tmp / "cmake-log.txt";
    Write(bin / "cmake", "#!/bin/sh\nfor a in \"$@\"; do printf '%s\\n' \"$a\"; done >> '" + log.string() + "'\necho '--' >> '" + log.string() +
                         "'\nif [ \"$1\" = \"--build\" ]; then /bin/mkdir -p \"$2/bin\"; : > \"$2/bin/OkayEngine\"; printf 'CMAKE_HOME_DIRECTORY:INTERNAL=%s\\n' '" + src.string() + "' > \"$2/CMakeCache.txt\"; fi\nexit 0\n");
    fs::permissions(bin / "cmake", fs::perms::owner_all);
    Write(bin / "ninja", "#!/bin/sh\nexit 0\n");
    fs::permissions(bin / "ninja", fs::perms::owner_all);
    const char* old = std::getenv("PATH");
    const std::string saved = old ? old : "";
    SetEnv("PATH", bin.string());
    Said s;
    const int rc = Cli({"engine", "build", "editor", "--source", src.string(), "-j", "3"}, s);
    SetEnv("PATH", saved);
    CHECK(rc == 0);
    const std::string l = Read(log);
    const fs::path build = src / "build-local";
    CHECK(Has(l, "-S\n" + src.string() + "\n-B\n" + build.string() + "\n-DOKAY_BUILD_EDITOR=ON\n"));
    CHECK(Has(l, "-G\nNinja\n") && Has(l, "-DCMAKE_MAKE_PROGRAM=" + (bin / "ninja").string()));
    CHECK(!Has(l, "-DCMAKE_PREFIX_PATH="));   // no third-party library to point CMake at
    CHECK(Has(l, "--build\n" + build.string() + "\n--config\nRelease\n--parallel\n3\n--target\nokay-editor\n--"));
    CHECK(Has(s.out, "the editor: " + (build / "bin" / "OkayEngine").string()));
    // --clean deletes only a CMake build folder.
    fs::create_directories(src / "notabuild");
    Write(src / "notabuild" / "keep.txt", "mine");
    SetEnv("PATH", bin.string());
    CHECK(Cli({"engine", "build", "editor", "--source", src.string(), "--dir", "notabuild", "--clean"}, s) == 1 && Has(s.err, "not a CMake build folder"));
    CHECK(Cli({"engine", "build", "editor", "--source", src.string(), "--clean"}, s) == 0 && Has(s.out, "Removing"));
    CHECK(Cli({"engine", "build", "--source", (tmp / "nocmake").string()}, s) == 1 && Has(s.err, "not an engine checkout"));
    // --clean never deletes a game project that has a CMakeCache.txt in it, nor a build of another checkout.
    const fs::path game = tmp / "a-game";
    Write(game / "CMakeCache.txt", "CMAKE_HOME_DIRECTORY:INTERNAL=" + src.string() + "\n");
    Write(game / "Assets" / "Level.okayscene", "mine");
    CHECK(Cli({"engine", "build", "editor", "--source", src.string(), "--dir", game.string(), "--clean"}, s) == 1 && Has(s.err, "Assets folder"));
    const fs::path other = tmp / "other-build";
    Write(other / "CMakeCache.txt", "CMAKE_HOME_DIRECTORY:INTERNAL=" + (tmp / "another-engine").string() + "\n");
    CHECK(Cli({"engine", "build", "editor", "--source", src.string(), "--dir", other.string(), "--clean"}, s) == 1 && Has(s.err, "was made from"));
    SetEnv("PATH", saved);
    CHECK(fs::exists(src / "notabuild" / "keep.txt"));
    CHECK(fs::exists(game / "Assets" / "Level.okayscene") && fs::exists(other / "CMakeCache.txt"));
}
#endif

// ---- okay project / scene / script / vs / asset, help, completion and --json -------------------------------------

void TestHelpAndCompletion() {
    RUN_SUITE("okay cli: grouped help, Tab completion, build --list-targets, export");
    Said s;
    CHECK(Cli({"help"}, s) == 0);
    for (const char* line : {"Projects:", "Scenes, scripts and assets:", "Version control:", "The engine itself:", "This program:",
                             "scene check", "script run", "asset import", "asset used-by", "project settings", "vs check", "vc save",
                             "engine build", "completion bash", "export", "--json"})
        CHECK(Has(s.out, line));
    CHECK(Cli({"help", "scene", "check"}, s) == 0 && Has(s.out, "Usage: okay scene check"));
    CHECK(Cli({"help", "project"}, s) == 0 && Has(s.out, "upgrade") && Has(s.out, "settings"));
    CHECK(Cli({"help", "scene", "nosuch"}, s) == 2);
    CHECK(Cli({"scene", "nosuch"}, s) == 2 || Has(s.err, "There is no OkaySpace project"));

    CHECK(Cli({"completion", "bash"}, s) == 0 && Has(s.out, "complete -o default -F _okay_complete okay okay.exe") &&
          Has(s.out, "scene) COMPREPLY=( $(compgen -W \"list new check diff info\"") && Has(s.out, "vc) COMPREPLY") &&
          Has(s.out, "project"));
    CHECK(Cli({"completion", "powershell"}, s) == 0 && Has(s.out, "Register-ArgumentCompleter -Native -CommandName okay, okay.exe") &&
          Has(s.out, "'asset' = @('list', 'find', 'used-by', 'uses', 'import')"));
    CHECK(Cli({"completion", "fish"}, s) == 2);
    CHECK(Cli({"completion"}, s) == 2);
    // Every command in the help completes, and every command that has sub-commands lists them.
    const std::string bash = okaycli::CompletionScript("bash");
    for (const std::string& c : okaycli::CommandNames()) CHECK(Has(bash, " " + c + " ") || Has(bash, "\"" + c + " "));
    CHECK(okaycli::SubCommandNames("asset").size() == 5 && okaycli::SubCommandNames("build").empty());
#if !defined(_WIN32)
    // The bash script, in a real bash when there is one: Tab after "okay sc" and after "okay --project G asset u".
    if (!okaycli::FindOnPath("bash").empty()) {
        const okaycli::RunResult r = okaycli::Run({"bash", "-c", bash + "\nCOMP_WORDS=(okay sc); COMP_CWORD=1; _okay_complete; echo \"1:${COMPREPLY[*]}\"\n"
                                                 "COMP_WORDS=(okay --project G asset u); COMP_CWORD=4; _okay_complete; echo \"2:${COMPREPLY[*]}\"\n"}, "", true);
        CHECK(r.started && r.exitCode == 0 && Has(r.out, "1:scene script") && Has(r.out, "2:used-by uses"));
    }
#endif

    CHECK(Cli({"build", "--list-targets"}, s) == 0 && Has(s.out, "Desktop") && Has(s.out, "Android") && Has(s.out, "Xbox") &&
          Has(s.out, "okay build makes this one"));
    CHECK(Cli({"build", "--list-targets", "--json"}, s) == 0 && Has(s.out, "{\"targets\":[{\"name\":\"Desktop\",\"okayBuild\":true}"));
    CHECK(Cli({"export", "--help"}, s) == 0 && Has(s.out, "Usage: okay export"));
    CHECK(Cli({"export", "--list-targets"}, s) == 0 && Has(s.out, "Desktop"));
    CHECK(Cli({"info", "--json"}, s) == 0 && s.out.rfind("{\"engine\":", 0) == 0 && Has(s.out, "\"versionControl\":"));
}

void TestScriptCommands(const fs::path& tmp) {
    RUN_SUITE("okay script: new, check, run");
    const fs::path proj = tmp / "ScriptGame";
    std::string err;
    CHECK(okaycli::CreateEmptyProject(proj.string(), "ScriptGame", "", err));
    const std::string P = proj.string();
    Said s;
    CHECK(Cli({"--project", P, "script", "new", "Player"}, s) == 0 && Has(s.out, "Assets/Scripts/Player.okay"));
    CHECK(Has(Read(proj / "Assets" / "Scripts" / "Player.okay"), "void Start()"));
    CHECK(Cli({"--project", P, "script", "new", "Player"}, s) == 1 && Has(s.err, "already exists"));
    CHECK(Cli({"--project", P, "script", "new", "../evil"}, s) == 2);
    CHECK(Cli({"--project", P, "script", "check", "Assets/Scripts/Player.okay"}, s) == 0 && Has(s.out, "fine"));
    Write(proj / "Assets" / "Scripts" / "Bad.okay", "void Start()\n{\n    let x = ;\n}\n");
    CHECK(Cli({"--project", P, "script", "check", "Assets/Scripts/Bad.okay"}, s) == 1 && Has(s.err, "Assets/Scripts/Bad.okay:") &&
          Has(s.err, "error"));
    CHECK(Cli({"--project", P, "script", "check", "--json"}, s) == 1 && Has(s.out, "\"file\":\"Assets/Scripts/Bad.okay\"") &&
          Has(s.out, "\"severity\":\"error\"") && Has(s.out, "\"file\":\"Assets/Scripts/Player.okay\",\"problems\":[]"));
    Write(proj / "Hello.okay", "void Start()\n{\n    print(\"hello from start\");\n}\nvoid Main()\n{\n    print(\"main ran\");\n}\n"
                               "void Update()\n{\n    print(\"tick\");\n}\n");
    CHECK(Cli({"--project", P, "script", "run", (proj / "Hello.okay").string(), "--frames", "2", "a", "b"}, s) == 0);
    CHECK(Has(s.out, "hello from start") && Has(s.out, "main ran"));
    const std::size_t first = s.out.find("tick");
    CHECK(first != std::string::npos && s.out.find("tick", first + 1) != std::string::npos);
    CHECK(Cli({"script", "run", (proj / "Assets" / "Scripts" / "Bad.okay").string()}, s) == 1 && Has(s.err, "does not compile"));
    CHECK(Cli({"script", "run", (proj / "nosuch.okay").string()}, s) == 1);
    CHECK(Cli({"script", "nosuch"}, s) == 2);
}

void TestSceneCommands(const fs::path& tmp) {
    RUN_SUITE("okay scene: list, new, check, diff, info");
    const fs::path proj = tmp / "SceneGame";
    std::string err;
    CHECK(okaycli::CreateEmptyProject(proj.string(), "SceneGame", "", err));
    const std::string P = proj.string();
    Said s;
    CHECK(Cli({"--project", P, "scene", "new", "Level2"}, s) == 0 && fs::exists(proj / "Assets" / "Scenes" / "Level2.okayscene"));
    CHECK(Cli({"--project", P, "scene", "new", "Level2"}, s) == 1);
    CHECK(Cli({"--project", P, "scene", "new", "a/b"}, s) == 2);
    CHECK(Cli({"--project", P, "scene", "list"}, s) == 0 && Has(s.out, "Assets/Scenes/SceneGame.okayscene   (the start scene)") &&
          Has(s.out, "Assets/Scenes/Level2.okayscene") && Has(s.out, "2 scenes"));
    CHECK(Cli({"--project", P, "scene", "list", "--json"}, s) == 0 && Has(s.out, "{\"path\":\"Assets/Scenes/SceneGame.okayscene\",\"start\":true}") &&
          Has(s.out, "\"count\":2"));
    // A scene with a script that points at a file that is not there, and one at a file that is.
    {
        okay::Scene sc("Broken");
        okay::GameObject* hero = sc.CreateGameObject("Hero");
        hero->AddComponent<okay::ScriptComponent>()->SetPath("Assets/Scripts/Missing.okay");
        okay::GameObject* cam = sc.CreateGameObject("Cam");
        cam->AddComponent<okay::ScriptComponent>()->SetPath("Assets/Scripts/Real.okay");
        Write(proj / "Assets" / "Scripts" / "Real.okay", "void Start()\n{\n}\n");
        Write(proj / "Assets" / "Scenes" / "Broken.okayscene", okay::SceneSerializer::Serialize(sc));
    }
    CHECK(Cli({"--project", P, "scene", "check", "Assets/Scenes/Broken.okayscene"}, s) == 1 && Has(s.err, "Missing.okay") && Has(s.err, "error"));
    CHECK(Cli({"--project", P, "scene", "check", "Assets/Scenes/Broken.okayscene", "--json"}, s) == 1 && Has(s.out, "script.missing_file") &&
          Has(s.out, "\"errors\":1"));
    CHECK(Cli({"--project", P, "scene", "check", "Assets/Scenes/Level2.okayscene"}, s) == 0);
    CHECK(Cli({"--project", P, "scene", "info", "Assets/Scenes/Broken.okayscene"}, s) == 0 && Has(s.out, "2 objects") &&
          Has(s.out, "Components:") && Has(s.out, "Missing.okay") && Has(s.out, "MISSING"));
    CHECK(Cli({"--project", P, "scene", "info", "Assets/Scenes/Broken.okayscene", "--json"}, s) == 0 && Has(s.out, "\"objects\":2") &&
          Has(s.out, "\"exists\":false") && Has(s.out, "\"exists\":true"));
    CHECK(Cli({"--project", P, "scene", "diff", "Assets/Scenes/Level2.okayscene", "Assets/Scenes/Broken.okayscene"}, s) == 0 && Has(s.out, "Hero"));
    CHECK(Cli({"--project", P, "scene", "diff", "Assets/Scenes/Level2.okayscene", "Assets/Scenes/Level2.okayscene"}, s) == 0 && Has(s.out, "the same"));
    CHECK(Cli({"--project", P, "scene", "diff", "Assets/Scenes/Level2.okayscene"}, s) == 2);
    CHECK(Cli({"--project", (tmp / "nowhere").string(), "scene", "list"}, s) == 1 && Has(s.err, "no OkaySpace project"));
}

void TestAssetCommands(const fs::path& tmp) {
    RUN_SUITE("okay asset: list, find, used-by, uses, import");
    const fs::path proj = tmp / "AssetGame";
    std::string err;
    CHECK(okaycli::CreateEmptyProject(proj.string(), "AssetGame", "", err));
    const std::string P = proj.string();
    Write(proj / "Assets" / "Textures" / "grass.png", "not really a png");
    Write(proj / "Assets" / "Scripts" / "Mover.okay", "void Start()\n{\n}\n");
    {
        okay::Scene sc("Field");
        sc.CreateGameObject("Walker")->AddComponent<okay::ScriptComponent>()->SetPath("Scripts/Mover.okay");
        Write(proj / "Assets" / "Field.okayscene", okay::SceneSerializer::Serialize(sc));
    }
    Said s;
    CHECK(Cli({"--project", P, "asset", "list"}, s) == 0 && Has(s.out, "Assets/Textures/grass.png") && Has(s.out, "Assets/Field.okayscene"));
    CHECK(Cli({"--project", P, "asset", "list", "--type", "texture"}, s) == 0 && Has(s.out, "grass.png") && !Has(s.out, "Field.okayscene"));
    CHECK(Cli({"--project", P, "asset", "list", "--type", "bogus"}, s) == 2);
    CHECK(Cli({"--project", P, "asset", "list", "--type", "texture", "--json"}, s) == 0 && Has(s.out, "\"path\":\"Assets/Textures/grass.png\"") &&
          Has(s.out, "\"count\":1"));
    CHECK(Cli({"--project", P, "asset", "find", "gra"}, s) == 0 && Has(s.out, "grass.png"));
    CHECK(Cli({"--project", P, "asset", "find", "zzz"}, s) == 0 && Has(s.out, "Nothing is called"));
    // The dependency graph (a material names the texture; .okay scripts are not in the graph yet, so a scene's script
    // line is not a dependency there).
    Write(proj / "Assets" / "Grass.okaymat", "okaymat 1\ntexture Textures/grass.png\n");
    CHECK(Cli({"--project", P, "asset", "used-by", "Assets/Textures/grass.png"}, s) == 0 && Has(s.out, "Assets/Grass.okaymat") && Has(s.out, "1 file."));
    CHECK(Cli({"--project", P, "asset", "used-by", "Textures/grass.png", "--json"}, s) == 0 && Has(s.out, "\"path\":\"Assets/Grass.okaymat\",\"type\":\"material\""));
    CHECK(Cli({"--project", P, "asset", "uses", "Assets/Grass.okaymat"}, s) == 0 && Has(s.out, "Assets/Textures/grass.png"));
    CHECK(Cli({"--project", P, "asset", "uses", "Assets/Textures/grass.png"}, s) == 0 && Has(s.out, "uses no other asset"));
    CHECK(Cli({"--project", P, "asset", "used-by", "Assets/none.png"}, s) == 1);
    // Import: copied in under a free name, with its .okaymeta; the original is untouched.
    Write(tmp / "outside" / "tree.png", "tree bytes");
    CHECK(Cli({"--project", P, "asset", "import", (tmp / "outside" / "tree.png").string(), "--to", "Textures"}, s) == 0 &&
          Has(s.out, "Assets/Textures/tree.png") && Has(s.out, ".okaymeta"));
    CHECK(Read(proj / "Assets" / "Textures" / "tree.png") == "tree bytes");
    CHECK(fs::exists(proj / "Assets" / "Textures" / "tree.png.okaymeta") && Has(Read(proj / "Assets" / "Textures" / "tree.png.okaymeta"), "okaymeta"));
    CHECK(fs::exists(tmp / "outside" / "tree.png"));
    CHECK(Cli({"--project", P, "asset", "import", (tmp / "outside" / "tree.png").string(), "--to", "Textures"}, s) == 0 &&
          !Has(s.out, "Assets/Textures/tree.png as"));   // a second copy gets a free name, the first is kept
    CHECK(Read(proj / "Assets" / "Textures" / "tree.png") == "tree bytes");
    CHECK(Cli({"--project", P, "asset", "import", (tmp / "outside" / "nosuch.png").string()}, s) == 1);
}

void TestProjectAndVsCommands(const fs::path& tmp) {
    RUN_SUITE("okay project: info, settings, upgrade; okay vs: list, check");
    const fs::path proj = tmp / "ProjGame";
    std::string err;
    CHECK(okaycli::CreateEmptyProject(proj.string(), "ProjGame", "", err));
    const std::string P = proj.string();
    Said s;
    CHECK(Cli({"--project", P, "project", "info"}, s) == 0 && Has(s.out, "Project ProjGame") && Has(s.out, "start scene: Assets/Scenes/ProjGame.okayscene"));
    CHECK(Cli({"--project", P, "project", "info", "--json"}, s) == 0 && Has(s.out, "\"name\":\"ProjGame\"") && Has(s.out, "\"scenes\":1"));
    CHECK(Cli({"--project", P, "project", "settings", "get", "width"}, s) == 0 && Trim(s.out) == "1280");
    const std::string fileBefore = Read(proj / "ProjectSettings" / "project.okayproj");
    Write(proj / "ProjectSettings" / "project.okayproj", fileBefore + "some_future_key=keep me\n");
    CHECK(Cli({"--project", P, "project", "settings", "set", "width", "1920"}, s) == 0 && Has(s.out, "width is 1920"));
    CHECK(Cli({"--project", P, "project", "settings", "get", "width"}, s) == 0 && Trim(s.out) == "1920");
    CHECK(Cli({"--project", P, "project", "settings", "set", "company", "Okay", "Games"}, s) == 0);
    CHECK(Cli({"--project", P, "project", "settings", "get", "company"}, s) == 0 && Trim(s.out) == "Okay Games");
    const std::string after = Read(proj / "ProjectSettings" / "project.okayproj");
    CHECK(Has(after, "some_future_key=keep me"));   // a line okay does not own is kept
    const okayver::Stamp stamp = okayver::ReadProjectStamp(P);
    CHECK(Cli({"--project", P, "project", "settings", "set", "nosuch", "1"}, s) == 1 && Has(s.err, "no project setting"));
    CHECK(Cli({"--project", P, "project", "settings", "get", "nosuch"}, s) == 1);
    CHECK(Cli({"--project", P, "project", "settings"}, s) == 0 && Has(s.out, "width=1920"));
    // Upgrade: nothing to do on this version; an older stamp is upgraded with a backup beside the project.
    CHECK(Cli({"--project", P, "project", "upgrade"}, s) == 0);
    if (!stamp.version.empty()) {
        CHECK(Has(s.out, "already on this engine"));
        Write(proj / "ProjectSettings" / "project.okayproj", okayver::SetStampInText(Read(proj / "ProjectSettings" / "project.okayproj"), "2.0.0"));
        CHECK(Cli({"--project", P, "project", "upgrade"}, s) == 0 && Has(s.out, "Upgraded the project"));
        CHECK(okayver::ReadProjectStamp(P).version == stamp.version);
        bool backup = false;
        std::error_code ec;
        for (const auto& e : fs::directory_iterator(tmp, ec)) if (Has(e.path().filename().string(), "ProjGame (backup")) backup = true;
        CHECK(backup);
        Write(proj / "ProjectSettings" / "project.okayproj", okayver::SetStampInText(Read(proj / "ProjectSettings" / "project.okayproj"), "999.0.0"));
        CHECK(Cli({"--project", P, "project", "upgrade"}, s) == 1 && Has(s.err, "newer engine"));
    }

    // Visual Script graphs: the repository's example graph is fine; a damaged one is an error.
    const fs::path repo = RepoRoot();
    fs::create_directories(proj / "Assets" / "Graphs");
    std::error_code ec;
    fs::copy_file(repo / "examples" / "visual_script" / "patrol.okayvs", proj / "Assets" / "Graphs" / "patrol.okayvs", ec);
    CHECK(!ec);
    CHECK(Cli({"--project", P, "vs", "list"}, s) == 0 && Has(s.out, "Assets/Graphs/patrol.okayvs"));
    CHECK(Cli({"--project", P, "vs", "list", "--json"}, s) == 0 && Has(s.out, "{\"graphs\":[\"Assets/Graphs/patrol.okayvs\"],\"count\":1}"));
    const int good = Cli({"--project", P, "vs", "check", "Assets/Graphs/patrol.okayvs"}, s);
    CHECK(good == 0);
    if (good != 0) std::cout << "    " << s.all() << "\n";
    // A damaged graph: the checker skips lines it can't read and says so; okay reports that as a warning on the file.
    Write(proj / "Assets" / "Graphs" / "broken.okayvs", "this is not a graph {{{\n");
    CHECK(Cli({"--project", P, "vs", "check", "Assets/Graphs/broken.okayvs"}, s) == 0 && Has(s.out, "warning") && Has(s.out, "unknown line"));
    CHECK(Cli({"--project", P, "vs", "check", "--json"}, s) == 0 && Has(s.out, "\"file\":\"Assets/Graphs/broken.okayvs\"") &&
          Has(s.out, "\"severity\":\"warning\"") && Has(s.out, "\"file\":\"Assets/Graphs/patrol.okayvs\",\"problems\":[]"));
    // A graph with a real error: a wire between pins that do not fit, or a variable read and never set, comes from the
    // checker itself (the editor's Problems list); the example graphs have none, so this checks the error path through
    // a missing file instead.
    CHECK(Cli({"--project", P, "vs", "check", "Assets/Graphs/nosuch.okayvs"}, s) == 1 && Has(s.err, "can't be read"));
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
    TestRelInside(tmp);
    TestProjects(tmp);
#if !defined(_WIN32)
    TestPathLookup(tmp);
#endif
    TestMergeDriverDirect(tmp);
    TestHelpAndCompletion();
    TestScriptCommands(tmp);
    TestSceneCommands(tmp);
    TestAssetCommands(tmp);
    TestProjectAndVsCommands(tmp);
    TestEngineArgs();
    TestEngineDetect(tmp);
    TestEnginePlan(tmp);
    TestEngineClean(tmp);
#if !defined(_WIN32)
    TestEngineBuildRun(tmp);
#endif
    const bool git = okaycli::vc::Git::Available();
    if (git) TestGit(tmp);
    else std::cout << "  SKIP: git is not installed, so the version control checks did not run\n";
    fs::remove_all(tmp, ec);
    std::cout << ::okaytest::g_checks << " checks, " << ::okaytest::g_failures << " failures\n";
    if (::okaytest::g_failures) return 1;
    return git ? 0 : 77;
}
