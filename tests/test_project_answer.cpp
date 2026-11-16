// `--project-answer=upgrade|open-anyway` (shared/ProjectAnswer.hpp): the one flag the launcher uses to tell the editor "I already asked
// the version question and the person answered, do not ask again". Pure parsing, no editor, no window. The rule it must keep: the
// answer is honoured ONLY when the editor was started by a live launcher (`--launcher <pid>`, the same hand-shake the editor needs to
// start at all) and ONLY for the project named by `--open`; anything else is ignored and the editor asks its own question as before.
// The editor's gate that applies the answer is checked in tests/test_project_upgrade.cpp.
#include "test_framework.hpp"
#include "ProjectAnswer.hpp"
#include "AppRestart.hpp"   // okay::restart::PidAlive: the live-process check the editor itself uses

#include <cstring>

#if !defined(_WIN32)
#  include <sys/wait.h>
#  include <unistd.h>
#endif

using namespace okayver;
using Strings = std::vector<std::string>;

namespace {

bool AlwaysAlive(unsigned long) { return true; }
bool NeverAlive(unsigned long) { return false; }

Strings Args(const std::string& pid, const std::string& scene, const std::string& answer) {
    return {"--launcher", pid, "--open", scene, "--project-answer=" + answer};
}

}  // namespace

int main() {
    RUN_SUITE("project_answer");
    const std::string scene = "C:\\Users\\me\\My Projects\\Spa\xC3\x9F \"2\"\\Assets\\main.okayscene";

    // ---- the names -------------------------------------------------------------------------------------------------------------------
    CHECK(std::string(ProjectAnswerFlag()) == "--project-answer");
    CHECK(std::string(PreAnswerName(PreAnswer::Upgrade)) == "upgrade" && std::string(PreAnswerName(PreAnswer::OpenAnyway)) == "open-anyway" &&
          std::string(PreAnswerName(PreAnswer::None)).empty());
    CHECK(PreAnswerArg(PreAnswer::Upgrade) == "--project-answer=upgrade" && PreAnswerArg(PreAnswer::OpenAnyway) == "--project-answer=open-anyway" &&
          PreAnswerArg(PreAnswer::None).empty());
    for (PreAnswer a : {PreAnswer::Upgrade, PreAnswer::OpenAnyway}) CHECK(ParsePreAnswerName(PreAnswerName(a)) == a);
    for (const char* bad : {"", "Upgrade", "UPGRADE", "open_anyway", "openanyway", "Open-Anyway", "upgrade ", " upgrade", "yes", "keep", "cancel", "upgrade=1", "open-anyway\n"})
        CHECK(ParsePreAnswerName(bad) == PreAnswer::None);

    // ---- honoured: a live launcher, and the --open project ----------------------------------------------------------------------------
    {
        PreAnswerRequest r = ParsePreAnswer(Args("4242", scene, "upgrade"), AlwaysAlive);
        CHECK(r.Valid() && r.answer == PreAnswer::Upgrade && r.scene == scene && r.launcherPid == 4242);        // the scene comes through unchanged
        r = ParsePreAnswer(Args("4242", scene, "open-anyway"), AlwaysAlive);
        CHECK(r.Valid() && r.answer == PreAnswer::OpenAnyway && r.scene == scene && r.launcherPid == 4242);
        // the other spellings of the two flags the editor reads
        r = ParsePreAnswer({"--launcher=4242", "--open=" + scene, "--project-answer=upgrade"}, AlwaysAlive);
        CHECK(r.Valid() && r.scene == scene && r.launcherPid == 4242);
        // any order, other flags around them
        r = ParsePreAnswer({"--project-answer=open-anyway", "--keep-version", "--gpu", "1", "--open", scene, "--launcher", "7"}, AlwaysAlive);
        CHECK(r.Valid() && r.answer == PreAnswer::OpenAnyway && r.scene == scene && r.launcherPid == 7);
        // the process that is asked about is the one on the command line
        unsigned long asked = 0;
        r = ParsePreAnswer(Args("31337", scene, "upgrade"), [&](unsigned long pid) { asked = pid; return true; });
        CHECK(r.Valid() && asked == 31337);
    }

    // ---- ignored: not started by the launcher -------------------------------------------------------------------------------------------
    for (const char* name : {"upgrade", "open-anyway"}) {
        const std::string ans = std::string("--project-answer=") + name;
        CHECK(!ParsePreAnswer({"--open", scene, ans}, AlwaysAlive).Valid());                                      // no --launcher at all
        CHECK(!ParsePreAnswer({ans}, AlwaysAlive).Valid() && !ParsePreAnswer({}, AlwaysAlive).Valid());
        CHECK(!ParsePreAnswer({"--launcher", "4242", "--open", scene, ans}, NeverAlive).Valid());                 // the launcher has gone
        CHECK(!ParsePreAnswer({"--launcher=4242", "--open", scene, ans}, NeverAlive).Valid());
        CHECK(!ParsePreAnswer({"--launcher", "4242", "--open", scene, ans}, nullptr).Valid());                    // nothing to check the process with: refuse
        CHECK(!ParsePreAnswer({"--launcher", "0", "--open", scene, ans}, AlwaysAlive).Valid());                   // pid 0 is no process
        for (const char* huge : {"2147483648", "4294967295", "4294967296", "18446744073709551615", "99999999999999999999999", "-1"})
            CHECK(!ParsePreAnswer({"--launcher", huge, "--open", scene, ans}, AlwaysAlive).Valid());                 // not a process id at all
        CHECK(!ParsePreAnswer({"--launcher", "abc", "--open", scene, ans}, AlwaysAlive).Valid());                 // not a number
        CHECK(!ParsePreAnswer({"--launcher", "", "--open", scene, ans}, AlwaysAlive).Valid());
        CHECK(!ParsePreAnswer({"--launcher=", "--open", scene, ans}, AlwaysAlive).Valid());
        CHECK(!ParsePreAnswer({"--open", scene, ans, "--launcher"}, AlwaysAlive).Valid());                        // --launcher with no value
        CHECK(!ParsePreAnswer({"--dev", "--open", scene, ans}, AlwaysAlive).Valid());                             // --dev skips the hand-shake, but never counts as one
    }

    // ---- ignored: not for a project named by --open --------------------------------------------------------------------------------------
    CHECK(!ParsePreAnswer({"--launcher", "4242", "--project-answer=upgrade"}, AlwaysAlive).Valid());              // no --open: no project to answer for
    CHECK(!ParsePreAnswer({"--launcher", "4242", "--open", "", "--project-answer=upgrade"}, AlwaysAlive).Valid());
    CHECK(!ParsePreAnswer({"--launcher", "4242", "--open=", "--project-answer=upgrade"}, AlwaysAlive).Valid());
    CHECK(!ParsePreAnswer({"--launcher", "4242", "--open"}, AlwaysAlive).Valid());
    CHECK(!ParsePreAnswer({"--launcher", "4242", scene, "--project-answer=upgrade"}, AlwaysAlive).Valid());      // a bare path is not --open
    CHECK(!ParsePreAnswer({"--launcher", "4242", "--new-project", scene, "--project-answer=upgrade"}, AlwaysAlive).Valid());
    CHECK(!ParsePreAnswer({"--launcher", "4242", "--template", scene, "--project-answer=upgrade"}, AlwaysAlive).Valid());
    // a scene that is called like the flag is a scene, not the flag
    CHECK(!ParsePreAnswer({"--launcher", "4242", "--open", "--project-answer=upgrade"}, AlwaysAlive).Valid());
    {
        const PreAnswerRequest r = ParsePreAnswer({"--launcher", "4242", "--open", "--project-answer=upgrade", "--project-answer=open-anyway"}, AlwaysAlive);
        CHECK(r.Valid() && r.answer == PreAnswer::OpenAnyway && r.scene == "--project-answer=upgrade");
    }
    // the last --open is the project the editor opens, so it is the one the answer is for
    {
        const PreAnswerRequest r = ParsePreAnswer({"--launcher", "4242", "--open", "a.okayscene", "--open", "b.okayscene", "--project-answer=upgrade"}, AlwaysAlive);
        CHECK(r.Valid() && r.scene == "b.okayscene");
    }

    // ---- ignored: not an answer we know -------------------------------------------------------------------------------------------------
    for (const char* bad : {"", "Upgrade", "UPGRADE", "open_anyway", "openanyway", "keep", "cancel", "yes", "1", "upgrade ", "upgrade,open-anyway"})
        CHECK(!ParsePreAnswer(Args("4242", scene, bad), AlwaysAlive).Valid());
    CHECK(!ParsePreAnswer({"--launcher", "4242", "--open", scene, "--project-answer", "upgrade"}, AlwaysAlive).Valid());   // only the one-token form is read
    CHECK(!ParsePreAnswer({"--launcher", "4242", "--open", scene, "--project-answer"}, AlwaysAlive).Valid());
    CHECK(!ParsePreAnswer({"--launcher", "4242", "--open", scene, "--project-answers=upgrade"}, AlwaysAlive).Valid());
    CHECK(!ParsePreAnswer({"--launcher", "4242", "--open", scene, "--Project-Answer=upgrade"}, AlwaysAlive).Valid());
    // the last answer wins, even when it is the bad one (the safe side: the editor asks)
    CHECK(ParsePreAnswer({"--launcher", "4242", "--open", scene, "--project-answer=bogus", "--project-answer=upgrade"}, AlwaysAlive).answer == PreAnswer::Upgrade);
    CHECK(!ParsePreAnswer({"--launcher", "4242", "--open", scene, "--project-answer=upgrade", "--project-answer=bogus"}, AlwaysAlive).Valid());
    // --keep-version is a different thing and neither makes the other
    CHECK(!ParsePreAnswer({"--launcher", "4242", "--open", scene, "--keep-version"}, AlwaysAlive).Valid());

    // ---- the same request through the single-instance hand-off -----------------------------------------------------------------------------
    {
        PreAnswerRequest r = PreAnswerFromHandoff("upgrade", "4242", scene, AlwaysAlive);
        CHECK(r.Valid() && r.answer == PreAnswer::Upgrade && r.scene == scene && r.launcherPid == 4242);
        r = PreAnswerFromHandoff("open-anyway", "4242", scene, AlwaysAlive);
        CHECK(r.Valid() && r.answer == PreAnswer::OpenAnyway);
        CHECK(!PreAnswerFromHandoff("upgrade", "4242", scene, NeverAlive).Valid());                // the launcher has gone since the second editor started
        CHECK(!PreAnswerFromHandoff("upgrade", "4242", scene, nullptr).Valid());
        CHECK(!PreAnswerFromHandoff("upgrade", "0", scene, AlwaysAlive).Valid());
        CHECK(!PreAnswerFromHandoff("upgrade", "", scene, AlwaysAlive).Valid());
        CHECK(!PreAnswerFromHandoff("upgrade", "abc", scene, AlwaysAlive).Valid());
        for (const char* huge : {"2147483648", "4294967295", "18446744073709551615", "99999999999999999999999", "-1"})
            CHECK(!PreAnswerFromHandoff("open-anyway", huge, scene, AlwaysAlive).Valid());
        CHECK(PreAnswerFromHandoff("upgrade", "2147483647", scene, AlwaysAlive).Valid());                         // the biggest id that can be one
        CHECK(!PreAnswerFromHandoff("upgrade", "4242", "", AlwaysAlive).Valid());                  // no project
        CHECK(!PreAnswerFromHandoff("", "4242", scene, AlwaysAlive).Valid());
        CHECK(!PreAnswerFromHandoff("Upgrade", "4242", scene, AlwaysAlive).Valid());
        CHECK(!PreAnswerFromHandoff("keep", "4242", scene, AlwaysAlive).Valid());
        CHECK(!PreAnswerRequest().Valid());
    }

    // ---- with the real process check -----------------------------------------------------------------------------------------------------
    {
        const std::string me = std::to_string(okay::restart::CurrentPid());
        CHECK(ParsePreAnswer(Args(me, scene, "upgrade"), okay::restart::PidAlive).Valid());               // this test process is alive
        CHECK(PreAnswerFromHandoff("open-anyway", me, scene, okay::restart::PidAlive).Valid());
        CHECK(!ParsePreAnswer(Args("0", scene, "upgrade"), okay::restart::PidAlive).Valid());
        CHECK(!ParsePreAnswer(Args("4294967295", scene, "upgrade"), okay::restart::PidAlive).Valid());              // (PidAlive would take this for "everything" on Linux)
        CHECK(!PreAnswerFromHandoff("upgrade", "4294967295", scene, okay::restart::PidAlive).Valid());
#if !defined(_WIN32)
        // a launcher that has exited: a child that ran and was collected
        const pid_t child = fork();
        if (child == 0) _exit(0);
        int status = 0;
        CHECK(child > 0 && waitpid(child, &status, 0) == child);
        CHECK(!okay::restart::PidAlive(static_cast<unsigned long>(child)));
        CHECK(!ParsePreAnswer(Args(std::to_string((unsigned long)child), scene, "upgrade"), okay::restart::PidAlive).Valid());
        CHECK(!PreAnswerFromHandoff("upgrade", std::to_string((unsigned long)child), scene, okay::restart::PidAlive).Valid());
#endif
    }

    // ---- a restart never carries the answer ---------------------------------------------------------------------------------------------
    // The editor starts itself again after a restore or an update with the arguments it was started with (okay::restart::CurrentArguments).
    // An answer is for one start only: if it came along, the restarted editor would act on a question the person never saw again.
    {
        const char* argv[] = {"OkayEngine.exe", "--launcher", "4242", "--open", "a.okayscene", "--project-answer=upgrade", "--keep-version", nullptr};
        okay::restart::RememberArguments(7, const_cast<char**>(argv));
        CHECK(okay::restart::CurrentArguments() == (Strings{"--launcher", "4242", "--open", "a.okayscene", "--keep-version"}));
        const char* argv2[] = {"OkayEngine.exe", "--project-answer=open-anyway", "--launcher=9", nullptr};
        okay::restart::RememberArguments(3, const_cast<char**>(argv2));
        CHECK(okay::restart::CurrentArguments() == (Strings{"--launcher=9"}));
        const char* argv3[] = {"OkayEngine.exe", "--launcher", "4242", "--open", "a.okayscene", nullptr};
        okay::restart::RememberArguments(5, const_cast<char**>(argv3));
        CHECK(okay::restart::CurrentArguments() == (Strings{"--launcher", "4242", "--open", "a.okayscene"}));   // everything else is kept as it was
    }

    TEST_MAIN_RESULT();
}
