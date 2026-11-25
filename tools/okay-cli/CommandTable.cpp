// okay CLI: the command tables. See CommandTable.hpp. Moved out of Cli.cpp unchanged so the launcher's Command line page
// completes from the same lists the help prints (CLAUDE.md rule 19c: one list, not a copy). Our own code, std only.
#include "CommandTable.hpp"

namespace okaycli {

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
        {"build", "okay build [--target windows] [--scene S] [--out FOLDER] [--name GAME] [--zip] [--dry-run] [--dev] [--list-targets]",
         "Build the game so others can play it.",
         "Uses the editor's Build (the project's Build Settings) without opening a window. The game goes to\n"
         "<project>/build unless you say --out. Builds are for the computer you are on; Windows is the main target.\n"
         "--list-targets lists the platforms Build Settings knows and which ones okay build can make here."},
        {"export", "okay export [the same options as okay build]", "Build the game and package it as a .zip to hand out.",
         "The same as okay build --zip: the editor's build, then its zip step beside the game folder."},
        {"test", "okay test [--engine [BUILD_FOLDER]] [-R NAME]",
         "Check the project.",
         "Checks that every scene and prefab in the project loads and has no merge clash marks.\n"
         "--engine runs the engine's own tests (ctest) in a build folder instead; -R picks tests by name."},
        {"info", "okay info [--json]", "Show the engine version, the project and its version control.", ""},
        {"doctor", "okay doctor", "Check that everything a project needs is in place, and say how to fix what is not.", ""},
        {"project", "okay project <info|upgrade|settings> ...", "The project's settings and engine version.", ""},
        {"scene", "okay scene <list|new|check|diff|info> ...", "List, make, check and compare scenes.", ""},
        {"script", "okay script <run|check|new> ...", "Run, check and make OkayScript scripts.", ""},
        {"vs", "okay vs <list|check> ...", "List and check Visual Script graphs.", ""},
        {"asset", "okay asset <list|find|used-by|uses|import> ...", "Find assets, see what uses what, and bring files in.", ""},
        {"vc", "okay vc <init|clone|status|save|resolve|history|undo|branch|switch|sync|remote|who> ...",
         "Version control (Git underneath).", ""},
        {"engine", "okay engine <build|doctor> ...", "Build the engine's own editor, launcher and player from an engine checkout.",
         "okay engine build [editor|launcher|player|all] [--debug] [--jobs N] [--clean] [--run] [--dir BUILD]\n"
         "  builds OkayEngine.exe, OkaySpace.exe and OkaySpacePlayer.exe with CMake (as scripts/build-local.ps1 does).\n"
         "okay engine doctor  says what is missing (CMake, a compiler, Ninja, SDL2) and where to get it."},
        {"completion", "okay completion <bash|powershell>", "Print a script that completes okay's commands when you press Tab.",
         "bash:        okay completion bash > ~/.okay-completion.sh, then add  source ~/.okay-completion.sh  to ~/.bashrc\n"
         "PowerShell:  okay completion powershell | Out-String | Invoke-Expression  (add that line to your $PROFILE)"},
        {"help", "okay help [command]", "Show help for a command.", ""},
    };
    return c;
}

namespace {

const std::vector<CommandHelp>& ProjectCommands() {
    static const std::vector<CommandHelp> c = {
        {"info", "okay project info [--json]", "The project's name, folder, engine version, game version and scenes.", ""},
        {"upgrade", "okay project upgrade", "Upgrade an older project to this engine version (a backup is made first).",
         "The editor's own upgrade: the whole project is copied beside it first, every scene and prefab is checked,\n"
         "then rewritten; if anything fails the files are put back."},
        {"settings", "okay project settings [get <key> | set <key> <value>]", "Show, read or change a project setting.",
         "Without get or set it lists every setting (project.okayproj). Lines okay does not know are kept."},
    };
    return c;
}
const std::vector<CommandHelp>& SceneCommands() {
    static const std::vector<CommandHelp> c = {
        {"list", "okay scene list [--json]", "The project's scenes (the start scene is marked).", ""},
        {"new", "okay scene new <name>", "Make an empty scene in Assets/Scenes.", ""},
        {"check", "okay scene check [scene ...] [--json]", "Check scenes for problems (missing files, no camera, broken scripts ...).",
         "Without a scene every scene of the project is checked. Exit code 1 when a scene has an error."},
        {"diff", "okay scene diff <a> <b>", "What changed between two scenes, object by object.", ""},
        {"info", "okay scene info [scene] [--json]", "Objects, components and the files a scene uses.", ""},
    };
    return c;
}
const std::vector<CommandHelp>& ScriptCommands() {
    static const std::vector<CommandHelp> c = {
        {"run", "okay script run <file.okay> [--frames N] [args ...]", "Run a script: Start, then Main if it has one, then N updates.",
         "The script runs on one object in an empty scene. What it prints is shown here; `args` holds the words after the file."},
        {"check", "okay script check [file ...] [--json]", "Check scripts for errors, with line numbers (all the project's when none is named).", ""},
        {"new", "okay script new <name>", "Make Assets/Scripts/<name>.okay from the starter script.", ""},
    };
    return c;
}
const std::vector<CommandHelp>& VsGraphCommands() {
    static const std::vector<CommandHelp> c = {
        {"list", "okay vs list [--json]", "The project's Visual Script graphs (.okayvs).", ""},
        {"check", "okay vs check [graph ...] [--json]", "Check graphs as the editor's Problems button does.", ""},
    };
    return c;
}
const std::vector<CommandHelp>& AssetCommands() {
    static const std::vector<CommandHelp> c = {
        {"list", "okay asset list [--type T] [--json]", "Every asset, or only one type (texture, model, audio, scene, prefab ...).", ""},
        {"find", "okay asset find <name> [--json]", "Assets whose file name has these words in it.", ""},
        {"used-by", "okay asset used-by <file> [--json]", "Which assets use this file.", ""},
        {"uses", "okay asset uses <file> [--json]", "Which files this asset uses.", ""},
        {"import", "okay asset import <file> [--to FOLDER]", "Copy a file into Assets (under a free name) with its .okaymeta.",
         "The same import as the editor's Project panel; --to is a folder inside Assets."},
    };
    return c;
}
const std::vector<CommandHelp>& EngineCommands() {
    static const std::vector<CommandHelp> c = {
        {"build", "okay engine build [editor|launcher|player|all] ...", "Build OkayEngine.exe, OkaySpace.exe and OkaySpacePlayer.exe with CMake.", ""},
        {"doctor", "okay engine doctor", "What building the engine needs on this computer, and where to get what is missing.", ""},
    };
    return c;
}
const std::vector<CommandHelp>& CompletionCommands() {
    static const std::vector<CommandHelp> c = {
        {"bash", "okay completion bash", "The bash script.", ""},
        {"powershell", "okay completion powershell", "The PowerShell script.", ""},
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
        {"status", "okay vc status [--json]", "What changed since the last save, grouped into scenes, scripts and assets.", ""},
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

/// The sub-commands of a command ("" none).
const std::vector<CommandHelp>* SubCommands(const std::string& cmd) {
    if (cmd == "vc") return &VcCommands();
    if (cmd == "project") return &ProjectCommands();
    if (cmd == "scene") return &SceneCommands();
    if (cmd == "script") return &ScriptCommands();
    if (cmd == "vs") return &VsGraphCommands();
    if (cmd == "asset") return &AssetCommands();
    if (cmd == "engine") return &EngineCommands();
    if (cmd == "completion") return &CompletionCommands();
    return nullptr;
}

const std::vector<HelpGroup>& Groups() {
    static const std::vector<HelpGroup> g = {
        {"Projects", {"new", "open", "play", "run", "build", "export", "test", "info", "doctor", "project"}},
        {"Scenes, scripts and assets", {"scene", "script", "vs", "asset"}},
        {"Version control", {"vc"}},
        {"The engine itself", {"engine"}},
        {"This program", {"completion", "help"}},
    };
    return g;
}

const CommandHelp* FindCommand(const std::string& name) {
    for (const CommandHelp& c : Commands()) if (name == c.name) return &c;
    return nullptr;
}

std::vector<std::string> CommandNames() {
    std::vector<std::string> v;
    for (const CommandHelp& c : Commands()) v.push_back(c.name);
    return v;
}
std::vector<std::string> SubCommandNames(const std::string& command) {
    std::vector<std::string> v;
    if (const std::vector<CommandHelp>* subs = SubCommands(command))
        for (const CommandHelp& s : *subs) v.push_back(s.name);
    return v;
}

const char* CommandSummary(const std::string& command) {
    const std::size_t sp = command.find(' ');
    const CommandHelp* c = FindCommand(command.substr(0, sp));
    if (!c) return "";
    if (sp == std::string::npos) return c->summary;
    if (const std::vector<CommandHelp>* subs = SubCommands(command.substr(0, sp)))
        for (const CommandHelp& s : *subs) if (command.substr(sp + 1) == s.name) return s.summary;
    return "";
}

} // namespace okaycli
