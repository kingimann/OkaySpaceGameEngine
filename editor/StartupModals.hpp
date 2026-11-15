#pragma once
// Which of the editor's startup questions may be on screen this frame, and what an answered recovery copy takes with it.
// Pure: no ImGui, no SDL, no disk (tests/test_startup_modals.cpp steps it through frames).
//
// Why it exists (issue #888): Dear ImGui keeps one modal per popup level. Two questions that each call OpenPopup every
// frame at level 0 close each other: each is "appearing" again every frame, so neither is ever drawn, and the invisible
// modal still blocks all input. That froze the editor when a project with no engine_version was opened with --open while a
// recovery copy from a crash was waiting: the "Project version" question and "Restore recovered work?" fought, and the
// scene never opened. Now every startup question asks only when it is its turn, one at a time, in this order:
//
//   1. the project-version question (and its progress and result)  - it decides whether the scene may open at all
//   2. "Save changes?"                                               - the person asked for something; answer that first
//   3. the "Open a project" chooser                                  - the editor works inside a project
//   4. an old "<scene>.autosave" next to the scene just opened ("Recover Autosave")
//   5. "Restore recovered work?" (the editor's own recovery copies) - last: it restores into the open project
//
// Written for this engine from the bug report; no outside code.
#include <cstddef>
#include <string>
#include <vector>

namespace okay::editor::startup {

enum class Modal { None, ProjectVersion, SaveChanges, ProjectChooser, SceneAutosave, Recovery };

/// What is due this frame (each flag says the question wants to be on screen).
struct Pending {
    bool projectVersion = false;   ///< ProjectGateBusy(): the version question, its progress or its result
    bool saveChanges = false;      ///< "Save changes?" is open
    bool projectChooser = false;   ///< no project is open and the chooser would show
    bool recovery = false;         ///< a recovery copy waits for "Restore recovered work?"
    bool sceneAutosave = false;    ///< a newer "<scene>.autosave" waits for "Recover Autosave"
};

/// The one question whose turn it is (None when nothing is due).
inline Modal Turn(const Pending& p) {
    if (p.projectVersion) return Modal::ProjectVersion;
    if (p.saveChanges) return Modal::SaveChanges;
    if (p.projectChooser) return Modal::ProjectChooser;
    if (p.sceneAutosave) return Modal::SceneAutosave;
    if (p.recovery) return Modal::Recovery;
    return Modal::None;
}

/// May question `m` open (or keep) its modal this frame? True for at most one question.
inline bool MayAsk(Modal m, const Pending& p) { return m != Modal::None && Turn(p) == m; }

/// After the person answered recovery copy `answered` (Restore or Discard) of the list (`sessions[i]`: copy i's session):
/// the indices of the copies to delete. All copies of that crashed session go: the others are older saves of the same work,
/// and the question would otherwise come back for each. When the answered copy could not be read, only it goes, so the
/// next older copy of that session is offered instead. Copies of other crashed sessions stay (they are asked about next).
inline std::vector<std::size_t> CopiesToDiscard(const std::vector<std::string>& sessions, std::size_t answered, bool answeredReadable) {
    std::vector<std::size_t> out;
    if (answered >= sessions.size()) return out;
    if (!answeredReadable) { out.push_back(answered); return out; }
    for (std::size_t i = 0; i < sessions.size(); ++i)
        if (sessions[i] == sessions[answered]) out.push_back(i);
    return out;
}

/// Where the editor keeps its recovery copies.
struct RecoveryFolders {
    std::string folder;   ///< absolute: <user data folder>/okay_recovery
    std::string legacy;   ///< the old place, "<start folder>/okay_recovery", read once for copies left there; "" when it is `folder`
};

namespace detail {
inline std::string Slashes(std::string s) {
    for (char& c : s) if (c == '\\') c = '/';
    while (s.size() > 1 && s.back() == '/' && !(s.size() == 3 && s[1] == ':')) s.pop_back();
    return s;
}
inline bool Absolute(const std::string& s) {
    if (s.empty()) return false;
    if (s[0] == '/') return true;
    return s.size() >= 3 && s[1] == ':' && s[2] == '/' &&
           ((s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z'));
}
inline std::string Join(const std::string& a, const char* b) { return a.empty() || a.back() == '/' ? a + b : a + "/" + b; }
inline bool SamePath(std::string a, std::string b) {
    if (a.size() != b.size()) return false;
#if defined(_WIN32)
    for (std::size_t i = 0; i < a.size(); ++i) {   // Windows paths ignore letter case
        if (a[i] >= 'A' && a[i] <= 'Z') a[i] = (char)(a[i] - 'A' + 'a');
        if (b[i] >= 'A' && b[i] <= 'Z') b[i] = (char)(b[i] - 'A' + 'a');
    }
#endif
    return a == b;
}
}  // namespace detail

/// `userDataDir`: okay::Paths::UserDataDir() (%APPDATA%/OkaySpace, ~/.local/share/OkaySpace). `startDir`: the absolute working
/// folder the editor started in (where the old relative "okay_recovery" was). The copies used to live in the working folder,
/// so an editor started from another folder (a shortcut, the launcher, a terminal) never saw them, and a crash copy could be
/// offered in the wrong place. A user data folder that is not absolute (no HOME / APPDATA) falls back to the start folder.
/// Paths come back with '/' separators.
inline RecoveryFolders PlanRecoveryFolders(const std::string& userDataDir, const std::string& startDir) {
    const std::string user = detail::Slashes(userDataDir), start = detail::Slashes(startDir);
    RecoveryFolders r;
    r.legacy = detail::Join(start, "okay_recovery");
    r.folder = detail::Absolute(user) ? detail::Join(user, "okay_recovery") : r.legacy;
    if (detail::SamePath(r.folder, r.legacy)) r.legacy.clear();
    return r;
}

}  // namespace okay::editor::startup
