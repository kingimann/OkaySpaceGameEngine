// okay: the engine's command line. See Cli.hpp. Our own code, written on the engine's own APIs (ProjectCreate,
// ProjectVersion, ProjectState, SceneSerializer, SceneMerge, SceneDiff, SceneMigration, CommandLine) and the git
// documentation for the merge and diff driver interfaces (gitattributes: "Defining a custom merge driver",
// "Defining an external diff driver").
#include "Cli.hpp"
#include "CliInternal.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "Engine.hpp"
#include "Git.hpp"
#include "Process.hpp"
#include "ProjectState.hpp"     // editor/: ProjectSettings and the text of project.okayproj
#include "ProjectCreate.hpp"    // shared/: name and place checks, the folders of a new project, templates
#include "ProjectVersion.hpp"   // shared/: the engine_version stamp and what a version difference means
#include "okay/Core/CommandLine.hpp"
#include "okay/Core/StringUtil.hpp"
#include "okay/Input/InputAsset.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneDiff.hpp"
#include "okay/Scene/SceneMerge.hpp"
#include "okay/Scene/SceneMigration.hpp"
#include "okay/Scene/SceneSerializer.hpp"

#ifndef OKAY_ENGINE_VERSION
#define OKAY_ENGINE_VERSION "dev"
#endif

namespace okaycli {
namespace fs = std::filesystem;
using okay::str::Trim;

const char* EngineVersion() { return OKAY_ENGINE_VERSION; }

void Io::Say(const std::string& text) const {
    std::string t = text;
    if (t.empty() || t.back() != '\n') t += '\n';
    if (out) *out += t;
    else { std::fputs(t.c_str(), stdout); std::fflush(stdout); }
}
void Io::Warn(const std::string& text) const {
    std::string t = text;
    if (t.empty() || t.back() != '\n') t += '\n';
    if (err) *err += t;
    else { std::fputs(t.c_str(), stderr); std::fflush(stderr); }
}

namespace detail {

std::string Pad(std::string s, std::size_t n) { if (s.size() < n) s.resize(n, ' '); return s; }

std::string ReadFile(const std::string& path, bool* ok) {
    std::string t;
    const bool r = okayproj::ReadText(path, t);
    if (ok) *ok = r;
    return t;
}

std::string JoinWords(const std::vector<std::string>& v, std::size_t from) {
    std::string s;
    for (std::size_t i = from; i < v.size(); ++i) { if (i > from) s += ' '; s += v[i]; }
    return s;
}

/// Parse a command's arguments; a bad command line is said in plain words.
bool ParseArgs(okay::CommandLine& cl, const std::vector<std::string>& args, const std::string& command, const Io& io) {
    cl.Flag("help", "show this help").Short('h');
    if (cl.Parse(args)) return true;
    io.Warn(cl.Error() + "\n" + HelpText(command));
    return false;
}

std::string RelTo(const std::string& path, const std::string& root) {
    std::error_code ec;
    const fs::path r = fs::relative(path, root, ec);
    return ec || r.empty() ? path : r.generic_string();
}

bool HasClashMarks(const std::string& text) {
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);)
        if (line.rfind("<<<<<<< ", 0) == 0 || line == "<<<<<<<" || line.rfind(">>>>>>> ", 0) == 0) return true;
    return false;
}

std::string StableOkayPath() {
    // okay on PATH is where a person (and the installer) puts it; it does not move when a version is removed.
    const std::string onPath = FindOnPath("okay");
    if (!onPath.empty()) return onPath;
    const std::string self = SelfPath();
    if (self.empty()) return "";
    // A kept copy of an older version (<L>/Versions/<v>/Tools/okay) is removed when the launcher prunes versions:
    // use the launcher-level <L>/Tools/okay when there is one.
    std::error_code ec;
    const fs::path dir = fs::path(self).parent_path();
    if (dir.parent_path().parent_path().filename() == okayver::VersionsDirName()) {
        const fs::path top = okayver::LauncherDirOf(dir) / okayver::ToolsDirName() / fs::path(self).filename();
        if (fs::is_regular_file(top, ec)) return top.string();
    }
    return self;
}

} // namespace detail
using namespace detail;

namespace {

// ---- the commands and their help ---------------------------------------------------------------------------------

struct CommandHelp { const char* name; const char* usage; const char* summary; const char* details; };

const std::vector<CommandHelp>& Commands() {
    static const std::vector<CommandHelp> c = {
        {"new", "okay new <name> [--template NAME] [--in FOLDER] [--version V] [--no-git]",
         "Make a new project.",
         "Without --template you get an empty project (one empty scene) and no window opens.\n"
         "With --template the editor opens and builds that starter scene (okay new --template list shows them).\n"
         "The project gets Git version control unless you add --no-git (it needs Git installed)."},
        {"open", "okay open [scene]", "Open the project in the editor.",
         "Opens the project's start scene, or the scene you name. This window waits until you close the editor."},
        {"play", "okay play [scene]", "Play the game in the player.",
         "Plays the project's start scene, or the scene you name."},
        {"run", "okay run <scene> [--input IN.json] [--check CHECK.json] [--out FOLDER] [--frames N]",
         "Play a scene with no window, with scripted input, and check what happened.",
         "The same as scripts/okay_run.py (docs/process/okay_run.md). It needs Python and the okay_run tool,\n"
         "which come with the engine's source folder. Exit code 0 means every check passed."},
        {"build", "okay build [--target windows] [--scene S] [--out FOLDER] [--name GAME] [--zip] [--dry-run] [--dev]",
         "Build the game so others can play it.",
         "Uses the editor's Build (the project's Build Settings) without opening a window. The game goes to\n"
         "<project>/build unless you say --out. Builds are for the computer you are on; Windows is the main target."},
        {"test", "okay test [--engine [BUILD_FOLDER]] [-R NAME]",
         "Check the project.",
         "Checks that every scene and prefab in the project loads and has no merge clash marks.\n"
         "--engine runs the engine's own tests (ctest) in a build folder instead; -R picks tests by name."},
        {"info", "okay info", "Show the engine version, the project and its version control.", ""},
        {"doctor", "okay doctor", "Check that everything a project needs is in place, and say how to fix what is not.", ""},
        {"engine", "okay engine <build|doctor> ...", "Build the engine's own editor, launcher and player from an engine checkout.",
         "okay engine build [editor|launcher|player|all] [--debug] [--jobs N] [--clean] [--run] [--dir BUILD]\n"
         "  builds OkayEngine.exe, OkaySpace.exe and OkaySpacePlayer.exe with CMake (as scripts/build-local.ps1 does).\n"
         "okay engine doctor  says what is missing (CMake, a compiler, Ninja, SDL2) and where to get it.\n"
         "okay help engine build tells you more."},
        {"vc", "okay vc <init|clone|status|save|resolve|history|undo|branch|switch|sync|remote|who> ...",
         "Version control (Git underneath).", ""},
        {"help", "okay help [command]", "Show help for a command.", ""},
    };
    return c;
}

const std::vector<CommandHelp>& VcCommands() {
    static const std::vector<CommandHelp> c = {
        {"init", "okay vc init [--separate | --outer]", "Start version control in this project.",
         "Runs git init if needed, writes .gitignore (the build output folder, caches, backups, per-person files) and\n"
         ".gitattributes (scenes merge with the engine's scene merge, pictures and sounds are binary), and\n"
         "tells Git to merge and compare .okayscene and .okayprefab files with the engine. Safe to run again.\n"
         "A project inside another Git folder needs --separate (its own) or --outer (set up the outer one)."},
        {"clone", "okay vc clone <url or folder> [folder]", "Get a copy of an existing project and set it up.", ""},
        {"status", "okay vc status", "What changed since the last save, grouped into scenes, scripts and assets.", ""},
        {"save", "okay vc save \"message\"", "Save a version of every change, with a message saying what you did.",
         "It refuses while a clash from a merge or sync is not settled (see okay vc resolve)."},
        {"resolve", "okay vc resolve <file> --mine | --theirs | --as-is", "Settle a clash after a merge or sync.",
         "--mine keeps your side of each clash, --theirs takes theirs; every change that did not clash is kept\n"
         "either way (a file that is not a scene is taken whole). --as-is keeps the file as you fixed it.\n"
         "The clashes are listed in .okay/clashes/<file>.txt."},
        {"history", "okay vc history [file] [-n N]", "The saved versions, newest first (of one file, or of everything).", ""},
        {"undo", "okay vc undo <file> [--to ID]", "Put a file back to how it was at the last save (or at save ID).",
         "Your current copy is kept in .okay/backups/undo first, so nothing is lost."},
        {"branch", "okay vc branch [name]", "List the version lines, or start a new one and move to it.",
         "A version line (a Git branch) lets you try something without touching the main line."},
        {"switch", "okay vc switch <name> [--carry]", "Move to another version line.",
         "Save your changes first, or add --carry to take them with you."},
        {"sync", "okay vc sync", "Get the online copy's new saves and send yours.",
         "Works with any Git server or a shared folder. Set the online copy once with okay vc remote <url>."},
        {"remote", "okay vc remote [url]", "Show or set the online copy (the Git remote called origin).", ""},
        {"who", "okay vc who [\"Your Name\" email] [--global]", "Show or set the name and email your saves are signed with.",
         "Stored for this project; --global stores it for every project on this computer."},
    };
    return c;
}


} // namespace

std::string HelpText(const std::string& command) {
    if (command.empty()) {
        std::string t = std::string("okay ") + EngineVersion() + ": the OkaySpace engine from the command line.\n\nCommands:\n";
        for (const CommandHelp& c : Commands()) t += "  " + Pad(c.name, 8) + c.summary + "\n";
        t += "\nVersion control:\n";
        for (const CommandHelp& c : VcCommands()) t += "  vc " + Pad(c.name, 9) + c.summary + "\n";
        t += "\nPut --project FOLDER before the command to work on another project (default: the one this folder is in).\n"
             "okay help <command> tells you more.\n";
        return t;
    }
    if (command == "engine build") return engine::BuildUsage();
    if (command == "engine doctor") return engine::DoctorUsage();
    std::string cmd = command, sub;
    if (cmd.rfind("vc ", 0) == 0) { sub = cmd.substr(3); cmd = "vc"; }
    if (cmd == "vc" && !sub.empty()) {
        for (const CommandHelp& c : VcCommands())
            if (sub == c.name) return std::string("Usage: ") + c.usage + "\n\n" + c.summary + "\n" + (c.details[0] ? std::string(c.details) + "\n" : "");
        return "";
    }
    for (const CommandHelp& c : Commands()) {
        if (cmd != c.name) continue;
        std::string t = std::string("Usage: ") + c.usage + "\n\n" + c.summary + "\n" + (c.details[0] ? std::string(c.details) + "\n" : "");
        if (cmd == "vc") {
            t += "\n";
            for (const CommandHelp& v : VcCommands()) t += "  " + Pad(v.name, 9) + v.summary + "\n";
            t += "\nokay help vc <command> tells you more.\n";
        }
        return t;
    }
    return "";
}

// ---- projects ----------------------------------------------------------------------------------------------------

std::string FindProjectRoot(const std::string& start) {
    std::error_code ec;
    fs::path p = fs::absolute(start.empty() ? fs::path(".") : fs::path(start), ec);
    if (ec) return "";
    p = p.lexically_normal();
    if (p.has_parent_path() && p.filename().empty()) p = p.parent_path();   // "/x/Game/." normalizes to "/x/Game/"
    if (!fs::is_directory(p, ec)) p = p.parent_path();
    fs::path withAssets;
    for (fs::path q = p; !q.empty(); q = q.parent_path()) {
        if (fs::is_regular_file(q / okayproj::SettingsFileName(), ec)) return q.string();
        if (withAssets.empty() && fs::is_directory(q / "Assets", ec)) withAssets = q;
        if (q == q.parent_path()) break;
    }
    return withAssets.string();
}

std::string MainScene(const std::string& root) {
    std::error_code ec;
    std::ifstream f((fs::path(root) / ".okay" / "scene_build.txt").string());
    for (std::string line; f && std::getline(f, line);) {   // "1|Assets/Level.okayscene": ticked, path (the Scene Manager's list)
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.size() < 3 || line[0] != '1' || line[1] != '|') continue;
        fs::path p(line.substr(2));
        if (!p.is_absolute()) p = fs::path(root) / p;
        if (fs::is_regular_file(p, ec)) return p.lexically_normal().string();
    }
    std::vector<std::string> found;
    const fs::path assets = fs::path(root) / "Assets";
    if (!fs::is_directory(assets, ec)) return "";
    for (fs::recursive_directory_iterator it(assets, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec))
        if (it->is_regular_file(ec) && it->path().extension() == ".okayscene") found.push_back(it->path().string());
    if (found.empty()) return "";
    std::sort(found.begin(), found.end(), [](const std::string& a, const std::string& b) {
        const auto da = std::count(a.begin(), a.end(), '/') + std::count(a.begin(), a.end(), '\\');
        const auto db = std::count(b.begin(), b.end(), '/') + std::count(b.begin(), b.end(), '\\');
        return da != db ? da < db : a < b;   // a scene at the top of Assets first, then by name
    });
    return found.front();
}

bool CreateEmptyProject(const std::string& root, const std::string& name, const std::string& version, std::string& error) {
    okayproj::Made made;
    if (!okayproj::MakeSkeleton(fs::path(root), made, error)) return false;
    oked::ProjectSettings settings;
    if (!version.empty()) {
        const std::string why = okayproj::CheckProjectVersion(version);
        if (!why.empty()) { okayproj::Undo(made); error = why; return false; }
        std::snprintf(settings.version, sizeof(settings.version), "%s", version.c_str());
    }
    std::string text = oked::WriteProjectSettings(settings, "");
    const std::string stamp = okayver::NormalizeVersion(EngineVersion());
    if (!stamp.empty()) text = okayver::SetStampInText(text, stamp);
    const fs::path settingsFile = okayproj::SettingsPath(root);
    const fs::path scene = fs::path(root) / "Assets" / (name + ".okayscene");
    const fs::path input = fs::path(root) / "Assets" / "input.okayinput";
    okay::Scene empty(name);
    bool ok = okayproj::WriteTextAtomic(settingsFile.string(), text, &error);
    if (ok) { made.Add(settingsFile); ok = okayproj::WriteTextAtomic(scene.string(), okay::SceneSerializer::Serialize(empty), &error); }
    if (ok) { made.Add(scene); ok = okayproj::WriteTextAtomic(input.string(), okay::SerializeInputAsset(okay::DefaultInputAsset()), &error); }
    if (ok) made.Add(input);
    if (!ok) { okayproj::Undo(made); return false; }
    return true;
}

namespace {

// ---- finding the engine's programs -------------------------------------------------------------------------------

#if defined(_WIN32)
const char* kExe = ".exe";
#else
const char* kExe = "";
#endif

/// The editor, the player or a tool: the environment variable, else beside this program (and in a Tools folder beside
/// or above it, the release layout), else PATH.
std::string FindEngineProgram(const char* envVar, const std::string& stem) {
    std::error_code ec;
    if (const char* e = std::getenv(envVar); e && *e) return fs::is_regular_file(e, ec) ? std::string(e) : std::string();
    const std::string file = stem + kExe;
    const fs::path self = SelfPath();
    if (!self.empty()) {
        const fs::path dir = self.parent_path();
        for (const fs::path& d : {dir, dir / "Tools", dir.parent_path(), dir.parent_path() / "Tools", dir.parent_path() / "bin"})
            if (fs::is_regular_file(d / file, ec)) return (d / file).string();
    }
    return FindOnPath(file);
}
std::string EditorProgram() { return FindEngineProgram("OKAY_EDITOR", "OkayEngine"); }
std::string PlayerProgram() { return FindEngineProgram("OKAY_PLAYER", "OkaySpacePlayer"); }

/// A file of the engine's source folder (scripts/okay_run.py ...), searched upward from this program and from `from`.
std::string FindSourceFile(const std::string& rel, const std::string& from) {
    std::error_code ec;
    for (fs::path start : {fs::path(SelfPath()).parent_path(), fs::absolute(from.empty() ? "." : from, ec)}) {
        for (fs::path q = start; !q.empty(); q = q.parent_path()) {
            if (fs::is_regular_file(q / rel, ec)) return (q / rel).string();
            if (q == q.parent_path()) break;
        }
    }
    return "";
}

std::string PythonProgram() {
    for (const char* p : {"python3", "python", "py"}) {
        const std::string f = FindOnPath(p);
        if (!f.empty()) return f;
    }
    return "";
}

unsigned long OwnPid() {
#if defined(_WIN32)
    return (unsigned long)GetCurrentProcessId();
#else
    return (unsigned long)::getpid();
#endif
}





/// The project root to work on: --project, else the project this folder is in.
std::string ProjectOrSay(const std::string& given, const Io& io, bool quiet = false) {
    const std::string root = FindProjectRoot(given.empty() ? "." : given);
    if (root.empty() && !quiet)
        io.Warn((given.empty() ? std::string("This folder is not inside an OkaySpace project.") : "\"" + given + "\" is not an OkaySpace project.") +
                " Go to a project folder, or add --project FOLDER.");
    return root;
}


// ---- okay new / open / play / run / build / test -------------------------------------------------------------------

int CmdNew(const std::vector<std::string>& args, const std::string&, const Io& io) {
    okay::CommandLine cl;
    cl.Value("template", "NAME").Value("in", "FOLDER").Value("version", "V").Flag("no-git");
    if (!ParseArgs(cl, args, "new", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("new")); return 0; }
    const std::string tpl = cl.Get("template");
    if (okay::str::LowerAscii(tpl) == "list") {
        std::string t = "Templates (use the name in quotes with --template):\n";
        for (const auto& info : okayproj::Templates()) t += "  \"" + std::string(info.title) + "\"  " + info.blurb + "\n";
        io.Say(t);
        return 0;
    }
    const std::string name = JoinWords(cl.Positionals());
    if (name.empty()) { io.Warn("Say what the project is called: okay new MyGame"); return 2; }
    const okayproj::TemplateInfo* info = nullptr;
    if (!tpl.empty()) {
        for (const auto& t : okayproj::Templates()) if (okay::str::LowerAscii(t.title) == okay::str::LowerAscii(tpl)) info = &t;
        if (!info) { io.Warn("There is no template called \"" + tpl + "\". See them with: okay new --template list"); return 2; }
    }
    const okayproj::Plan plan = okayproj::PlanProject(cl.Get("in", "."), name);
    if (!plan.ok()) { io.Warn(plan.Problem() + " Nothing was made."); return 1; }
    if (plan.renamed) io.Say("A project called " + plan.name + " is already there, so this one is called " + plan.folder + ".");
    const std::string root = plan.root.string();
    const std::string version = cl.Get("version");
    if (!version.empty()) {
        const std::string why = okayproj::CheckProjectVersion(version);
        if (!why.empty()) { io.Warn(why); return 2; }
    }
    if (info && std::string(info->title) != "Empty") {
        // A starter scene is built by the editor's own templates: hand over to the editor, as the launcher does.
        const std::string editor = EditorProgram();
        if (editor.empty()) {
            io.Warn("The \"" + std::string(info->title) + "\" template is made by the editor, and the editor was not found. Nothing was made.\n"
                    "Put okay beside OkayEngine, or set OKAY_EDITOR to the editor's path. Without --template you get an empty project.");
            return 1;
        }
        std::vector<std::string> argv = {editor, "--launcher", std::to_string(OwnPid()), "--new-project", root, "--template", info->title};
        if (!version.empty()) { argv.push_back("--project-version"); argv.push_back(version); }
        io.Say("Opening the editor to make " + plan.folder + " from the \"" + std::string(info->title) + "\" template. This window waits until you close the editor.");
        const RunResult r = Run(argv, "", false);
        if (!r.started) { io.Warn("The editor could not be started (" + editor + ")."); return 1; }
    } else {
        std::string err;
        if (!CreateEmptyProject(root, plan.folder, version, err)) { io.Warn("Couldn't make the project: " + err); return 1; }
        io.Say("Made the project " + plan.folder + " at " + root + ".");
    }
    std::error_code ec;
    if (!fs::is_directory(fs::path(root) / "Assets", ec)) { io.Warn("The project was not made (the editor closed before it saved it)."); return 1; }
    if (!cl.Has("no-git")) {
        if (!vc::Git::Available()) io.Say("Git is not installed, so the project has no version control yet. Install Git, then run: okay vc init");
        else {
            const int rc = Main({"--project", root, "vc", "init"}, io);
            if (rc != 0) return rc;
        }
    }
    io.Say("Next: cd \"" + root + "\" and run okay open (or okay play).");
    return 0;
}

int CmdOpen(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    if (!ParseArgs(cl, args, "open", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("open")); return 0; }
    std::string scene = cl.Positional(0);
    const std::string root = ProjectOrSay(project.empty() && !scene.empty() ? scene : project, io);
    if (root.empty()) return 1;
    if (scene.empty()) scene = MainScene(root);
    std::error_code ec;
    if (!scene.empty() && !fs::is_regular_file(scene, ec)) { io.Warn("There is no scene at " + scene + "."); return 1; }
    const std::string editor = EditorProgram();
    if (editor.empty()) { io.Warn("The editor (OkayEngine) was not found. Put okay beside it, or set OKAY_EDITOR to its path."); return 1; }
    std::vector<std::string> argv = {editor, "--launcher", std::to_string(OwnPid())};
    if (!scene.empty()) { argv.push_back("--open"); argv.push_back(fs::absolute(scene, ec).string()); }
    io.Say(scene.empty() ? "Opening the editor. This window waits until you close it."
                         : "Opening " + RelTo(scene, root) + " in the editor. This window waits until you close it.");
    const RunResult r = Run(argv, root, false);
    if (!r.started) { io.Warn("The editor could not be started (" + editor + ")."); return 1; }
    return r.exitCode == 0 ? 0 : 1;
}

int CmdPlay(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    if (!ParseArgs(cl, args, "play", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("play")); return 0; }
    std::string scene = cl.Positional(0);
    if (scene.empty()) {
        const std::string root = ProjectOrSay(project, io);
        if (root.empty()) return 1;
        scene = MainScene(root);
        if (scene.empty()) { io.Warn("This project has no scene to play yet. Make one in the editor (okay open)."); return 1; }
    }
    std::error_code ec;
    if (!fs::is_regular_file(scene, ec)) { io.Warn("There is no scene at " + scene + "."); return 1; }
    const std::string player = PlayerProgram();
    if (player.empty()) { io.Warn("The player (OkaySpacePlayer) was not found. Put okay beside it, or set OKAY_PLAYER to its path."); return 1; }
    io.Say("Playing " + fs::path(scene).filename().string() + ".");
    const RunResult r = Run({player, fs::absolute(scene, ec).string()}, "", false);
    if (!r.started) { io.Warn("The player could not be started (" + player + ")."); return 1; }
    return r.exitCode;
}

int CmdRun(const std::vector<std::string>& args, const std::string&, const Io& io) {
    okay::CommandLine cl;
    cl.Value("input", "IN.json").Value("check", "CHECK.json").Value("out", "FOLDER").Value("frames", "N").Value("dt", "S")
      .Value("trace", "NAMES").Value("shots", "F,F").Value("size", "WxH").Value("bin", "PATH");
    if (!ParseArgs(cl, args, "run", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("run")); return 0; }
    const std::string scene = cl.Positional(0);
    if (scene.empty()) { io.Warn("Say which scene to run: okay run Assets/Level.okayscene --input in.json --check check.json"); return 2; }
    const std::string script = FindSourceFile("scripts/okay_run.py", ".");
    const std::string python = PythonProgram();
    if (script.empty() || python.empty()) {
        io.Warn(std::string("okay run needs ") + (python.empty() ? "Python" : "the engine's source folder (scripts/okay_run.py)") +
                ", which was not found. It is a tool for the engine's source checkout (docs/process/okay_run.md).");
        return 1;
    }
    std::vector<std::string> argv = {python, script, scene};
    for (const char* v : {"input", "check", "out", "frames", "dt", "trace", "shots", "size", "bin"})
        if (cl.Has(v)) { argv.push_back(std::string("--") + v); argv.push_back(cl.Get(v)); }
    if (!cl.Has("bin")) {   // the okay_run tool beside this program, when the python script would not find it itself
        const fs::path self = SelfPath();
        std::error_code ec;
        const fs::path bin = self.parent_path() / (std::string("okay_run") + kExe);
        if (!self.empty() && fs::is_regular_file(bin, ec)) { argv.push_back("--bin"); argv.push_back(bin.string()); }
    }
    const RunResult r = Run(argv, "", false);
    if (!r.started) { io.Warn("Python could not be started (" + python + ")."); return 1; }
    return r.exitCode;
}

int CmdBuild(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    cl.Value("target", "PLATFORM").Value("scene", "S").Value("out", "FOLDER").Value("name", "GAME").Value("layout", "L")
      .Value("version", "V").Value("report", "FILE").Flag("zip").Flag("dry-run").Flag("dev");
    if (!ParseArgs(cl, args, "build", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("build")); return 0; }
#if defined(_WIN32)
    const std::string here = "windows";
#elif defined(__APPLE__)
    const std::string here = "macos";
#else
    const std::string here = "linux";
#endif
    const std::string target = okay::str::LowerAscii(cl.Get("target", here));
    if (target != here) {
        io.Warn("This computer builds " + here + " games. A " + target + " build has to be made on " + target +
                (target == "windows" ? " (or by the engine's release build)." : "."));
        return 1;
    }
    const std::string root = ProjectOrSay(project, io);
    if (root.empty()) return 1;
    std::string scene = cl.Get("scene", cl.Positional(0));
    if (scene.empty()) scene = MainScene(root);
    if (scene.empty()) { io.Warn("This project has no scene to build yet."); return 1; }
    std::error_code ec;
    const std::string out = cl.Has("out") ? fs::absolute(cl.Get("out"), ec).string() : (fs::path(root) / "build").string();
    const std::string editor = EditorProgram();
    if (editor.empty()) { io.Warn("Building uses the editor (OkayEngine), which was not found. Put okay beside it, or set OKAY_EDITOR."); return 1; }
    std::vector<std::string> argv = {editor, "--build-game", fs::absolute(scene, ec).string(), out};
    for (const char* v : {"name", "layout", "version", "report"})
        if (cl.Has(v)) { argv.push_back(std::string("--") + v); argv.push_back(cl.Get(v)); }
    for (const char* f : {"zip", "dry-run", "dev"})
        if (cl.Has(f)) argv.push_back(std::string("--") + f);
    io.Say("Building " + RelTo(scene, root) + " into " + out + ".");
    const RunResult r = Run(argv, root, false);
    if (!r.started) { io.Warn("The editor could not be started (" + editor + ")."); return 1; }
    io.Say(r.exitCode == 0 ? "The build is done." : "The build did not work. The lines above say why.");
    return r.exitCode == 0 ? 0 : 1;
}


int CmdTest(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    cl.OptionalValue("engine", "BUILD_FOLDER").Value("filter", "NAME").Short('R');
    if (!ParseArgs(cl, args, "test", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("test")); return 0; }
    std::error_code ec;
    if (cl.Has("engine")) {
        std::string build = cl.Get("engine");
        if (build.empty()) {
            const std::string f = FindSourceFile("build/CTestTestfile.cmake", ".");
            if (!f.empty()) build = fs::path(f).parent_path().string();
        }
        if (build.empty() || !fs::is_regular_file(fs::path(build) / "CTestTestfile.cmake", ec)) {
            io.Warn("No engine build folder with tests was found. Say which one: okay test --engine build");
            return 1;
        }
        const std::string ctest = FindOnPath("ctest");
        if (ctest.empty()) { io.Warn("ctest (part of CMake) is not installed."); return 1; }
        std::vector<std::string> argv = {ctest, "--test-dir", build, "--output-on-failure"};
        if (cl.Has("filter")) { argv.push_back("-R"); argv.push_back(cl.Get("filter")); }
        const RunResult r = Run(argv, "", false);
        return r.started ? (r.exitCode == 0 ? 0 : 1) : 1;
    }
    const std::string root = ProjectOrSay(project, io);
    if (root.empty()) return 1;
    int scenes = 0, prefabs = 0;
    std::vector<std::string> problems;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string fname = it->path().filename().string();
        if (it->is_directory(ec) && (fname == ".git" || fname == ".okay" || fname == "build" || fname == "Build" || fname == "export")) {
            it.disable_recursion_pending();
            continue;
        }
        const std::string ext = it->path().extension().string();
        if (!it->is_regular_file(ec) || (ext != ".okayscene" && ext != ".okayprefab")) continue;
        (ext == ".okayscene" ? scenes : prefabs)++;
        const std::string rel = RelTo(it->path().string(), root);
        bool readOk = false;
        const std::string text = ReadFile(it->path().string(), &readOk);
        if (!readOk) { problems.push_back(rel + ": could not be read."); continue; }
        if (HasClashMarks(text)) { problems.push_back(rel + ": still has merge clash marks (<<<<<<<) in it."); continue; }
        okay::Scene probe("probe");
        std::string err;
        if (!okay::SceneSerializer::Deserialize(probe, text, &err)) problems.push_back(rel + ": does not load (" + err + ").");
    }
    std::string t = "Checked " + std::to_string(scenes) + (scenes == 1 ? " scene" : " scenes") + " and " + std::to_string(prefabs) +
                    (prefabs == 1 ? " prefab" : " prefabs") + ".";
    if (problems.empty()) { io.Say(t + " Everything loads."); return 0; }
    t += " " + std::to_string(problems.size()) + (problems.size() == 1 ? " problem:" : " problems:");
    for (const std::string& p : problems) t += "\n  " + p;
    io.Warn(t);
    return 1;
}

// ---- okay info / doctor ----------------------------------------------------------------------------------------

std::string ReadSetting(const std::string& projectFileText, const std::string& key) {
    std::istringstream in(projectFileText);
    for (std::string line; std::getline(in, line);) {
        while (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind(key + "=", 0) == 0) return line.substr(key.size() + 1);
    }
    return "";
}

std::string StampWords(const std::string& projectStamp) {
    using D = okayver::Decision;
    switch (okayver::Decide(projectStamp, EngineVersion())) {
        case D::Same: return "the same as this engine";
        case D::SamePatchLine: return "the same release line as this engine";
        case D::Older: return "older than this engine (the editor offers to upgrade it when you open it)";
        case D::Newer: return "newer than this engine: use a newer okay and editor";
        case D::DevBuild: return "this engine is a development build";
        default: return "not recorded yet";
    }
}

int CmdInfo(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    if (!ParseArgs(cl, args, "info", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("info")); return 0; }
    std::string t = std::string("Engine: OkaySpace ") + EngineVersion() + "\n";
    const std::string self = SelfPath();
    t += "This program: " + (self.empty() ? std::string("(unknown)") : self) + "\n";
    const std::string editor = EditorProgram(), player = PlayerProgram();
    t += "Editor: " + (editor.empty() ? std::string("not found") : editor) + "\n";
    t += "Player: " + (player.empty() ? std::string("not found") : player) + "\n";
    const std::string root = FindProjectRoot(project.empty() ? "." : project);
    if (root.empty()) t += "Project: none here (go to a project folder, or add --project FOLDER)\n";
    else {
        const std::string settings = ReadFile(okayproj::SettingsPath(root));
        const okayver::Stamp st = okayver::ReadProjectStamp(root);
        t += "Project: " + fs::path(root).filename().string() + " at " + root + "\n";
        const std::string gameVersion = ReadSetting(settings, "version");
        t += "Game version: " + (gameVersion.empty() ? std::string("not set") : gameVersion) + "\n";
        t += "Made with engine: " + (st.version.empty() ? std::string("not recorded") : st.version) + " (" + StampWords(st.version) + ")\n";
        const std::string scene = MainScene(root);
        t += "Start scene: " + (scene.empty() ? std::string("none yet") : RelTo(scene, root)) + "\n";
    }
    const std::string gv = vc::Git::Version();
    t += "Git: " + (gv.empty() ? std::string("not installed") : gv) + "\n";
    if (!root.empty() && !gv.empty()) {
        vc::Git git(root);
        if (!git.IsRepo()) t += "Version control: not started (okay vc init)\n";
        else {
            const std::string branch = git.Branch(), remote = git.Remote();
            const auto changes = git.Status();
            t += "Version line: " + (branch.empty() ? std::string("(none)") : branch) + ", " + std::to_string(changes.size()) + " unsaved change(s)\n";
            t += "Online copy: " + (remote.empty() ? std::string("none (okay vc remote <url>)") : remote) + "\n";
        }
    }
    io.Say(t);
    return 0;
}

int CmdDoctor(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    if (!ParseArgs(cl, args, "doctor", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("doctor")); return 0; }
    int problems = 0;
    std::string t;
    auto good = [&](const std::string& s) { t += "  ok       " + s + "\n"; };
    auto bad = [&](const std::string& s, const std::string& fix) { t += "  problem  " + s + "\n           Fix: " + fix + "\n"; ++problems; };
    auto note = [&](const std::string& s) { t += "  note     " + s + "\n"; };
    t += std::string("OkaySpace ") + EngineVersion() + "\n";
    const std::string editor = EditorProgram(), player = PlayerProgram();
    if (editor.empty()) bad("The editor (OkayEngine) was not found.", "put okay beside the editor, or set OKAY_EDITOR to its path.");
    else good("The editor is at " + editor);
    if (player.empty()) bad("The player (OkaySpacePlayer) was not found.", "put okay beside the player, or set OKAY_PLAYER to its path.");
    else good("The player is at " + player);
    const std::string gv = vc::Git::Version();
    if (gv.empty()) bad("Git is not installed, so version control does not work.", "install Git (git-scm.com), then run okay vc init.");
    else good(gv);
    const std::string root = FindProjectRoot(project.empty() ? "." : project);
    if (root.empty()) note("No project here, so only the programs were checked.");
    else {
        std::error_code ec;
        bool readOk = false;
        const std::string settings = ReadFile(okayproj::SettingsPath(root), &readOk);
        if (!readOk) bad("The project has no project.okayproj.", "open it once in the editor (okay open), which writes it.");
        else good("Project " + fs::path(root).filename().string() + " at " + root);
        const okayver::Stamp st = okayver::ReadProjectStamp(root);
        if (okayver::Decide(st.version, EngineVersion()) == okayver::Decision::Newer)
            bad("The project was saved by a newer engine (" + st.version + ").", "use that engine version or newer.");
        else if (!st.version.empty()) good("Made with engine " + st.version + " (" + StampWords(st.version) + ")");
        if (MainScene(root).empty()) note("The project has no scene yet.");
        if (!gv.empty()) {
            vc::Git git(root);
            if (!git.IsRepo()) note("Version control is not started here (okay vc init starts it).");
            else {
                const RunResult d = git.Capture({"config", "--get", "merge.okayscene.driver"});
                const std::string want = vc::MergeDriverCommand(StableOkayPath());
                const std::string have = Trim(d.out);
                if (have.empty()) bad("Git does not merge scenes with the engine yet.", "run okay vc init.");
                else if (have != want) bad("Git's scene merge points at a different okay program (" + have + ").", "run okay vc init with this okay.");
                else good("Git merges and compares scenes with the engine");
                const std::string attrs = ReadFile((fs::path(git.TopLevel().empty() ? root : git.TopLevel()) / ".gitattributes").string()) +
                                          ReadFile((fs::path(root) / ".gitattributes").string());
                if (attrs.find("merge=okayscene") == std::string::npos) bad(".gitattributes does not send scenes to the engine's merge.", "run okay vc init.");
                const std::string user = git.Capture({"config", "user.name"}).out;
                if (user.empty()) bad("Git does not know your name, so saves would not work.", "okay vc who \"Your Name\" you@example.com");
                for (const vc::Change& c : git.Status())
                    if (c.state == vc::State::Conflict) bad(c.path + " has a merge clash.", "okay vc resolve " + c.path + " --mine | --theirs | --as-is, then okay vc save \"message\".");
            }
        }
    }
    t += problems == 0 ? "Everything looks fine.\n" : std::to_string(problems) + (problems == 1 ? " problem.\n" : " problems.\n");
    io.Say(t);
    return problems == 0 ? 0 : 1;
}

} // namespace

int Main(const std::vector<std::string>& argsIn, const Io& io) {
    // --project FOLDER (or -C FOLDER) may come before the command, and only there: after the command every word
    // belongs to it (a save message, a file name, anything after --).
    std::size_t i = 0;
    std::string project;
    while (i < argsIn.size()) {
        const std::string& a = argsIn[i];
        if ((a == "--project" || a == "-C") && i + 1 < argsIn.size()) { project = argsIn[i + 1]; i += 2; continue; }
        if (a.rfind("--project=", 0) == 0) { project = a.substr(10); ++i; continue; }
        break;
    }
    const std::vector<std::string> args(argsIn.begin() + (std::ptrdiff_t)i, argsIn.end());
    if (args.empty()) { io.Say(HelpText("")); return 0; }
    const std::string cmd = args[0];
    const std::vector<std::string> rest(args.begin() + 1, args.end());
    if (cmd == "--version" || cmd == "version") { io.Say(std::string("okay ") + EngineVersion()); return 0; }
    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        const std::string what = JoinWords(rest);
        const std::string t = HelpText(what);
        if (t.empty()) { io.Warn("There is no command called \"" + what + "\".\n" + HelpText("")); return 2; }
        io.Say(t);
        return 0;
    }
    if (cmd == "new") return CmdNew(rest, project, io);
    if (cmd == "open") return CmdOpen(rest, project, io);
    if (cmd == "play") return CmdPlay(rest, project, io);
    if (cmd == "run") return CmdRun(rest, project, io);
    if (cmd == "build") return CmdBuild(rest, project, io);
    if (cmd == "test") return CmdTest(rest, project, io);
    if (cmd == "info") return CmdInfo(rest, project, io);
    if (cmd == "doctor") return CmdDoctor(rest, project, io);
    if (cmd == "vc") return detail::VcMain(rest, project, io);
    if (cmd == "engine") return engine::EngineMain(rest, io);
    io.Warn("There is no command called \"" + cmd + "\".\n" + HelpText(""));
    return 2;
}

} // namespace okaycli
