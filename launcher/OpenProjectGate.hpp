#pragma once
// The question the launcher asks BEFORE it starts the editor on a project.
//
// Until now a plain Open in the launcher started the editor, and only then did a window INSIDE the editor ask "This project was made
// with v2.850.0 ... Upgrade / Keep / Cancel". Now the launcher asks first, in its own window, before any editor window exists. This
// file is the decision behind that window and nothing else: header-only, no ImGui, no SDL, no disk (tests/test_open_project_gate.cpp
// walks every combination). The window is launcher/OpenProjectUI.cpp; the one function every way of opening a project calls is
// OpenProjectChecked in launcher/main.cpp.
//
//   Ask(in)          which question, its words, and its buttons in order (exactly one is the default)
//   Proceed(in)      what a plain open starts when nothing needs asking
//   Resolve(in, a)   what an answer does: which editor to start with which arguments, or which page to show
//
// It reuses what the editor and the Versions page already have, and adds no second copy of any rule:
//   okayver::Decide / ResolveKeepTarget      the comparison and "which installed editor of the project's line" (shared/ProjectVersion.hpp)
//   okayversions::ChooseEditorForOpen        the default editor for a project with no version (launcher/VersionsManager.hpp)
//   okayver::KeepVersionArgs                 --open <scene> --keep-version, the arguments "Open with" has always used
//   okayver::question::*                     the editor's own sentences and labels (shared/ProjectQuestion.hpp)
//   okayver::PreAnswerArg                    --project-answer=upgrade / open-anyway, which tells the editor not to ask again (shared/ProjectAnswer.hpp)
//
// Which question:
//   the project is not part of a project folder, or the editor is a development build   none, open as before
//   made by this version, or another patch of this line                                  none, open as before
//   made by an older line                                                                Older:       Upgrade project / Open with v2.850.x (or Install it) / Cancel
//   no version recorded                                                                  Unversioned: Upgrade project / Cancel   (a default editor, if one is set and installed, takes it with no question)
//   made by a NEWER line                                                                 Newer:       Check for updates / Open anyway (a backup is made first) / Cancel
// An editor chosen on purpose ("Open with") never gets the upgrade question: an older or unversioned project opens in it as before
// (--keep-version), and a project newer than that editor gets the Newer question. Written for this engine from docs/project_upgrade.md.
#include <string>
#include <vector>

#include "ProjectAnswer.hpp"
#include "ProjectQuestion.hpp"
#include "ProjectVersion.hpp"
#include "VersionsManager.hpp"

namespace okayopen {

enum class Question { None, Older, Unversioned, Newer };
enum class Answer { Upgrade, OpenWithOld, InstallOld, CheckForUpdates, OpenAnyway, Cancel };

inline const char* QuestionName(Question q) {
    switch (q) {
        case Question::None: return "None";
        case Question::Older: return "Older";
        case Question::Unversioned: return "Unversioned";
        case Question::Newer: return "Newer";
    }
    return "?";
}
inline const char* AnswerName(Answer a) {
    switch (a) {
        case Answer::Upgrade: return "Upgrade";
        case Answer::OpenWithOld: return "OpenWithOld";
        case Answer::InstallOld: return "InstallOld";
        case Answer::CheckForUpdates: return "CheckForUpdates";
        case Answer::OpenAnyway: return "OpenAnyway";
        case Answer::Cancel: return "Cancel";
    }
    return "?";
}

/// What the launcher knows when the person presses Open.
struct Input {
    std::string scene;                     ///< the scene the editor is asked to open (--open)
    std::string projectName;               ///< what the sentences call the project ("MyGame"); "" = "This project"
    std::string projectRoot;               ///< the folder okayroot::ProjectRootForScene found; "" = the scene is not part of a project: never asks
    std::string projectVersion;            ///< the stamp in project.okayproj ("" = none recorded; text that is not a version counts as none)
    std::string editorVersion;             ///< the editor a plain Open starts: this launcher's own ("dev" or "" = a development build: never asks)
    std::string chosenVersion;             ///< "Open with": the installed editor the person picked on purpose ("" = a plain Open)
    std::vector<std::string> installed;    ///< okayver::InstalledVersions(launcherDir)
    std::string defaultEditor;             ///< Versions page: the editor for projects with no version ("" = none)
    unsigned long launcherPid = 0;         ///< this launcher's process id; 0 = no hand-shake (the editor would refuse to start)
};

struct Option {
    Answer answer = Answer::Cancel;
    std::string label;                     ///< the button's text
    bool primary = false;                  ///< the default button (accent colour); exactly one per question
    std::string tip;                       ///< one plain sentence for the tooltip ("" = none)
};

/// The question, ready to draw: top to bottom, `eyebrow`, `heading`, `root`, `paragraphs`, `status`, then `options` left to right.
struct Prompt {
    Question question = Question::None;
    std::string eyebrow;                   ///< the small heading above ("PROJECT VERSION")
    std::string heading;                   ///< the question in one sentence
    bool warn = false;                     ///< the heading is the amber data-loss warning
    std::string root;                      ///< the project's folder, shown under the heading
    std::vector<std::string> paragraphs;   ///< the explanation, in order
    std::string status;                    ///< "v2.850.1 is installed and would open it." / "v2.850.x isn't installed on this computer." ("" = none)
    bool statusGood = false;               ///< green (true) or amber (false)
    std::vector<Option> options;           ///< in order; Cancel is last
};

/// What to start.
struct Launch {
    bool start = false;
    std::string editorVersion;             ///< "" = this launcher's own editor; otherwise the installed version (okayver::InstalledEditorPath)
    std::vector<std::string> args;         ///< the arguments after the program; quote each with okayversions::QuoteArg (JoinArgs)
};
enum class Next {
    None,
    ShowVersions,                          ///< go to the Versions page with Outcome::version highlighted
    CheckForUpdates,                       ///< the launcher's own update check
};
struct Outcome {
    Launch launch;
    Next next = Next::None;
    std::string version;                   ///< ShowVersions: the project's version (the page picks its line)
    std::string message;                   ///< what to tell the person when `launch.start` ("Opening MyGame", "Opening MyGame with v2.850.1")
};

namespace detail {

using Strings = std::vector<std::string>;

/// The editor this open would start, as the sentences name it: the one chosen on purpose, else this launcher's own.
inline std::string Target(const Input& in) { return in.chosenVersion.empty() ? in.editorVersion : in.chosenVersion; }

/// --launcher <pid> --open <scene>; without a pid (no hand-shake) just --open <scene>.
inline Strings PlainArgs(const Input& in) {
    Strings a;
    if (in.launcherPid != 0) { a.push_back("--launcher"); a.push_back(std::to_string(in.launcherPid)); }
    a.push_back("--open");
    a.push_back(in.scene);
    return a;
}
/// PlainArgs plus --keep-version: the version was chosen on purpose (the same arguments okayversions::OpenWithArgs makes).
inline Strings KeepArgs(const Input& in) {
    Strings a;
    if (in.launcherPid != 0) { a.push_back("--launcher"); a.push_back(std::to_string(in.launcherPid)); }
    for (std::string& x : okayver::KeepVersionArgs(in.scene)) a.push_back(std::move(x));
    return a;
}
/// The answer flag, only when there is a hand-shake for the editor to check it against.
inline void AddAnswer(const Input& in, okayver::PreAnswer answer, Strings& args) {
    if (in.launcherPid != 0) args.push_back(okayver::PreAnswerArg(answer));
}
inline std::string Opening(const Input& in, const std::string& editorVersion) {
    const std::string what = in.projectName.empty() ? std::string("the project") : in.projectName;
    return "Opening " + what + (editorVersion.empty() ? std::string() : " with v" + editorVersion);
}
inline Launch Plan(const std::string& editorVersion, Strings args) {
    Launch l;
    l.start = true;
    l.editorVersion = editorVersion;
    l.args = std::move(args);
    return l;
}

}  // namespace detail

/// Which question the launcher asks before it starts the editor on `in.scene`.
inline Question WhichQuestion(const Input& in) {
    if (in.projectRoot.empty()) return Question::None;                       // a loose scene: nothing to upgrade, the editor asks nothing either
    const bool chosen = !in.chosenVersion.empty();
    switch (okayver::Decide(in.projectVersion, detail::Target(in))) {
        case okayver::Decision::Same:
        case okayver::Decision::SamePatchLine:                               // a patch never changes a file format
        case okayver::Decision::DevBuild:
            return Question::None;
        case okayver::Decision::Older:
            return chosen ? Question::None : Question::Older;                // chosen on purpose: opened as it always was
        case okayver::Decision::Unknown:
            if (chosen) return Question::None;
            // a default editor (Versions page) takes a project with no version, as before
            return okayversions::ChooseEditorForOpen(in.projectVersion, in.editorVersion, in.installed, in.defaultEditor).version.empty()
                       ? Question::Unversioned : Question::None;
        case okayver::Decision::Newer:
            return Question::Newer;
    }
    return Question::None;
}

/// The question for `in`, with its words and buttons. `question == None` (and no options) when nothing needs asking.
inline Prompt Ask(const Input& in) {
    namespace qs = okayver::question;
    Prompt p;
    p.question = WhichQuestion(in);
    if (p.question == Question::None) return p;
    const std::string subject = in.projectName.empty() ? std::string("This project") : in.projectName;
    const std::string pv = okayver::NormalizeVersion(in.projectVersion), rv = okayver::NormalizeVersion(detail::Target(in));
    p.eyebrow = qs::Eyebrow();
    p.root = in.projectRoot;
    auto add = [&](Answer a, std::string label, bool primary, std::string tip) {
        Option o;
        o.answer = a; o.label = std::move(label); o.primary = primary; o.tip = std::move(tip);
        p.options.push_back(std::move(o));
    };
    const std::string cancelTip = "Nothing is started and nothing is changed.";
    switch (p.question) {
        case Question::Older: {
            const std::string line = okayver::LineLabel(pv);
            const std::string target = okayver::ResolveKeepTarget(pv, in.installed);   // the newest installed editor of the project's line
            p.heading = qs::OlderHeading(subject, pv, rv);
            p.paragraphs = {qs::UpgradeParagraph(rv), qs::KeepParagraph("Opening it with " + line)};
            p.statusGood = !target.empty();
            p.status = target.empty() ? qs::NotInstalledLine(pv) : qs::InstalledLine(target);
            add(Answer::Upgrade, qs::UpgradeLabel(rv), true,
                "Makes a complete backup copy next to the project, then brings its scenes and prefabs up to date. The editor shows the progress and the result.");
            if (!target.empty())
                add(Answer::OpenWithOld, "Open with " + line, false, "Starts the installed v" + target + " editor on the project. Nothing in the project is changed.");
            else
                add(Answer::InstallOld, "Install " + line, false, "Opens the Versions page with " + line + " highlighted. Install it there, then press Open again.");
            add(Answer::Cancel, qs::CancelLabel(), false, cancelTip);
            break;
        }
        case Question::Unversioned:
            p.heading = qs::UnknownHeading(subject, rv);
            p.paragraphs = {qs::UnknownParagraph(rv)};
            add(Answer::Upgrade, qs::UpgradeLabel(rv), true,
                "Makes a complete backup copy next to the project, then brings its scenes and prefabs up to date and marks it with " + qs::Label(rv) + ".");
            add(Answer::Cancel, qs::CancelLabel(), false, cancelTip);
            break;
        case Question::Newer:
            p.warn = true;
            p.heading = qs::NewerHeading(subject, pv, rv);
            p.paragraphs = {qs::NewerParagraph()};
            add(Answer::CheckForUpdates, "Check for updates", true, "Looks for a newer OkaySpace. Update, then press Open again.");
            add(Answer::OpenAnyway, qs::OpenAnywayLabel(), false,
                "Makes a complete backup copy first, then opens the project in this editor, which may drop data it does not understand when it saves.");
            add(Answer::Cancel, qs::CancelLabel(), false, cancelTip);
            break;
        case Question::None:
            break;
    }
    return p;
}

/// What a plain open starts when `Ask` says nothing needs asking: this launcher's editor, the default editor for a project with no
/// version (--keep-version), or the editor that was chosen on purpose (--keep-version). The same choices the launcher always made.
inline Outcome Proceed(const Input& in) {
    Outcome o;
    if (!in.chosenVersion.empty()) {
        const std::string v = okayver::NormalizeVersion(in.chosenVersion);
        o.launch = detail::Plan(v, detail::KeepArgs(in));
    } else {
        const okayversions::OpenTarget t = okayversions::ChooseEditorForOpen(in.projectVersion, in.editorVersion, in.installed, in.defaultEditor);
        if (!t.version.empty()) o.launch = detail::Plan(t.version, detail::KeepArgs(in));
        else o.launch = detail::Plan(std::string(), detail::PlainArgs(in));
    }
    o.message = detail::Opening(in, o.launch.editorVersion);
    return o;
}

/// What the answer does. An answer the question did not offer does nothing at all (so "upgrade" can never be produced for a project
/// from a newer version, nor "open anyway" for an older one).
inline Outcome Resolve(const Input& in, Answer answer) {
    Outcome o;
    const Prompt p = Ask(in);
    bool offered = false;
    for (const Option& op : p.options) if (op.answer == answer) offered = true;
    if (!offered) return o;
    switch (answer) {
        case Answer::Cancel:
            return o;
        case Answer::CheckForUpdates:
            o.next = Next::CheckForUpdates;
            return o;
        case Answer::InstallOld:
            o.next = Next::ShowVersions;
            o.version = okayver::NormalizeVersion(in.projectVersion);
            return o;
        case Answer::Upgrade: {                                              // this launcher's editor does it: backup, progress, result, then opens
            detail::Strings args = detail::PlainArgs(in);
            detail::AddAnswer(in, okayver::PreAnswer::Upgrade, args);
            o.launch = detail::Plan(std::string(), std::move(args));
            break;
        }
        case Answer::OpenWithOld: {                                          // the installed editor of the project's own line, as "Open with" does
            const std::string target = okayver::ResolveKeepTarget(in.projectVersion, in.installed);
            o.launch = detail::Plan(target, detail::KeepArgs(in));
            break;
        }
        case Answer::OpenAnyway: {                                           // the editor makes the backup first, then opens it
            const bool chosen = !in.chosenVersion.empty();
            detail::Strings args = chosen ? detail::KeepArgs(in) : detail::PlainArgs(in);
            detail::AddAnswer(in, okayver::PreAnswer::OpenAnyway, args);
            o.launch = detail::Plan(chosen ? okayver::NormalizeVersion(in.chosenVersion) : std::string(), std::move(args));
            break;
        }
    }
    o.message = detail::Opening(in, o.launch.editorVersion);
    return o;
}

}  // namespace okayopen
