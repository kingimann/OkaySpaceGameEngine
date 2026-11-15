#pragma once
// The crash handlers (docs/crash_reports.md): what runs INSIDE the program when it is going down, and writes
// okay_crash.txt (the latest report) and okay_crashlog.txt (every report, each after a
// "========== crash @ <when> (v<version>) ==========" line) beside the program, or in $OKAY_CRASH_DIR.
//
//   Windows  SetUnhandledExceptionFilter (access violation, stack overflow, ...), std::set_terminate (an uncaught C++ exception,
//            which is not an SEH fault and was never reported before) and SIGABRT (assert / abort()).
//   Linux    sigaction on SIGSEGV SIGABRT SIGFPE SIGILL SIGBUS (on an alternate stack, so a stack overflow is reported too)
//            and the same terminate hook. macOS compiles the same code (parked, never run).
//
// Rules the code follows (owner brief 2026-11-15):
//   * The report is written FIRST, from static memory, with no heap use and no stdio: ctx::Out builds the text, one write call puts it
//     on disk. Nothing is sent from here; the launcher uploads on its next look (shared/CrashUpload.hpp).
//   * Windows: the full report with raw "module + offset" frames is on disk before dbghelp is even loaded. Function names are a second
//     pass that replaces the file only if it works, so a hang or a fault in dbghelp costs the names, not the report.
//   * A fault inside the handler ends the process (one guard flag), it never loops.
//   * No <mutex>, <thread>, condition_variable or call_once (CLAUDE.md rule 21a: the Windows build cannot compile them).
//
// Written from the Windows exception-filter and x64 unwind documentation (RtlLookupFunctionEntry / RtlVirtualUnwind) and from
// signal-safety(7) / sigaction(2); the previous handler in editor/main.cpp is the only code this replaces.
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <string>
#include <typeinfo>
#include <cxxabi.h>

#include "CrashContext.hpp"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <dbghelp.h>
#  include <psapi.h>
#  include <csignal>
#  if defined(__x86_64__) || defined(__i386__)
#    include <cpuid.h>
#  endif
#else
#  include <execinfo.h>
#  include <fcntl.h>
#  include <pthread.h>
#  include <signal.h>
#  include <sys/stat.h>
#  include <sys/utsname.h>
#  include <ucontext.h>
#  include <unistd.h>
#  if defined(__APPLE__)
#    include <crt_externs.h>
#    include <mach-o/dyld.h>
#  else
extern "C" char** environ;
#  endif
#endif

namespace okaycrash {
namespace handler {

struct Config {
    char dir[1100];        // where the reports go, with a trailing separator
    char exe[1100];        // full path of the program
    char version[32];
    long utcOffset;        // seconds east of UTC for "When:" (POSIX; fixed at startup)
    bool dialog;           // Windows: show the "OkaySpace crashed" box
    unsigned long mainThread;
};
inline Config g_cfg;
inline char g_buf[192 * 1024];                 // the report text, built here then written in one go
inline std::atomic<int> g_entered{0};          // set by the first crash; a second one (another thread, or the handler itself) just ends
inline const char* kCrashFile = "okay_crash.txt";
inline const char* kLogFile = "okay_crashlog.txt";

inline void CopyStr(char* dst, std::size_t cap, const char* src) { ctx::SetText(dst, cap, src); }

// "<dir><name>" into `out`.
inline void PathIn(char* out, std::size_t cap, const char* name) {
    ctx::Out o(out, cap);
    o.S(g_cfg.dir).S(name);
}

// A description of the exception being handled (std::terminate), without the heap where it can be avoided.
inline void DescribeCurrentException(char* out, std::size_t cap) {
    out[0] = '\0';
    std::exception_ptr ep = std::current_exception();
    if (!ep) { CopyStr(out, cap, "no active exception (std::terminate was called directly)"); return; }
    try {
        std::rethrow_exception(ep);
    } catch (const std::exception& e) {
        int status = 0;
        char* dem = abi::__cxa_demangle(typeid(e).name(), nullptr, nullptr, &status);
        ctx::Out o(out, cap);
        o.S(status == 0 && dem ? dem : typeid(e).name()).S(": ").S(e.what());
        std::free(dem);
    } catch (...) {
        CopyStr(out, cap, "an exception that is not a std::exception");
    }
}

// What a "C++ exception" report calls itself (the signature keys on the word "terminate", the text after it is free).
inline void TerminateText(char* out, std::size_t cap) {
    char what[400];
    DescribeCurrentException(what, sizeof(what));
    ctx::Out o(out, cap);
    o.S("C++ exception (std::terminate): ").S(what);
}

#if defined(_WIN32)
// =============================================================== Windows ==============================================================
namespace win {

using GetProcessMemoryInfoFn = BOOL(WINAPI*)(HANDLE, PROCESS_MEMORY_COUNTERS*, DWORD);
inline GetProcessMemoryInfoFn g_getMem = nullptr;   // K32GetProcessMemoryInfo, looked up at startup (no psapi link needed)

// File name (no folder) of the module that holds `addr`, and the module's load address.
inline void ModuleOf(unsigned long long addr, char* name, std::size_t cap, unsigned long long& base) {
    base = 0;
    if (name && cap) name[0] = '\0';
    HMODULE m = nullptr;
    if (addr && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)(uintptr_t)addr, &m) && m) {
        char full[MAX_PATH];
        const DWORD n = GetModuleFileNameA(m, full, MAX_PATH);
        const char* b = full;
        for (const char* p = full; n && *p; ++p) if (*p == '\\' || *p == '/') b = p + 1;
        if (n) CopyStr(name, cap, b);
        base = (unsigned long long)(uintptr_t)m;
    }
}

// Return addresses from a captured CPU context, newest first. On x64 this is the documented unwind loop, so frame 0 is the faulting
// instruction itself; anywhere else it falls back to CaptureStackBackTrace.
inline std::size_t Unwind(const CONTEXT* start, unsigned long long* pcs, std::size_t max, std::size_t skip) {
#if defined(__x86_64__)
    CONTEXT c = *start;
    std::size_t n = 0, seen = 0;
    for (int guard = 0; guard < 256 && n < max; ++guard) {
        const unsigned long long pc = c.Rip;
        if (!pc) break;
        if (seen++ >= skip) pcs[n++] = pc;
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION rf = RtlLookupFunctionEntry(pc, &imageBase, nullptr);
        if (rf) {
            PVOID handlerData = nullptr; DWORD64 establisher = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, pc, rf, &c, &handlerData, &establisher, nullptr);
        } else {   // a leaf function: the return address is on top of the stack (read it safely, the stack may be damaged)
            DWORD64 ret = 0; SIZE_T got = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), (LPCVOID)(uintptr_t)c.Rsp, &ret, sizeof(ret), &got) || got != sizeof(ret)) break;
            c.Rip = ret; c.Rsp += 8;
        }
    }
    return n;
#else
    (void)start;
    void* raw[64];
    const USHORT got = CaptureStackBackTrace((DWORD)skip, (DWORD)(max < 64 ? max : 64), raw, nullptr);
    for (USHORT i = 0; i < got; ++i) pcs[i] = (unsigned long long)(uintptr_t)raw[i];
    return got;
#endif
}

inline bool WriteFileAt(const char* path, const char* data, std::size_t n, bool append, unsigned long long truncateTo = (unsigned long long)-1,
                        unsigned long long* endBefore = nullptr) {
    HANDLE h = CreateFileA(path, append ? FILE_APPEND_DATA | FILE_WRITE_DATA : GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    if (append) {
        LARGE_INTEGER zero; zero.QuadPart = 0;
        LARGE_INTEGER pos; pos.QuadPart = 0;
        if (truncateTo != (unsigned long long)-1) {   // replace what an earlier pass appended from `truncateTo` on
            LARGE_INTEGER at; at.QuadPart = (LONGLONG)truncateTo;
            SetFilePointerEx(h, at, nullptr, FILE_BEGIN);
            SetEndOfFile(h);
        } else {
            SetFilePointerEx(h, zero, &pos, FILE_END);
            if (endBefore) *endBefore = (unsigned long long)pos.QuadPart;
        }
    }
    DWORD wrote = 0;
    bool ok = true;
    for (std::size_t done = 0; done < n && ok; done += wrote) {
        wrote = 0;
        ok = WriteFile(h, data + done, (DWORD)(n - done > 0x40000000 ? 0x40000000 : n - done), &wrote, nullptr) && wrote > 0;
    }
    CloseHandle(h);
    return ok;
}

// The text of a fault (everything that is not the backtrace itself).
struct Details {
    char exception[512];
    char faultModule[320];
    char detail[256];
    unsigned long threadId;
};

inline void FillFault(ctx::Fault& f, const Details& d, char* whenBuf, std::size_t whenCap, ctx::Out*) {
    SYSTEMTIME st; GetLocalTime(&st);
    ctx::Out w(whenBuf, whenCap);
    w.U(st.wYear).C('-').D2(st.wMonth).C('-').D2(st.wDay).C(' ').D2(st.wHour).C(':').D2(st.wMinute).C(':').D2(st.wSecond);
    f.version = g_cfg.version;
    f.when = whenBuf;
    f.exe = g_cfg.exe;
    f.exception = d.exception;
    f.faultModule = d.faultModule[0] ? d.faultModule : nullptr;
    f.detail = d.detail[0] ? d.detail : nullptr;
    f.thread = d.threadId == g_cfg.mainThread ? "main" : "worker";
    f.now = (std::int64_t)std::time(nullptr);
    if (g_getMem) {
        PROCESS_MEMORY_COUNTERS pmc; std::memset(&pmc, 0, sizeof(pmc)); pmc.cb = sizeof(pmc);
        if (g_getMem(GetCurrentProcess(), &pmc, sizeof(pmc))) { f.memMb = (long long)(pmc.WorkingSetSize >> 20); f.memPeakMb = (long long)(pmc.PeakWorkingSetSize >> 20); }
    }
    MEMORYSTATUSEX ms; std::memset(&ms, 0, sizeof(ms)); ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) f.ramFreeMb = (long long)(ms.ullAvailPhys >> 20);
}

// Compose, then write. `symbolize`: ask dbghelp for function names and file:line (the second pass).
inline std::size_t Compose(const ctx::Fault& f, const unsigned long long* pcs, std::size_t n, bool symbolize, bool exactFirst) {
    ctx::Out o(g_buf, sizeof(g_buf));
    ctx::ComposeHead(o, f);
    HANDLE proc = GetCurrentProcess();
    bool symOk = false;
    if (symbolize) {
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        symOk = SymInitialize(proc, nullptr, TRUE) != FALSE;
    }
    char symBuf[sizeof(SYMBOL_INFO) + 512];
    for (std::size_t i = 0; i < n; ++i) {
        char mod[MAX_PATH]; unsigned long long base = 0;
        ModuleOf(pcs[i], mod, sizeof(mod), base);
        const unsigned long long off = base ? pcs[i] - base : pcs[i];
        if (!symOk) { ctx::FrameLine(o, (int)i, mod, off); continue; }
        SYMBOL_INFO* si = (SYMBOL_INFO*)symBuf;
        std::memset(si, 0, sizeof(SYMBOL_INFO));
        si->SizeOfStruct = sizeof(SYMBOL_INFO); si->MaxNameLen = 500;
        DWORD64 disp = 0;
        const DWORD64 look = (DWORD64)pcs[i] - (i == 0 && exactFirst ? 0 : 1);   // a return address may sit one past the end of a function that never returns
        const bool haveName = SymFromAddr(proc, look, &disp, si) != FALSE;
        IMAGEHLP_LINE64 line; std::memset(&line, 0, sizeof(line)); line.SizeOfStruct = sizeof(line);
        DWORD ld = 0;
        const bool haveLine = SymGetLineFromAddr64(proc, look, &ld, &line) && line.FileName;
        ctx::FrameLine(o, (int)i, mod, off, haveName ? si->Name : nullptr, (unsigned long long)disp, haveLine ? line.FileName : nullptr, haveLine ? line.LineNumber : 0);
    }
    if (symOk) SymCleanup(proc);
    ctx::ComposeTail(o, f);
    return o.size();
}

// Two passes: raw frames (on disk before dbghelp is loaded), then names. The history keeps the better of the two.
inline void WriteReport(const Details& d, const CONTEXT* from, std::size_t skip, bool allowSymbols, bool exactFirst) {
    unsigned long long pcs[48];
    const std::size_t n = from ? Unwind(from, pcs, 48, skip) : 0;
    ctx::Fault f; char when[40];
    FillFault(f, d, when, sizeof(when), nullptr);
    char crashPath[1300], logPath[1300];
    PathIn(crashPath, sizeof(crashPath), kCrashFile);
    PathIn(logPath, sizeof(logPath), kLogFile);

    std::size_t len = Compose(f, pcs, n, false, exactFirst);
    WriteFileAt(crashPath, g_buf, len, false);
    char mark[160];
    { ctx::Out m(mark, sizeof(mark)); m.S("\n========== crash @ ").S(when).S(" (v").S(g_cfg.version).S(") ==========\n"); }
    unsigned long long histStart = 0;
    {   // history: marker + report + a blank line, appended
        static char entry[sizeof(g_buf) + 256];
        ctx::Out e(entry, sizeof(entry));
        e.S(mark).S(g_buf).C('\n');
        WriteFileAt(logPath, e.data(), e.size(), true, (unsigned long long)-1, &histStart);
    }
    if (!allowSymbols || n == 0) return;
    len = Compose(f, pcs, n, true, exactFirst);
    WriteFileAt(crashPath, g_buf, len, false);
    {
        static char entry[sizeof(g_buf) + 256];
        ctx::Out e(entry, sizeof(entry));
        e.S(mark).S(g_buf).C('\n');
        WriteFileAt(logPath, e.data(), e.size(), true, histStart);
    }
}

struct SehJob { EXCEPTION_POINTERS* ep; Details d; };
inline void FromException(const SehJob& job) {
    const EXCEPTION_RECORD* er = job.ep ? job.ep->ExceptionRecord : nullptr;
    Details d = job.d;
    const unsigned long code = er ? er->ExceptionCode : 0;
    { ctx::Out o(d.exception, sizeof(d.exception)); o.S("0x").H(code, 8); }
    const unsigned long long addr = er ? (unsigned long long)(uintptr_t)er->ExceptionAddress : 0;
    char mod[MAX_PATH]; unsigned long long base = 0;
    ModuleOf(addr, mod, sizeof(mod), base);
    { ctx::Out o(d.faultModule, sizeof(d.faultModule)); o.S(mod[0] ? mod : "?").S(" + 0x").H(base ? addr - base : addr); }
    if (er && code == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2) {
        ctx::Out o(d.detail, sizeof(d.detail));
        o.S(er->ExceptionInformation[0] == 0 ? "read from address 0x" : er->ExceptionInformation[0] == 1 ? "write to address 0x" : "execute at address 0x")
         .H((unsigned long long)er->ExceptionInformation[1]);
    }
    const bool stackOverflow = code == 0xC00000FD;   // little stack is left: no dbghelp (it needs more than is there)
    WriteReport(d, job.ep ? job.ep->ContextRecord : nullptr, 0, !stackOverflow, true);
}
inline DWORD WINAPI SehWorker(LPVOID p) { FromException(*static_cast<SehJob*>(p)); return 0; }

inline void ShowBox(const char* exception, const char* step) {
    if (!g_cfg.dialog) return;
    char msg[1400];
    ctx::Out o(msg, sizeof(msg));
    o.S("OkaySpace crashed.\n\nVersion: ").S(g_cfg.version).S("\nLast step: ").S(step).S("\nException: ").S(exception)
     .S("\n\nA report was saved to okay_crash.txt next to the editor.\n"
        "Open the OkaySpace launcher: Settings > Crash reports shows it, and sends it for you if that is on.");
    MessageBoxA(nullptr, msg, "OkaySpace crash", MB_OK | MB_ICONERROR);
}

inline LONG WINAPI Filter(EXCEPTION_POINTERS* ep) {
    if (g_entered.exchange(1)) return EXCEPTION_EXECUTE_HANDLER;   // a fault while reporting: end the process
    SehJob job; std::memset(&job, 0, sizeof(job));
    job.ep = ep; job.d.threadId = GetCurrentThreadId();
    if (ep && ep->ExceptionRecord && ep->ExceptionRecord->ExceptionCode == 0xC00000FD) {
        // The stack is used up: do the work on a thread with a stack of its own.
        HANDLE h = CreateThread(nullptr, 1024 * 1024, SehWorker, &job, 0, nullptr);
        if (h) { WaitForSingleObject(h, 30000); CloseHandle(h); }
    } else {
        FromException(job);
    }
    char exc[64]; { ctx::Out o(exc, sizeof(exc)); o.S("0x").H(ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0, 8); }
    ShowBox(exc, ctx::LastStep());
    return EXCEPTION_EXECUTE_HANDLER;
}

// abort() and std::terminate have no exception record: capture where we are.
__attribute__((noinline)) inline void ReportHere(const char* what, std::size_t skip) {
    if (g_entered.exchange(1)) return;
    Details d; std::memset(&d, 0, sizeof(d));
    d.threadId = GetCurrentThreadId();
    CopyStr(d.exception, sizeof(d.exception), what);
    CONTEXT c; std::memset(&c, 0, sizeof(c));
    RtlCaptureContext(&c);
    WriteReport(d, &c, skip, true, false);
    ShowBox(d.exception, ctx::LastStep());
}
inline void AbortSignal(int) { ReportHere("SIGABRT (abort)", 3); std::signal(SIGABRT, SIG_DFL); std::abort(); }
inline void TerminateHook() {
    char text[600]; TerminateText(text, sizeof(text));
    ReportHere(text, 3);
    std::abort();
}

inline void CollectSystemInfo() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    HMODULE nt = GetModuleHandleA("ntdll.dll");
    char os[160]; ctx::Out o(os, sizeof(os));
    o.S("Windows");
    if (nt) {
        if (auto fn = (RtlGetVersionFn)(void*)GetProcAddress(nt, "RtlGetVersion")) {
            OSVERSIONINFOW v; std::memset(&v, 0, sizeof(v)); v.dwOSVersionInfoSize = sizeof(v);
            if (fn(&v) == 0) { o.C(' ').U(v.dwMajorVersion).C('.').U(v.dwMinorVersion).C('.').U(v.dwBuildNumber); if (v.dwBuildNumber >= 22000) o.S(" (Windows 11)"); }
        }
        if (GetProcAddress(nt, "wine_get_version")) o.S(" [Wine]");
    }
    SYSTEM_INFO si; GetNativeSystemInfo(&si);
    o.S(si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? " (x64)" : si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? " (arm64)" : " (x86)");
    ctx::SetOs(os);
    char cpu[160]; ctx::Out c(cpu, sizeof(cpu));
#if defined(__x86_64__) || defined(__i386__)
    unsigned a = 0, b = 0, cc = 0, dd = 0;
    char brand[49]; std::memset(brand, 0, sizeof(brand));
    if (__get_cpuid(0x80000000u, &a, &b, &cc, &dd) && a >= 0x80000004u)
        for (unsigned i = 0; i < 3; ++i) { __get_cpuid(0x80000002u + i, &a, &b, &cc, &dd); std::memcpy(brand + i * 16, &a, 4); std::memcpy(brand + i * 16 + 4, &b, 4); std::memcpy(brand + i * 16 + 8, &cc, 4); std::memcpy(brand + i * 16 + 12, &dd, 4); }
    const char* trimmed = brand; while (*trimmed == ' ') ++trimmed;
    c.S(*trimmed ? trimmed : "unknown CPU");
#else
    c.S("unknown CPU");
#endif
    c.S(" (").U(si.dwNumberOfProcessors).S(" logical)");
    ctx::SetCpu(cpu);
    MEMORYSTATUSEX ms; std::memset(&ms, 0, sizeof(ms)); ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) ctx::SetRamTotalMb((long long)(ms.ullTotalPhys >> 20));
    if (HMODULE k = GetModuleHandleA("kernel32.dll")) g_getMem = (GetProcessMemoryInfoFn)(void*)GetProcAddress(k, "K32GetProcessMemoryInfo");
}

// "pe-<link time>-<image size>": the pair that names a build of the program (the split-off OkayEngine.debug has the same headers).
inline void BuildId(char* out, std::size_t cap, const char* stamp) {
    ctx::Out o(out, cap);
    const unsigned char* base = (const unsigned char*)GetModuleHandleA(nullptr);
    if (base && base[0] == 'M' && base[1] == 'Z') {
        const LONG nt = *(const LONG*)(base + 0x3C);
        const IMAGE_NT_HEADERS* h = (const IMAGE_NT_HEADERS*)(base + nt);
        if (nt > 0 && nt < 0x1000 && h->Signature == IMAGE_NT_SIGNATURE) o.S("pe-").H(h->FileHeader.TimeDateStamp, 8).C('-').H(h->OptionalHeader.SizeOfImage);
    }
    if (stamp && *stamp) o.S(o.size() ? ", built " : "built ").S(stamp);
}

inline void InstallPlatform(const char* buildStamp) {
    char exe[MAX_PATH] = "";
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    CopyStr(g_cfg.exe, sizeof(g_cfg.exe), exe);
    CopyStr(g_cfg.dir, sizeof(g_cfg.dir), exe);
    char* slash = std::strrchr(g_cfg.dir, '\\');
    if (slash) slash[1] = '\0'; else g_cfg.dir[0] = '\0';
    if (const char* dev = std::getenv("OKAY_CRASH_DIR"); dev && *dev) {
        const DWORD a = GetFileAttributesA(dev);
        if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) {
            ctx::Out o(g_cfg.dir, sizeof(g_cfg.dir));
            o.S(dev);
            const std::size_t n = std::strlen(dev);
            if (n && dev[n - 1] != '\\' && dev[n - 1] != '/') o.C('\\');
        }
    }
    g_cfg.mainThread = GetCurrentThreadId();
    g_cfg.dialog = !std::getenv("OKAY_CRASH_NO_DIALOG");
    CollectSystemInfo();
    char id[112]; BuildId(id, sizeof(id), buildStamp); ctx::SetBuild(id);
    SetUnhandledExceptionFilter(Filter);
    std::signal(SIGABRT, AbortSignal);
    std::set_terminate(TerminateHook);
}

} // namespace win
#else
// ================================================================ POSIX ===============================================================
namespace posix {

inline bool WriteAll(int fd, const char* p, std::size_t n) {
    while (n > 0) {
        const ssize_t w = ::write(fd, p, n);
        if (w <= 0) return false;
        p += w; n -= (std::size_t)w;
    }
    return true;
}

// Small integer / text readers over a file (open, read, close: all async-signal-safe).
inline long long ReadStatmRssMb() {
    int fd = ::open("/proc/self/statm", O_RDONLY);
    if (fd < 0) return -1;
    char b[128]; const ssize_t n = ::read(fd, b, sizeof(b) - 1);
    ::close(fd);
    if (n <= 0) return -1;
    b[n] = '\0';
    const char* p = b;
    while (*p && *p != ' ') ++p;       // skip "size"
    long long pages = 0;
    while (*p == ' ') ++p;
    while (*p >= '0' && *p <= '9') pages = pages * 10 + (*p++ - '0');
    const long ps = ::sysconf(_SC_PAGESIZE);
    return pages * (ps > 0 ? ps : 4096) / (1024 * 1024);
}

inline void FillFault(ctx::Fault& f, char* whenBuf, std::size_t whenCap, const char* exception, const char* module, const char* detail) {
    ctx::Out w(whenBuf, whenCap);
    const std::int64_t now = (std::int64_t)std::time(nullptr);
    ctx::FormatWhen(w, now + g_cfg.utcOffset);
    f.version = g_cfg.version; f.when = whenBuf; f.exe = g_cfg.exe;
    f.exception = exception; f.faultModule = module; f.detail = detail;
    f.thread = (unsigned long)::pthread_self() == g_cfg.mainThread ? "main" : "worker";
    f.now = now;
    f.memMb = ReadStatmRssMb();
#if defined(_SC_AVPHYS_PAGES)
    const long ap = ::sysconf(_SC_AVPHYS_PAGES), ps = ::sysconf(_SC_PAGESIZE);
    if (ap > 0 && ps > 0) f.ramFreeMb = (long long)ap * ps / (1024 * 1024);
#endif
}

// Write the whole report: head, then the frames straight from backtrace_symbols_fd, then the tail. Then add it to the history.
// `firstExact`: frames[0] is the interrupted instruction itself (a fault); otherwise every frame is a return address. A return
// address can be one byte past the end of a function that ended in a call that never returns (abort()), so it is looked up as
// address - 1, the usual rule.
inline void WriteReport(const char* exception, void** frames, int nframes, bool firstExact) {
    char crashPath[1300], logPath[1300];
    PathIn(crashPath, sizeof(crashPath), kCrashFile);
    PathIn(logPath, sizeof(logPath), kLogFile);
    for (int i = firstExact ? 1 : 0; i < nframes; ++i) frames[i] = (void*)((char*)frames[i] - 1);
    const char* exeName = g_cfg.exe;
    for (const char* p = g_cfg.exe; *p; ++p) if (*p == '/') exeName = p + 1;
    ctx::Fault f; char when[40];
    FillFault(f, when, sizeof(when), exception, exeName, nullptr);
    const int fd = ::open(crashPath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        ctx::Out head(g_buf, sizeof(g_buf));
        ctx::ComposeHead(head, f);
        WriteAll(fd, head.data(), head.size());
        if (nframes > 0) backtrace_symbols_fd(frames, nframes, fd);
        ctx::Out tail(g_buf, sizeof(g_buf));
        ctx::ComposeTail(tail, f);
        WriteAll(fd, tail.data(), tail.size());
        ::close(fd);
    }
    const int rd = ::open(crashPath, O_RDONLY);
    const int hist = rd >= 0 ? ::open(logPath, O_WRONLY | O_CREAT | O_APPEND, 0644) : -1;
    if (rd >= 0 && hist >= 0) {
        char mark[160];
        { ctx::Out m(mark, sizeof(mark)); m.S("\n========== crash @ ").S(when).S(" (v").S(g_cfg.version).S(") ==========\n"); }
        WriteAll(hist, mark, std::strlen(mark));
        for (;;) {
            const ssize_t n = ::read(rd, g_buf, sizeof(g_buf));
            if (n <= 0) break;
            WriteAll(hist, g_buf, (std::size_t)n);
        }
        WriteAll(hist, "\n", 1);
    }
    if (rd >= 0) ::close(rd);
    if (hist >= 0) ::close(hist);
    const char* a = "OkaySpace crashed (";
    WriteAll(2, a, std::strlen(a)); WriteAll(2, exception, std::strlen(exception));
    const char* b = "). Last step: ";
    WriteAll(2, b, std::strlen(b)); const char* ls = ctx::LastStep(); WriteAll(2, ls, std::strlen(ls));
    const char* c = ". Report: ";
    WriteAll(2, c, std::strlen(c)); WriteAll(2, crashPath, std::strlen(crashPath)); WriteAll(2, "\n", 1);
}

inline const char* SignalName(int sig) {
    return sig == SIGSEGV ? "SIGSEGV (signal 11)" : sig == SIGABRT ? "SIGABRT (signal 6)" : sig == SIGFPE ? "SIGFPE (signal 8)"
         : sig == SIGILL ? "SIGILL (signal 4)" : sig == SIGBUS ? "SIGBUS (signal 7)" : "signal";
}

inline void OnSignal(int sig, siginfo_t*, void* uctx) {
    if (g_entered.exchange(1)) { ::signal(sig, SIG_DFL); ::raise(sig); return; }
    ::signal(sig, SIG_DFL);   // a fault while reporting ends the process with the default action
    void* frames[64];
    int n = ::backtrace(frames, 64);
    int skip = 2;             // the handler itself and the signal trampoline
#if defined(__linux__) && defined(__x86_64__)
    if (uctx) {   // the interrupted instruction is the first frame that matters
        const void* pc = (const void*)((ucontext_t*)uctx)->uc_mcontext.gregs[REG_RIP];
        for (int i = 0; i < n; ++i) if (frames[i] == pc) { skip = i; break; }
    }
#else
    (void)uctx;
#endif
    if (skip > n) skip = n;
    WriteReport(SignalName(sig), frames + skip, n - skip, true);
    ::raise(sig);
}

__attribute__((noinline)) inline void ReportHere(const char* what, int skip) {
    if (g_entered.exchange(1)) return;
    void* frames[64];
    int n = ::backtrace(frames, 64);
    if (skip > n) skip = n;
    WriteReport(what, frames + skip, n - skip, false);
}
inline void TerminateHook() {
    char text[600]; TerminateText(text, sizeof(text));
    ReportHere(text, 3);
    ::signal(SIGABRT, SIG_DFL);
    std::abort();
}

inline void CollectSystemInfo() {
    struct utsname u;
    char os[160]; ctx::Out o(os, sizeof(os));
    if (::uname(&u) == 0) o.S(u.sysname).C(' ').S(u.release).C(' ').S(u.machine);
#if defined(__linux__)
    {   // the distribution's own name
        FILE* f = std::fopen("/etc/os-release", "r");
        char line[200];
        while (f && std::fgets(line, sizeof(line), f)) {
            if (std::strncmp(line, "PRETTY_NAME=", 12) == 0) {
                std::size_t n = std::strlen(line);
                while (n && (line[n - 1] == '\n' || line[n - 1] == '"')) line[--n] = '\0';
                o.S(" (").S(line[12] == '"' ? line + 13 : line + 12).C(')');
                break;
            }
        }
        if (f) std::fclose(f);
    }
#endif
    ctx::SetOs(os);
    char cpu[160]; ctx::Out c(cpu, sizeof(cpu));
#if defined(__linux__)
    {
        FILE* f = std::fopen("/proc/cpuinfo", "r");
        char line[300];
        while (f && std::fgets(line, sizeof(line), f)) {
            if (std::strncmp(line, "model name", 10) == 0) {
                const char* v = std::strchr(line, ':');
                if (v) { ++v; while (*v == ' ') ++v; std::size_t n = std::strlen(v); while (n && (v[n - 1] == '\n' || v[n - 1] == ' ')) --n; for (std::size_t i = 0; i < n; ++i) c.C(v[i]); }
                break;
            }
        }
        if (f) std::fclose(f);
    }
#endif
    if (!c.size()) c.S("unknown CPU");
    c.S(" (").N(::sysconf(_SC_NPROCESSORS_ONLN)).S(" logical)");
    ctx::SetCpu(cpu);
#if defined(_SC_PHYS_PAGES)
    const long pp = ::sysconf(_SC_PHYS_PAGES), ps = ::sysconf(_SC_PAGESIZE);
    if (pp > 0 && ps > 0) ctx::SetRamTotalMb((long long)pp * ps / (1024 * 1024));
#endif
}

inline void InstallPlatform(const char* buildStamp) {
    char exe[1100] = "";
#if defined(__APPLE__)
    uint32_t sz = sizeof(exe);
    if (_NSGetExecutablePath(exe, &sz) != 0) exe[0] = '\0';
#else
    const ssize_t n = ::readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    exe[n > 0 ? n : 0] = '\0';
#endif
    CopyStr(g_cfg.exe, sizeof(g_cfg.exe), exe);
    CopyStr(g_cfg.dir, sizeof(g_cfg.dir), exe);
    char* slash = std::strrchr(g_cfg.dir, '/');
    if (slash) slash[1] = '\0'; else CopyStr(g_cfg.dir, sizeof(g_cfg.dir), "./");
    if (const char* dev = std::getenv("OKAY_CRASH_DIR"); dev && *dev) {
        struct stat st;
        if (::stat(dev, &st) == 0 && S_ISDIR(st.st_mode)) {
            ctx::Out o(g_cfg.dir, sizeof(g_cfg.dir));
            o.S(dev);
            const std::size_t len = std::strlen(dev);
            if (len && dev[len - 1] != '/') o.C('/');
        }
    }
    g_cfg.mainThread = (unsigned long)::pthread_self();
    g_cfg.dialog = false;
    {   // UTC offset for "When:": localtime is not safe inside a handler, so it is read once here
        const std::time_t now = std::time(nullptr);
        struct tm lt; ::localtime_r(&now, &lt);
        g_cfg.utcOffset = lt.tm_gmtoff;
    }
    CollectSystemInfo();
    ctx::SetBuild(buildStamp && *buildStamp ? (std::string("built ") + buildStamp).c_str() : "");
    // Warm up backtrace() (its first call may load libgcc) and give signals a stack of their own, so a stack overflow is reported.
    { void* warm[4]; ::backtrace(warm, 4); }
    static char altStack[128 * 1024];
    stack_t ss; std::memset(&ss, 0, sizeof(ss));
    ss.ss_sp = altStack; ss.ss_size = sizeof(altStack);
    ::sigaltstack(&ss, nullptr);
    for (int s : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS}) {
        struct sigaction sa; std::memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = OnSignal;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        ::sigemptyset(&sa.sa_mask);
        ::sigaction(s, &sa, nullptr);
    }
    std::set_terminate(TerminateHook);
}

} // namespace posix
#endif

// Install the handlers. `version`: the program's version string; `buildStamp`: __DATE__ " " __TIME__ of the caller (it names the
// build with no link-time information, e.g. on Linux). Call once, first thing in main(). Everything here also fills the parts of the
// crash context that never change (OS, CPU, RAM, build id, the OKAY_* switches that are on); the program sets the rest as it runs.
inline void Install(const char* version, const char* buildStamp = nullptr) {
    std::memset(&g_cfg, 0, sizeof(g_cfg));
    CopyStr(g_cfg.version, sizeof(g_cfg.version), version);
    ctx::SetVersion(version);
    ctx::g_state.objects = -1; ctx::g_state.assets = -1; ctx::g_state.autosave = -1;
    ctx::StartClock();
    {
        char sw[512];
#if defined(_WIN32)
        ctx::FormatSwitches(sw, sizeof(sw), _environ);
#elif defined(__APPLE__)
        ctx::FormatSwitches(sw, sizeof(sw), *_NSGetEnviron());
#else
        ctx::FormatSwitches(sw, sizeof(sw), ::environ);
#endif
        ctx::SetOwnCode(sw);
    }
#if defined(_WIN32)
    win::InstallPlatform(buildStamp);
#else
    posix::InstallPlatform(buildStamp);
#endif
}

} // namespace handler
} // namespace okaycrash
