#pragma once
// The one flag the launcher and the editor agree on, so the version question is asked ONCE and before the editor opens.
//
// A plain Open in the launcher used to start the editor, which then asked "This project was made with v2.850.0 ..." in a window of
// its own. Now the launcher asks first (launcher/OpenProjectGate.hpp). When the person answers there, the launcher starts the editor
// with the answer on its command line, so the editor does not ask a second time:
//
//     OkayEngine.exe --launcher <pid> --open "<scene>" --project-answer=upgrade       "Upgrade project": back up, upgrade, then open
//     OkayEngine.exe --launcher <pid> --open "<scene>" --project-answer=open-anyway   "Open anyway" for a project from a newer version
//
// The answer is honoured only when ALL of this holds (ParsePreAnswer; each is a test in tests/test_project_answer.cpp):
//   * `--launcher <pid>` (or `--launcher=<pid>`) is on the command line and that process is alive: the editor was started by the
//     launcher, the same hand-shake the editor needs to start at all;
//   * `--open <scene>` (or `--open=<scene>`) is there: the answer is for THAT project and no other;
//   * the answer is one of the two names above, spelled exactly.
// Anything else is ignored and the editor behaves exactly as before: it asks its own question (the safety net for every other way
// a project reaches it: File > Open, a double-clicked .okayscene, a command line). The editor applies it once, to the project the
// scene belongs to, and only while that project is still of the age the launcher saw: "upgrade" never applies to a project from a
// newer version, and "open anyway" never to an older one.
//
// Header-only, no ImGui, SDL or engine code: the editor, the launcher and the tests share it. Written for this engine; no outside code.
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

namespace okayver {

/// What the person answered in the launcher.
enum class PreAnswer { None, Upgrade, OpenAnyway };

/// The command-line flag (`--project-answer=<name>`; only that one-token form is read).
inline const char* ProjectAnswerFlag() { return "--project-answer"; }
/// "upgrade", "open-anyway"; "" for None.
inline const char* PreAnswerName(PreAnswer a) {
    switch (a) {
        case PreAnswer::Upgrade: return "upgrade";
        case PreAnswer::OpenAnyway: return "open-anyway";
        case PreAnswer::None: break;
    }
    return "";
}
/// The name as spelled in the flag (exactly; no other case or spelling counts); None for anything else.
inline PreAnswer ParsePreAnswerName(const std::string& name) {
    if (name == "upgrade") return PreAnswer::Upgrade;
    if (name == "open-anyway") return PreAnswer::OpenAnyway;
    return PreAnswer::None;
}
/// "--project-answer=upgrade"; "" for None. What the launcher adds to the editor's command line.
inline std::string PreAnswerArg(PreAnswer a) {
    return a == PreAnswer::None ? std::string() : std::string(ProjectAnswerFlag()) + "=" + PreAnswerName(a);
}

/// What the command line asked: the answer, the scene it is for, and the launcher that vouched for it.
struct PreAnswerRequest {
    PreAnswer answer = PreAnswer::None;
    std::string scene;               ///< the `--open` scene the answer belongs to
    unsigned long launcherPid = 0;   ///< the live launcher process
    bool Valid() const { return answer != PreAnswer::None && !scene.empty() && launcherPid != 0; }
};

/// `args` are the arguments after the program name. `launcherAlive(pid)` says whether that process is running. Returns an invalid
/// request (answer None) unless every condition in the header comment holds. The last `--launcher`, `--open` and
/// `--project-answer` win, the way the editor's own parsing reads them.
inline PreAnswerRequest ParsePreAnswer(const std::vector<std::string>& args, const std::function<bool(unsigned long)>& launcherAlive) {
    PreAnswerRequest r;
    PreAnswer answer = PreAnswer::None;
    std::string scene;
    unsigned long pid = 0;
    bool havePid = false;
    const std::string flagEq = std::string(ProjectAnswerFlag()) + "=";
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--launcher" && i + 1 < args.size()) { pid = std::strtoul(args[i + 1].c_str(), nullptr, 10); havePid = true; ++i; }
        else if (a.rfind("--launcher=", 0) == 0) { pid = std::strtoul(a.c_str() + 11, nullptr, 10); havePid = true; }
        else if (a == "--open" && i + 1 < args.size()) { scene = args[i + 1]; ++i; }
        else if (a.rfind("--open=", 0) == 0) scene = a.c_str() + 7;
        else if (a.rfind(flagEq, 0) == 0) answer = ParsePreAnswerName(a.c_str() + flagEq.size());
    }
    if (answer == PreAnswer::None || scene.empty() || !havePid || pid == 0) return r;
    if (!launcherAlive || !launcherAlive(pid)) return r;
    r.answer = answer;
    r.scene = scene;
    r.launcherPid = pid;
    return r;
}

/// The same request as it arrives through the single-instance hand-off file (a second editor, started by the launcher, passes its
/// command line on to the editor that is already open): the answer's name, the launcher's pid as text and the scene. The pid is
/// checked again here, because the launcher may have gone since the second editor started.
inline PreAnswerRequest PreAnswerFromHandoff(const std::string& answerName, const std::string& pidText, const std::string& scene,
                                             const std::function<bool(unsigned long)>& launcherAlive) {
    PreAnswerRequest r;
    const PreAnswer answer = ParsePreAnswerName(answerName);
    const unsigned long pid = std::strtoul(pidText.c_str(), nullptr, 10);
    if (answer == PreAnswer::None || scene.empty() || pid == 0) return r;
    if (!launcherAlive || !launcherAlive(pid)) return r;
    r.answer = answer;
    r.scene = scene;
    r.launcherPid = pid;
    return r;
}

}  // namespace okayver
