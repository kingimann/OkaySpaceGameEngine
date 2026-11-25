// The editor closes when the launcher that started it has gone (editor/LauncherWatch.hpp; owner bug 2026-11-25: "if i quit
// the launcher from the tray, the editor doesnt close"): checked at most once a second, one normal close request (the
// quit prompt, so unsaved work is asked about first), never repeated after Cancel, and not watched without a launcher
// token (--dev / OKAY_DEV). The source checks pin the wiring in editor/main.cpp and the tray fix in launcher/main.cpp.
#include "test_framework.hpp"
#include "LauncherWatch.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace oked::launcherwatch;

static std::string ReadText(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

int main() {
    RUN_SUITE("editor_launcher_watch");

    {   // Not started from a launcher (--dev, OKAY_DEV, tests): never checked, never closes.
        State s;
        CHECK(!CheckDue(s, 0));
        CHECK(!CheckDue(s, 5000));
        CHECK(!OnChecked(s, false));
        CHECK(!ShowLauncherClosedNote(s));
    }
    {   // Watching: checked at once, then at most once per kCheckMs (the frame is never held up by it).
        State s; Start(s);
        CHECK(CheckDue(s, 100));
        CHECK(!OnChecked(s, true));            // alive: nothing happens
        CHECK(!CheckDue(s, 101));
        CHECK(!CheckDue(s, 100 + kCheckMs - 1));
        CHECK(CheckDue(s, 100 + kCheckMs));
        CHECK(!OnChecked(s, true));
        CHECK(!ShowLauncherClosedNote(s));
    }
    {   // The launcher has gone: exactly one close request, the prompt's note on, and no more checks.
        State s; Start(s);
        CHECK(CheckDue(s, 0));
        CHECK(OnChecked(s, false));
        CHECK(ShowLauncherClosedNote(s));
        CHECK(!CheckDue(s, 10 * kCheckMs));    // Cancel on the prompt keeps the editor open: it is not asked again
        CHECK(!OnChecked(s, false));
    }
    {   // The millisecond counter wraps: the check still comes about once a second.
        State s; Start(s);
        CHECK(CheckDue(s, 0xFFFFFF00u));
        CHECK(!CheckDue(s, 0xFFFFFF10u));
        CHECK(CheckDue(s, 0xFFFFFF00u + kCheckMs));   // (wrapped past zero)
    }
    {   // Start resets a used state.
        State s; Start(s); CheckDue(s, 0); OnChecked(s, false);
        Start(s);
        CHECK(s.watching && !s.launcherGone && !s.quitAsked);
    }

    {   // The wiring in editor/main.cpp: the gate starts the watch, the loop polls it, the close goes through the quit
        // prompt (g_quitRequested, not running = false), and the prompt shows the launcher line.
        const std::string ed = ReadText(fs::path(OKAY_SOURCE_DIR) / "editor" / "main.cpp");
        CHECK(!ed.empty());
        CHECK(ed.find("#include \"LauncherWatch.hpp\"") != std::string::npos);
        CHECK(ed.find("{ StartLauncherWatch(launcherPid); return true; }") != std::string::npos);
        CHECK(ed.find("PollLauncherAlive();") != std::string::npos);
        const size_t poll = ed.find("static void PollLauncherAlive()");
        CHECK(poll != std::string::npos);
        if (poll != std::string::npos) {
            const std::string body = ed.substr(poll, ed.find("\n}\n", poll) - poll);
            CHECK(body.find("g_quitRequested = true;") != std::string::npos);
            CHECK(body.find("running = false") == std::string::npos);   // never skips the save prompt
        }
        CHECK(ed.find("ShowLauncherClosedNote(g_launcherWatch)") != std::string::npos);
        CHECK(ed.find("WaitForSingleObject(g_launcherHandle, 0)") != std::string::npos);   // a zero wait: never blocks
    }
    {   // The launcher: a "Close the editor first" that starts waiting while hidden in the tray brings the window back,
        // and switching to the alternative launcher exe is refused while the editor is open (it would close the editor).
        const std::string ln = ReadText(fs::path(OKAY_SOURCE_DIR) / "launcher" / "main.cpp");
        CHECK(ln.find("okaytray::ShowForPendingDialog(pendingNow, s_pendingBefore,") != std::string::npos);
        const size_t show = ln.find("okaytray::ShowForPendingDialog(");
        const size_t skip = ln.find("okaytray::IdleDelayMs(g_tray->Hidden()");
        CHECK(show != std::string::npos && skip != std::string::npos && show < skip);   // before the hidden loop skips drawing
        CHECK(ln.find("if (EditorRunning()) Toast(\"Close the editor first: it closes with this launcher.\", 2);") != std::string::npos);
    }

    TEST_MAIN_RESULT();
}
