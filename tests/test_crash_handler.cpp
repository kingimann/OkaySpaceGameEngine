#include "test_framework.hpp"
#include "CrashHandler.hpp"
#include "CrashReports.hpp"
#include "CrashUpload.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

// The crash handler for real (shared/CrashHandler.hpp, the POSIX half): a child process installs the handler, fills the crash context
// like the editor does, then really faults. The parent checks how the child died and reads back the files the handler left, with the
// same parser the launcher uses. Windows has its own handler (compiled, not run, on this machine: see docs/online/crash_reports.md), so this
// test only runs where there are signals.
#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#include <pthread.h>

using namespace okaycrash;
namespace fs = std::filesystem;

// Faults, each in a function of its own with a name the backtrace can show (the test links with -rdynamic).
extern "C" __attribute__((noinline)) void CrashHereSegv() { int* bad = nullptr; asm volatile("" : "+r"(bad)); *bad = 1; asm volatile(""); }   // (the asm hides the null from the optimiser, which would otherwise drop the store)
extern "C" __attribute__((noinline)) void CrashHereFpe() { volatile int zero = 0; volatile int x = 10 / zero; (void)x; asm volatile(""); }
extern "C" __attribute__((noinline)) void CrashHereAbort() { std::abort(); }
extern "C" __attribute__((noinline)) void CrashHereThrow() { throw std::out_of_range("index 7 is out of range for C:\\Users\\alice\\data.bin"); }
extern "C" __attribute__((noinline)) int CrashHereRecurse(int n) { volatile char pad[512]; pad[0] = (char)n; if (pad[0] == 127 && n < 0) return 0; return CrashHereRecurse(n + 1) + pad[0]; }

static void* ThreadCrash(void*) { usleep(1500); CrashHereSegv(); return nullptr; }

enum class Kind { Segv, Fpe, Abort, Throw, Overflow, TwoThreads };

// The child: install, describe, fault. Never returns.
static void Child(Kind kind, const char* step, int lines) {
    handler::Install("9.9.9", "test build");
    ctx::SetGpu("Test GPU | Test Vendor | OpenGL 4.6");
    ctx::SetUiBackend("imgui");
    ctx::SetMode(true, false);
    ctx::SetSceneName("/home/bob/projects/space/level1.okayscene");
    ctx::SetSceneStats(77, 12);
    ctx::SetAutosave(0);
    for (int i = 0; i < 30; ++i) ctx::NextFrame();
    ctx::Breadcrumb("UI.Inspector");
    ctx::Breadcrumb(step);
    for (int i = 0; i < lines; ++i) ctx::ConsoleLine(i % 9 == 0 ? 1 : 0, ("console line " + std::to_string(i)).c_str());
    switch (kind) {
        case Kind::Segv: CrashHereSegv(); break;
        case Kind::Fpe: CrashHereFpe(); break;
        case Kind::Abort: CrashHereAbort(); break;
        case Kind::Throw: CrashHereThrow(); break;
        case Kind::Overflow: CrashHereRecurse(0); break;
        case Kind::TwoThreads: { pthread_t t; pthread_create(&t, nullptr, ThreadCrash, nullptr); usleep(1500); CrashHereSegv(); break; }   // two threads fault at about the same moment
    }
    _exit(0);   // not reached: the fault killed the process
}

// Run a child that faults; return how it ended (the signal number, 0 if it exited) and leave the reports in `dir`.
static int RunChild(const fs::path& dir, Kind kind, const char* step, int lines = 5) {
    setenv("OKAY_CRASH_DIR", dir.string().c_str(), 1);
    setenv("OKAY_OWN_FBX", "1", 1);
    setenv("OKAY_ACCOUNT_API_KEY", "sb_secret_must_never_appear_0123456789", 1);
    std::fflush(nullptr);
    const pid_t pid = fork();
    if (pid == 0) {
        if (FILE* quiet = std::freopen("/dev/null", "w", stderr)) (void)quiet;   // the handler says one line on stderr; keep the test log clean
        Child(kind, step, lines);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFSIGNALED(status) ? WTERMSIG(status) : 0;
}

static std::string Slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}

static std::string ExeBase() {
    std::error_code ec;
    return fs::read_symlink("/proc/self/exe", ec).filename().string();
}

int main() {
    RUN_SUITE("crash_handler");
    const fs::path base = fs::temp_directory_path() / ("okay_crash_handler_test_" + std::to_string((long)getpid()));
    fs::remove_all(base); fs::create_directories(base);
    const std::string exe = ExeBase();

    // ---- A segmentation fault: the whole detailed report -----------------------------------------------------------------------------
    const fs::path d1 = base / "segv"; fs::create_directories(d1);
    CHECK(RunChild(d1, Kind::Segv, "Render3D:gl", 300) == SIGSEGV);   // the handler re-raises: the program still dies of the signal
    CHECK(fs::exists(d1 / "okay_crash.txt") && fs::exists(d1 / "okay_crashlog.txt"));
    const std::string text = Slurp(d1 / "okay_crash.txt");
    const okaycrash::Report r = okaycrash::ParseOne(text);
    CHECK(r.version == "9.9.9" && r.build.find("built test build") != std::string::npos);
    CHECK(r.exception == "SIGSEGV (signal 11)" && okaycrash::ExceptionName(r.exception) == "Access violation");
    CHECK(r.lastStep == "Render3D:gl" && !r.when.empty() && r.when.size() == 19);
    CHECK(!r.os.empty() && r.os.find("Linux") != std::string::npos);
    CHECK(!r.Info("CPU").empty() && r.Info("CPU").find("logical") != std::string::npos);
    CHECK(r.Info("RAM").find("MB total") != std::string::npos);
    CHECK(r.Info("Process memory").find("MB in use") != std::string::npos);
    CHECK(r.Info("GPU") == "Test GPU | Test Vendor | OpenGL 4.6" && r.Info("UI backend") == "imgui");
    CHECK(r.Info("Mode") == "play" && r.Info("Scene") == "level1.okayscene" && r.Info("Objects") == "77" && r.Info("Assets loaded") == "12");
    CHECK(r.Info("Recovery copy") == "none" && r.Info("Uptime").find("30 frames") != std::string::npos);
    CHECK(r.Info("Own code").find("OKAY_OWN_FBX=1") != std::string::npos);
    CHECK(text.find("sb_secret_must_never_appear") == std::string::npos && text.find("OKAY_ACCOUNT_API_KEY") == std::string::npos);   // keys never reach a report
    CHECK(text.find("/home/bob") == std::string::npos);   // the scene's folder is not in it
    CHECK(r.frames >= 3);
    CHECK(TopFrame(r) == exe + "!CrashHereSegv");         // the fault is the first frame, not the handler or the signal trampoline
    CHECK(r.breadcrumbs.size() == 2 && r.breadcrumbs[0] == "frame 30: UI.Inspector" && r.breadcrumbs[1] == "frame 30: Render3D:gl");
    CHECK(r.console.size() == 200);                       // 300 lines logged, the last 200 kept
    CHECK(r.console.front().find("console line 100") != std::string::npos && r.console.back().find("console line 299") != std::string::npos);
    CHECK(r.console.back().find("[-0s] I ") == 0 || r.console.back().find("[-1s] I ") == 0);
    CHECK(r.signature.size() == 16);
    // The history entry is the same report, behind its marker, and the launcher's loader sees exactly one crash.
    const std::string hist = Slurp(d1 / "okay_crashlog.txt");
    CHECK(hist.find("========== crash @ " + r.when + " (v9.9.9) ==========") != std::string::npos);
    const auto loaded = okaycrash::Load(d1);
    CHECK(loaded.size() == 1 && loaded[0].signature == r.signature && loaded[0].frames == r.frames);
    CHECK(loaded[0].console.size() == 200);

    // What the launcher would send for this real report: scrubbed (the program's folder stands in for the user's home), within 64 KB, valid columns.
    {
        const std::string exeDir = fs::path(r.Info("Exe")).parent_path().string();
        okaycrash::ScrubContext sc; sc.homeDirs = {exeDir};
        sc.usernames = {"bob"};
        CHECK(exeDir.size() >= 4 && text.find(exeDir) != std::string::npos);   // the raw report has the folder ...
        const okaycrash::upload::Payload pl = okaycrash::upload::BuildPayload(r, sc);
        CHECK(pl.ok && pl.report.find(exeDir) == std::string::npos && pl.report.find("Exe: ~") == 0 + pl.report.find("Exe: ~") && pl.report.find("Exe: ~") != std::string::npos);   // ... the row does not
        CHECK(pl.json.size() <= okaycrash::upload::kMaxBodyBytes && pl.signature == r.signature && pl.topFrame == exe + "!CrashHereSegv" && pl.lastStep == "Render3D:gl");
    }

    // The same crash again, from another step: same signature, and the history now has two entries.
    CHECK(RunChild(d1, Kind::Segv, "Play.Tick", 3) == SIGSEGV);
    const auto two = okaycrash::Load(d1);
    CHECK(two.size() == 2 && two[0].lastStep == "Play.Tick" && two[0].signature == r.signature);
    // Another fault: another signature.
    const fs::path d2 = base / "fpe"; fs::create_directories(d2);
    CHECK(RunChild(d2, Kind::Fpe, "Physics.Step") == SIGFPE);
    const auto fpe = okaycrash::Load(d2);
    CHECK(fpe.size() == 1 && fpe[0].exception == "SIGFPE (signal 8)" && fpe[0].signature != r.signature && TopFrame(fpe[0]) == exe + "!CrashHereFpe");

    // ---- abort(): reported once, whatever calls it -----------------------------------------------------------------------------------
    const fs::path d3 = base / "abort"; fs::create_directories(d3);
    CHECK(RunChild(d3, Kind::Abort, "Assets.Load") == SIGABRT);
    const auto ab = okaycrash::Load(d3);
    CHECK(ab.size() == 1 && ab[0].exception == "SIGABRT (signal 6)" && TopFrame(ab[0]) == exe + "!CrashHereAbort");

    // ---- An uncaught C++ exception: the type and the message are in the report, and there is ONE report (not terminate + SIGABRT) ----
    const fs::path d4 = base / "throw"; fs::create_directories(d4);
    CHECK(RunChild(d4, Kind::Throw, "Scene.Load") == SIGABRT);
    const auto th = okaycrash::Load(d4);
    CHECK(th.size() == 1);
    if (th.size() == 1) {
        CHECK(th[0].exception.find("C++ exception (std::terminate): std::out_of_range: index 7 is out of range") == 0);
        CHECK(okaycrash::ExceptionName(th[0].exception) == "Unhandled C++ exception");
        CHECK(th[0].frames >= 2 && th[0].lastStep == "Scene.Load");
        CHECK(TopFrame(th[0]) == exe + "!CrashHereThrow");   // the machinery between terminate and the throwing function is skipped
    }

    // ---- A stack overflow: the handler runs on its own stack and still writes the report -------------------------------------------
    const fs::path d5 = base / "overflow"; fs::create_directories(d5);
    CHECK(RunChild(d5, Kind::Overflow, "Hierarchy.Recurse") == SIGSEGV);
    const auto so = okaycrash::Load(d5);
    CHECK(so.size() == 1 && so[0].exception == "SIGSEGV (signal 11)" && so[0].lastStep == "Hierarchy.Recurse" && TopFrame(so[0]) == exe + "!CrashHereRecurse");

    // ---- Two threads fault at once: ONE complete report (the second thread waits for the first, it does not cut it short) ----------------
    const fs::path d6 = base / "twothreads"; fs::create_directories(d6);
    CHECK(RunChild(d6, Kind::TwoThreads, "Physics.Step") == SIGSEGV);
    const auto tt = okaycrash::Load(d6);
    CHECK(tt.size() == 1);
    if (tt.size() == 1) {
        CHECK(tt[0].text.find("--- End of report ---") != std::string::npos && tt[0].frames >= 3 && TopFrame(tt[0]) == exe + "!CrashHereSegv");
        CHECK(tt[0].console.size() == 5);
    }

    // ---- The history does not grow for ever: at startup an oversized file is cut to its newest reports, at a report boundary -----------------
    {
        const fs::path dir = base / "trim"; fs::create_directories(dir);
        const fs::path hist = dir / "okay_crashlog.txt";
        std::string entry;
        for (int i = 0; i < 120; ++i) {
            entry = "\n========== crash @ 2026-11-15 10:" + std::string(i < 10 ? "0" : "") + std::to_string(i % 60) + ":00 (v1.0." + std::to_string(i) + ") ==========\nOkaySpace crash report\nVersion: 1.0." + std::to_string(i) +
                    "\nBacktrace:\n" + std::string(100 * 1024, 'x') + "\n\n";   // 120 reports of 100 KB = 12 MB
            std::ofstream(hist, std::ios::binary | std::ios::app) << entry;
        }
        CHECK(fs::file_size(hist) > (8u << 20));
        handler::TrimHistory(hist);
        const auto size = fs::file_size(hist);
        CHECK(size < (5u << 20) && size > (3u << 20));                 // about the newest 4 MB
        const std::string kept = Slurp(hist);
        const std::string boundary = "\n========== crash @ 2026-11";
        CHECK(kept.compare(0, boundary.size(), boundary) == 0);   // it starts at a report boundary
        const auto reports = okaycrash::ParseLog(kept);
        CHECK(!reports.empty() && reports.front().version == "1.0.119" && reports.back().version.size() >= 5);   // newest first; the newest report is whole
        CHECK(!fs::exists(dir / "okay_crashlog.txt.tmp"));
        { struct stat st; CHECK(::stat(hist.c_str(), &st) == 0 && (st.st_mode & 0077) == 0); }   // the trimmed copy is private too
        const std::string before = Slurp(hist);
        handler::TrimHistory(hist);                                    // a small file is left alone
        CHECK(Slurp(hist) == before);
        handler::TrimHistory(dir / "no_such_file.txt");                // a missing file is not an error
        std::ofstream(dir / "noboundary.txt", std::ios::binary) << std::string(9u << 20, 'z');
        handler::TrimHistory(dir / "noboundary.txt");                  // no report boundary to cut at: left alone
        CHECK(fs::file_size(dir / "noboundary.txt") == (9u << 20));
    }

    // ---- The report files are private (0600), and a symbolic link planted where a file goes is not followed -----------------------------
    {
        const fs::path dir = base / "private"; fs::create_directories(dir);
        CHECK(RunChild(dir, Kind::Segv, "Render3D:gl", 5) == SIGSEGV);
        struct stat st;
        CHECK(::stat((dir / "okay_crash.txt").c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);        // the report holds the scene's name and the Console: not for other users
        CHECK(::stat((dir / "okay_crashlog.txt").c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
        // files an older version made world-readable become private when the next crash is written
        ::chmod((dir / "okay_crash.txt").c_str(), 0644); ::chmod((dir / "okay_crashlog.txt").c_str(), 0644);
        CHECK(RunChild(dir, Kind::Segv, "Play.Tick", 5) == SIGSEGV);
        CHECK(::stat((dir / "okay_crash.txt").c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
        CHECK(::stat((dir / "okay_crashlog.txt").c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
        CHECK(okaycrash::Load(dir).size() == 2);
    }
    {
        const fs::path dir = base / "symlink"; fs::create_directories(dir);
        const fs::path victim = dir / "victim.txt", victim2 = dir / "victim2.txt";
        std::ofstream(victim) << "precious"; std::ofstream(victim2) << "also precious";
        fs::create_symlink(victim, dir / "okay_crash.txt");           // somebody planted links where the handler writes
        fs::create_symlink(victim2, dir / "okay_crashlog.txt");
        CHECK(RunChild(dir, Kind::Segv, "Render3D:gl", 5) == SIGSEGV);
        CHECK(Slurp(victim) == "precious" && Slurp(victim2) == "also precious");                         // nothing was written through them
        CHECK(!fs::is_symlink(dir / "okay_crash.txt") && Slurp(dir / "okay_crash.txt").find("OkaySpace crash report") == 0);   // and the report was still written
        CHECK(!fs::is_symlink(dir / "okay_crashlog.txt") && okaycrash::Load(dir).size() == 1);
    }

    // ---- The history file is replaced from where the first pass started, and never from 0 by accident (Windows second pass) ----------------
    CHECK(!handler::CanReplaceHistory(handler::kNoHistoryStart) && handler::CanReplaceHistory(0) && handler::CanReplaceHistory(4096));
    CHECK(handler::kNoHistoryStart == (unsigned long long)-1);

    // ---- A folder that does not exist: the report goes beside the program instead of being lost (here: nothing to check but no crash) ----
    // (OKAY_CRASH_DIR that is not a directory is ignored: covered by the dir check in Install.)

    fs::remove_all(base);
    TEST_MAIN_RESULT();
}
#else
int main() {
    RUN_SUITE("crash_handler");
    std::cout << "skipped: no POSIX signals on this platform\n";
    TEST_MAIN_RESULT();
}
#endif
