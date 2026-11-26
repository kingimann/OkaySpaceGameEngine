// okay run inside another program (tools/okay-cli/CommandJob.hpp, the editor's Command Line panel): one command given as
// words gives its lines and its exit code without printing or exiting; the project folder is the working folder (okay's
// --project); a command on a worker thread streams its lines and can be stopped, also while a program it started runs
// (Process.hpp AttachedOutput: a fake git that would sleep for 30 s); git is told to use the okay program beside the host,
// never the host itself. Written for this repository.
#include "test_framework.hpp"

#include "Cli.hpp"
#include "CliInternal.hpp"   // StableOkayPath
#include "CommandInput.hpp"
#include "CommandJob.hpp"
#include "Process.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>   // Sleep
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using okaycli::OutLine;

namespace {

bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

std::string Joined(const std::vector<OutLine>& lines, OutLine::Kind kind) {
    std::string t;
    for (const OutLine& l : lines) if (l.kind == kind) t += l.text + "\n";
    return t;
}

void SetEnv(const char* k, const std::string& v) {
#if defined(_WIN32)
    _putenv_s(k, v.c_str());
#else
    setenv(k, v.c_str(), 1);
#endif
}
void UnsetEnv(const char* k) {
#if defined(_WIN32)
    _putenv_s(k, "");
#else
    unsetenv(k);
#endif
}

// Poll a job until it ends (or `limitMs` passes). Returns true when it ended.
bool WaitJob(okaycli::CommandJob& job, std::vector<OutLine>& lines, int& code, bool& stopped, int limitMs = 20000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(limitMs);
    while (std::chrono::steady_clock::now() < end) {
        if (job.Poll(lines, code, stopped)) return true;
#if defined(_WIN32)
        Sleep(5);
#else
        usleep(5000);
#endif
    }
    return false;
}

void TestRunInProcess(const fs::path& tmp) {
    RUN_SUITE("okay inside a program: words in, lines and exit codes out");
    {   // help: normal lines, exit 0, nothing on the real console (the words come back as lines)
        const okaycli::InProcessResult r = okaycli::RunInProcess({"help", "vc"}, "");
        CHECK(r.exitCode == 0);
        CHECK(!r.stopped);
        CHECK(!r.lines.empty());
        CHECK(Has(Joined(r.lines, OutLine::Out), "vc save"));
        CHECK(Joined(r.lines, OutLine::Err).empty());
    }
    {   // an unknown command: a problem line and exit code 2, and the program goes on
        const okaycli::InProcessResult r = okaycli::RunInProcess({"frobnicate"}, "");
        CHECK(r.exitCode == 2);
        CHECK(Has(Joined(r.lines, OutLine::Err), "There is no command called \"frobnicate\""));
    }
    {   // a command that does not work: exit code 1
        const okaycli::InProcessResult r = okaycli::RunInProcess({"play"}, tmp.string());
        CHECK(r.exitCode == 1);
        CHECK(Has(Joined(r.lines, OutLine::Err), "is not an OkaySpace project"));
    }
    // The folder is the working folder: `scene list` with no --project lists the folder's project.
    const fs::path proj = tmp / "RunGame";
    std::string err;
    CHECK(okaycli::CreateEmptyProject(proj.string(), "RunGame", "", err));
    {
        const okaycli::InProcessResult r = okaycli::RunInProcess({"scene", "list"}, proj.string());
        CHECK(r.exitCode == 0);
        CHECK(Has(Joined(r.lines, OutLine::Out), "Assets/RunGame.okayscene"));
    }
    {   // the words may name the project themselves; then the folder is not added
        CHECK(okaycli::ArgsInFolder({"--project", "X", "info"}, "F") == std::vector<std::string>({"--project", "X", "info"}));
        CHECK(okaycli::ArgsInFolder({"info"}, "F") == std::vector<std::string>({"--project", "F", "info"}));
        CHECK(okaycli::ArgsInFolder({"info"}, "") == std::vector<std::string>({"info"}));
    }
    {   // a stop that was asked for before the start: nothing runs past the first message
        std::atomic<bool> stop{true};
        const okaycli::InProcessResult r = okaycli::RunInProcess({"help"}, "", &stop);
        CHECK(r.stopped);
        CHECK(r.exitCode == 130);
        CHECK(r.lines.empty());
    }
    {   // Outside RunInProcess git's merge driver is this program (the okay program itself); inside, the okay program
        // beside this one, or none: never the host. (No okay program sits beside the test, and none is on PATH here.)
        const std::string saved = std::getenv("PATH") ? std::getenv("PATH") : "";
        const std::string realGit = okaycli::FindOnPath("git");
        if (!realGit.empty()) SetEnv("OKAY_GIT", realGit);   // git by its full path: PATH is emptied next
        SetEnv("PATH", (tmp / "empty-bin").string());
        CHECK(okaycli::detail::StableOkayPath() == okaycli::SelfPath());
        CHECK(!okaycli::detail::RunningInsideHost());
        const okaycli::InProcessResult r = okaycli::RunInProcess({"vc", "init"}, proj.string());
        SetEnv("PATH", saved);
        UnsetEnv("OKAY_GIT");
        if (realGit.empty()) {
            std::cout << "  (git is not installed: the vc init check is skipped)\n";
        } else {
            CHECK(r.exitCode == 1);
            CHECK(Has(Joined(r.lines, OutLine::Err), "Couldn't find the okay program's path"));
        }
    }
}

#if !defined(_WIN32)
void TestJob(const fs::path& tmp) {
    RUN_SUITE("okay on a worker thread: lines stream, Stop ends it, also inside a started program");
    {   // a quick command: its lines and Done
        okaycli::CommandJob job;
        CHECK(job.Start({"help"}, ""));
        CHECK(job.Running());
        CHECK(!job.Start({"help"}, ""));   // one at a time
        std::vector<OutLine> lines;
        int code = -1;
        bool stopped = true;
        CHECK(WaitJob(job, lines, code, stopped));
        CHECK(code == 0 && !stopped);
        CHECK(!job.Running());
        CHECK(Has(Joined(lines, OutLine::Out), "okay "));
    }
    // A fake git that answers --version and then, for clone, says one line and sleeps 30 s: `okay vc clone` runs it as an
    // attached program, so its line arrives while it runs and Stop ends it long before the 30 s.
    const fs::path bin = tmp / "fakebin";
    fs::create_directories(bin);
    const fs::path git = bin / "git";
    {
        std::ofstream f(git);
        f << "#!/bin/sh\nif [ \"$1\" = --version ]; then echo 'git version 2.40.0'; exit 0; fi\n"
             "echo 'cloning slowly'\nsleep 30\necho 'never'\n";
    }
    ::chmod(git.c_str(), 0755);
    SetEnv("OKAY_GIT", git.string());
    {
        okaycli::CommandJob job;
        const auto t0 = std::chrono::steady_clock::now();
        CHECK(job.Start({"vc", "clone", "https://example.invalid/repo.git", (tmp / "clone-target").string()}, ""));
        std::vector<OutLine> lines;
        int code = -1;
        bool stopped = false;
        bool sawLine = false;
        while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(10)) {
            job.Poll(lines, code, stopped);
            for (const OutLine& l : lines) if (l.text == "cloning slowly") sawLine = true;
            if (sawLine) break;
            usleep(5000);
        }
        CHECK(sawLine);           // streamed while the program still runs
        CHECK(job.Running());
        job.Stop();
        const bool ended = WaitJob(job, lines, code, stopped, 10000);
        CHECK(ended);
        CHECK(stopped);
        CHECK(code == 130);
        CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(20));   // the fake git alone takes 30 s
        CHECK(!Has(Joined(lines, OutLine::Out), "never"));
    }
    {   // the hook is only on the thread that runs the command, and only while it runs
        CHECK(okaycli::GetAttachedOutput() == nullptr);
    }
    {   // dropping a job that still runs stops it; the program goes on
        auto* job = new okaycli::CommandJob();
        CHECK(job->Start({"vc", "clone", "https://example.invalid/repo.git", (tmp / "clone-target2").string()}, ""));
        usleep(50000);
        delete job;   // the detached thread ends on its own
        okaycli::CommandJob next;   // and the program goes on: another command runs at once
        CHECK(next.Start({"help"}, ""));
        std::vector<OutLine> lines;
        int code = -1;
        bool stopped = true;
        CHECK(WaitJob(next, lines, code, stopped) && code == 0);
    }
    UnsetEnv("OKAY_GIT");
}
#endif

void TestOutputLog() {
    RUN_SUITE("output lines: partial lines, the cap and the joined text");
    okaycli::OutputLog log(3);
    std::string part;
    log.Feed(part, "one\ntw", OutLine::Out, false);
    CHECK(log.Lines().size() == 1 && part == "tw");
    log.Feed(part, "o\nthree\n\x1b[31mfour\x1b[0m\r\n", OutLine::Out, false);
    CHECK(log.Lines().size() == 3);
    CHECK(log.Text() == "two\nthree\nfour");   // the oldest went, colour codes and \r are gone
    log.Add("five", OutLine::Err);
    CHECK(log.Text() == "three\nfour\nfive");
    CHECK(log.Lines().back().kind == OutLine::Err);
    const unsigned rev = log.Revision();
    log.Clear();
    CHECK(log.Lines().empty() && log.Text().empty() && log.Revision() != rev);
}

} // namespace

int main() {
    std::error_code ec;
    const fs::path tmp = fs::temp_directory_path(ec) / ("okay_cli_run_test_" +
        std::to_string((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(tmp, ec);
    TestOutputLog();
    TestRunInProcess(tmp);
#if !defined(_WIN32)
    TestJob(tmp);
#endif
    fs::remove_all(tmp, ec);
    TEST_MAIN_RESULT();
}
