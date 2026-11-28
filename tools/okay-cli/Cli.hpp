#pragma once
// okay: the engine's command line. One program, `okay` (okay.exe on Windows), that makes, opens, plays, runs, builds and
// tests projects, and keeps them in version control with the person's installed git underneath (okay vc ...).
// It wraps what the engine already has and adds no second copy:
//   okay new    shared/ProjectCreate.hpp (name and place checks, folders) + the editor's --new-project for templates
//   okay open   the editor's --open        okay play    the player        okay build   the editor's --build-game
//   okay run    scripts/okay_run.py and the okay_run tool (docs/process/okay_run.md)
//   okay test   every scene and prefab loads (SceneSerializer), or ctest for an engine build folder
//   okay engine build  the editor, launcher and player from an engine checkout (scripts/build-local.ps1's recipe)
//   okay vc     git, with SceneMerge / SceneDiff as git's merge and diff drivers for .okayscene and .okayprefab
// Programs are started only through Process.hpp; git only through Git.cpp. Messages are short plain sentences.
#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "CommandTable.hpp"

namespace okaycli {

/// Where the CLI's words go. Tests collect them; the program prints them; a window that runs okay inside itself (the
/// editor's Command Line panel, CommandJob.hpp) gets them one message at a time through `sink`.
struct Io {
    std::string* out = nullptr;   ///< null: stdout
    std::string* err = nullptr;   ///< null: stderr
    /// Set: every message goes here instead (`problem` true for Warn), with its newline. Called on the thread running the command.
    std::function<void(const std::string& text, bool problem)> sink;
    /// Set and true: the person pressed Stop. The next Say or Warn throws Cancelled, which ends the command (RunInProcess
    /// catches it); programs the command started are stopped by Process.hpp's AttachedOutput.
    const std::atomic<bool>* cancel = nullptr;
    void Say(const std::string& text) const;    ///< a normal message (a newline is added when missing)
    void Warn(const std::string& text) const;   ///< a problem (stderr)
};

/// Thrown by Io::Say / Io::Warn once Io::cancel is set: the command stops at its next message.
struct Cancelled {};

/// Run one command line (without the program name). Returns the exit code: 0 done, 1 it did not work, 2 bad arguments.
int Main(const std::vector<std::string>& args, const Io& io);

/// The help: the overview for "", else the help of one command ("vc", "vc save", "new" ...). "" when unknown.
std::string HelpText(const std::string& command);

/// The project folder that holds `start` (a folder or a file in it): the nearest folder upward with project.okayproj,
/// else the nearest with an Assets folder. "" when there is none.
std::string FindProjectRoot(const std::string& start);
/// The scene a project starts on: the first ticked scene of .okay/scene_build.txt, else the first .okayscene in Assets
/// (by name). "" when the project has no scene.
std::string MainScene(const std::string& projectRoot);

/// Make an empty project at `root` (folders, project.okayproj stamped with this engine version, one empty scene named
/// after the folder) without the editor. False with a plain reason; nothing is left behind on failure.
bool CreateEmptyProject(const std::string& root, const std::string& name, const std::string& version, std::string& error);

/// git's merge driver for scenes and prefabs: merge `theirsPath` and `basePath` into `oursPath` with the engine's scene
/// merge. 0 = merged cleanly (written to oursPath); 1 = a real clash (oursPath holds a loadable scene with our side
/// kept at each clash, and the clashes are reported in plain words); files that are not scenes are merged as text.
int SceneMergeDriver(const std::string& basePath, const std::string& oursPath, const std::string& theirsPath,
                     const std::string& shownName, const Io& io);
/// git's diff driver for scenes: `path old-file old-hex old-mode new-file new-hex new-mode` (or `old new`).
int SceneDiffDriver(const std::vector<std::string>& args, const Io& io);

/// CommandNames(), SubCommandNames() and the tables behind them: CommandTable.hpp.
/// The Tab-completion script for "bash" or "powershell" ("" for another shell).
std::string CompletionScript(const std::string& shell);

/// The engine version this program was built with.
const char* EngineVersion();

/// okay mcp and okay automation (tools/okay-cli/Automation.cpp: AutomationMain). They need the editor's state, which
/// only the okay program links alongside this library (the editor links its own), so Main reaches them through this
/// pointer; the okay program sets it in main(), and where it is unset (the editor's Command Line panel) they say so.
using AutomationFn = int (*)(const std::vector<std::string>& args, const std::string& project, const Io& io);
AutomationFn& AutomationEntry();
int AutomationMain(const std::vector<std::string>& args, const std::string& project, const Io& io);

} // namespace okaycli
