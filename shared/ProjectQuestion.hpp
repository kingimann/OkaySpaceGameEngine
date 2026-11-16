#pragma once
// The words of the project-version question, in one place. The editor asks it in a window when a project of another version
// is opened (editor/ProjectUpgradeUI.cpp, DrawGatePrompt); the launcher asks the same thing BEFORE it starts the editor
// (launcher/OpenProjectGate.hpp). Both build their sentences and button labels here, so the two places read the same and a
// change of wording is made once. Header-only, no ImGui, SDL or engine code.
//
// `subject` is who the sentence is about: "This project" in the editor, the project's name in the launcher.
// Versions are passed as text ("2.850.0", "v2.850", "2.850.1"); they are normalized here, and printed as "v2.850.0".
//
// The sentences are the editor's own, moved here unchanged (tests/test_open_project_gate.cpp keeps the exact text, so an
// accidental edit shows up).
#include <string>

#include "ProjectVersion.hpp"

namespace okayver::question {

/// The small heading above the question.
inline const char* Eyebrow() { return "PROJECT VERSION"; }

/// "v2.850.0" for a version, "" for text that is not one.
inline std::string Label(const std::string& version) {
    const std::string n = NormalizeVersion(version);
    return n.empty() ? std::string() : "v" + n;
}

// ---- the headings --------------------------------------------------------------------------------------------------------

/// "This project was made with v2.850.0. This editor is v2.875.0."
inline std::string OlderHeading(const std::string& subject, const std::string& projectVersion, const std::string& runningVersion) {
    return subject + " was made with " + Label(projectVersion) + ". This editor is " + Label(runningVersion) + ".";
}
/// "This project has no version recorded (made before this feature). This editor is v2.875.0."
inline std::string UnknownHeading(const std::string& subject, const std::string& runningVersion) {
    return subject + " has no version recorded (made before this feature). This editor is " + Label(runningVersion) + ".";
}
/// "This project was last saved by v2.880.0, which is newer than this editor (v2.875.0)."
inline std::string NewerHeading(const std::string& subject, const std::string& projectVersion, const std::string& runningVersion) {
    return subject + " was last saved by " + Label(projectVersion) + ", which is newer than this editor (" + Label(runningVersion) + ").";
}

// ---- the explanations ----------------------------------------------------------------------------------------------------

inline std::string UpgradeParagraph(const std::string& runningVersion) {
    return "Upgrading brings the project's scenes and prefabs up to date for " + Label(runningVersion) +
           ". A complete backup copy of the project is made first, next to it, so you can always go back.";
}
/// `action` is the sentence's start: "Keeping v2.850.x" (the editor's button) or "Opening it with v2.850.x" (the launcher's).
inline std::string KeepParagraph(const std::string& action) {
    return action + " does not touch the project at all: it opens with that version's editor instead.";
}
inline std::string UnknownParagraph(const std::string& runningVersion) {
    return "There is no way to tell which version made it, so only upgrading is offered. A complete backup copy of the project is made first, "
           "next to it, then its scenes and prefabs are brought up to date for " + Label(runningVersion) + " and the project is marked with that version.";
}
inline std::string NewerParagraph() {
    return "This editor may drop data it does not understand when it saves. Update OkaySpace to open it safely, or open it anyway: "
           "a complete backup copy is made first, next to the project, and nothing in the project is changed by that.";
}

// ---- is the project's version installed? ---------------------------------------------------------------------------------

/// "v2.850.1 is installed and would open it."  (`installedVersion`: the newest installed editor of the project's line)
inline std::string InstalledLine(const std::string& installedVersion) { return Label(installedVersion) + " is installed and would open it."; }
/// "v2.850.x isn't installed on this computer."
inline std::string NotInstalledLine(const std::string& projectVersion) { return LineLabel(projectVersion) + " isn't installed on this computer."; }

// ---- the buttons ---------------------------------------------------------------------------------------------------------

inline std::string UpgradeLabel(const std::string& runningVersion) { return "Upgrade project to " + Label(runningVersion); }
inline std::string KeepLabel(const std::string& projectVersion) { return "Keep " + LineLabel(projectVersion); }   // the editor's button
inline const char* OpenAnywayLabel() { return "Open anyway (a backup is made first)"; }
inline const char* UpdateLabel() { return "Update OkaySpace"; }                                                    // the editor's button
inline const char* CancelLabel() { return "Cancel"; }

}  // namespace okayver::question
