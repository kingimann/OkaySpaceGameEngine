#pragma once
// What the editor knows about itself, kept where a crash handler can read it without
// allocating or locking (docs/online/crash_reports.md). Written from the plain facts the editor
// already has (version, OS, GPU, scene, Console lines, the trail of steps it took) and
// turned into the text of okay_crash.txt by Compose*() below.
//
//  * Everything lives in one static block (`g_state`), zero at load time, so the crash
//    handler never has to construct, allocate or take a lock. The writers are a few
//    bounded copies; the Console ring and the breadcrumb ring are lock free (an atomic
//    counter picks the slot, so a crash while another thread logs still reads whole
//    slots, at worst a line that is half new).
//  * The text is built by `Out`, a bounded writer with no stdio and no heap: usable
//    from a fatal-signal handler and from a Windows exception filter alike.
//  * Header only, standard C++17, no <mutex>/<thread> (CLAUDE.md rule 21a), so the
//    Windows editor, the launcher's tests and the tests here all include the same code.
//
// Written from the existing okay_crash.txt layout (shared/CrashReports.hpp parses it);
// the new lines are added before "Backtrace:" and the new sections after it, each section
// starting with a line "--- Name ---" so an older reader still sees a plain backtrace.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>

namespace okaycrash {
namespace ctx {

constexpr int kCrumbSlots   = 64;    // the last 64 distinct steps
constexpr int kCrumbLen     = 72;
constexpr int kConsoleSlots = 200;   // the last 200 Console lines
constexpr int kConsoleLen   = 232;   // longer lines are cut (the cut is marked)

struct Crumb { char text[kCrumbLen]; std::uint32_t frame; std::uint32_t repeat; };
struct ConsoleEntry { char text[kConsoleLen]; std::int64_t time; std::int32_t level; };

struct State {
    char version[32];
    char build[112];     // "pe-6553A1B2-5F2000, built Nov 15 2026 10:00:00"
    char os[160];
    char cpu[160];
    char gpu[256];
    char ui[32];         // "imgui" / "okayui"
    char ownCode[512];   // the startup stack line and the OKAY_* switches that are on
    char scene[112];     // the open scene's file name, never its folder
    long long ramTotalMb;
    int objects;         // -1 = unknown
    int assets;          // loaded asset count, -1 = unknown
    int playing;         // 1 = the game was running in the editor
    int prefab;          // 1 = a prefab was open
    int autosave;        // -1 unknown, 0 none, 1 an autosave / recovery copy exists
    std::int64_t startTime;
    std::atomic<std::uint32_t> frame;
    std::atomic<std::uint32_t> crumbNext;
    std::atomic<std::uint32_t> consoleNext;
    Crumb crumbs[kCrumbSlots];
    ConsoleEntry console[kConsoleSlots];
};

inline State g_state;   // C++17 inline variable: one block for the whole program, zero-initialised

// ---- Writers (the editor calls these; all bounded, none allocate) ---------------------------------------------------------------
// A single-line text field: control characters become spaces, the copy is cut to fit and always ends in a NUL.
inline void SetText(char* dst, std::size_t cap, const char* src) {
    if (!dst || cap == 0) return;
    std::size_t i = 0;
    if (src) for (; src[i] && i + 1 < cap; ++i) {
        const unsigned char c = (unsigned char)src[i];
        dst[i] = (c < 0x20 || c == 0x7F) ? ' ' : (char)c;
    }
    dst[i] = '\0';
}
inline void SetVersion(const char* s)  { SetText(g_state.version, sizeof(g_state.version), s); }
inline void SetBuild(const char* s)    { SetText(g_state.build, sizeof(g_state.build), s); }
inline void SetOs(const char* s)       { SetText(g_state.os, sizeof(g_state.os), s); }
inline void SetCpu(const char* s)      { SetText(g_state.cpu, sizeof(g_state.cpu), s); }
inline void SetGpu(const char* s)      { SetText(g_state.gpu, sizeof(g_state.gpu), s); }
inline void SetUiBackend(const char* s){ SetText(g_state.ui, sizeof(g_state.ui), s); }
inline void SetOwnCode(const char* s)  { SetText(g_state.ownCode, sizeof(g_state.ownCode), s); }
inline void SetRamTotalMb(long long mb){ g_state.ramTotalMb = mb; }
inline void StartClock()               { if (!g_state.startTime) g_state.startTime = (std::int64_t)std::time(nullptr); }
inline void NextFrame()                { g_state.frame.fetch_add(1, std::memory_order_relaxed); }
inline std::uint32_t Frame()           { return g_state.frame.load(std::memory_order_relaxed); }
inline void SetMode(bool playing, bool prefab) { g_state.playing = playing ? 1 : 0; g_state.prefab = prefab ? 1 : 0; }
inline void SetAutosave(int v)         { g_state.autosave = v; }
inline void SetSceneStats(int objects, int assets) { g_state.objects = objects; g_state.assets = assets; }

// The open scene: the file name only. A full path would carry the user's folders (and name).
inline void SetSceneName(const char* path) {
    if (!path || !*path) { g_state.scene[0] = '\0'; return; }
    const char* name = path;
    for (const char* p = path; *p; ++p) if (*p == '/' || *p == '\\') name = p + 1;
    SetText(g_state.scene, sizeof(g_state.scene), name);
}

// One step of what the editor is doing ("UI.Inspector", "Render3D:gl"). The same step twice in a row only raises a counter,
// so 64 slots hold the last 64 DIFFERENT steps, not one frame's worth.
inline void Breadcrumb(const char* step) {
    if (!step) return;
    const std::uint32_t next = g_state.crumbNext.load(std::memory_order_relaxed);
    if (next > 0) {
        Crumb& last = g_state.crumbs[(next - 1) % kCrumbSlots];
        if (std::strncmp(last.text, step, kCrumbLen - 1) == 0 && last.text[0] != '\0') {
            if (last.repeat < 0xFFFFFFF0u) ++last.repeat;
            last.frame = g_state.frame.load(std::memory_order_relaxed);
            return;
        }
    }
    const std::uint32_t slot = g_state.crumbNext.fetch_add(1, std::memory_order_relaxed) % kCrumbSlots;
    Crumb& c = g_state.crumbs[slot];
    SetText(c.text, kCrumbLen, step);
    c.frame = g_state.frame.load(std::memory_order_relaxed);
    c.repeat = 1;
}
// The newest step ("startup" before the first one); the pointer is into the static block.
inline const char* LastStep() {
    const std::uint32_t next = g_state.crumbNext.load(std::memory_order_relaxed);
    if (next == 0) return "startup";
    const char* t = g_state.crumbs[(next - 1) % kCrumbSlots].text;
    return *t ? t : "startup";
}

// One Console line: 0 = info, 1 = warning, 2 = error. Kept as it was logged (cut at 231 bytes; a cut is marked with "...").
inline void ConsoleLine(int level, const char* text, std::int64_t when = 0) {
    if (!text) return;
    const std::uint32_t slot = g_state.consoleNext.fetch_add(1, std::memory_order_relaxed) % kConsoleSlots;
    ConsoleEntry& e = g_state.console[slot];
    SetText(e.text, kConsoleLen, text);
    const std::size_t n = std::strlen(text);
    if (n >= (std::size_t)kConsoleLen) std::memcpy(e.text + kConsoleLen - 4, "...", 4);   // (4 bytes: the three dots and the NUL)
    e.level = level < 0 ? 0 : level > 2 ? 2 : level;
    e.time = when ? when : (std::int64_t)std::time(nullptr);
}

// Tests only: back to the empty block.
inline void Reset() {
    std::memset(g_state.version, 0, sizeof(g_state.version)); std::memset(g_state.build, 0, sizeof(g_state.build));
    std::memset(g_state.os, 0, sizeof(g_state.os)); std::memset(g_state.cpu, 0, sizeof(g_state.cpu));
    std::memset(g_state.gpu, 0, sizeof(g_state.gpu)); std::memset(g_state.ui, 0, sizeof(g_state.ui));
    std::memset(g_state.ownCode, 0, sizeof(g_state.ownCode)); std::memset(g_state.scene, 0, sizeof(g_state.scene));
    g_state.ramTotalMb = 0; g_state.objects = 0; g_state.assets = 0; g_state.playing = 0; g_state.prefab = 0;
    g_state.autosave = 0; g_state.startTime = 0;
    g_state.frame = 0; g_state.crumbNext = 0; g_state.consoleNext = 0;
    std::memset(g_state.crumbs, 0, sizeof(g_state.crumbs)); std::memset(g_state.console, 0, sizeof(g_state.console));
}

// ---- Which OKAY_* switches are on ---------------------------------------------------------------------------------------------------
// "OKAY_OWN_FBX=1 OKAY_UI_BACKEND=okayui": the names and values of the environment variables that start with OKAY_ and look like
// switches. A value that is long or has anything but letters, digits and . _ - is a path, a URL or a key, so it is left out, and so
// is any name that says KEY, TOKEN, SECRET, PASSWORD, URL or DIR. `envp` is the usual NULL-terminated "NAME=value" list.
inline bool SwitchNameAllowed(const char* name, std::size_t len) {
    if (len < 6 || std::strncmp(name, "OKAY_", 5) != 0) return false;
    static const char* const kBlocked[] = {"KEY", "TOKEN", "SECRET", "PASSWORD", "PASS", "URL", "DIR", "PATH", "FILE", "USER", "MAIL", "HOST", "CRED", "AUTH"};
    for (const char* b : kBlocked) {
        const std::size_t bl = std::strlen(b);
        for (std::size_t i = 0; i + bl <= len; ++i) if (std::strncmp(name + i, b, bl) == 0) return false;
    }
    return true;
}
inline bool SwitchValueAllowed(const char* v) {
    const std::size_t n = std::strlen(v);
    if (n == 0 || n > 16) return false;
    for (std::size_t i = 0; i < n; ++i) {
        const char c = v[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '.' || c == '_' || c == '-')) return false;
    }
    return true;
}
inline void FormatSwitches(char* out, std::size_t cap, const char* const* envp) {
    if (!out || cap == 0) return;
    std::size_t n = 0;
    out[0] = '\0';
    if (!envp) return;
    for (const char* const* e = envp; *e; ++e) {
        const char* eq = std::strchr(*e, '=');
        if (!eq) continue;
        const std::size_t nameLen = (std::size_t)(eq - *e);
        if (!SwitchNameAllowed(*e, nameLen) || !SwitchValueAllowed(eq + 1)) continue;
        const std::size_t need = nameLen + 1 + std::strlen(eq + 1) + (n ? 1 : 0);
        if (n + need + 1 > cap) break;
        if (n) out[n++] = ' ';
        std::memcpy(out + n, *e, nameLen); n += nameLen;
        out[n++] = '=';
        const std::size_t vl = std::strlen(eq + 1);
        std::memcpy(out + n, eq + 1, vl); n += vl;
        out[n] = '\0';
    }
}

// ---- A bounded text writer (no stdio, no heap) -------------------------------------------------------------------------------------
class Out {
public:
    Out(char* buf, std::size_t cap) : b_(buf), cap_(cap) { if (b_ && cap_) b_[0] = '\0'; }
    Out& S(const char* s) { if (s) for (; *s; ++s) C(*s); return *this; }
    Out& C(char c) { if (n_ + 1 < cap_) { b_[n_++] = c; b_[n_] = '\0'; } else truncated_ = true; return *this; }
    Out& N(long long v) {
        if (v < 0) { C('-'); return U((unsigned long long)(-(v + 1)) + 1u); }
        return U((unsigned long long)v);
    }
    Out& U(unsigned long long v) {
        char t[24]; int k = 0;
        do { t[k++] = (char)('0' + v % 10); v /= 10; } while (v);
        while (k) C(t[--k]);
        return *this;
    }
    // Upper-case hex with at least `minDigits` digits ("0x" is the caller's).
    Out& H(unsigned long long v, int minDigits = 1) {
        char t[20]; int k = 0;
        do { t[k++] = "0123456789ABCDEF"[v & 15]; v >>= 4; } while (v);
        for (int pad = minDigits - k; pad > 0; --pad) C('0');
        while (k) C(t[--k]);
        return *this;
    }
    // "Key: value\n", or nothing when the value is empty.
    Out& Line(const char* key, const char* value) {
        if (!value || !*value) return *this;
        return S(key).S(": ").S(value).C('\n');
    }
    // Two digits ("05").
    Out& D2(int v) { C((char)('0' + (v / 10) % 10)); C((char)('0' + v % 10)); return *this; }
    const char* data() const { return b_; }
    std::size_t size() const { return n_; }
    bool truncated() const { return truncated_; }
private:
    char* b_; std::size_t cap_; std::size_t n_ = 0; bool truncated_ = false;
};

// ---- Dates without localtime (it can take a lock) ------------------------------------------------------------------------------------
// Seconds since 1970 (already shifted to local time by the caller) -> "YYYY-MM-DD HH:MM:SS".
inline void FormatWhen(Out& o, std::int64_t t) {
    std::int64_t days = t >= 0 ? t / 86400 : -((-t + 86399) / 86400);
    std::int64_t secs = t - days * 86400;
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = (unsigned)(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t y = (std::int64_t)yoe + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    o.N(y + (m <= 2 ? 1 : 0)).C('-').D2((int)m).C('-').D2((int)d).C(' ')
     .D2((int)(secs / 3600)).C(':').D2((int)((secs / 60) % 60)).C(':').D2((int)(secs % 60));
}

// ---- The report text -------------------------------------------------------------------------------------------------------------------
// What the platform code knows at the moment of the fault. Every string may be null (the line is then left out).
struct Fault {
    const char* version = nullptr;       // OKAY_ENGINE_VERSION (falls back to the one SetVersion gave)
    const char* when = nullptr;          // "YYYY-MM-DD HH:MM:SS", local time
    const char* exe = nullptr;           // full path of the program (the scrubber turns the user's folders into %USERPROFILE%)
    const char* exception = nullptr;     // "0xC0000005", "SIGSEGV (signal 11)", "C++ exception (std::terminate): ..."
    const char* faultModule = nullptr;   // "OkayEngine.exe + 0x1A2B3C"
    const char* detail = nullptr;        // "write to address 0x0" and the like
    const char* thread = nullptr;        // "main" / "worker"
    std::int64_t now = 0;                // seconds since 1970 now (uptime and the Console's "seconds before")
    long long memMb = -1;                // this program's memory in use, MB (-1 unknown)
    long long memPeakMb = -1;
    long long ramFreeMb = -1;            // free physical memory now
};

constexpr const char* kReportTitle = "OkaySpace crash report";
constexpr const char* kSectionMark = "--- ";

// Everything up to and including the "Backtrace:" line. The platform code then writes one line per frame (FrameLine).
inline void ComposeHead(Out& o, const Fault& f) {
    const State& s = g_state;
    o.S(kReportTitle).C('\n');
    o.Line("Version", f.version && *f.version ? f.version : s.version);
    o.Line("Build", s.build);
    o.Line("When", f.when);
    o.Line("Exe", f.exe);
    o.Line("Last step", LastStep());
    o.Line("Exception", f.exception);
    o.Line("Fault module", f.faultModule);
    o.Line("Fault detail", f.detail);
    o.Line("Thread", f.thread);
    o.Line("OS", s.os);
    o.Line("CPU", s.cpu);
    if (s.ramTotalMb > 0 || f.ramFreeMb >= 0) {
        o.S("RAM: ");
        if (s.ramTotalMb > 0) o.N(s.ramTotalMb).S(" MB total");
        if (f.ramFreeMb >= 0) { if (s.ramTotalMb > 0) o.S(", "); o.N(f.ramFreeMb).S(" MB free"); }
        o.C('\n');
    }
    if (f.memMb >= 0) {
        o.S("Process memory: ").N(f.memMb).S(" MB in use");
        if (f.memPeakMb >= 0) o.S(", ").N(f.memPeakMb).S(" MB peak");
        o.C('\n');
    }
    o.Line("GPU", s.gpu);
    o.Line("UI backend", s.ui);
    o.Line("Own code", s.ownCode);
    o.S("Mode: ").S(s.playing ? "play" : "edit");
    if (s.prefab) o.S(" (prefab open)");
    o.C('\n');
    o.Line("Scene", s.scene);
    if (s.objects >= 0 && (s.objects > 0 || s.scene[0])) o.S("Objects: ").N(s.objects).C('\n');
    if (s.assets >= 0 && s.assets > 0) o.S("Assets loaded: ").N(s.assets).C('\n');
    if (s.startTime > 0 && f.now >= s.startTime) o.S("Uptime: ").N(f.now - s.startTime).S(" s, ").U(Frame()).S(" frames\n");
    if (s.autosave >= 0) o.S("Recovery copy: ").S(s.autosave ? "present" : "none").C('\n');
    o.C('\n');
    o.S("Backtrace:\n");
}

// One backtrace line in the format the launcher already reads: "  #00 OkayEngine.exe + 0x1A2B3C  Name +0x40  (file:123)".
// `module` is the file name only. `fn` / `file` may be null.
inline void FrameLine(Out& o, int index, const char* module, unsigned long long offset, const char* fn = nullptr,
                      unsigned long long fnDisp = 0, const char* file = nullptr, unsigned long line = 0) {
    o.S("  #").D2(index).C(' ').S(module && *module ? module : "?").S(" + 0x").H(offset);
    if (fn && *fn) o.S("  ").S(fn).S(" +0x").H(fnDisp);
    if (file && *file) o.S("  (").S(file).C(':').U(line).C(')');
    o.C('\n');
}

// The step trail, the Console lines and the end marker.
inline void ComposeTail(Out& o, const Fault& f) {
    const State& s = g_state;
    o.C('\n');
    {   // breadcrumbs, oldest first
        const std::uint32_t next = s.crumbNext.load(std::memory_order_relaxed);
        const std::uint32_t count = next < (std::uint32_t)kCrumbSlots ? next : (std::uint32_t)kCrumbSlots;
        o.S(kSectionMark).S("Breadcrumbs (oldest first: frame, step) ---\n");
        for (std::uint32_t i = 0; i < count; ++i) {
            const Crumb& c = s.crumbs[(next - count + i) % kCrumbSlots];
            if (!c.text[0]) continue;
            o.S("frame ").U(c.frame).S(": ").S(c.text);
            if (c.repeat > 1) o.S(" x").U(c.repeat);
            o.C('\n');
        }
    }
    {   // Console, oldest first
        const std::uint32_t next = s.consoleNext.load(std::memory_order_relaxed);
        const std::uint32_t count = next < (std::uint32_t)kConsoleSlots ? next : (std::uint32_t)kConsoleSlots;
        o.S(kSectionMark).S("Console (last ").U(count).S(" lines, oldest first: seconds before the crash, level) ---\n");
        for (std::uint32_t i = 0; i < count; ++i) {
            const ConsoleEntry& e = s.console[(next - count + i) % kConsoleSlots];
            if (!e.text[0]) continue;
            long long ago = f.now > 0 && e.time > 0 ? (long long)(f.now - e.time) : 0;
            if (ago < 0) ago = 0;
            o.S("[-").N(ago).S("s] ").C(e.level == 2 ? 'E' : e.level == 1 ? 'W' : 'I').C(' ').S(e.text).C('\n');
        }
    }
    o.S(kSectionMark).S("End of report ---\n");
}

} // namespace ctx
} // namespace okaycrash
