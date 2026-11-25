// The launcher's command line (launcher/CommandLine.cpp): words and the argument list (no shell), Tab completion from
// okay's own command table, history, streaming a running program's output, Stop, and the page's layout. Headless.
// The running tests use a small fake okay (a shell script that prints its arguments) on Linux and macOS; on Windows
// they are skipped here (the Windows pipes and job object are unverified until run on Windows).
#include "test_framework.hpp"

#include "CommandLine.hpp"
#include "CommandTable.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace okaylcli;
namespace fs = std::filesystem;

static std::vector<std::string> Words(const std::string& line) {
    std::vector<std::string> w;
    std::string err;
    SplitWords(line, w, err);
    return w;
}
static bool Has(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }

#if !defined(_WIN32)
// Poll until the session's command ends (or a few seconds pass).
static void WaitDone(Session& s, int ms = 5000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (s.Running() && std::chrono::steady_clock::now() < end) { s.Poll(); usleep(2000); }
    s.Poll();
}
static std::string WriteFakeOkay(const fs::path& dir) {
    // Prints each argument in brackets, one per line; "slow" streams three lines 150 ms apart; "err" writes to stderr
    // and exits 3; "forever" sleeps (for Stop); "pwd" prints its folder.
    const fs::path p = dir / "okay";
    std::ofstream f(p);
    f << "#!/bin/sh\n"
         "case \"$1\" in\n"
         "  slow) echo one; sleep 0.15; echo two; sleep 0.15; printf three; exit 0;;\n"
         "  err) echo bad >&2; exit 3;;\n"
         "  forever) echo started; sleep 30; echo never; exit 0;;\n"
         "  pwd) pwd; exit 0;;\n"
         "esac\n"
         "for a in \"$@\"; do echo \"[$a]\"; done\n";
    f.close();
    ::chmod(p.c_str(), 0755);
    return p.string();
}
#endif

int main() {
    RUN_SUITE("launcher_cli");

    // ---- words and the argument list ----
    {
        CHECK(Words("scene check") == (std::vector<std::string>{"scene", "check"}));
        CHECK(Words("  vc   save  ") == (std::vector<std::string>{"vc", "save"}));
        CHECK(Words("vc save \"first try\"") == (std::vector<std::string>{"vc", "save", "first try"}));
        CHECK(Words("asset import 'C:\\My Files\\a b.png'") == (std::vector<std::string>{"asset", "import", "C:\\My Files\\a b.png"}));
        CHECK(Words("vc save \"say \\\"hi\\\"\"") == (std::vector<std::string>{"vc", "save", "say \"hi\""}));
        CHECK(Words("x\"a b\"y") == (std::vector<std::string>{"xa by"}));
        CHECK(Words("open C:\\Games\\level.okayscene") == (std::vector<std::string>{"open", "C:\\Games\\level.okayscene"}));
        CHECK(Words("vc save \"\"") == (std::vector<std::string>{"vc", "save", ""}));   // an empty word is kept
        std::vector<std::string> w;
        std::string err;
        CHECK(!SplitWords("vc save \"no end", w, err));
        CHECK(err.find("double quote") != std::string::npos);
        CHECK(w.empty());
    }
    {   // Shell characters are plain text inside words: nothing chains, pipes or redirects.
        std::vector<std::string> argv;
        std::string err;
        CHECK(BuildArgv("/tools/okay", "scene list & del /q * | x > y; rm -rf $HOME `id`", argv, err));
        CHECK(argv.size() == 15);
        CHECK(argv[0] == "/tools/okay");
        CHECK(argv[1] == "scene" && argv[2] == "list" && argv[3] == "&" && argv[6] == "*" && argv[7] == "|");
        CHECK(argv[9] == ">" && argv[10] == "y;" && argv[13] == "$HOME" && argv[14] == "`id`");
        CHECK(BuildArgv("/tools/okay", "okay help vc", argv, err));   // a typed "okay" is dropped
        CHECK(argv == (std::vector<std::string>{"/tools/okay", "help", "vc"}));
        CHECK(BuildArgv("C:/T/okay.exe", "OKAY.EXE info", argv, err));
        CHECK(argv == (std::vector<std::string>{"C:/T/okay.exe", "info"}));
        CHECK(!BuildArgv("/tools/okay", "okay", argv, err));
        CHECK(!BuildArgv("", "help", argv, err));
        CHECK(err.find("okay.exe") != std::string::npos);
        CHECK(!BuildArgv("/tools/okay", "vc save 'x", argv, err));
    }
    {
        CHECK(Classify("") == LineKind::Empty);
        CHECK(Classify("   ") == LineKind::Empty);
        CHECK(Classify("clear") == LineKind::Clear);
        CHECK(Classify(" CLS ") == LineKind::Clear);
        CHECK(Classify("clear now") == LineKind::Okay);
        CHECK(Classify("help") == LineKind::Okay);
    }

    // ---- Tab completion from okay's own table ----
    {
        const std::vector<std::string> cmds = okaycli::CommandNames();
        CHECK(Has(cmds, "vc") && Has(cmds, "scene") && Has(cmds, "help"));
        Completion c = Complete("");
        CHECK(c.line == "");                                   // everything fits: no common start, all listed
        CHECK(c.choices.size() == cmds.size() + 1);            // + the page's own "clear"
        CHECK(Has(c.choices, "clear"));
        CHECK(Complete("he").line == "help ");
        CHECK(Complete("okay he").line == "okay help ");      // a typed okay is skipped
        CHECK(Complete("vc sa").line == "vc save ");
        CHECK(Complete("vc s").line == "vc s");                 // status, save, switch, sync: no longer common start
        CHECK(Complete("vc s").choices.size() == 4);
        CHECK(Complete("help sc").line == "help sc" && Complete("help sc").choices.size() == 2);   // scene, script
        CHECK(Complete("help sce").line == "help scene ");
        c = Complete("e");                                      // engine, export
        CHECK(c.line == "e" && c.choices.size() == 2 && Has(c.choices, "engine") && Has(c.choices, "export"));
        c = Complete("ex");
        CHECK(c.line == "export ");
        c = Complete("scene ");
        CHECK(c.choices == okaycli::SubCommandNames("scene"));  // the same list as okay's help
        CHECK(Complete("xyz").line == "xyz" && Complete("xyz").choices.empty());
        CHECK(Complete("vc save m").line == "vc save m");      // the third word is the person's own
        CHECK(Complete("asset us").line == "asset use");       // used-by, uses agree as far as "use"
        CHECK(Complete("asset use").line == "asset use");
        CHECK(Complete("asset used").line == "asset used-by ");
        CHECK(std::string(okaycli::CommandSummary("vc save")).find("Save") != std::string::npos);
    }

    // ---- history ----
    {
        History h(3);
        std::string line;
        CHECK(!h.Up("typed", line));
        h.Push("help");
        h.Push("scene list");
        h.Push("scene list");   // the same as the newest: once
        CHECK(h.Size() == 2);
        CHECK(h.Up("half", line) && line == "scene list");
        CHECK(h.Up("ignored", line) && line == "help");
        CHECK(!h.Up("", line));                  // nothing older
        CHECK(h.Down(line) && line == "scene list");
        CHECK(h.Down(line) && line == "half");   // back to what was being typed
        CHECK(!h.Down(line));
        h.Push("a"); h.Push("b"); h.Push("c");
        CHECK(h.Size() == 3 && h.Lines().front() == "a");   // the limit drops the oldest
        h.Push("");
        CHECK(h.Size() == 3);
        CHECK(h.Up("", line) && line == "c");
    }

    // ---- output cleaning ----
    CHECK(CleanOutput("\x1b[32mok\x1b[0m\r\n") == "ok\n");
    CHECK(CleanOutput("a\x1b[1;31mb") == "ab");

    // ---- the page's layout: nothing overlaps, everything is inside ----
    {
        const float sizes[][2] = {{960, 600}, {690, 430}, {740, 460}, {520, 380}, {1600, 900}, {360, 300}};
        for (const auto& sz : sizes) {
            const float w = sz[0], h = sz[1];
            const PageLayout L = LayoutPage(w, h, 26.0f, 16.0f);
            const std::vector<Rect> all = L.All();
            bool inside = true, apart = true, sized = true;
            for (std::size_t i = 0; i < all.size(); ++i) {
                const Rect& r = all[i];
                if (r.x < 0 || r.y < 0 || r.R() > w + 0.01f || r.B() > h + 0.01f) inside = false;
                if (r.w <= 0 || r.h <= 0) sized = false;
                for (std::size_t j = i + 1; j < all.size(); ++j) if (Overlaps(r, all[j])) apart = false;
            }
            CHECK(inside);
            CHECK(apart);
            CHECK(sized);
            CHECK(L.output.h >= 100.0f);            // the output gets the room
            CHECK(L.input.w >= 160.0f);
            CHECK(L.input.y > L.output.B());   // the line is under the output
        }
        // The page inside a 960 x 600 launcher window: the sidebar, header and footer take some; it still fits.
        const PageLayout L = LayoutPage(960 - 232 - 40, 600 - 150 - 44, 26.0f, 16.0f);
        CHECK(L.output.h > 200.0f);
        CHECK(!Overlaps(L.project, L.terminal));
    }

#if !defined(_WIN32)
    // ---- running okay: arguments arrive one by one, output streams, Stop ends it ----
    const fs::path dir = fs::temp_directory_path() / ("okay_launcher_cli_" + std::to_string((long)getpid()));
    fs::create_directories(dir / "My Project");
    const std::string fake = WriteFakeOkay(dir);
    {
        Session s;
        s.SetProgram(fake);
        s.SetFolder((dir / "My Project").string());
        CHECK(s.Submit("okay vc save \"two words\" 'it''s' $HOME;ls \"a\\\"b\""));
        WaitDone(s);
        const std::string t = s.Text();
        CHECK(t.find("> okay vc save") != std::string::npos);         // the command is echoed
        CHECK(t.find("[vc]\n[save]\n[two words]\n[its]\n[$HOME;ls]\n[a\"b]") != std::string::npos);
        CHECK(t.find("Done.") != std::string::npos);
        CHECK(s.GetHistory().Size() == 1);
        CHECK(s.Submit("pwd"));
        WaitDone(s);
        CHECK(s.Text().find("My Project") != std::string::npos);      // it runs in the project's folder
    }
    {   // Streaming: lines show while the program still runs, not only at the end.
        Session s;
        s.SetProgram(fake);
        CHECK(s.Submit("slow"));
        bool sawEarly = false;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (s.Running() && std::chrono::steady_clock::now() < end) {
            s.Poll();
            for (const OutLine& l : s.Lines())
                if (l.text == "one" && s.Running()) sawEarly = true;
            usleep(2000);
        }
        s.Poll();
        CHECK(sawEarly);
        CHECK(s.Text().find("one\ntwo\nthree\nDone.") != std::string::npos);   // the last line had no newline
        CHECK(s.Lines()[1].kind == OutLine::Out);
    }
    {   // stderr and the exit code
        Session s;
        s.SetProgram(fake);
        CHECK(s.Submit("err"));
        WaitDone(s);
        bool errLine = false;
        for (const OutLine& l : s.Lines()) if (l.text == "bad" && l.kind == OutLine::Err) errLine = true;
        CHECK(errLine);
        CHECK(s.Text().find("Finished with exit code 3.") != std::string::npos);
    }
    {   // Stop ends a running command at once, and the page says so; one at a time.
        Session s;
        s.SetProgram(fake);
        CHECK(s.Submit("forever"));
        const auto t0 = std::chrono::steady_clock::now();
        while (s.Text().find("started") == std::string::npos && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(5)) {
            s.Poll();
            usleep(2000);
        }
        CHECK(s.Running());
        CHECK(!s.Submit("help"));   // still running
        CHECK(s.Text().find("still running") != std::string::npos);
        s.Stop();
        WaitDone(s, 3000);
        CHECK(!s.Running());
        CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10));
        CHECK(s.Text().find("Stopped.") != std::string::npos);
        CHECK(s.Text().find("never") == std::string::npos);
        s.Submit("clear");
        CHECK(s.Lines().empty() && s.Text().empty());
    }
    {   // A program that is not there, and no program at all
        Session s;
        s.SetProgram((dir / "missing-okay").string());
        CHECK(!s.Submit("help"));
        CHECK(s.Text().find("Could not find") != std::string::npos);
        Session none;
        CHECK(!none.Submit("help"));
        CHECK(none.Text().find("okay.exe") != std::string::npos);
    }
    {   // okaycli::Child directly: a folder that does not exist is a failed start, not a hang
        okaycli::Child c;
        std::string err;
        CHECK(!c.Start({fake, "x"}, (dir / "nope").string(), err));
        CHECK(!err.empty());
        CHECK(!c.Running());
    }
    {   // The line cap keeps the newest lines
        Session s(5);
        s.SetProgram(fake);
        CHECK(s.Submit("a b c d e f g"));
        WaitDone(s);
        CHECK(s.Lines().size() == 5);
        CHECK(s.Lines().back().text == "Done.");
        CHECK(s.Text().find("[a]") == std::string::npos);
    }
    {   // Opening a terminal refuses a folder that is not there (the window itself is not opened in a test)
        std::string err;
        CHECK(!OpenTerminal((dir / "nope").string(), dir.string(), err));
        CHECK(!err.empty());
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
#endif
    TEST_MAIN_RESULT();
}
