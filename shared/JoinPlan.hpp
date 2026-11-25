#pragma once
// Which program the launcher starts for a join (feature map 17.15.2): the player with an exported
// game (also for a dedicated server), the editor of the right version with a project when the friend
// is playing their project in the editor, or a question when the game is not found. The version check
// says both versions in words. Pure: the launcher's MpLaunch gathers the library and the installed
// editor versions and acts on the Plan (tests/test_chat_invites.cpp).
// The editor half (feature map 17.15.7): the launcher opens the project with `--join <link>`; the editor
// waits for the sign-in gate and the project, enters Play mode, joins, and says in words why a join
// failed (EditorJoinProblem, NextEditorJoinStep, WatchEditorJoin).
// Versions are compared with okayver (shared/ProjectVersion.hpp), the same rules the editor
// uses for project stamps.
#include "ProjectVersion.hpp"
#include "okay/Net/JoinLink.hpp"

#include <cctype>
#include <cstddef>
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


// ---- The editor half: `--join <link>` ----------------------------------------------------------------------------

/// The join link from the editor's command line: "--join <link>" or "--join=<link>". "" when there is none or it is not
/// a usable join link (so a bad argument never starts Play mode).
inline std::string JoinArgFrom(const std::vector<std::string>& args) {
    std::string v;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--join" && i + 1 < args.size()) v = args[++i];
        else if (args[i].rfind("--join=", 0) == 0) v = args[i].substr(7);
    }
    JoinLink l;
    return !v.empty() && JoinLink::Parse(v, l) && l.Valid() ? v : std::string();
}
/// The editor's arguments with `--join <link>` added (the launcher's open-with arguments, then the join). An empty or
/// unusable link adds nothing.
inline std::vector<std::string> WithJoinArg(std::vector<std::string> args, const std::string& link) {
    if (JoinArgFrom({"--join", link}).empty()) return args;
    args.push_back("--join");
    args.push_back(link);
    return args;
}

/// Can the running editor (`runningVersion`, "" = a development build: no check) join this session? "" = yes; otherwise
/// the words for the console and the toast. The host's editor version must be of the same line, the rule PlanJoin uses.
inline std::string EditorJoinProblem(const JoinLink& l, const std::string& runningVersion) {
    if (!l.Valid()) return "That isn't a join link.";
    const std::string host = l.host.empty() ? std::string("The host") : l.host;
    if (!l.engine.empty() && !runningVersion.empty() && okayver::ResolveKeepTarget(l.engine, {runningVersion}).empty())
        return host + " is playing " + l.game + " in editor " + okayver::LineLabel(l.engine) + ", and this editor is " +
               okayver::NormalizeVersion(runningVersion) + ". Join from the launcher: it opens the project in the right editor version.";
    return {};
}

/// Where the editor is, for a join it was asked to make.
struct EditorJoinState {
    bool projectsAllowed = false;   ///< the sign-in gate is open (editor/SignInGate.hpp ProjectsAllowed)
    bool projectBusy = false;       ///< a project is still being opened: an --open, a hand-off, its version question or a save question
    bool projectOpen = false;       ///< a project is open
    bool projectIsTheOneAsked = true;   ///< it is the project the launcher asked for (true when none was named)
    bool playing = false;           ///< Play mode is on
    bool playAsked = false;         ///< this join already asked for Play mode
    bool playBusy = false;          ///< Play is waiting on the "Save before continuing?" question
};
enum class EditorJoinStep {
    Wait,        ///< not yet: the sign-in gate, the project or the save question comes first
    EnterPlay,   ///< press Play
    Join,        ///< in Play mode: join now (then WatchEditorJoin)
    Drop,        ///< give up (EditorJoinDropText says why)
};
inline EditorJoinStep NextEditorJoinStep(const EditorJoinState& s) {
    if (!s.projectsAllowed || s.projectBusy) return EditorJoinStep::Wait;   // no join before the sign-in gate lets a project open
    if (!s.projectOpen || !s.projectIsTheOneAsked) return EditorJoinStep::Drop;
    if (s.playing) return EditorJoinStep::Join;
    if (s.playBusy) return EditorJoinStep::Wait;
    return s.playAsked ? EditorJoinStep::Drop : EditorJoinStep::EnterPlay;
}
/// Why a join was dropped before it started, in words.
inline std::string EditorJoinDropText(const EditorJoinState& s, const std::string& game) {
    const std::string g = game.empty() ? std::string("the game") : game;
    if (!s.projectOpen || !s.projectIsTheOneAsked) return "The project for " + g + " did not open, so the join was stopped.";
    return "Play mode did not start, so the join was stopped. Press Play and join from the Multiplayer panel to try again.";
}

/// What the joining NetworkManager says, each frame after the join started.
struct JoinProgress {
    bool joined = false;
    bool rejected = false;          ///< NetworkManager::JoinRejected
    std::string rejectReason;       ///< NetworkManager::JoinRejectReason
    std::string lastError;          ///< NetworkManager::LastError (relay errors in words)
    std::string gateRefusal;        ///< NetworkManager::LastJoinRefusal (a session ticket was refused)
    bool stopped = false;           ///< the manager went offline without joining (or there is none)
    float seconds = 0.0f;           ///< since the join started
};
enum class JoinState { Waiting, Joined, Failed };
struct JoinOutcome { JoinState state = JoinState::Waiting; std::string text; };
/// The join's result in words for the console and the toast: full, a wrong password, ended (no game with that room code,
/// the host stopped), no answer within `timeoutSeconds`.
inline JoinOutcome WatchEditorJoin(const JoinProgress& p, const std::string& hostName, float timeoutSeconds = 30.0f) {
    const std::string host = hostName.empty() ? std::string("the host") : hostName;
    const std::string game = host + "'s game";
    JoinOutcome o;
    if (p.joined) { o.state = JoinState::Joined; o.text = "Joined " + game + "."; return o; }
    auto has = [](const std::string& s, const char* part) { return s.find(part) != std::string::npos; };
    o.state = JoinState::Failed;
    if (!p.gateRefusal.empty()) { o.text = "Couldn't join " + game + ": " + p.gateRefusal; return o; }
    if (p.rejected) {
        const std::string& r = p.rejectReason;
        if (has(r, "full")) o.text = game + " is full. Try again when someone leaves.";
        else if (has(r, "password")) o.text = "The password for " + game + " is wrong. Ask " + host + " for a new invite.";
        else if (has(r, "did not answer")) o.text = game + " did not answer. It may have ended.";
        else o.text = "Couldn't join " + game + (r.empty() ? std::string(".") : ": " + r + ".");
        return o;
    }
    if (!p.lastError.empty()) {
        const std::string& e = p.lastError;
        if (has(e, "No game is hosted")) o.text = game + " has ended (no game is hosted with that room code any more).";
        else if (has(e, "game ended")) o.text = game + " has ended.";
        else o.text = "Couldn't join " + game + ". " + e;
        return o;
    }
    if (p.stopped) { o.text = "Couldn't join " + game + ": the connection closed. It may have ended."; return o; }
    if (timeoutSeconds > 0.0f && p.seconds >= timeoutSeconds) { o.text = game + " did not answer. It may have ended."; return o; }
    o.state = JoinState::Waiting;
    return o;
}

}  // namespace joinplan
}  // namespace okay
