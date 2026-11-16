// The two data-loss findings of docs/install_audit.md (section 4, findings 1 and 2), as tests that ran red against the code before the fix:
//
//   1. A kept editor (an older version the launcher keeps in Versions/<v>/Tools) must never update itself in place, and a bare
//      `--keep-version` must not make a newer editor open an older project with no question and no backup. Here: the pure rules in
//      shared/ProjectVersion.hpp (is this folder a kept copy; who trusts which flag) and the arguments the launcher passes. The editor's
//      gate itself is checked in test_project_upgrade.cpp (it needs the engine library).
//   2. "Remove version" (and the prune, and every other delete of a version folder) must never delete a folder that holds a project of the
//      person's. The default folder for a new project is beside the editor exe, so for a kept editor it is Versions/<v>/Tools/Projects.
//
// Everything runs on temp folders; nothing needs a window, a network or the engine. Links are only made on non-Windows systems.
// Written from docs/install_audit.md (findings 1 and 2, Appendix B) and the public interfaces of launcher/VersionsManager.hpp and
// shared/ProjectVersion.hpp. No outside code or test was used.
#include "test_framework.hpp"
#include "VersionsManager.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>

#if !defined(_WIN32)
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace okayversions;

namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        static int n = 0;
        path = fs::temp_directory_path() / ("okay_vsafe_" + std::to_string(++n) + "_" +
               std::to_string((long long)std::chrono::steady_clock::now().time_since_epoch().count()));
        std::error_code ec;
        fs::remove_all(path, ec);
        fs::create_directories(path, ec);
    }
    ~TempDir() {
        std::error_code ec;
        fs::permissions(path, fs::perms::all, ec);
        for (auto& e : fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied, ec)) fs::permissions(e.path(), fs::perms::all, ec);
        fs::remove_all(path, ec);
    }
};

void Put(const fs::path& p, const std::string& text) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    std::ofstream(p, std::ios::binary | std::ios::trunc) << text;
}
std::string Get(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

/// Every file and folder under `root` with its content: two snapshots are equal when nothing changed. Links are recorded, never followed.
std::map<std::string, std::string> Snapshot(const fs::path& root) {
    std::map<std::string, std::string> out;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string rel = fs::relative(it->path(), root, ec).generic_string();
        std::error_code e2;
        if (it->is_symlink(e2)) out[rel] = "<link " + fs::read_symlink(it->path(), e2).string() + ">";
        else if (it->is_directory(e2)) out[rel] = "<dir>";
        else out[rel] = Get(it->path());
    }
    return out;
}

/// A launcher folder with a current install in Tools/ (the running version).
fs::path MakeLauncher(const fs::path& root, const std::string& ver) {
    fs::path dir = root / "Okay Space L\xC3\xA4uncher \xE6\x97\xA5\xE6\x9C\xAC";   // spaces and unicode
    Put(dir / "Tools" / okayver::EditorExeName(), "editor " + ver);
    Put(dir / "Tools" / "OkaySpacePlayer.exe", "player " + ver);
    Put(dir / "Tools" / "SDL2.dll", "sdl");
    Put(dir / "OkaySpace.exe", "launcher " + ver);
    return dir;
}
/// A version folder the way Install / KeepCurrent leave it.
fs::path MakeKept(const fs::path& launcher, const std::string& ver, const std::string& source = "kept") {
    const fs::path d = okayver::VersionsDir(launcher) / ver;
    Put(d / "Tools" / okayver::EditorExeName(), "editor " + ver);
    Put(d / "Tools" / "OkaySpacePlayer.exe", "player " + ver);
    Put(d / "Tools" / "SDL2.dll", "sdl");
    Put(d / "install.txt", "okayspace install 1\nversion=" + ver + "\nsource=" + source + "\ndate=2026-11-01\n");
    return d;
}
bool IsThere(const fs::path& launcher, const std::string& ver) { return okayver::IsInstalled(launcher, ver); }

/// A project the way the editor writes one.
fs::path MakeProject(const fs::path& root, bool withFile = true, bool withScene = true) {
    if (withFile) Put(root / "project.okayproj", "engine_version=2.850.1\ncompany=Me\n");
    if (withScene) Put(root / "Assets" / "Scenes" / "level1.okayscene", "okayscene 3\nname \"Level 1\"\n");
    Put(root / "Assets" / "Scripts" / "hero.okay", "class Hero { }\n");
    return root;
}

RemoveGuards Guards() {
    RemoveGuards g;
    g.running = "2.900.0";
    g.launcher = "2.900.0";
    return g;
}

IO QuietIO() {
    IO io;
    io.availableSpace = [](const fs::path&, std::error_code& ec) { ec.clear(); return (std::uintmax_t)64 * 1024 * kMiB; };
    return io;
}

/// The projects a scan finds in a folder made from `layout` (relative paths; a trailing '/' makes an empty folder).
std::vector<fs::path> FoundIn(const std::vector<std::string>& layout, const ScanLimits& limits = {}) {
    TempDir t;
    const fs::path root = t.path / "v";
    std::error_code ec;
    fs::create_directories(root, ec);
    for (const std::string& rel : layout) {
        if (!rel.empty() && rel.back() == '/') fs::create_directories(root / rel, ec);
        else Put(root / rel, "x");
    }
    std::vector<fs::path> out;
    for (const fs::path& p : ScanFolderForProjects(root, limits).projects) out.push_back(fs::relative(p, root, ec));
    return out;
}

}  // namespace

int main() {
    RUN_SUITE("versions_safety");

    // ================================================================================================================================
    // Finding 1: kept copies never update themselves, and a bare --keep-version is not trusted by a current editor
    // ================================================================================================================================

    // ---- is this folder a kept copy? -----------------------------------------------------------------------------------------------
    {
        using okayver::IsKeptCopyDir;
        using okayver::KeptCopyVersion;
        // the layout the launcher makes, in every spelling a program can get its own folder in
        CHECK(IsKeptCopyDir("/home/u/OkaySpace/Versions/2.850.1/Tools", false));
        CHECK(KeptCopyVersion("/home/u/OkaySpace/Versions/2.850.1/Tools", false) == "2.850.1");
        CHECK(IsKeptCopyDir("C:\\Users\\me\\AppData\\Local\\OkaySpace\\Versions\\2.850.1\\Tools", false));      // backslashes
        CHECK(KeptCopyVersion("C:\\Users\\me\\AppData\\Local\\OkaySpace\\Versions\\2.850.1\\Tools", false) == "2.850.1");
        CHECK(IsKeptCopyDir("C:/Users/me/OkaySpace/Versions/2.850.1/Tools/", false));                           // forward slashes and a trailing one
        CHECK(IsKeptCopyDir("C:\\Users\\me\\OkaySpace\\Versions\\2.850.1\\Tools\\", false));                    // a trailing backslash
        CHECK(IsKeptCopyDir("C:\\Users\\me\\OkaySpace\\Versions\\2.850.1\\Tools\\\\", false));                  // two of them
        CHECK(IsKeptCopyDir("C:\\Users\\me\\OkaySpace\\Versions/2.850.1\\Tools", false));                       // both kinds mixed
        CHECK(IsKeptCopyDir("C:\\a\\.\\OkaySpace\\Versions\\2.850.1\\.\\Tools\\.", false));                     // "." parts
        CHECK(IsKeptCopyDir("/a/b/../OkaySpace/Versions/2.850.1/Tools", false));                                // ".." parts are resolved as text
        CHECK(IsKeptCopyDir("\\\\server\\share\\OkaySpace\\Versions\\2.850.1\\Tools", false));                  // a network folder
        CHECK(IsKeptCopyDir("\\\\?\\C:\\Games\\OkaySpace\\Versions\\2.850.1\\Tools", false));                   // the long-path spelling
        CHECK(IsKeptCopyDir("C:\\OkaySpace\\Versions\\2.850.1\\Tools", false) && IsKeptCopyDir("Versions/2.850.1/Tools", false));   // a relative path
        CHECK(IsKeptCopyDir("/Versions/2.850.1/Tools", false));                                                 // the launcher is the root folder
        CHECK(IsKeptCopyDir("/home/u/Okay Space L\xC3\xA4uncher \xE6\x97\xA5\xE6\x9C\xAC/Versions/2.850.1/Tools", false));   // spaces and unicode
        CHECK(IsKeptCopyDir("/home/u/Tools/OkaySpace/Versions/2.850.1/Tools", false));                           // a Tools folder higher up is nothing to do with it
        CHECK(IsKeptCopyDir("/home/u/OkaySpace/Versions/2.850.1.5/Tools", false) && KeptCopyVersion("/o/Versions/2.850.1.5/Tools", false) == "2.850.1.5");   // four numbers are a canonical version too
        CHECK(IsKeptCopyDir("/o/Versions/Versions/2.850.1/Tools", false));                                      // only the last three parts count

        // case: Windows folders do not care, other systems do
        CHECK(IsKeptCopyDir("C:\\GAMES\\OKAYSPACE\\VERSIONS\\2.850.1\\TOOLS", true));
        CHECK(IsKeptCopyDir("c:\\games\\okayspace\\versions\\2.850.1\\tools", true));
        CHECK(KeptCopyVersion("C:\\Games\\OkaySpace\\vErSiOnS\\2.850.1\\tOOLs", true) == "2.850.1");
        CHECK(!IsKeptCopyDir("C:\\GAMES\\OKAYSPACE\\VERSIONS\\2.850.1\\TOOLS", false));
        CHECK(!IsKeptCopyDir("/home/u/OkaySpace/versions/2.850.1/tools", false));
        CHECK(IsKeptCopyDir("/home/u/OkaySpace/VERSIONS/2.850.1/Tools", okayver::kFoldPathCase) == okayver::kFoldPathCase);   // the default follows the system
        CHECK(IsKeptCopyDir("/home/u/OkaySpace/Versions/2.850.1/Tools"));                                       // the exact spelling is a kept copy everywhere

        // not kept copies: the current editor, and every lookalike
        CHECK(!IsKeptCopyDir("/home/u/OkaySpace/Tools", false));                                                // the editor of the install
        CHECK(!IsKeptCopyDir("C:\\Users\\me\\AppData\\Local\\OkaySpace\\Tools", true));
        CHECK(!IsKeptCopyDir("/home/u/OkaySpace", false) && !IsKeptCopyDir("/home/u/OkaySpace/Versions", false));
        CHECK(!IsKeptCopyDir("/home/u/OkaySpace/Versions/2.850.1", false));                                      // the version folder itself: the editor sits in Tools
        CHECK(!IsKeptCopyDir("/home/u/OkaySpace/Versions/Tools", false));                                        // a folder called Versions with no version folder under it
        CHECK(!IsKeptCopyDir("/home/u/Versions/MyBuild/Tools", false));                                          // a Versions folder of somebody's own: no version name
        CHECK(!IsKeptCopyDir("/home/u/Versions/v2.850.1/Tools", false));                                         // the launcher writes the plain spelling only
        CHECK(!IsKeptCopyDir("/home/u/OkaySpace/Versions/2.850/Tools", false));                                  // (the same test InstalledVersions uses: not canonical)
        CHECK(!IsKeptCopyDir("/home/u/OkaySpace/Versions/2.850.1/Tools/Tools", false));                          // a Tools folder inside Tools
        CHECK(!IsKeptCopyDir("/home/u/OkaySpace/Versions/2.850.1/Tools/Plugins", false));
        CHECK(!IsKeptCopyDir("/home/u/OkaySpace/Versions/2.850.1/Plugins", false));
        CHECK(!IsKeptCopyDir("/home/u/OkaySpace/Versions/.partial-2.860.0-1/Tools", false));                     // scratch folders are not versions
        CHECK(!IsKeptCopyDir("/home/u/OkaySpace/Versions/dev/Tools", false));
        CHECK(!IsKeptCopyDir("/home/u/Tools/Versions", false) && !IsKeptCopyDir("/home/u/2.850.1/Tools", false));
        CHECK(!IsKeptCopyDir("/home/u/OkaySpace/2.850.1/Versions/Tools", false));                                // the right names in the wrong order
        CHECK(!IsKeptCopyDir("", false) && !IsKeptCopyDir("/", false) && !IsKeptCopyDir("\\", false) && !IsKeptCopyDir(".", false) && !IsKeptCopyDir("Tools", false));
        CHECK(!IsKeptCopyDir("/Versions/..", false) && !IsKeptCopyDir("/../../Tools", false) && !IsKeptCopyDir("../../..", false));
        CHECK(KeptCopyVersion("/home/u/OkaySpace/Tools", false).empty() && KeptCopyVersion("", false).empty());

        // the plain line a kept copy shows
        CHECK(okayver::KeptCopyNotice("2.850.1") == "This is a kept copy of v2.850.1. Update OkaySpace from the launcher.");
        CHECK(okayver::KeptCopyNotice("v2.850") == "This is a kept copy of v2.850.0. Update OkaySpace from the launcher.");
        CHECK(okayver::KeptCopyNotice("") == "This is a kept copy. Update OkaySpace from the launcher.");
    }

    // ---- --keep-version: who trusts which spelling ----------------------------------------------------------------------------------
    {
        using okayver::HonoursKeepFlag;
        // The flag matrix. Rows: what the flag says. Columns: the editor that reads it (its version, and whether it runs from a kept copy).
        //                                  kept copy 2.850.1     current editor 2.860.0     current editor 2.850.1
        CHECK( HonoursKeepFlag("",          "2.850.1", true));                                   // a bare flag: a kept copy trusts it ...
        CHECK(!HonoursKeepFlag("",          "2.860.0", false));                                  // ... a current editor does not (this was the silent open of finding 1)
        CHECK(!HonoursKeepFlag("",          "2.850.1", false));                                  // not even one of the very version that is on the page
        CHECK( HonoursKeepFlag("  ",        "2.850.1", true) && !HonoursKeepFlag("  ", "2.860.0", false));    // blanks are a bare flag
        CHECK( HonoursKeepFlag("2.850.1",   "2.850.1", true));                                   // a named flag: the editor of exactly that version trusts it,
        CHECK( HonoursKeepFlag("2.850.1",   "2.850.1", false));                                  // kept copy or not,
        CHECK(!HonoursKeepFlag("2.850.1",   "2.860.0", false));                                  // and no other version does,
        CHECK(!HonoursKeepFlag("2.850.1",   "2.860.0", true));                                   // not even a kept copy of another version (a folder whose editor updated itself)
        CHECK(!HonoursKeepFlag("2.850.1",   "2.850.0", true));                                   // not another patch either: it names ONE version
        CHECK( HonoursKeepFlag("v2.850.1",  "2.850.1", false) && HonoursKeepFlag("2.850.1", "v2.850.1", false));   // spelling does not matter
        CHECK( HonoursKeepFlag("2.850",     "2.850.0", false));                                  // a missing number is a zero
        CHECK(!HonoursKeepFlag("banana",    "2.850.1", true) && !HonoursKeepFlag("banana", "2.850.1", false));     // not a version: nobody
        CHECK(!HonoursKeepFlag("2.850.1x",  "2.850.1", true));                                   // (a suffix that is not a version suffix)
        CHECK(!HonoursKeepFlag("2.850.1",   "dev", false) && !HonoursKeepFlag("banana", "dev", false));   // a development build has no version to be named
        CHECK( HonoursKeepFlag("",          "dev", true));                                       // (a dev build in a kept folder: bare is fine, it never asks anyway)

        // The flag as one command-line argument.
        std::string v = "x";
        CHECK(okayver::ParseKeepFlag("--keep-version", &v) && v.empty());
        CHECK(okayver::ParseKeepFlag("--keep-version=2.850.1", &v) && v == "2.850.1");
        CHECK(okayver::ParseKeepFlag("--keep-version=", &v) && v.empty());                       // empty name = bare
        CHECK(okayver::ParseKeepFlag("--keep-version=a=b", &v) && v == "a=b");
        CHECK(okayver::ParseKeepFlag("--keep-version") && okayver::ParseKeepFlag("--keep-version=1"));   // no output needed
        CHECK(!okayver::ParseKeepFlag("--keep-versions", &v) && !okayver::ParseKeepFlag("--keep-version2.850.1", &v) && !okayver::ParseKeepFlag("--keep", &v) &&
              !okayver::ParseKeepFlag("keep-version", &v) && !okayver::ParseKeepFlag("--KEEP-VERSION", &v) && !okayver::ParseKeepFlag("", &v) && !okayver::ParseKeepFlag("--open", &v));

        // The command line the program builds.
        CHECK(okayver::KeepVersionArgs("a.okayscene") == (std::vector<std::string>{"--open", "a.okayscene", "--keep-version"}));                  // bare (no version given)
        CHECK(okayver::KeepVersionArgs("a.okayscene", "2.850.1") == (std::vector<std::string>{"--open", "a.okayscene", "--keep-version=2.850.1"}));  // named
        CHECK(okayver::KeepVersionArgs("a b.okayscene", "v2.850.1")[2] == "--keep-version=2.850.1" && okayver::KeepVersionArgs("s", "2.850")[2] == "--keep-version=2.850.0");   // canonical spelling
        CHECK(okayver::KeepVersionArgs("s", "junk")[2] == "--keep-version" && okayver::KeepVersionArgs("s", "dev")[2] == "--keep-version");        // not a version: bare

        // What the launcher passes to an editor, read back by the editors the flag can reach (the whole chain of finding 1).
        const std::string scene = "/home/u/My Projects/Spa\xC3\x9F-Spiel/Assets/main.okayscene";
        const std::vector<std::string> named = OpenWithArgs(4242, scene, "2.850.1");
        CHECK(named == (std::vector<std::string>{"--launcher", "4242", "--open", scene, "--keep-version=2.850.1"}));
        CHECK(OpenWithArgs(4242, scene) == (std::vector<std::string>{"--launcher", "4242", "--open", scene, "--keep-version"}));      // no version: the bare form (only a kept copy trusts it)
        CHECK(OpenWithArgs(7, scene, "v2.850.1") == OpenWithArgs(7, scene, "2.850.1"));
        std::string forVersion;
        bool seen = false;
        for (const std::string& a : named) seen = okayver::ParseKeepFlag(a, &forVersion) || seen;
        CHECK(seen && forVersion == "2.850.1");
        CHECK(HonoursKeepFlag(forVersion, "2.850.1", true));            // the kept editor it was made for
        CHECK(!HonoursKeepFlag(forVersion, "2.860.0", false));          // that editor after it replaced itself with the newest: it asks its question
        CHECK(!HonoursKeepFlag("", "2.860.0", false));                  // and an old launcher's bare flag does not get a current editor to open an old project silently
    }

    // ================================================================================================================================
    // Finding 2: Remove, prune and every other delete of a version folder never delete a project
    // ================================================================================================================================

    // ---- what counts as "the person's work" ------------------------------------------------------------------------------------------
    {
        const std::vector<fs::path> none;
        auto one = [](const char* p) { return std::vector<fs::path>{fs::path(p)}; };
        CHECK(FoundIn({"Tools/Projects/MyGame/project.okayproj"}) == one("Tools/Projects/MyGame"));                       // the stamp file
        CHECK(FoundIn({"project.okayproj"}) == std::vector<fs::path>{fs::path(".")});                                      // right in the version folder
        CHECK(FoundIn({"Tools/Projects/Old/Assets/Scenes/level.okayscene"}) == one("Tools/Projects/Old"));               // Assets with a scene, no project file
        CHECK(FoundIn({"Tools/Projects/Old/Assets/Textures/hero.png"}) == one("Tools/Projects/Old"));                    // Assets with anything in it
        CHECK(FoundIn({"Tools/Projects/Old/level.okayscene", "Tools/Projects/Old/Assets/"}) == one("Tools/Projects/Old"));   // a scene beside an Assets folder (the flat layout)
        CHECK(FoundIn({"Tools/Untitled.okayscene"}) == one("Tools"));                                                      // a scene saved straight into the editor's folder
        CHECK(FoundIn({"Tools/Projects/P/project.okayproj", "Tools/Projects/P/Assets/a.okayscene"}) == one("Tools/Projects/P"));   // one project, listed once
        CHECK(FoundIn({"Tools/Projects/P/Assets/Assets/deep.png"}) == one("Tools/Projects/P"));
        CHECK(FoundIn({"Tools/Projects/P/PROJECT.OKAYPROJ"}) == one("Tools/Projects/P"));                                   // names are not case sensitive here (Windows is not)
        CHECK(FoundIn({"Tools/Projects/P/assets/hero.png"}) == one("Tools/Projects/P"));
        CHECK(FoundIn({"Tools/Projects/P/Scene.OKAYSCENE"}) == one("Tools/Projects/P"));
        // not the person's work
        CHECK(FoundIn({"Tools/Projects/Ghost/.okay/thumbs/a.png", "Tools/Projects/Ghost/.okay/build_profiles.cfg"}) == none);   // an .okay folder alone is not a project
        CHECK(FoundIn({"Tools/Projects/Empty/Assets/Scenes/", "Tools/Projects/Empty/Assets/Scripts/"}) == none);                 // a skeleton with nothing in it
        CHECK(FoundIn({"Tools/okay_crash.txt", "Tools/okay_crashlog.txt", "Tools/plugins_disabled.txt", "Tools/imgui.ini", "Tools/okay_recent.txt",
                       "Tools/OkayEngine", "Tools/OkaySpacePlayer.exe", "Tools/SDL2.dll", "install.txt"}) == none);                // the editor's own files
        CHECK(FoundIn({"Tools/Projects/P/project.okayproj/"}) == none);                                                          // a folder with that name is not the file
        CHECK(FoundIn({"Tools/Projects/P/notes.txt", "Tools/Projects/P/scene.okayscene.bak"}) == none);
        CHECK(FoundIn({}) == none);
        CHECK(ScanFolderForProjects("/no/such/folder/at/all").projects.empty() && !ScanFolderForProjects("/no/such/folder/at/all").unchecked);

        // Order and cap: the shallowest first, and the list stops where the answer is already no.
        {
            TempDir t;
            MakeProject(t.path / "v" / "Tools" / "Projects" / "Deep" / "er" / "than" / "the" / "rest");
            MakeProject(t.path / "v" / "Tools" / "Projects" / "Near");
            const FolderScan s = ScanFolderForProjects(t.path / "v");
            CHECK(s.projects.size() == 2 && s.projects[0].filename() == "Near" && s.projects[1].filename() == "rest" && !s.moreProjects && !s.unchecked);
        }
        {
            TempDir t;
            for (int i = 0; i < 12; ++i) MakeProject(t.path / "v" / "Tools" / "Projects" / ("P" + std::to_string(10 + i)));
            const FolderScan s = ScanFolderForProjects(t.path / "v");
            CHECK(s.projects.size() == 8 && s.moreProjects);
            ScanLimits few;
            few.maxReported = 3;
            CHECK(ScanFolderForProjects(t.path / "v", few).projects.size() == 3);
        }
    }

    // ---- Remove refuses, and nothing changes (the scratch program of the audit, R1) ----------------------------------------------------------------
    {
        TempDir t;
        const fs::path launcher = MakeLauncher(t.path, "2.900.0");
        const fs::path v = MakeKept(launcher, "2.850.1");
        const fs::path proj = MakeProject(v / "Tools" / "Projects" / "MyGame");                 // made in the kept editor's default folder
        const auto before = Snapshot(okayver::VersionsDir(launcher));
        const Result r = Remove(launcher, "2.850.1", Guards());
        CHECK(r.outcome == Outcome::Refused && !r.ok());
        CHECK(Has(r.message, "This version folder has a project of yours: " + proj.string() + ". Move it out first."));
        CHECK(Has(r.message, "Nothing was removed"));
        CHECK(r.path == proj.parent_path());                                                    // the folder an "Open folder" button shows
        CHECK(IsThere(launcher, "2.850.1") && Snapshot(okayver::VersionsDir(launcher)) == before);   // every byte of it, the project included
        CHECK(fs::exists(proj / "project.okayproj") && fs::exists(proj / "Assets" / "Scenes" / "level1.okayscene"));
        // the same through the check the window uses before the button is pressed
        const DeleteCheck c = CheckBeforeDelete(v);
        CHECK(!c.ok() && c.kind == DeleteCheck::HoldsProject && c.projects == std::vector<fs::path>{proj} && c.folder == proj.parent_path());
        CHECK(c.message == "This version folder has a project of yours: " + proj.string() + ". Move it out first.");
        // moved out: the same Remove works
        std::error_code ec;
        const fs::path elsewhere = t.path / "My Projects";
        fs::create_directories(elsewhere, ec);
        fs::rename(proj, elsewhere / "MyGame", ec);
        CHECK(!ec && CheckBeforeDelete(v).ok());
        const Result r2 = Remove(launcher, "2.850.1", Guards());
        CHECK(r2.outcome == Outcome::Removed && !IsThere(launcher, "2.850.1") && !fs::exists(v));
        CHECK(fs::exists(elsewhere / "MyGame" / "project.okayproj") && Get(elsewhere / "MyGame" / "Assets" / "Scenes" / "level1.okayscene") == "okayscene 3\nname \"Level 1\"\n");
    }

    // ---- every place a project can be, and the things that are not one ---------------------------------------------------------------------------------
    {
        struct Case { const char* what; std::vector<std::pair<std::string, std::string>> files; std::vector<std::string> emptyDirs; bool refused; };
        const std::vector<Case> cases = {
            {"a project right in the version folder", {{"project.okayproj", "engine_version=2.850.1\n"}}, {}, true},
            {"a project beside the editor", {{"Tools/project.okayproj", "x"}}, {}, true},
            {"a project in the default folder", {{"Tools/Projects/Game/project.okayproj", "x"}}, {}, true},
            {"a project deep inside", {{"Tools/Projects/Client/2026/Spring/Game/project.okayproj", "x"}}, {}, true},
            {"a project with no project file, only Assets and a scene", {{"Tools/Projects/Old/Assets/level.okayscene", "x"}}, {}, true},
            {"Assets with only a texture", {{"Tools/Assets/Textures/hero.png", "x"}}, {}, true},
            {"a lone scene in the editor's folder", {{"Tools/Untitled.okayscene", "x"}}, {}, true},
            {"a project beside the exe, found only with an upper case name", {{"Tools/Projects/Game/PROJECT.OKAYPROJ", "x"}}, {}, true},
            {"only an .okay folder", {{"Tools/Projects/Ghost/.okay/thumbs/a.png", "x"}, {"Tools/Projects/Ghost/.okay/build_profiles.cfg", "x"}}, {}, false},
            {"an empty Assets skeleton", {}, {"Tools/Projects/Empty/Assets/Scenes", "Tools/Projects/Empty/Assets/Scripts"}, false},
            {"the editor's own files", {{"Tools/okay_crash.txt", "x"}, {"Tools/okay_crashlog.txt", "x"}, {"Tools/plugins_disabled.txt", "x"}, {"Tools/imgui.ini", "x"},
                                       {"Tools/okay_recent.txt", "x"}, {"Tools/okay_settings.txt", "x"}, {"Tools/okay_ipc_request.txt", "x"}}, {}, false},
            {"nothing but the program", {}, {}, false},
        };
        for (const Case& c : cases) {
            TempDir t;
            const fs::path launcher = MakeLauncher(t.path, "2.900.0");
            const fs::path v = MakeKept(launcher, "2.850.1");
            for (const auto& f : c.files) Put(v / f.first, f.second);
            for (const std::string& d : c.emptyDirs) { std::error_code ec; fs::create_directories(v / d, ec); }
            const auto before = Snapshot(okayver::VersionsDir(launcher));
            const Result r = Remove(launcher, "2.850.1", Guards());
            if (c.refused != (r.outcome == Outcome::Refused)) std::cerr << "  case: " << c.what << ": outcome " << OutcomeName(r.outcome) << " (" << r.message << ")\n";
            if (c.refused) {
                CHECK(r.outcome == Outcome::Refused && Has(r.message, "project of yours") && Has(r.message, "Move it out first"));
                CHECK(IsThere(launcher, "2.850.1") && Snapshot(okayver::VersionsDir(launcher)) == before);
            } else {
                CHECK(r.outcome == Outcome::Removed && !fs::exists(v) && fs::exists(okayver::VersionsDir(launcher)));
            }
        }
    }

    // ---- a version with no project is removed exactly as before ---------------------------------------------------------------------------------------
    {
        TempDir t;
        const fs::path launcher = MakeLauncher(t.path, "2.900.0");
        MakeKept(launcher, "2.850.1");
        MakeKept(launcher, "2.800.0", "installed");
        const auto other = Snapshot(okayver::VersionsDir(launcher) / "2.800.0");
        const Result r = Remove(launcher, "2.850.1", Guards());
        CHECK(r.outcome == Outcome::Removed && r.ok() && Has(r.message, "Removed v2.850.1") && !fs::exists(okayver::VersionsDir(launcher) / "2.850.1"));
        CHECK(Snapshot(okayver::VersionsDir(launcher) / "2.800.0") == other && okayver::InstalledVersions(launcher) == std::vector<std::string>{"2.800.0"});
        // below the default limits a big folder is fine too
        {
            const fs::path v = okayver::VersionsDir(launcher) / "2.800.0" / "Tools" / "cache";
            for (int d = 0; d < 20; ++d)
                for (int f = 0; f < 250; ++f) Put(v / ("d" + std::to_string(d)) / ("f" + std::to_string(f) + ".bin"), "x");      // 5000 files
            CHECK(CheckBeforeDelete(okayver::VersionsDir(launcher) / "2.800.0").ok());
            CHECK(Remove(launcher, "2.800.0", Guards()).outcome == Outcome::Removed && !fs::exists(okayver::VersionsDir(launcher) / "2.800.0"));
        }
        // the old guards still go first: the running version is refused for its own reason, not for a project
        MakeKept(launcher, "2.790.0");
        MakeProject(okayver::VersionsDir(launcher) / "2.790.0" / "Tools" / "Projects" / "G");
        RemoveGuards g = Guards();
        g.running = "2.790.0";
        const Result rr = Remove(launcher, "2.790.0", g);
        CHECK(rr.outcome == Outcome::Refused && Has(rr.message, "running") && !Has(rr.message, "project"));
        g.running = "2.900.0";
        g.keep = {{"2.790.0", "it is your default editor"}};
        CHECK(Has(Remove(launcher, "2.790.0", g).message, "default editor"));
    }

    // ---- links are never followed, and a folder with one is not deleted --------------------------------------------------------------------------------
#if !defined(_WIN32)
    {
        TempDir t;
        const fs::path launcher = MakeLauncher(t.path, "2.900.0");
        const fs::path v = MakeKept(launcher, "2.850.1");
        std::error_code ec;
        // a loop: a link to the folder it sits in, and two links that point at each other
        fs::create_directory_symlink(".", v / "Tools" / "loop", ec);
        CHECK(!ec);
        fs::create_symlink("b", v / "Tools" / "a", ec);
        fs::create_symlink("a", v / "Tools" / "b", ec);
        const auto before = Snapshot(okayver::VersionsDir(launcher));
        const auto t0 = std::chrono::steady_clock::now();
        const FolderScan s = ScanFolderForProjects(v);
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(secs < 5.0 && !s.link.empty() && s.entries < 100);                                // it ended, and it did not walk the loop
        const Result r = Remove(launcher, "2.850.1", Guards());
        CHECK(r.outcome == Outcome::Refused && Has(r.message, "link") && Has(r.message, "Nothing was removed"));
        CHECK(IsThere(launcher, "2.850.1") && Snapshot(okayver::VersionsDir(launcher)) == before);
        const DeleteCheck c = CheckBeforeDelete(v);
        CHECK(c.kind == DeleteCheck::HasLink && !c.ok() && c.folder == v / "Tools");

        // a link to a folder somewhere else that holds the person's projects: refused, and the other folder is not touched
        TempDir t2;
        const fs::path launcher2 = MakeLauncher(t2.path, "2.900.0");
        const fs::path v2 = MakeKept(launcher2, "2.850.1");
        const fs::path real = t2.path / "My Real Projects";
        MakeProject(real / "Game");
        fs::create_directory_symlink(real, v2 / "Tools" / "Projects", ec);
        const auto realBefore = Snapshot(real);
        const Result r2 = Remove(launcher2, "2.850.1", Guards());
        CHECK(r2.outcome == Outcome::Refused && IsThere(launcher2, "2.850.1") && Snapshot(real) == realBefore);
        // a link to a folder with no project in it is refused too: deleting must never reach what the link points to
        TempDir t3;
        const fs::path launcher3 = MakeLauncher(t3.path, "2.900.0");
        const fs::path v3 = MakeKept(launcher3, "2.850.1");
        Put(t3.path / "Textures" / "a.png", "png");
        fs::create_directory_symlink(t3.path / "Textures", v3 / "Tools" / "Textures", ec);
        CHECK(Remove(launcher3, "2.850.1", Guards()).outcome == Outcome::Refused && fs::exists(t3.path / "Textures" / "a.png") && IsThere(launcher3, "2.850.1"));
        // a link to a file, and a link that points nowhere, are only links: the folder goes and what they pointed at stays
        TempDir t4;
        const fs::path launcher4 = MakeLauncher(t4.path, "2.900.0");
        const fs::path v4 = MakeKept(launcher4, "2.850.1");
        Put(t4.path / "notes.txt", "mine");
        fs::create_symlink(t4.path / "notes.txt", v4 / "Tools" / "notes-link.txt", ec);
        fs::create_symlink(t4.path / "gone", v4 / "Tools" / "dangling", ec);
        CHECK(CheckBeforeDelete(v4).ok());
        CHECK(Remove(launcher4, "2.850.1", Guards()).outcome == Outcome::Removed && !fs::exists(v4) && Get(t4.path / "notes.txt") == "mine");
    }
#endif

    // ---- a folder that is too big or too deep to be sure about is not deleted ---------------------------------------------------------------------------------
    {
        // small limits: the refusal says why, and the folder is exactly as it was
        {
            TempDir t;
            const fs::path launcher = MakeLauncher(t.path, "2.900.0");
            const fs::path v = MakeKept(launcher, "2.850.1");
            for (int i = 0; i < 60; ++i) Put(v / "Tools" / "cache" / ("f" + std::to_string(i) + ".bin"), "x");
            IO io;
            io.scan.maxEntries = 50;
            const auto before = Snapshot(okayver::VersionsDir(launcher));
            const Result r = Remove(launcher, "2.850.1", Guards(), io);
            CHECK(r.outcome == Outcome::Refused && Has(r.message, "can't be checked") && Has(r.message, "Nothing was removed") && Has(r.message, "50"));
            CHECK(IsThere(launcher, "2.850.1") && Snapshot(okayver::VersionsDir(launcher)) == before);
            const DeleteCheck c = CheckBeforeDelete(v, io.scan);
            CHECK(c.kind == DeleteCheck::Unchecked && c.folder == v);
            io.scan.maxEntries = 5000;                                                           // with room to look, it is only a folder
            CHECK(Remove(launcher, "2.850.1", Guards(), io).outcome == Outcome::Removed);
        }
        // too deep: a file at the bottom of a chain of folders
        {
            TempDir t;
            const fs::path launcher = MakeLauncher(t.path, "2.900.0");
            const fs::path v = MakeKept(launcher, "2.850.1");
            fs::path deep = v / "Tools";
            for (int i = 0; i < 14; ++i) deep /= "d";
            Put(deep / "x.txt", "x");
            const Result r = Remove(launcher, "2.850.1", Guards());                              // the default limit is 12 folders
            CHECK(r.outcome == Outcome::Refused && Has(r.message, "deep") && IsThere(launcher, "2.850.1") && fs::exists(deep / "x.txt"));
            ScanLimits wide;
            wide.maxDepth = 30;
            CHECK(CheckBeforeDelete(v, wide).ok());
            // a PROJECT below the limit is refused all the same (it was not looked at, and it was not proven absent)
            Put(deep / "project.okayproj", "x");
            CHECK(Remove(launcher, "2.850.1", Guards()).outcome == Outcome::Refused && fs::exists(deep / "project.okayproj"));
            CHECK(CheckBeforeDelete(v, wide).kind == DeleteCheck::HoldsProject);
        }
        // the real limits: more than 20,000 files and folders
        {
            TempDir t;
            const fs::path launcher = MakeLauncher(t.path, "2.900.0");
            const fs::path v = MakeKept(launcher, "2.850.1");
            for (int d = 0; d < 41; ++d) {
                const fs::path dir = v / "Tools" / "junk" / ("d" + std::to_string(d));
                std::error_code ec;
                fs::create_directories(dir, ec);
                for (int f = 0; f < 500; ++f) std::ofstream(dir / ("f" + std::to_string(f))) << 'x';        // 20,500 files
            }
            const ScanLimits defaults;
            CHECK(defaults.maxEntries >= 5000 && defaults.maxEntries <= 100000 && defaults.maxDepth >= 8 && defaults.maxReported >= 1);   // sane, not silly
            const Result r = Remove(launcher, "2.850.1", Guards());
            CHECK(r.outcome == Outcome::Refused && Has(r.message, "can't be checked") && Has(r.message, "Nothing was removed"));
            CHECK(IsThere(launcher, "2.850.1") && fs::exists(v / "Tools" / "junk" / "d40" / "f499") && fs::exists(v / "Tools" / "junk" / "d0" / "f0"));
            CHECK(CheckBeforeDelete(v).kind == DeleteCheck::Unchecked);
        }
        // a folder that can't be read is not proven free of projects either (a normal user; root can read everything)
#if !defined(_WIN32)
        if (::geteuid() != 0) {
            TempDir t;
            const fs::path launcher = MakeLauncher(t.path, "2.900.0");
            const fs::path v = MakeKept(launcher, "2.850.1");
            Put(v / "Tools" / "secret" / "a.txt", "x");
            ::chmod((v / "Tools" / "secret").c_str(), 0);
            const Result r = Remove(launcher, "2.850.1", Guards());
            ::chmod((v / "Tools" / "secret").c_str(), 0700);
            CHECK(r.outcome == Outcome::Refused && Has(r.message, "could not be read") && IsThere(launcher, "2.850.1") && fs::exists(v / "Tools" / "secret" / "a.txt"));
        } else {
            std::cout << "  (skipped: the unreadable-folder case needs a user that is not root)\n";
        }
#endif
    }

    // ---- the prune (the audit's R4): a version with a project in it stays, the others go ------------------------------------------------------------------
    {
        TempDir t;
        const fs::path launcher = MakeLauncher(t.path, "2.900.0");
        for (const char* v : {"2.855.0", "2.854.0", "2.853.0", "2.852.0"}) MakeKept(launcher, v);
        const fs::path proj = MakeProject(okayver::VersionsDir(launcher) / "2.853.0" / "Tools" / "Projects" / "Keepsake");
        const auto projBefore = Snapshot(proj);
        const PruneResult pr = Prune(launcher, 1, "", Guards(), QuietIO());
        CHECK(pr.removed == (std::vector<std::string>{"2.854.0", "2.852.0"}));
        CHECK(pr.failed == std::vector<std::string>{"2.853.0"});
        CHECK(pr.notRemoved.size() == 1 && pr.notRemoved[0].first == "2.853.0" && Has(pr.notRemoved[0].second, "project of yours") && Has(pr.notRemoved[0].second, proj.string()));
        CHECK(okayver::InstalledVersions(launcher) == (std::vector<std::string>{"2.855.0", "2.853.0"}) && Snapshot(proj) == projBefore);
        // and every later prune says the same, still without touching it
        const PruneResult again = Prune(launcher, 0, "", Guards(), QuietIO());
        CHECK(again.removed == std::vector<std::string>{"2.855.0"} && again.failed == std::vector<std::string>{"2.853.0"} && Snapshot(proj) == projBefore);
        // a prune with nothing in the way is what it was
        TempDir t2;
        const fs::path launcher2 = MakeLauncher(t2.path, "2.900.0");
        for (const char* v : {"2.855.0", "2.854.0"}) MakeKept(launcher2, v);
        const PruneResult p2 = Prune(launcher2, 1, "", Guards(), QuietIO());
        CHECK(p2.removed == std::vector<std::string>{"2.854.0"} && p2.failed.empty() && p2.notRemoved.empty());
    }

    // ---- every other way a version folder is deleted: installing over a folder that is in the way, keeping the current editor, discarding it -------------------
    {
        // Install: a version folder with no editor in it is "in the way" and used to be deleted whole at the end of an install.
        TempDir t;
        const fs::path launcher = MakeLauncher(t.path, "2.900.0");
        const fs::path v = okayver::VersionsDir(launcher) / "2.850.1";
        const fs::path proj = MakeProject(v / "Tools" / "Projects" / "MyGame");                  // (the editor file was deleted by a virus scanner, say)
        const auto before = Snapshot(okayver::VersionsDir(launcher));
        InstallPlan plan;
        plan.ok = true;
        plan.version = "2.850.1";
        PlanFile pf;
        pf.asset = Asset::Editor;
        pf.indexName = "OkayEngine.exe";
        pf.diskName = DiskName(Asset::Editor);
        pf.localCopy = launcher / "Tools" / okayver::EditorExeName();
        plan.files.push_back(pf);
        int progressCalls = 0;
        IO io = QuietIO();
        io.progress = [&](const Progress&) { ++progressCalls; };
        const Result r = Install(launcher, plan, {}, io);
        CHECK(r.outcome == Outcome::Refused && Has(r.message, "project of yours") && Has(r.message, proj.string()) && Has(r.message, "Move it out first"));
        CHECK(progressCalls == 0 && Snapshot(okayver::VersionsDir(launcher)) == before && !IsThere(launcher, "2.850.1"));   // refused before any file was fetched
        // no project in the way: it is cleared and the version installed, as before
        fs::remove_all(v / "Tools" / "Projects");
        Put(v / "leftover.txt", "half an install");
        const Result r2 = Install(launcher, plan, {}, io);
        CHECK(r2.outcome == Outcome::Installed && IsThere(launcher, "2.850.1") && !fs::exists(v / "leftover.txt"));

        // KeepCurrent: the same folder in the way
        TempDir t2;
        const fs::path launcher2 = MakeLauncher(t2.path, "2.850.1");
        const fs::path v2 = okayver::VersionsDir(launcher2) / "2.850.1";
        const fs::path proj2 = MakeProject(v2 / "Tools" / "Projects" / "MyGame");
        const auto before2 = Snapshot(okayver::VersionsDir(launcher2));
        const Result k = KeepCurrent(launcher2, "2.850.1", FindCurrentInstall(launcher2), 1, true, QuietIO());
        CHECK(k.outcome != Outcome::Installed && !k.ok() && Has(k.message, "project of yours") && Snapshot(okayver::VersionsDir(launcher2)) == before2);
        CHECK(k.outcome == Outcome::Skipped);                                                    // "the update goes on without keeping it": never a failure
        fs::remove_all(v2 / "Tools" / "Projects");
        CHECK(KeepCurrent(launcher2, "2.850.1", FindCurrentInstall(launcher2), 1, true, QuietIO()).outcome == Outcome::Installed && IsThere(launcher2, "2.850.1"));

        // DiscardKept: only a copy the update made, and never one that has something of the person's in it
        TempDir t3;
        const fs::path launcher3 = MakeLauncher(t3.path, "2.900.0");
        const fs::path v3 = MakeKept(launcher3, "2.850.1");
        const fs::path proj3 = MakeProject(v3 / "Tools" / "Projects" / "MyGame");
        DiscardKept(launcher3, "2.850.1");
        CHECK(IsThere(launcher3, "2.850.1") && fs::exists(proj3 / "project.okayproj"));
        fs::remove_all(proj3);
        DiscardKept(launcher3, "2.850.1");
        CHECK(!fs::exists(v3));

        // CleanStale: a scratch folder that an older launcher left half deleted, with a project in it, is left for the person
        TempDir t4;
        const fs::path launcher4 = MakeLauncher(t4.path, "2.900.0");
        const fs::path versions = okayver::VersionsDir(launcher4);
        const fs::path stale = versions / ".removing-2.850.1-1";
        const fs::path salvage = MakeProject(stale / "Tools" / "Projects" / "MyGame");
        Put(versions / ".partial-2.860.0-1" / "Tools" / "OkayEngine.exe.part", "x");
        CHECK(CleanStale(launcher4) == 1);                                                       // the download scratch goes, the folder with a project stays
        CHECK(fs::exists(salvage / "project.okayproj") && !fs::exists(versions / ".partial-2.860.0-1"));
        fs::remove_all(salvage);
        CHECK(CleanStale(launcher4) == 1 && !fs::exists(stale));
    }

    // ---- how many of the person's projects were made with the version ------------------------------------------------------------------------------------------
    {
        const std::vector<std::string> stamps = {"2.850.0", "2.850.1", "v2.850.7", "2.855.0", "2.700.2", "", "garbage", "2.8500.0", "2.85.1"};
        CHECK(CountProjectsMadeWith(stamps, "2.850.1") == 3);                                     // the whole line v2.850.x
        CHECK(CountProjectsMadeWith(stamps, "2.850.0") == 3 && CountProjectsMadeWith(stamps, "v2.850") == 3);
        CHECK(CountProjectsMadeWith(stamps, "2.855.0") == 1 && CountProjectsMadeWith(stamps, "2.700.0") == 1);
        CHECK(CountProjectsMadeWith(stamps, "2.900.0") == 0);                                     // none
        CHECK(CountProjectsMadeWith({}, "2.850.1") == 0 && CountProjectsMadeWith(stamps, "") == 0 && CountProjectsMadeWith(stamps, "junk") == 0);   // nothing to count, or no version to count for
        CHECK(CountProjectsMadeWith({"", "", "x"}, "2.850.1") == 0);                              // a project with no stamp is nobody's
        CHECK(ProjectsMadeWithSentence(3, "2.850.1") == "3 of your projects were made with v2.850.x. You can open them with that version again after you install it.");
        CHECK(ProjectsMadeWithSentence(1, "2.850.1") == "1 of your projects was made with v2.850.x. You can open it with that version again after you install it.");
        CHECK(ProjectsMadeWithSentence(0, "2.850.1") == "None of your projects were made with v2.850.x.");
        CHECK(ProjectsMadeWithSentence(12, "v2.855.0") == "12 of your projects were made with v2.855.x. You can open them with that version again after you install it.");
    }

    TEST_MAIN_RESULT();
}
