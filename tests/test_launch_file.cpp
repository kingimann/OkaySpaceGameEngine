// The editor's launcher (editor/LaunchFile.cpp) on Linux. A fake "xdg-open" and a fake "code" are put first (and alone) on PATH;
// each one writes the name it was started as and every argument it got to a file. That shows, without opening any real program:
//   * a file name reaches the program as ONE untouched argument, whatever characters it has ($(...), a backtick, quotes, ; & |, spaces),
//   * a name that starts with "-" is not read as an option,
//   * a program, installer, shortcut or script type (and an executable file with no extension) is refused before anything starts,
//     while the explicitly named text editor still opens a script for editing,
//   * a missing file, a folder, a missing program and a relative PATH entry are reported or ignored, never guessed at.
// The Windows half (ShellExecuteW) and macOS are not covered here: this test is only built where fork and execv are used.
// The programs are started by a grandchild, so the test waits (by polling the file, 5 s at most) for what it expects.
#include "test_framework.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(__linux__)
#include <sys/stat.h>
#include <unistd.h>
#include "../editor/LaunchFile.hpp"
#include "../editor/ProjectContextModel.hpp"

namespace fs = std::filesystem;
using oked::launch::Result;

namespace {

std::vector<std::string> ReadLines(const std::string& path) {
    std::vector<std::string> out;
    std::ifstream in(path, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) out.push_back(line);
    return out;
}

// The lines the fake programs have written beyond the `seen` that were already taken; waits until `want` of them are there.
std::vector<std::string> Next(const std::string& log, std::size_t& seen, std::size_t want) {
    std::vector<std::string> all;
    for (int i = 0; i < 250; ++i) {   // 250 x 20 ms
        all = ReadLines(log);
        if (all.size() >= seen + want) break;
        ::usleep(20000);
    }
    std::vector<std::string> mine;
    for (std::size_t i = seen; i < all.size() && i < seen + want; ++i) mine.push_back(all[i]);
    seen += mine.size();
    return mine;
}

// Nothing was started: the log is still as long as it was, after a moment that is long enough for a started program to write.
bool NothingStarted(const std::string& log, std::size_t seen) {
    ::usleep(250000);
    return ReadLines(log).size() == seen;
}

void WriteFile(const fs::path& p, const std::string& text, mode_t mode = 0644) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    { std::ofstream(p, std::ios::binary) << text; }
    ::chmod(p.c_str(), mode);
}

// A fake program: records "PROG:<its name>" and one "ARG:<argument>" line for every argument.
void FakeProgram(const fs::path& p) {
    WriteFile(p,
              "#!/bin/sh\n"
              "{\n"
              "  printf 'PROG:%s\\n' \"${0##*/}\"\n"
              "  for a in \"$@\"; do printf 'ARG:%s\\n' \"$a\"; done\n"
              "} >> \"$OKAY_TEST_LAUNCH_LOG\"\n",
              0755);
}

bool Contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

} // namespace

int main() {
    RUN_SUITE("launch_file");
    const fs::path here = fs::current_path();
    const fs::path root = fs::temp_directory_path() / ("okay_launch_" + std::to_string(::getpid()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "bin", ec);
    fs::create_directories(root / "Assets", ec);
    const std::string log = (root / "launch.log").generic_string();
    ::setenv("OKAY_TEST_LAUNCH_LOG", log.c_str(), 1);
    ::setenv("PATH", (root / "bin").c_str(), 1);   // only the fake programs
    FakeProgram(root / "bin" / "xdg-open");
    FakeProgram(root / "bin" / "code");
    std::size_t seen = 0;
    auto at = [&](const std::string& rel) { return (root / "Assets" / fs::u8path(rel)).generic_u8string(); };

    // ---- the default app: the name arrives as one argument, nothing reads it ----
    {
        const std::string weird = "we$ird `echo hi` $(touch PWNED) ;&|\"'  name.png";
        WriteFile(at(weird), "x");
        const Result r = oked::launch::OpenWithDefaultApp(at(weird));
        CHECK(r.ok && r.error.empty());
        const std::vector<std::string> got = Next(log, seen, 2);
        CHECK(got == (std::vector<std::string>{"PROG:xdg-open", "ARG:" + at(weird)}));
        CHECK(!fs::exists(root / "PWNED") && !fs::exists(root / "Assets" / "PWNED") && !fs::exists(here / "PWNED"));
    }
    // a name that starts with '-' is not an option
    {
        WriteFile(root / "Assets" / "-rf.png", "x");
        fs::current_path(root / "Assets");
        const Result r = oked::launch::OpenWithDefaultApp("-rf.png");
        fs::current_path(here);
        CHECK(r.ok);
        CHECK(Next(log, seen, 2) == (std::vector<std::string>{"PROG:xdg-open", "ARG:./-rf.png"}));
    }
    // Build Settings > Web > Run in browser: only a page on this computer, as one argument
    {
        CHECK(oked::launch::OpenLocalUrl("http://127.0.0.1:8080/").ok);
        CHECK(Next(log, seen, 2) == (std::vector<std::string>{"PROG:xdg-open", "ARG:http://127.0.0.1:8080/"}));
        for (const char* bad : {"https://example.com/", "file:///etc/passwd", "http://127.0.0.1:8080/ --new-window",
                                "http://127.0.0.1:8080/\"x", "http://localhost:8080/", "-http://127.0.0.1:1/", ""}) {
            CHECK(!oked::launch::OpenLocalUrl(bad).ok);
            CHECK(NothingStarted(log, seen));
        }
    }
    // a file with no extension and no execute bit is data; the same file marked executable is a program
    {
        WriteFile(root / "Assets" / "data", "x", 0644);
        CHECK(oked::launch::OpenWithDefaultApp(at("data")).ok);
        CHECK(Next(log, seen, 2).size() == 2);
        WriteFile(root / "Assets" / "prog", "x", 0755);
        const Result r = oked::launch::OpenWithDefaultApp(at("prog"));
        CHECK(!r.ok && Contains(r.error, "can run code"));
        CHECK(NothingStarted(log, seen));
    }

    // ---- types that run code are refused before anything starts ----
    for (const char* name : {"tool.bat", "tool.cmd", "tool.exe", "TOOL.EXE", "run.sh", "x.js", "x.jse", "x.vbs", "x.ps1", "setup.msi", "a.lnk", "x.hta",
                             "x.reg", "x.jar", "x.scr", "x.com", "script.py", "photo.png.exe", "x.exe.", "x.docm"}) {
        WriteFile(root / "Assets" / name, "x");
        const Result r = oked::launch::OpenWithDefaultApp(at(name));
        if (r.ok || !Contains(r.error, "can run code")) std::cerr << "  not refused: " << name << " (" << r.error << ")\n";
        CHECK(!r.ok && Contains(r.error, "can run code"));
    }
    CHECK(NothingStarted(log, seen));
    // what the menu uses to grey the item is the same rule
    CHECK(!okaypb::ctx::LaunchRefusal(at("x.js")).empty() && okaypb::ctx::LaunchRefusal(at("data")).empty());

    // ---- the text editor is named, so a script is edited and never run ----
    {
        const Result r = oked::launch::OpenInTextEditor(at("x.js"));
        CHECK(r.ok);
        CHECK(Next(log, seen, 2) == (std::vector<std::string>{"PROG:code", "ARG:" + at("x.js")}));
        const std::string weird = "a $(touch PWNED2) `b` 'c'.txt";
        WriteFile(at(weird), "x");
        CHECK(oked::launch::OpenInTextEditor(at(weird)).ok);
        CHECK(Next(log, seen, 2) == (std::vector<std::string>{"PROG:code", "ARG:" + at(weird)}));
        CHECK(!fs::exists(root / "PWNED2") && !fs::exists(root / "Assets" / "PWNED2"));
        // (no "xdg-open" for the editor: it is never asked what to do with the type)
        WriteFile(root / "Assets" / "-dash.txt", "x");
        fs::current_path(root / "Assets");
        CHECK(oked::launch::OpenInTextEditor("-dash.txt").ok);
        fs::current_path(here);
        CHECK(Next(log, seen, 2) == (std::vector<std::string>{"PROG:code", "ARG:./-dash.txt"}));
    }

    // ---- Show in the file manager ----
    {
        CHECK(oked::launch::Reveal(at("data"), true).ok);
        CHECK(Next(log, seen, 2) == (std::vector<std::string>{"PROG:xdg-open", "ARG:" + (root / "Assets").generic_u8string()}));   // (the folder that holds it)
        fs::create_directories(root / "Assets" / "Sub Folder", ec);
        CHECK(oked::launch::Reveal(at("Sub Folder"), false).ok);
        CHECK(Next(log, seen, 2) == (std::vector<std::string>{"PROG:xdg-open", "ARG:" + at("Sub Folder")}));
        // a program is only shown, never started, so Reveal does not refuse one
        CHECK(oked::launch::Reveal(at("tool.exe"), true).ok);
        CHECK(Next(log, seen, 2).size() == 2);
    }

    // ---- what can go wrong is said in words ----
    {
        const Result gone = oked::launch::OpenWithDefaultApp(at("not-there.png"));
        CHECK(!gone.ok && Contains(gone.error, "not there"));
        const Result goneEd = oked::launch::OpenInTextEditor(at("not-there.txt"));
        CHECK(!goneEd.ok && Contains(goneEd.error, "not there"));
        const Result folder = oked::launch::OpenWithDefaultApp(at("Sub Folder"));
        CHECK(!folder.ok && Contains(folder.error, "folder"));
        CHECK(!oked::launch::OpenWithDefaultApp("").ok && !oked::launch::OpenInTextEditor("").ok && !oked::launch::Reveal("", false).ok);
        CHECK(!oked::launch::Reveal(at("not-there.png"), true).ok);
        CHECK(NothingStarted(log, seen));
    }

    // ---- no program to start with: said, not guessed ----
    {
        fs::create_directories(root / "empty", ec);
        ::setenv("PATH", (root / "empty").c_str(), 1);
        const Result noOpen = oked::launch::OpenWithDefaultApp(at("data"));
        CHECK(!noOpen.ok && Contains(noOpen.error, "xdg-open"));
        const Result noEditor = oked::launch::OpenInTextEditor(at("data"));
        CHECK(!noEditor.ok && Contains(noEditor.error, "text editor"));
        // a relative PATH entry is not searched (it would be read from whatever folder the editor was started in, a project folder maybe)
        FakeProgram(root / "Assets" / "rel" / "xdg-open");
        ::setenv("PATH", "rel", 1);
        fs::current_path(root / "Assets");
        const Result rel = oked::launch::OpenWithDefaultApp(at("data"));
        fs::current_path(here);
        CHECK(!rel.ok);
        CHECK(NothingStarted(log, seen));
    }

    fs::current_path(here);
    fs::remove_all(root, ec);
    TEST_MAIN_RESULT();
}

#else  // not Linux: the launcher tests need fake programs on PATH and fork/execv

int main() {
    RUN_SUITE("launch_file");
    std::cout << "  (skipped: this test runs on Linux only)\n";
    TEST_MAIN_RESULT();
}

#endif
