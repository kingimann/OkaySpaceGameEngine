#pragma once
// Which program the launcher starts for a join (feature map 17.15.2): the player with an exported
// game (also for a dedicated server), the editor of the right version with a project when the friend
// is playing their project in the editor, or a question when the game is not found. The version check
// says both versions in words. Pure: the launcher's MpLaunch gathers the library and the installed
// editor versions and acts on the Plan (tests/test_chat_invites.cpp).
// Versions are compared with okayver (shared/ProjectVersion.hpp), the same rules the editor
// uses for project stamps.
#include "ProjectVersion.hpp"
#include "okay/Net/JoinLink.hpp"

#include <cctype>
#include <string>
#include <vector>

namespace okay {
namespace joinplan {

struct Candidate {
    enum class Kind { Exported, Project } kind = Kind::Exported;
    std::string title;           ///< the game's title
    std::string path;            ///< the scene to start (exported) or the project folder (project)
    std::string gameVersion;     ///< the exported game's version ("" = unknown)
    std::string engineVersion;   ///< the project's engine_version stamp ("" = unknown)
};

enum class Action {
    RunPlayer,        ///< start the player with lib[index].path and --join
    OpenEditor,       ///< open lib[index] (a project) in the editor `editorVersion`, then join from Play mode
    NeedEditor,       ///< a project, but no editor of its line is installed (text names the version)
    VersionMismatch,  ///< the game's version differs from the host's (text says both)
    AskWhichGame,     ///< not found by title: ask which of your games it is
    Missing,          ///< you have no games at all to pick from
    Invalid,          ///< not a usable join link
};

struct Plan {
    Action action = Action::Invalid;
    int index = -1;                 ///< into the candidate list
    std::string editorVersion;      ///< for OpenEditor
    std::string text;               ///< what to tell the person ("" for RunPlayer / OpenEditor)
    std::string theirVersion;       ///< NeedEditor: the editor version wanted; VersionMismatch: the host's game version
    std::string yourVersion;        ///< VersionMismatch: the version you have
};

/// The button under a NeedEditor or VersionMismatch answer, which opens the Versions page ("" = no button).
inline std::string GetVersionLabel(const Plan& p) {
    if ((p.action != Action::NeedEditor && p.action != Action::VersionMismatch) || p.theirVersion.empty()) return {};
    return "Get version " + p.theirVersion;
}

namespace detail {
inline std::string Key(const std::string& t) {
    std::string o;
    for (unsigned char c : t) if (!std::isspace(c)) o += (char)std::tolower(c);
    return o;
}
}  // namespace detail

inline Plan PlanJoin(const JoinLink& l, const std::vector<Candidate>& lib, const std::vector<std::string>& installedEditors) {
    Plan p;
    if (!l.Valid()) { p.text = "That isn't a join link."; return p; }
    const std::string key = detail::Key(l.game);
    const std::string host = l.host.empty() ? std::string("The host") : l.host;
    int exported = -1, project = -1;
    for (int i = 0; i < (int)lib.size(); ++i) {
        if (detail::Key(lib[i].title) != key) continue;
        if (lib[i].kind == Candidate::Kind::Exported && exported < 0) exported = i;
        if (lib[i].kind == Candidate::Kind::Project && project < 0) project = i;
    }
    if (l.target == "project" && project >= 0) {
        const std::string want = !l.engine.empty() ? l.engine : lib[project].engineVersion;
        const std::string ed = okayver::ResolveKeepTarget(want, installedEditors);
        if (ed.empty()) {
            p.action = Action::NeedEditor; p.index = project; p.theirVersion = okayver::NormalizeVersion(want);
            p.text = host + " is playing " + l.game + " in editor " + okayver::LineLabel(want) +
                     ", which is not installed. Install it from the Versions page to join.";
            return p;
        }
        p.action = Action::OpenEditor; p.index = project; p.editorVersion = ed;
        return p;
    }
    if (exported >= 0) {
        const Candidate& c = lib[exported];
        if (!l.version.empty() && !c.gameVersion.empty() && okayver::CompareVersions(l.version, c.gameVersion) != 0) {
            p.action = Action::VersionMismatch; p.index = exported; p.theirVersion = l.version; p.yourVersion = c.gameVersion;
            p.text = host + " is playing " + c.title + " " + l.version + "; you have " + c.gameVersion +
                     ". Update the game (or ask " + host + " to) so you both have the same version.";
            return p;
        }
        p.action = Action::RunPlayer; p.index = exported;
        return p;
    }
    if (lib.empty()) { p.action = Action::Missing; p.text = "You don't have " + l.game + ". Get the game first, then join."; return p; }
    p.action = Action::AskWhichGame;
    p.text = "Which of your games is " + l.game + "?";
    return p;
}

}  // namespace joinplan
}  // namespace okay
