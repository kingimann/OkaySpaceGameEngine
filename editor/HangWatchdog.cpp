// The hang watchdog's thread (editor/HangWatchdog.hpp has the rules and the texts). It looks at the editor's heartbeat four times a
// second; it only reads atomics and fixed buffers the main thread writes (okaycrash::ctx, okay::Profiler::LiveScopes), and it takes
// no lock the main thread could be holding while it is stuck, except the heap's when it builds its texts (a main thread stuck inside
// the allocator is not a case we can report anyway).
//
// Windows: the main thread's stack is read the way the crash handler reads a crashed thread's (shared/CrashHandler.hpp, Unwind and
// ModuleOf): the thread is suspended, its registers taken, the return addresses walked into a fixed array, the thread resumed, and only
// then are the addresses turned into "module+offset" text. The question is a MessageBox on this thread (it has its own message loop).
// Linux and macOS: the log line and the report only.
//
// Threads only through okay::thr (shared/OkayThreads.hpp; CLAUDE.md rule 21a). Written from the Win32 documentation (SuspendThread,
// GetThreadContext, MessageBoxW, TerminateProcess); nothing copied.
#include "HangWatchdog.hpp"

#include "AppRestart.hpp"      // okay::restart::AppendLog, CurrentExecutable: the same writer the slow-frame log uses
#include "CrashContext.hpp"    // the heartbeat (frame counter), the breadcrumbs, the scene name, PersonWait
#include "CrashHandler.hpp"    // the report folder and the main thread (handler::g_cfg); Windows: Unwind, ModuleOf
#include "OkayThreads.hpp"
#include "okay/Core/Paths.hpp"     // LogDir: where the hang report and the perf log go
#include "okay/Core/Profiler.hpp"

#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace okay::editor::hang {
namespace {

namespace fs = std::filesystem;

struct Shared {
    okay::thr::AtomicInt stop;
    Settings settings;
    fs::path perfLog, report;
    std::string version;
    unsigned long mainThread = 0;
};
Shared* g_shared = nullptr;
okay::thr::Thread* g_thread = nullptr;

double NowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::vector<std::string> Scopes() {
    const char* names[okay::Profiler::kLiveDepth] = {};
    const int n = okay::Prof().LiveScopes(names, okay::Profiler::kLiveDepth);
    std::vector<std::string> out;
    for (int i = 0; i < n; ++i) {
        // A literal's pointer; copied with a bound in case a name is long.
        char buf[96] = {};
        if (names[i]) std::strncpy(buf, names[i], sizeof(buf) - 1);
        out.emplace_back(buf[0] ? buf : "?");
    }
    return out;
}

std::vector<std::string> Crumbs(std::size_t keep) {
    using namespace okaycrash::ctx;
    std::vector<std::string> out;
    const std::uint32_t next = g_state.crumbNext.load(std::memory_order_relaxed);
    const std::uint32_t have = next < (std::uint32_t)kCrumbSlots ? next : (std::uint32_t)kCrumbSlots;
    const std::uint32_t from = next - (have < keep ? have : (std::uint32_t)keep);
    for (std::uint32_t i = from; i < next; ++i) {
        const Crumb& c = g_state.crumbs[i % kCrumbSlots];
        char text[kCrumbLen + 1] = {};
        std::memcpy(text, c.text, kCrumbLen);
        char line[kCrumbLen + 48];
        std::snprintf(line, sizeof(line), "frame %u: %s%s", c.frame, text, c.repeat > 1 ? " (repeated)" : "");
        out.emplace_back(line);
    }
    return out;
}

std::string LastStep() {
    char text[okaycrash::ctx::kCrumbLen + 1] = {};
    std::strncpy(text, okaycrash::ctx::LastStep(), okaycrash::ctx::kCrumbLen);
    return text;
}

std::string WhenNow() {
    const std::time_t t = std::time(nullptr);
    char buf[40] = {};
    if (std::tm* g = std::gmtime(&t)) std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", g);
    return buf;
}

#if defined(_WIN32)
// The main thread's return addresses, newest first, as "module+0xoffset".
std::vector<std::string> MainStack(unsigned long tid) {
    std::vector<std::string> out;
    if (!tid) return out;
    HANDLE th = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, (DWORD)tid);
    if (!th) return out;
    unsigned long long pcs[48] = {};
    std::size_t n = 0;
    if (SuspendThread(th) != (DWORD)-1) {
        CONTEXT c;
        std::memset(&c, 0, sizeof(c));
        c.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(th, &c)) n = okaycrash::handler::win::Unwind(&c, pcs, 48, 0);
        ResumeThread(th);
    }
    CloseHandle(th);
    for (std::size_t i = 0; i < n; ++i) {
        char name[MAX_PATH] = {};
        unsigned long long base = 0;
        okaycrash::handler::win::ModuleOf(pcs[i], name, sizeof(name), base);
        char line[MAX_PATH + 48];
        std::snprintf(line, sizeof(line), "%s+0x%llx", name[0] ? name : "?", base ? pcs[i] - base : pcs[i]);
        out.emplace_back(line);
    }
    return out;
}
// Yes = wait, No = close. On top of other windows: the editor's own window does not answer.
bool AskWait(const std::string& text) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? (std::size_t)n : 1u, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &w[0], n);
    const int r = MessageBoxW(nullptr, w.c_str(), L"OkaySpace Editor is not responding", MB_YESNO | MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND | MB_TASKMODAL);
    return r != IDNO;
}
[[noreturn]] void CloseNow() { TerminateProcess(GetCurrentProcess(), (UINT)kClosedExitCode); for (;;) Sleep(1000); }
void SleepMs(int ms) { Sleep((DWORD)ms); }
#else
std::vector<std::string> MainStack(unsigned long) { return {}; }
bool AskWait(const std::string&) { return true; }   // no native question here: the report is written, the editor is left alone
[[noreturn]] void CloseNow() { std::_Exit(kClosedExitCode); }
void SleepMs(int ms) {
    timespec ts; ts.tv_sec = ms / 1000; ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    while (nanosleep(&ts, &ts) != 0) {}
}
#endif

int Run(void* p) {
    Shared& s = *static_cast<Shared*>(p);
    Tracker t;
    std::string stuckStage;   // where the last report found it (the Recovered line names that, not where it is now)
    while (!okay::thr::AtomicGet(&s.stop)) {
        SleepMs(250);
        if (okay::thr::AtomicGet(&s.stop)) break;
        const std::uint32_t beat = okaycrash::ctx::Frame();
        const double now = NowSeconds();
        const Action a = t.Step(beat, now, okaycrash::ctx::WaitingOnPerson(), s.settings);
        if (a == Action::None) continue;
        const std::vector<std::string> scopes = Scopes();
        const std::string stage = a == Action::Recovered && !stuckStage.empty() ? stuckStage : StageText(scopes, LastStep());
        if (a == Action::Report) stuckStage = stage;
        okay::restart::AppendLog(s.perfLog, LogLine(a, t.stuckFor, beat, stage));
        if (a == Action::Report) {
            char scene[sizeof(okaycrash::ctx::g_state.scene) + 1] = {};
            std::memcpy(scene, okaycrash::ctx::g_state.scene, sizeof(okaycrash::ctx::g_state.scene));
            const std::string text = ReportText(s.version, WhenNow(), t.stuckFor, beat, stage, scopes, Crumbs(16), scene, MainStack(s.mainThread));
            std::ofstream(s.report, std::ios::binary | std::ios::trunc) << text;
        } else if (a == Action::Ask) {
            if (AskWait(AskText(t.stuckFor, stage))) {
                t.Waited(NowSeconds(), s.settings);
                okay::restart::AppendLog(s.perfLog, "hang: the person chose Wait");
            } else {
                okay::restart::AppendLog(s.perfLog, "hang: the person chose Close; the editor ends with exit code " + std::to_string(kClosedExitCode));
                CloseNow();
            }
        }
    }
    return 0;
}

}  // namespace

bool Start(const char* version) {
    if (g_thread) return true;
    const Settings st = Settings::From(std::getenv("OKAY_HANG_REPORT_S"), std::getenv("OKAY_HANG_ASK_S"));
    if (st.reportSeconds <= 0.0 && st.askSeconds <= 0.0) return false;
    Shared* s = new Shared();
    s->settings = st;
    s->version = version ? version : "";
    s->mainThread = okaycrash::handler::g_cfg.mainThread;
    fs::path self = okay::restart::CurrentExecutable();
    // The person's log folder (%LOCALAPPDATA%/OkaySpace/logs on Windows), not beside the program (docs/engine/project_layout.md).
    const std::string logs = okay::Paths::LogDir();
    const bool haveLogs = !logs.empty() && okay::Paths::EnsureDir(logs);
    s->perfLog = haveLogs ? fs::u8path(logs) / "okay_editor_perf.log" : self.empty() ? fs::path("okay_editor_perf.log") : self.parent_path() / "okay_editor_perf.log";
#if defined(_WIN32)
    const fs::path dir = okaycrash::handler::g_cfg.dirW[0] ? fs::path(okaycrash::handler::g_cfg.dirW) : (self.empty() ? fs::path() : self.parent_path());
#else
    const fs::path dir = okaycrash::handler::g_cfg.dir[0] ? fs::path(okaycrash::handler::g_cfg.dir) : (self.empty() ? fs::path() : self.parent_path());
#endif
    s->report = haveLogs ? fs::u8path(logs) / "okay_hang.txt" : dir / "okay_hang.txt";
    g_thread = okay::thr::CreateThread(&Run, "okay-hang-watchdog", s);
    if (!g_thread) { delete s; return false; }
    g_shared = s;
    return true;
}

void Stop() {
    if (!g_thread) return;
    okay::thr::AtomicSet(&g_shared->stop, 1);
    okay::thr::WaitThread(g_thread, nullptr);   // at most one 250 ms sleep (a question on screen: the person answers it first)
    g_thread = nullptr;
    delete g_shared;
    g_shared = nullptr;
}

}  // namespace okay::editor::hang
