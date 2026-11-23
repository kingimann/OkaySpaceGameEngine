// okay: the engine's command line. See Cli.hpp. Our own code, written on the engine's own APIs (ProjectCreate,
// ProjectVersion, ProjectState, SceneSerializer, SceneMerge, SceneDiff, SceneMigration, CommandLine) and the git
// documentation for the merge and diff driver interfaces (gitattributes: "Defining a custom merge driver",
// "Defining an external diff driver").
#include "Cli.hpp"

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

#include "Git.hpp"
#include "Process.hpp"
#include "ProjectState.hpp"     // editor/: ProjectSettings and the text of project.okayproj
#include "ProjectCreate.hpp"    // shared/: name and place checks, the folders of a new project, templates
#include "ProjectVersion.hpp"   // shared/: the engine_version stamp and what a version difference means
#include "okay/Core/CommandLine.hpp"
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
        {"vc", "okay vc <init|status|save|history|undo|branch|switch|sync|remote|who> ...",
         "Version control (Git underneath).", ""},
        {"help", "okay help [command]", "Show help for a command.", ""},
    };
    return c;
}

const std::vector<CommandHelp>& VcCommands() {
    static const std::vector<CommandHelp> c = {
        {"init", "okay vc init", "Start version control in this project.",
         "Runs git init if needed, writes .gitignore (build output, caches, backups, per-person files) and\n"
         ".gitattributes (scenes merge with the engine's scene merge, pictures and sounds are binary), and\n"
         "tells Git to merge and compare .okayscene and .okayprefab files with the engine. Safe to run again."},
        {"status", "okay vc status", "What changed since the last save, grouped into scenes, scripts and assets.", ""},
        {"save", "okay vc save \"message\"", "Save a version of every change, with a message saying what you did.", ""},
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
        {"who", "okay vc who [\"Your Name\" email]", "Show or set the name and email your saves are signed with.", ""},
    };
    return c;
}

std::string Pad(std::string s, std::size_t n) { if (s.size() < n) s.resize(n, ' '); return s; }

} // namespace

std::string HelpText(const std::string& command) {
    if (command.empty()) {
        std::string t = std::string("okay ") + EngineVersion() + ": the OkaySpace engine from the command line.\n\nCommands:\n";
        for (const CommandHelp& c : Commands()) t += "  " + Pad(c.name, 8) + c.summary + "\n";
        t += "\nVersion control:\n";
        for (const CommandHelp& c : VcCommands()) t += "  vc " + Pad(c.name, 9) + c.summary + "\n";
        t += "\nEvery command takes --project FOLDER (default: the project this folder is in).\n"
             "okay help <command> tells you more.\n";
        return t;
    }
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

std::string ReadFile(const std::string& path, bool* ok = nullptr) {
    std::string t;
    const bool r = okayproj::ReadText(path, t);
    if (ok) *ok = r;
    return t;
}

std::string Lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

std::string JoinWords(const std::vector<std::string>& v, std::size_t from = 0) {
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

/// The project root to work on: --project, else the project this folder is in.
std::string ProjectOrSay(const std::string& given, const Io& io, bool quiet = false) {
    const std::string root = FindProjectRoot(given.empty() ? "." : given);
    if (root.empty() && !quiet)
        io.Warn((given.empty() ? std::string("This folder is not inside an OkaySpace project.") : "\"" + given + "\" is not an OkaySpace project.") +
                " Go to a project folder, or add --project FOLDER.");
    return root;
}

std::string RelTo(const std::string& path, const std::string& root) {
    std::error_code ec;
    const fs::path r = fs::relative(path, root, ec);
    return ec || r.empty() ? path : r.generic_string();
}

// ---- okay new / open / play / run / build / test -------------------------------------------------------------------

int CmdNew(const std::vector<std::string>& args, const std::string&, const Io& io) {
    okay::CommandLine cl;
    cl.Value("template", "NAME").Value("in", "FOLDER").Value("version", "V").Flag("no-git");
    if (!ParseArgs(cl, args, "new", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("new")); return 0; }
    const std::string tpl = cl.Get("template");
    if (Lower(tpl) == "list") {
        std::string t = "Templates (use the name in quotes with --template):\n";
        for (const auto& info : okayproj::Templates()) t += "  \"" + std::string(info.title) + "\"  " + info.blurb + "\n";
        io.Say(t);
        return 0;
    }
    const std::string name = JoinWords(cl.Positionals());
    if (name.empty()) { io.Warn("Say what the project is called: okay new MyGame"); return 2; }
    const okayproj::TemplateInfo* info = nullptr;
    if (!tpl.empty()) {
        for (const auto& t : okayproj::Templates()) if (Lower(t.title) == Lower(tpl)) info = &t;
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
            const int rc = Main({"vc", "init", "--project", root}, io);
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
    const std::string target = Lower(cl.Get("target", here));
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

bool HasClashMarks(const std::string& text) {
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);)
        if (line.rfind("<<<<<<< ", 0) == 0 || line == "<<<<<<<" || line.rfind(">>>>>>> ", 0) == 0) return true;
    return false;
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
                const std::string want = vc::MergeDriverCommand(SelfPath());
                std::string have = d.out;
                while (!have.empty() && (have.back() == '\n' || have.back() == '\r')) have.pop_back();
                if (have.empty()) bad("Git does not merge scenes with the engine yet.", "run okay vc init.");
                else if (have != want) bad("Git's scene merge points at a different okay program (" + have + ").", "run okay vc init with this okay.");
                else good("Git merges and compares scenes with the engine");
                const std::string attrs = ReadFile((fs::path(git.TopLevel().empty() ? root : git.TopLevel()) / ".gitattributes").string()) +
                                          ReadFile((fs::path(root) / ".gitattributes").string());
                if (attrs.find("merge=okayscene") == std::string::npos) bad(".gitattributes does not send scenes to the engine's merge.", "run okay vc init.");
                const std::string user = git.Capture({"config", "user.name"}).out;
                if (user.empty()) bad("Git does not know your name, so saves would not work.", "okay vc who \"Your Name\" you@example.com");
                for (const vc::Change& c : git.Status())
                    if (c.state == vc::State::Conflict) bad(c.path + " has a merge clash.", "open it, keep what you want, then okay vc save \"message\".");
            }
        }
    }
    t += problems == 0 ? "Everything looks fine.\n" : std::to_string(problems) + (problems == 1 ? " problem.\n" : " problems.\n");
    io.Say(t);
    return problems == 0 ? 0 : 1;
}

// ---- okay vc ---------------------------------------------------------------------------------------------------

/// The git folder for a vc command: the project, else this folder (vc works in any folder, but says so).
std::string VcFolder(const std::string& project) {
    const std::string root = FindProjectRoot(project.empty() ? "." : project);
    if (!root.empty()) return root;
    std::error_code ec;
    return fs::absolute(project.empty() ? "." : project, ec).string();
}

bool NeedGit(const Io& io) {
    if (vc::Git::Available()) return true;
    io.Warn("Git is not installed (or not on PATH). Install it from git-scm.com, then try again.");
    return false;
}

bool NeedRepo(const vc::Git& git, const Io& io) {
    if (!NeedGit(io)) return false;
    if (git.IsRepo()) return true;
    io.Warn("Version control is not started in this project yet. Start it with: okay vc init");
    return false;
}

std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\n')) ++i;
    return s.substr(i);
}

/// git's own words when it refused, kept short (first lines of stderr).
std::string GitSaid(const RunResult& r) {
    std::string e = Trim(r.err.empty() ? r.out : r.err);
    if (e.size() > 600) e = e.substr(0, 600) + " ...";
    return e.empty() ? std::string() : "\nGit said: " + e;
}

bool IdentityProblem(const RunResult& r) {
    return r.err.find("Please tell me who you are") != std::string::npos || r.err.find("empty ident") != std::string::npos ||
           r.err.find("unable to auto-detect email") != std::string::npos;
}
const char* kWhoHint = "Git needs your name and email once, to sign your saves. Run: okay vc who \"Your Name\" you@example.com";

int VcInit(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    if (!ParseArgs(cl, args, "vc init", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("vc init")); return 0; }
    if (!NeedGit(io)) return 1;
    const std::string folder = VcFolder(project);
    if (FindProjectRoot(folder).empty()) io.Say("Note: this folder does not look like an OkaySpace project; starting version control anyway.");
    vc::Git git(folder);
    const bool existed = git.IsRepo();
    if (!existed) {
        const RunResult r = git.Capture({"init", "-q"});
        if (!r.started || r.exitCode != 0) { io.Warn("Couldn't start version control here." + GitSaid(r)); return 1; }
        git.Capture({"symbolic-ref", "HEAD", "refs/heads/main"});   // the first version line is called main (works on every git version)
    }
    int addedIgnore = 0, addedAttr = 0;
    std::string err;
    const fs::path ignore = fs::path(folder) / ".gitignore", attrs = fs::path(folder) / ".gitattributes";
    const std::string oldIgnore = ReadFile(ignore.string()), oldAttrs = ReadFile(attrs.string());
    const std::string newIgnore = vc::GitIgnoreText(oldIgnore, &addedIgnore), newAttrs = vc::GitAttributesText(oldAttrs, &addedAttr);
    if (addedIgnore && !okayproj::WriteTextAtomic(ignore.string(), newIgnore, &err)) { io.Warn("Couldn't write .gitignore: " + err); return 1; }
    if (addedAttr && !okayproj::WriteTextAtomic(attrs.string(), newAttrs, &err)) { io.Warn("Couldn't write .gitattributes: " + err); return 1; }
    // Tell git to merge and compare scenes and prefabs with this program (the engine's SceneMerge and SceneDiff).
    const std::string self = SelfPath();
    if (self.empty()) { io.Warn("Couldn't find this program's own path, so Git can't be told to merge scenes with it."); return 1; }
    const std::vector<std::vector<std::string>> config = {
        {"config", "merge.okayscene.name", "OkaySpace scene merge"},
        {"config", "merge.okayscene.driver", vc::MergeDriverCommand(self)},
        {"config", "diff.okayscene.command", vc::DiffDriverCommand(self)},
    };
    for (const auto& c : config) {
        const RunResult r = git.Capture(c);
        if (!r.started || r.exitCode != 0) { io.Warn("Couldn't set up Git's scene merge." + GitSaid(r)); return 1; }
    }
    std::string t = existed ? "Version control was already on here; it is set up for OkaySpace now.\n" : "Started version control in " + folder + ".\n";
    t += addedIgnore ? "  .gitignore: " + std::to_string(addedIgnore) + " line(s) added (build output, caches, backups, your own editor files).\n"
                     : "  .gitignore already had everything.\n";
    t += addedAttr ? "  .gitattributes: " + std::to_string(addedAttr) + " line(s) added (scenes merge with the engine; pictures and sounds are binary).\n"
                   : "  .gitattributes already had everything.\n";
    t += "  Scenes and prefabs now merge and compare with the engine's own scene merge.\n";
    if (!git.HasCommits()) t += "Make your first save with: okay vc save \"First version\"\n";
    io.Say(t);
    return 0;
}

int VcStatus(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    if (!ParseArgs(cl, args, "vc status", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("vc status")); return 0; }
    vc::Git git(VcFolder(project));
    if (!NeedRepo(git, io)) return 1;
    std::string t = vc::StatusText(git.Status(), git.Branch());
    if (!git.HasCommits()) t += "Nothing has been saved yet.\n";
    io.Say(t);
    return 0;
}

int VcWho(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    if (!ParseArgs(cl, args, "vc who", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("vc who")); return 0; }
    if (!NeedGit(io)) return 1;
    vc::Git git(VcFolder(project));
    if (cl.Positionals().empty()) {
        const std::string name = Trim(git.Capture({"config", "user.name"}).out), email = Trim(git.Capture({"config", "user.email"}).out);
        io.Say(name.empty() ? std::string(kWhoHint) : "Your saves are signed " + name + (email.empty() ? "" : " <" + email + ">") + ".");
        return 0;
    }
    if (cl.Positionals().size() != 2) { io.Warn("Give your name and your email: okay vc who \"Your Name\" you@example.com"); return 2; }
    for (const auto& kv : {std::make_pair("user.name", cl.Positional(0)), std::make_pair("user.email", cl.Positional(1))}) {
        const RunResult r = git.Capture({"config", "--global", kv.first, kv.second});
        if (!r.started || r.exitCode != 0) { io.Warn("Couldn't store that." + GitSaid(r)); return 1; }
    }
    io.Say("Your saves are signed " + cl.Positional(0) + " <" + cl.Positional(1) + "> from now on (every project on this computer).");
    return 0;
}

int VcSave(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    cl.Value("message", "TEXT").Short('m');
    if (!ParseArgs(cl, args, "vc save", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("vc save")); return 0; }
    const std::string folder = VcFolder(project);
    vc::Git git(folder);
    if (!NeedRepo(git, io)) return 1;
    std::string message = cl.Has("message") ? cl.Get("message") : JoinWords(cl.Positionals());
    if (Trim(message).empty()) { io.Warn("Say what you changed: okay vc save \"Added the second level\""); return 2; }
    const std::vector<vc::Change> before = git.Status();
    const std::string top = git.TopLevel();
    for (const vc::Change& c : before) {   // never save a file that still has text-merge clash marks in it
        if (c.state != vc::State::Conflict) continue;
        const std::string text = ReadFile((fs::path(top) / c.path).string());
        if (HasClashMarks(text)) {
            io.Warn(c.path + " still has clash marks (<<<<<<< and >>>>>>>) in it. Open it, keep what you want, remove the marks, then save again.");
            return 1;
        }
    }
    const bool merging = git.Capture({"rev-parse", "-q", "--verify", "MERGE_HEAD"}).exitCode == 0;
    if (before.empty() && !merging) { io.Say("Nothing has changed since the last save, so there is nothing to save."); return 0; }
    RunResult r = git.Capture({"add", "-A", "--", "."});
    if (!r.started || r.exitCode != 0) { io.Warn("Couldn't gather the changes." + GitSaid(r)); return 1; }
    r = git.Capture({"commit", "-q", "-m", message});
    if (!r.started || r.exitCode != 0) {
        if (IdentityProblem(r)) io.Warn(kWhoHint);
        else io.Warn("The save did not work." + GitSaid(r));
        return 1;
    }
    const std::string id = Trim(git.Capture({"rev-parse", "--short", "HEAD"}).out);
    io.Say("Saved " + std::to_string(before.size()) + (before.size() == 1 ? " change" : " changes") + " as " + id + ": " + message);
    return 0;
}

int VcHistory(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    cl.Value("count", "N", "how many", "20").Short('n');
    if (!ParseArgs(cl, args, "vc history", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("vc history")); return 0; }
    vc::Git git(VcFolder(project));
    if (!NeedRepo(git, io)) return 1;
    if (!git.HasCommits()) { io.Say("Nothing has been saved yet."); return 0; }
    int n = 20;
    if (!cl.TryGetInt("count", 1, 100000, n)) { io.Warn("-n needs a number from 1 up."); return 2; }
    std::vector<std::string> a = {"log", "-n", std::to_string(n), "--date=format:%Y-%m-%d %H:%M", "--format=%h%x1f%ad%x1f%an%x1f%s"};
    const std::string file = cl.Positional(0);
    std::error_code ec;
    if (!file.empty()) { a.push_back("--follow"); a.push_back("--"); a.push_back(fs::absolute(file, ec).string()); }
    const RunResult r = git.Capture(a);
    if (!r.started || r.exitCode != 0) { io.Warn("Couldn't read the history." + GitSaid(r)); return 1; }
    std::string t;
    int count = 0;
    std::istringstream in(r.out);
    for (std::string line; std::getline(in, line);) {
        std::vector<std::string> f;
        std::size_t at = 0;
        for (std::size_t s; (s = line.find('\x1f', at)) != std::string::npos; at = s + 1) f.push_back(line.substr(at, s - at));
        f.push_back(line.substr(at));
        if (f.size() < 4) continue;
        t += "  " + f[0] + "  " + f[1] + "  " + Pad(f[2], 16) + "  " + f[3] + "\n";
        ++count;
    }
    if (!count) { io.Say(file.empty() ? "Nothing has been saved yet." : file + " has never been saved."); return 0; }
    io.Say((file.empty() ? std::string("Saves, newest first:\n") : "Saves of " + file + ", newest first:\n") + t +
           "Put a file back with: okay vc undo <file> [--to ID]");
    return 0;
}

int VcUndo(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    cl.Value("to", "ID");
    if (!ParseArgs(cl, args, "vc undo", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("vc undo")); return 0; }
    const std::string folder = VcFolder(project);
    vc::Git git(folder);
    if (!NeedRepo(git, io)) return 1;
    const std::string file = cl.Positional(0);
    if (file.empty()) { io.Warn("Say which file to put back: okay vc undo Assets/Level.okayscene"); return 2; }
    if (!git.HasCommits()) { io.Warn("Nothing has been saved yet, so there is nothing to go back to."); return 1; }
    const std::string rev = cl.Get("to", "HEAD");
    std::error_code ec;
    const fs::path abs = fs::absolute(file, ec).lexically_normal();
    const std::string top = git.TopLevel();
    const std::string rel = RelTo(abs.string(), top);
    if (rel.rfind("..", 0) == 0) { io.Warn(file + " is not inside this project."); return 1; }
    const RunResult exists = git.Capture({"cat-file", "-e", rev + ":" + rel});
    if (exists.exitCode != 0) {
        io.Warn(rev == "HEAD" ? file + " was never saved, so there is no saved copy to go back to."
                              : file + " is not in save " + rev + " (or there is no save " + rev + "). See okay vc history " + file);
        return 1;
    }
    const RunResult same = git.Capture({"diff", "--quiet", rev, "--", abs.string()});
    if (same.exitCode == 0 && fs::exists(abs, ec)) {
        io.Say(file + (rev == "HEAD" ? " has no changes since the last save." : " is already the same as in save " + rev + "."));
        return 0;
    }
    std::string backup;
    if (fs::is_regular_file(abs, ec)) {   // keep the current copy first, so undo never loses work
        char stamp[32];
        const std::time_t now = std::time(nullptr);
        std::tm tmv{};
#if defined(_WIN32)
        localtime_s(&tmv, &now);
#else
        localtime_r(&now, &tmv);
#endif
        std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tmv);
        const fs::path dir = fs::path(folder) / ".okay" / "backups" / "undo";
        fs::create_directories(dir, ec);
        const fs::path dst = dir / (abs.filename().string() + "." + stamp);
        fs::copy_file(abs, dst, fs::copy_options::overwrite_existing, ec);
        if (ec) { io.Warn("Couldn't keep a copy of " + file + " first (" + ec.message() + "), so nothing was changed."); return 1; }
        backup = RelTo(dst.string(), folder);
    }
    const RunResult r = git.Capture({"checkout", rev, "--", abs.string()});
    if (!r.started || r.exitCode != 0) { io.Warn("Couldn't put " + file + " back." + GitSaid(r)); return 1; }
    const std::string id = Trim(git.Capture({"rev-parse", "--short", rev}).out);
    io.Say(file + " is back to how it was in save " + id + "." + (backup.empty() ? "" : " Your copy from before is in " + backup + "."));
    return 0;
}

std::vector<std::string> Branches(const vc::Git& git) {
    std::vector<std::string> v;
    std::istringstream in(git.Capture({"branch", "--format=%(refname:short)"}).out);
    for (std::string l; std::getline(in, l);) { l = Trim(l); if (!l.empty()) v.push_back(l); }
    return v;
}

int VcBranch(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    if (!ParseArgs(cl, args, "vc branch", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("vc branch")); return 0; }
    vc::Git git(VcFolder(project));
    if (!NeedRepo(git, io)) return 1;
    const std::string cur = git.Branch();
    const std::string name = cl.Positional(0);
    if (name.empty()) {
        std::vector<std::string> list = Branches(git);
        if (list.empty() && !cur.empty()) list.push_back(cur);
        std::string t = "Version lines:\n";
        for (const std::string& b : list) t += (b == cur ? "  * " : "    ") + b + (b == cur ? "  (you are here)" : "") + "\n";
        io.Say(t + "Start a new one with: okay vc branch <name>");
        return 0;
    }
    if (!git.HasCommits()) { io.Warn("Save once first (okay vc save \"First version\"); a new version line starts from a save."); return 1; }
    if (git.Capture({"check-ref-format", "--branch", name}).exitCode != 0) { io.Warn("\"" + name + "\" can't be a version line name. Use letters, numbers and dashes."); return 2; }
    const std::vector<std::string> list = Branches(git);
    if (std::find(list.begin(), list.end(), name) != list.end()) { io.Warn("There is already a version line called " + name + ". Move to it with: okay vc switch " + name); return 1; }
    const RunResult r = git.Capture({"checkout", "-q", "-b", name});
    if (!r.started || r.exitCode != 0) { io.Warn("Couldn't start the version line." + GitSaid(r)); return 1; }
    io.Say("Started the version line " + name + " from " + (cur.empty() ? std::string("here") : cur) + ", and you are on it now. Unsaved changes came with you.\n"
           "Go back with: okay vc switch " + (cur.empty() ? std::string("main") : cur));
    return 0;
}

int VcSwitch(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    cl.Flag("carry");
    if (!ParseArgs(cl, args, "vc switch", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("vc switch")); return 0; }
    vc::Git git(VcFolder(project));
    if (!NeedRepo(git, io)) return 1;
    const std::string name = cl.Positional(0);
    if (name.empty()) { io.Warn("Say which version line to move to. See them with: okay vc branch"); return 2; }
    const std::vector<std::string> list = Branches(git);
    if (std::find(list.begin(), list.end(), name) == list.end()) {
        std::string t = "There is no version line called " + name + ". There are:";
        for (const std::string& b : list) t += " " + b;
        io.Warn(t);
        return 1;
    }
    if (name == git.Branch()) { io.Say("You are already on " + name + "."); return 0; }
    const std::vector<vc::Change> changes = git.Status();
    if (!changes.empty() && !cl.Has("carry")) {
        io.Warn(std::to_string(changes.size()) + (changes.size() == 1 ? " change is" : " changes are") + " not saved yet. Save first (okay vc save \"message\"),\n"
                "or run okay vc switch " + name + " --carry to take the changes with you.");
        return 1;
    }
    const RunResult r = git.Capture({"checkout", "-q", name});
    if (!r.started || r.exitCode != 0) {
        io.Warn("Couldn't move to " + name + ". Your unsaved changes clash with files that differ there; save them first." + GitSaid(r));
        return 1;
    }
    io.Say("You are on " + name + " now. If the editor is open, open the scene again to see this version.");
    return 0;
}

int VcRemote(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    if (!ParseArgs(cl, args, "vc remote", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("vc remote")); return 0; }
    vc::Git git(VcFolder(project));
    if (!NeedRepo(git, io)) return 1;
    const std::string url = cl.Positional(0), have = git.Remote();
    if (url.empty()) {
        io.Say(have.empty() ? "This project has no online copy yet. Set one with: okay vc remote <url or folder>" : "The online copy is " + have + ".");
        return 0;
    }
    const RunResult r = git.Capture(have.empty() ? std::vector<std::string>{"remote", "add", "origin", url}
                                                 : std::vector<std::string>{"remote", "set-url", "origin", url});
    if (!r.started || r.exitCode != 0) { io.Warn("Couldn't set the online copy." + GitSaid(r)); return 1; }
    io.Say("The online copy is " + url + " now. Send your saves with: okay vc sync");
    return 0;
}

int VcSync(const std::vector<std::string>& args, const std::string& project, const Io& io) {
    okay::CommandLine cl;
    if (!ParseArgs(cl, args, "vc sync", io)) return 2;
    if (cl.Has("help")) { io.Say(HelpText("vc sync")); return 0; }
    vc::Git git(VcFolder(project));
    if (!NeedRepo(git, io)) return 1;
    if (git.Remote().empty()) { io.Warn("This project has no online copy yet. Set one with: okay vc remote <url or folder>"); return 1; }
    const std::string branch = git.Branch();
    if (branch.empty()) { io.Warn("You are not on a version line, so there is nothing to sync. Move to one with okay vc switch main."); return 1; }
    const std::vector<vc::Change> changes = git.Status();
    if (!changes.empty()) { io.Warn("Save your changes first (okay vc save \"message\"), then sync."); return 1; }
    io.Say("Getting new saves from the online copy...");
    RunResult r = git.Attached({"fetch", "-q", "origin"});
    if (!r.started || r.exitCode != 0) { io.Warn("Couldn't reach the online copy. Check the address (okay vc remote) and your connection."); return 1; }
    int got = 0;
    if (git.Capture({"rev-parse", "-q", "--verify", "refs/remotes/origin/" + branch}).exitCode == 0) {
        const std::string count = Trim(git.Capture({"rev-list", "--count", "HEAD..refs/remotes/origin/" + branch}).out);
        got = std::atoi(count.c_str());
        if (!git.HasCommits()) got = std::max(got, 1);
        if (got > 0) {
            r = git.Capture({"merge", "--no-edit", "-q", "refs/remotes/origin/" + branch});
            if (!r.started || r.exitCode != 0) {
                std::string t;
                for (const vc::Change& c : git.Status()) if (c.state == vc::State::Conflict) t += "\n  " + c.path;
                if (t.empty()) {
                    io.Warn(IdentityProblem(r) ? std::string(kWhoHint) : "Couldn't take in the new saves." + GitSaid(r));
                    return 1;
                }
                io.Warn("Someone else changed the same things as you. These files clash:" + t + "\n"
                        "Scenes kept your side of each clash and the lines above list them. Open each file, keep what you want,\n"
                        "then run okay vc save \"Merged\" and okay vc sync again.");
                return 1;
            }
        }
    }
    if (git.HasCommits()) {
        io.Say("Sending your saves...");
        r = git.Attached({"push", "-q", "-u", "origin", branch});
        if (!r.started || r.exitCode != 0) { io.Warn("Couldn't send your saves to the online copy. Check that you may write to it."); return 1; }
    }
    io.Say(got > 0 ? "Synced: took in " + std::to_string(got) + " new save(s) and sent yours." : "Synced: the online copy and this project match.");
    return 0;
}

// ---- the merge and diff drivers git calls ---------------------------------------------------------------------

std::string Upgraded(const std::string& text) {
    const okay::SceneMigration::Result m = okay::SceneMigration::MigrateToCurrent(text);
    return m.ok && m.complete ? m.text : text;
}

std::string Shown(const std::string& v) {
    if (v.empty()) return "(empty)";
    return v.size() > 40 ? v.substr(0, 37) + "..." : v;
}

/// One clash in plain words.
std::string ClashSentence(const okay::SceneMerge::Conflict& c) {
    using K = okay::SceneMerge::Conflict::Kind;
    const std::string who = !c.path.empty() ? c.path : !c.name.empty() ? c.name : std::string("Scene settings");
    const std::string part = c.record.empty() ? std::string("a part") : "the " + c.record + " part";
    std::string s;
    switch (c.kind) {
        case K::FieldChanged:
            s = who + ": " + (c.record.empty() ? std::string() : c.record + " ") + (c.field.empty() ? "value" : c.field) + " was " + Shown(c.base) +
                "; you made it " + Shown(c.ours) + " and they made it " + Shown(c.theirs) + ".";
            break;
        case K::RecordChanged: s = who + ": " + part + " was changed differently on both sides."; break;
        case K::RecordDeletedAndEdited: s = who + ": one side removed " + part + " and the other changed it."; break;
        case K::RecordAddedDifferently: s = who + ": both sides added " + part + ", in different ways."; break;
        case K::ObjectDeletedAndEdited: s = who + ": one side deleted this object and the other changed it."; break;
        case K::RenamedDifferently: s = who + ": you renamed it " + Shown(c.ours) + " and they renamed it " + Shown(c.theirs) + "."; break;
        case K::ParentChanged: s = who + ": you and they moved it under different parents."; break;
        case K::ParentDeleted: s = who + ": it was put under an object that the other side deleted."; break;
        case K::ParentCycle: s = who + ": the two moves together would put it inside itself."; break;
        case K::OrderChanged: s = who + ": you and they moved it to different places in the list."; break;
        case K::Invalid: return "The merged scene would not load, so your version was kept whole.";
    }
    if (!c.resolution.empty()) s += " Kept: " + (c.resolution == "ours" ? std::string("yours") : c.resolution) + ".";
    return s;
}

} // namespace

int SceneMergeDriver(const std::string& basePath, const std::string& oursPath, const std::string& theirsPath,
                     const std::string& shownName, const Io& io) {
    bool okB = false, okO = false, okT = false;
    const std::string base = ReadFile(basePath, &okB), ours = ReadFile(oursPath, &okO), theirs = ReadFile(theirsPath, &okT);
    const std::string name = shownName.empty() ? fs::path(oursPath).filename().string() : shownName;
    if (!okO || !okT) { io.Warn(name + ": couldn't read the two versions to merge."); return 2; }
    okay::SceneMerge::Options opt;
    opt.policy = okay::SceneMerge::Policy::Mark;
    opt.validator = [](const std::string& text, std::string* error) {   // the merged text must load into a real scene
        okay::Scene probe("probe");
        return okay::SceneSerializer::Deserialize(probe, text, error);
    };
    const okay::SceneMerge::Result r = okay::SceneMerge::Merge(Upgraded(base), Upgraded(ours), Upgraded(theirs), opt);
    if (!r.ok) {
        // Not a scene the engine can read (or damaged): let git merge it as plain text, with its usual clash marks.
        io.Warn(name + ": not a scene the engine can read (" + r.error + "), so it was merged as plain text.");
        const RunResult t = Run({vc::Git::Program(), "merge-file", "-L", "yours", "-L", "base", "-L", "theirs", oursPath, basePath, theirsPath}, "", true);
        if (!t.started) return 2;
        return t.exitCode == 0 ? 0 : 1;
    }
    std::string err;
    if (!okayproj::WriteTextAtomic(oursPath, r.text, &err)) { io.Warn(name + ": couldn't write the merged scene: " + err); return 2; }
    if (r.Clean()) {
        io.Warn(name + ": merged both sets of changes with the engine's scene merge.");
        return 0;
    }
    std::string t = name + ": " + std::to_string(r.conflicts.size()) + (r.conflicts.size() == 1 ? " change clashes" : " changes clash") +
                    " (you and they changed the same thing). The file has your side of each one and still loads:";
    for (const auto& c : r.conflicts) t += "\n  - " + ClashSentence(c);
    t += "\nOpen it in the editor, fix what you want, then save with: okay vc save \"Merged\"";
    io.Warn(t);
    return 1;
}

int SceneDiffDriver(const std::vector<std::string>& args, const Io& io) {
    std::string path, oldFile, newFile;
    if (args.size() >= 7) { path = args[0]; oldFile = args[1]; newFile = args[4]; }
    else if (args.size() == 2) { oldFile = args[0]; newFile = args[1]; path = newFile; }
    else { io.Warn("Usage: okay vc diff-driver <path> <old-file> <old-hex> <old-mode> <new-file> <new-hex> <new-mode>"); return 2; }
    auto load = [](const std::string& f) {
        if (f == "/dev/null" || f == "nul" || f == "NUL") return std::string();
        return ReadFile(f);
    };
    const std::string a = load(oldFile), b = load(newFile);
    const okay::SceneDiff::Result d = okay::SceneDiff::Compute(a, b);
    if (!d.ok) {   // not a readable scene: show the plain text difference
        const RunResult r = Run({vc::Git::Program(), "diff", "--no-index", "--no-ext-diff", "--", oldFile, newFile}, "", true);
        io.Say("scene " + path + " (not readable as a scene: " + d.error + "; plain text difference)\n" + r.out);
        return 0;
    }
    if (d.Empty()) { io.Say("scene " + path + ": no change to the scene (only how the file is laid out)"); return 0; }
    io.Say("scene " + path + "\n" + d.ToText());
    return 0;
}

int Main(const std::vector<std::string>& argsIn, const Io& io) {
    // --project FOLDER (or -C FOLDER) works with every command; take it out first.
    std::vector<std::string> args;
    std::string project;
    for (std::size_t i = 0; i < argsIn.size(); ++i) {
        const std::string& a = argsIn[i];
        if ((a == "--project" || a == "-C") && i + 1 < argsIn.size()) { project = argsIn[++i]; continue; }
        if (a.rfind("--project=", 0) == 0) { project = a.substr(10); continue; }
        args.push_back(a);
    }
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
    if (cmd == "vc") {
        if (rest.empty()) { io.Say(HelpText("vc")); return 0; }
        const std::string sub = rest[0];
        const std::vector<std::string> more(rest.begin() + 1, rest.end());
        if (sub == "init") return VcInit(more, project, io);
        if (sub == "status") return VcStatus(more, project, io);
        if (sub == "save") return VcSave(more, project, io);
        if (sub == "history" || sub == "log") return VcHistory(more, project, io);
        if (sub == "undo") return VcUndo(more, project, io);
        if (sub == "branch") return VcBranch(more, project, io);
        if (sub == "switch") return VcSwitch(more, project, io);
        if (sub == "sync") return VcSync(more, project, io);
        if (sub == "remote") return VcRemote(more, project, io);
        if (sub == "who") return VcWho(more, project, io);
        // Hidden: what git itself calls (set up by okay vc init).
        if (sub == "merge-driver") {
            if (more.size() < 3) { io.Warn("Usage: okay vc merge-driver <base> <ours> <theirs> [path]"); return 2; }
            return SceneMergeDriver(more[0], more[1], more[2], more.size() > 3 ? more[3] : std::string(), io);
        }
        if (sub == "diff-driver") return SceneDiffDriver(more, io);
        io.Warn("There is no okay vc command called \"" + sub + "\".\n" + HelpText("vc"));
        return 2;
    }
    io.Warn("There is no command called \"" + cmd + "\".\n" + HelpText(""));
    return 2;
}

} // namespace okaycli
