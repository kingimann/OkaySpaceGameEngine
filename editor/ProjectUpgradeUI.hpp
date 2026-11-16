#pragma once
// The editor's side of "upgrade project": the windows (the version question, the progress, the result, File > Project
// Upgrade...), and the few calls main.cpp makes into them. The decisions and the work are in ProjectUpgrade.hpp (no
// ImGui there); docs/project_upgrade.md describes what the person sees.
#include <functional>
#include <string>

namespace okay { namespace editor { class EditorState; } }

namespace oked {

/// What main.cpp gives this module (set once, before the first frame).
struct ProjectUpgradeHooks {
    std::function<void()> openUpdateDialog;       ///< Engine > Check for Updates...: the "Update OkaySpace" button
    std::function<void()> reloadProjectFiles;     ///< read project.okayproj and the build settings again from disk
};
extern ProjectUpgradeHooks g_upgradeHooks;

/// This editor's version: OKAY_ENGINE_VERSION, or OKAY_FAKE_VERSION in a development build (OKAY_DEV set), so a test project
/// can be stamped as one version and opened by "another".
std::string RunningVersion();
/// Read the command line (--keep-version, --open, --launcher), set up the gate and put it under EditorState::Load.
/// Call once at startup, before the first project is opened.
void ProjectUpgradeInit(int argc, char** argv);
/// True when a scene's project may be opened without asking right now (so the early look at its settings for the splash
/// is safe). Never writes anything.
bool ProjectMayOpenSilently(const std::string& scene);
/// Ask first when needed: `go` runs when the project may be opened (at once, or after the person's answer), `cancel` when not.
void RunAfterProjectCheck(okay::editor::EditorState& ed, const std::string& scene, std::function<void()> go, std::function<void()> cancel = {});
/// A project this session made: it is never asked about.
void NoteProjectCreated(const std::string& root);
/// The text of a new project's project.okayproj: `generated` plus the version stamp.
std::string StampedNewProjectText(const std::string& generated);
/// What a save of project.okayproj writes: `generated`, the stamp, and the lines of the file now on disk the editor does not know.
std::string ProjectFileTextForSave(const std::string& path, const std::string& generated);
/// After a scene was saved: stamp the project's project.okayproj with this editor's version.
void StampProjectAfterSave(const std::string& projectDir);
/// "v2.855.0" or, for a project the person chose to keep at an older version, "v2.850.0 (kept)".
std::string EditorVersionLabel(const std::string& projectDir);
/// True while a question or the upgrade is on screen (the project chooser waits).
bool ProjectGateBusy();
/// A hand-over from another editor instance said --keep-version (`version` is the version it was started as).
void NoteKeepVersionHandoff(const std::string& scene, const std::string& version);
/// True when this process was started with a --keep-version that it trusts for the project it was asked to open (a bare flag: only a kept copy trusts
/// it; a flag that names a version: only that version's editor), so its hand-off to a running editor may say so.
bool KeepVersionRequested();
/// True when this editor runs from a kept copy (<launcher dir>/Versions/<v>/Tools, okayver::IsKeptCopyDir). The launcher owns that folder, so a kept
/// copy never checks for updates, never shows the Engine Update window, never installs an update and never offers automatic updates: it says
/// "This is a kept copy of v2.850.1. Update OkaySpace from the launcher." (OpenKeptCopyNotice) with the Open launcher button.
bool IsKeptCopy();
/// The line a kept copy shows instead of an update ("" when this editor is not a kept copy).
std::string KeptCopyUpdateText();
/// Help > Check for Updates... and the version question's "Update OkaySpace", in a kept copy: opens the window with that one line.
void OpenKeptCopyNotice();
/// Every frame, after the other dialogs: advances the upgrade and draws the windows.
void ProjectUpgradeDraw(okay::editor::EditorState& ed);
/// True once an answer has asked the editor to close (Keep started another version's editor, or a restore wants a restart):
/// the main loop then ends without the unsaved-changes question, which was already asked before the project was left.
bool ProjectUpgradeExitRequested();
/// File > Project Upgrade...
void ProjectUpgradeMenuItem(okay::editor::EditorState& ed);
/// After the main loop: start the editor of another version, or this one again, that the person asked for (once this process is gone).
void ProjectUpgradeAtExit();

}  // namespace oked
