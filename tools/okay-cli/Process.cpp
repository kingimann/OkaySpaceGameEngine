// okay CLI: the one place that starts another program. See Process.hpp.
// Written from the Win32 CreateProcessW / pipe documentation and POSIX fork, execv, pipe and poll. Our own code.
// The Windows quoting, UTF-8 to UTF-16 and this program's path come from shared/AppRestart.hpp.
#include "Process.hpp"

#include "AppRestart.hpp"
#include "okay/Core/StringUtil.hpp"   // shared/: QuoteWindowsArg, Utf8ToWide, CurrentExecutable (one copy of each)

#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
extern char** environ;
#endif

namespace okaycli {
namespace fs = std::filesystem;

std::string SelfPath() { return okay::restart::Utf8(okay::restart::CurrentExecutable()); }

bool IsScriptFile(const std::string& program) {
    const std::string ext = okay::str::LowerAscii(fs::path(program).extension().string());
    return ext == ".cmd" || ext == ".bat";
}

std::string ResolveProgram(const std::string& program) {
    if (program.empty() || IsScriptFile(program)) return "";
    std::error_code ec;
    if (program.find('/') != std::string::npos || program.find('\\') != std::string::npos) {
        const fs::path p = fs::absolute(program, ec);
        return !ec && fs::is_regular_file(p, ec) && !IsScriptFile(p.string()) ? p.string() : std::string();
    }
    const std::string found = FindOnPath(program);
    return IsScriptFile(found) ? std::string() : found;
}

std::string FindOnPath(const std::string& program) {
    if (program.empty()) return "";
    std::error_code ec;
    if (program.find('/') != std::string::npos || program.find('\\') != std::string::npos)
        return fs::is_regular_file(program, ec) ? program : std::string();
    const char* path = std::getenv("PATH");
    if (!path) return "";
#if defined(_WIN32)
    const char sep = ';';
    const std::vector<std::string> names = {program, program + ".exe"};
#else
    const char sep = ':';
    const std::vector<std::string> names = {program};
#endif
    std::string p = path;
    std::size_t at = 0;
    while (at <= p.size()) {
        std::size_t end = p.find(sep, at);
        if (end == std::string::npos) end = p.size();
        const std::string dir = p.substr(at, end - at);
        at = end + 1;
        // An empty or relative entry ("", ".", "bin") would mean the current folder: a git.exe planted in a project
        // must never run. Only absolute folders are searched.
        if (dir.empty() || !fs::path(dir).is_absolute()) continue;
        for (const std::string& n : names) {
            const fs::path full = fs::path(dir) / n;
            if (!fs::is_regular_file(full, ec)) continue;
#if !defined(_WIN32)
            if (::access(full.c_str(), X_OK) != 0) continue;
#endif
            return full.string();
        }
    }
    return "";
}

#if defined(_WIN32)

namespace {
/// The command line CreateProcessW gets: each argument quoted the way CommandLineToArgvW reads it back
/// (shared/AppRestart.hpp QuoteWindowsArg, the editor's relaunch uses the same).
std::wstring CommandLine(const std::vector<std::string>& argv) {
    std::wstring line;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i) line += L' ';
        line += okay::restart::QuoteWindowsArg(okay::restart::Utf8ToWide(argv[i]));
    }
    return line;
}
void ReadAll(HANDLE h, std::string& out) {
    char buf[4096];
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(h, buf, sizeof(buf), &got, nullptr) || got == 0) break;
        out.append(buf, got);
    }
}
} // namespace

RunResult Run(const std::vector<std::string>& argv, const std::string& cwd, bool capture) {
    RunResult r;
    if (argv.empty()) return r;
    const std::string exe = ResolveProgram(argv[0]);   // a full path, never from the current folder, never a .cmd/.bat
    if (exe.empty()) return r;
    const std::wstring app = okay::restart::Utf8ToWide(exe);
    std::wstring cmd = CommandLine(argv);
    const std::wstring wd = okay::restart::Utf8ToWide(cwd);
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    HANDLE outRead = nullptr, outWrite = nullptr, errFile = INVALID_HANDLE_VALUE, inNul = INVALID_HANDLE_VALUE;
    if (capture) {
        SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
        if (!CreatePipe(&outRead, &outWrite, &sa, 0)) return r;
        SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);   // the child must not hold our read end open
        wchar_t dir[MAX_PATH + 1] = {}, name[MAX_PATH + 1] = {};
        if (GetTempPathW(MAX_PATH, dir) && GetTempFileNameW(dir, L"oky", 0, name))
            errFile = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &sa,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        inNul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = outWrite;
        si.hStdError = errFile != INVALID_HANDLE_VALUE ? errFile : outWrite;
        si.hStdInput = inNul != INVALID_HANDLE_VALUE ? inNul : GetStdHandle(STD_INPUT_HANDLE);
    } else {
        // Share this program's own standard handles, so `okay build > log.txt` also catches the editor's lines.
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    }
    const BOOL ok = CreateProcessW(app.c_str(), &cmd[0], nullptr, nullptr, TRUE, capture ? CREATE_NO_WINDOW : 0,
                                   nullptr, wd.empty() ? nullptr : wd.c_str(), &si, &pi);
    if (outWrite) CloseHandle(outWrite);
    if (inNul != INVALID_HANDLE_VALUE) CloseHandle(inNul);
    if (!ok) {
        if (outRead) CloseHandle(outRead);
        if (errFile != INVALID_HANDLE_VALUE) CloseHandle(errFile);
        return r;
    }
    r.started = true;
    CloseHandle(pi.hThread);
    if (capture) { ReadAll(outRead, r.out); CloseHandle(outRead); }   // ends when the child (and its children) close stdout
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    r.exitCode = (int)code;
    CloseHandle(pi.hProcess);
    if (errFile != INVALID_HANDLE_VALUE) {
        SetFilePointer(errFile, 0, nullptr, FILE_BEGIN);
        ReadAll(errFile, r.err);
        CloseHandle(errFile);   // FILE_FLAG_DELETE_ON_CLOSE removes it
    }
    return r;
}

bool StartDetached(const std::vector<std::string>& argv, const std::string& cwd) { return StartDetached(argv, cwd, DetachOptions()); }

bool StartDetached(const std::vector<std::string>& argv, const std::string& cwd, const DetachOptions& options) {
    if (argv.empty()) return false;
    const std::string exe = ResolveProgram(argv[0]);
    if (exe.empty()) return false;
    const std::wstring app = okay::restart::Utf8ToWide(exe);
    std::wstring cmd = CommandLine(argv);
    const std::wstring wd = okay::restart::Utf8ToWide(cwd);
    std::wstring title = okay::restart::Utf8ToWide(options.title);
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    if (options.newConsole && !title.empty()) si.lpTitle = &title[0];
    PROCESS_INFORMATION pi = {};
    // PATH for the new program only: our environment with the folder put first (shared/AppRestart.hpp EnvironmentBlock).
    std::wstring env;
    if (!options.pathFirst.empty()) {
        std::wstring path = okay::restart::Utf8ToWide(options.pathFirst);
        const DWORD need = GetEnvironmentVariableW(L"PATH", nullptr, 0);
        if (need > 0) {
            std::wstring old(need, L'\0');
            const DWORD got = GetEnvironmentVariableW(L"PATH", &old[0], need);
            old.resize(got);
            if (!old.empty()) path += L";" + old;
        }
        env = okay::restart::EnvironmentBlock(L"", L"PATH", path);
    }
    const DWORD flags = (options.newConsole ? CREATE_NEW_CONSOLE : (DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP)) |
                        (env.empty() ? 0 : CREATE_UNICODE_ENVIRONMENT);
    if (!CreateProcessW(app.c_str(), &cmd[0], nullptr, nullptr, FALSE, flags, env.empty() ? nullptr : (LPVOID)env.data(),
                        wd.empty() ? nullptr : wd.c_str(), &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

// ---- Child (Windows) ---------------------------------------------------------------------------------------------
struct Child::Impl {
    HANDLE process = nullptr, job = nullptr, outRead = nullptr, errRead = nullptr;
    bool running = false, stopped = false;
    int exitCode = -1;
};

Child::Child() : impl_(new Impl()) {}
Child::~Child() {
    Stop();
    std::string a, b;
    Poll(a, b);
    delete impl_;
}

bool Child::Start(const std::vector<std::string>& argv, const std::string& cwd, std::string& error) {
    if (impl_->running) { error = "A command is still running."; return false; }
    *impl_ = Impl();
    if (argv.empty()) { error = "Nothing to run."; return false; }
    const std::string exe = ResolveProgram(argv[0]);
    if (exe.empty()) { error = "Could not find " + argv[0] + "."; return false; }
    const std::wstring app = okay::restart::Utf8ToWide(exe);
    std::wstring cmd = CommandLine(argv);
    const std::wstring wd = okay::restart::Utf8ToWide(cwd);
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    HANDLE outWrite = nullptr, errWrite = nullptr;
    if (!CreatePipe(&impl_->outRead, &outWrite, &sa, 0)) { error = "Could not make a pipe."; return false; }
    if (!CreatePipe(&impl_->errRead, &errWrite, &sa, 0)) {
        CloseHandle(impl_->outRead); CloseHandle(outWrite); impl_->outRead = nullptr;
        error = "Could not make a pipe."; return false;
    }
    SetHandleInformation(impl_->outRead, HANDLE_FLAG_INHERIT, 0);   // the child must not hold our read ends open
    SetHandleInformation(impl_->errRead, HANDLE_FLAG_INHERIT, 0);
    HANDLE inNul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = outWrite;
    si.hStdError = errWrite;
    si.hStdInput = inNul != INVALID_HANDLE_VALUE ? inNul : nullptr;
    PROCESS_INFORMATION pi = {};
    // Suspended until it is in the job, so nothing it starts can slip out before Stop could reach it.
    impl_->job = CreateJobObjectW(nullptr, nullptr);
    const BOOL ok = CreateProcessW(app.c_str(), &cmd[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr,
                                   wd.empty() ? nullptr : wd.c_str(), &si, &pi);
    CloseHandle(outWrite);
    CloseHandle(errWrite);
    if (inNul != INVALID_HANDLE_VALUE) CloseHandle(inNul);
    if (!ok) {
        error = "Could not start " + exe + " (error " + std::to_string((unsigned long)GetLastError()) + ").";
        CloseHandle(impl_->outRead); CloseHandle(impl_->errRead);
        if (impl_->job) CloseHandle(impl_->job);
        *impl_ = Impl();
        return false;
    }
    if (impl_->job && !AssignProcessToJobObject(impl_->job, pi.hProcess)) { CloseHandle(impl_->job); impl_->job = nullptr; }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    impl_->process = pi.hProcess;
    impl_->running = true;
    return true;
}

namespace {
/// Read what a pipe holds now, without waiting. False once the pipe is closed and empty.
bool ReadReady(HANDLE& h, std::string& sink) {
    if (!h) return false;
    char buf[4096];
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) { CloseHandle(h); h = nullptr; return false; }   // broken: all writers gone
        if (avail == 0) return true;
        DWORD got = 0;
        if (!ReadFile(h, buf, avail < sizeof(buf) ? avail : (DWORD)sizeof(buf), &got, nullptr) || got == 0) {
            CloseHandle(h); h = nullptr; return false;
        }
        sink.append(buf, got);
    }
}
} // namespace

bool Child::Poll(std::string& out, std::string& err) {
    Impl& m = *impl_;
    const bool outOpen = ReadReady(m.outRead, out);
    const bool errOpen = ReadReady(m.errRead, err);
    if (m.process && WaitForSingleObject(m.process, 0) == WAIT_OBJECT_0) {
        DWORD code = 1;
        GetExitCodeProcess(m.process, &code);
        m.exitCode = (int)code;
        CloseHandle(m.process);
        m.process = nullptr;
    }
    // Done when the program ended and its pipes are drained. A program it left running (okay open's editor) can hold
    // the pipes open; once our own program has ended, that does not keep the command "running".
    if (!m.process && m.running) {

        ReadReady(m.outRead, out); ReadReady(m.errRead, err);
        if (m.outRead) { CloseHandle(m.outRead); m.outRead = nullptr; }
        if (m.errRead) { CloseHandle(m.errRead); m.errRead = nullptr; }
        if (m.job) { CloseHandle(m.job); m.job = nullptr; }
        m.running = false;
        (void)outOpen; (void)errOpen;
    }
    return m.running;
}

bool Child::Running() const { return impl_->running; }
bool Child::Stopped() const { return impl_->stopped; }
int Child::ExitCode() const { return impl_->exitCode; }

void Child::Stop() {
    Impl& m = *impl_;
    if (!m.running || !m.process) return;
    m.stopped = true;
    if (m.job) TerminateJobObject(m.job, 1);   // the program and everything it started
    else TerminateProcess(m.process, 1);
    WaitForSingleObject(m.process, 2000);
}

// ---- Pty (Windows): the pseudo console ---------------------------------------------------------------------------
// Written from Microsoft's "Creating a Pseudoconsole session" and the CreatePseudoConsole, ResizePseudoConsole,
// ClosePseudoConsole and UpdateProcThreadAttribute pages. The three functions are looked up in kernel32 at run time,
// so the launcher still starts on a Windows without them (it then says the shell needs a newer Windows).
namespace {
using PseudoConsole = void*;   // HPCON
using CreatePcFn = HRESULT(WINAPI*)(COORD, HANDLE, HANDLE, DWORD, PseudoConsole*);
using ResizePcFn = HRESULT(WINAPI*)(PseudoConsole, COORD);
using ClosePcFn = void(WINAPI*)(PseudoConsole);
constexpr DWORD_PTR kAttrPseudoConsole = 0x00020016;   // PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE: attribute 22, an input value
struct PcApi {
    CreatePcFn create = nullptr;
    ResizePcFn resize = nullptr;
    ClosePcFn close = nullptr;
};
const PcApi& Api() {
    static PcApi api = [] {
        PcApi a;
        if (HMODULE k = GetModuleHandleW(L"kernel32.dll")) {
            a.create = reinterpret_cast<CreatePcFn>(reinterpret_cast<void*>(GetProcAddress(k, "CreatePseudoConsole")));
            a.resize = reinterpret_cast<ResizePcFn>(reinterpret_cast<void*>(GetProcAddress(k, "ResizePseudoConsole")));
            a.close = reinterpret_cast<ClosePcFn>(reinterpret_cast<void*>(GetProcAddress(k, "ClosePseudoConsole")));
        }
        return a;
    }();
    return api;
}
COORD SizeOf(int cols, int rows) {
    COORD c;
    c.X = (SHORT)(cols < 2 ? 2 : cols > 32767 ? 32767 : cols);
    c.Y = (SHORT)(rows < 1 ? 1 : rows > 32767 ? 32767 : rows);
    return c;
}
} // namespace

struct Pty::Impl {
    PseudoConsole pc = nullptr;
    HANDLE process = nullptr, job = nullptr, inWrite = nullptr, outRead = nullptr;
    bool running = false, stopped = false;
    int exitCode = -1;
    void Close() {
        // Our read end goes first: on older Windows ClosePseudoConsole waits until its last output is read, and with
        // the pipe closed that write fails at once instead of waiting.
        if (outRead) { CloseHandle(outRead); outRead = nullptr; }
        if (inWrite) { CloseHandle(inWrite); inWrite = nullptr; }
        if (pc && Api().close) Api().close(pc);
        pc = nullptr;
        if (process) { CloseHandle(process); process = nullptr; }
        if (job) { CloseHandle(job); job = nullptr; }
    }
};

Pty::Pty() : impl_(new Impl()) {}
Pty::~Pty() {
    Stop();
    impl_->Close();
    delete impl_;
}

bool Pty::Start(const Options& o, std::string& error) {
    if (impl_->running) { error = "The shell is still running."; return false; }
    impl_->Close();
    *impl_ = Impl();
    const PcApi& api = Api();
    if (!api.create || !api.resize || !api.close) {
        error = "This version of Windows has no pseudo console. The shell needs Windows 10 (version 1809) or newer.";
        return false;
    }
    if (o.argv.empty()) { error = "Nothing to run."; return false; }
    std::error_code ec;
    if (!fs::is_regular_file(fs::u8path(o.argv[0]), ec)) { error = "Could not find " + o.argv[0] + "."; return false; }
    HANDLE inRead = nullptr, inWrite = nullptr, outRead = nullptr, outWrite = nullptr;
    if (!CreatePipe(&inRead, &inWrite, nullptr, 0)) { error = "Could not make a pipe."; return false; }
    if (!CreatePipe(&outRead, &outWrite, nullptr, 0)) {
        CloseHandle(inRead); CloseHandle(inWrite);
        error = "Could not make a pipe.";
        return false;
    }
    PseudoConsole pc = nullptr;
    const HRESULT hr = api.create(SizeOf(o.cols, o.rows), inRead, outWrite, 0, &pc);
    // The pseudo console holds its own copies of its ends of the pipes.
    CloseHandle(inRead);
    CloseHandle(outWrite);
    if (FAILED(hr) || !pc) {
        CloseHandle(inWrite); CloseHandle(outRead);
        error = "Could not make a pseudo console (error " + std::to_string((unsigned long)hr) + ").";
        return false;
    }
    impl_->pc = pc;
    impl_->inWrite = inWrite;
    impl_->outRead = outRead;

    SIZE_T listSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &listSize);
    std::vector<unsigned char> listMem(listSize ? listSize : 64);
    auto* list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(listMem.data());
    if (!InitializeProcThreadAttributeList(list, 1, 0, &listSize) ||
        !UpdateProcThreadAttribute(list, 0, kAttrPseudoConsole, pc, sizeof(pc), nullptr, nullptr)) {
        impl_->Close();
        error = "Could not attach the pseudo console.";
        return false;
    }
    STARTUPINFOEXW si = {};
    si.StartupInfo.cb = sizeof(si);
    si.lpAttributeList = list;
    std::wstring app = okay::restart::Utf8ToWide(o.argv[0]);
    std::wstring cmd = CommandLine(o.argv);
    const std::wstring wd = okay::restart::Utf8ToWide(o.cwd);
    std::wstring env;
    if (!o.pathFirst.empty()) {
        std::wstring path = okay::restart::Utf8ToWide(o.pathFirst);
        const DWORD need = GetEnvironmentVariableW(L"PATH", nullptr, 0);
        if (need > 0) {
            std::wstring old(need, L'\0');
            const DWORD got = GetEnvironmentVariableW(L"PATH", &old[0], need);
            old.resize(got);
            if (!old.empty()) path += L";" + old;
        }
        env = okay::restart::EnvironmentBlock(L"", L"PATH", path);
    }
    // Suspended until it is in the job, so nothing it starts can slip out. The job ends the whole tree when its last
    // handle closes (Stop, or the launcher closing).
    impl_->job = CreateJobObjectW(nullptr, nullptr);
    if (impl_->job) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION li = {};
        li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(impl_->job, JobObjectExtendedLimitInformation, &li, sizeof(li));
    }
    PROCESS_INFORMATION pi = {};
    const DWORD flags = EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | (env.empty() ? 0 : CREATE_UNICODE_ENVIRONMENT);
    const BOOL ok = CreateProcessW(app.c_str(), &cmd[0], nullptr, nullptr, FALSE, flags, env.empty() ? nullptr : (LPVOID)env.data(),
                                   wd.empty() ? nullptr : wd.c_str(), &si.StartupInfo, &pi);
    const DWORD lastError = GetLastError();
    DeleteProcThreadAttributeList(list);
    if (!ok) {
        impl_->Close();
        error = "Could not start " + o.argv[0] + " (error " + std::to_string((unsigned long)lastError) + ").";
        return false;
    }
    if (impl_->job && !AssignProcessToJobObject(impl_->job, pi.hProcess)) { CloseHandle(impl_->job); impl_->job = nullptr; }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    impl_->process = pi.hProcess;
    impl_->running = true;
    return true;
}

bool Pty::Read(std::string& out) {
    Impl& m = *impl_;
    if (!m.running) return false;
    ReadReady(m.outRead, out);
    if (m.process && WaitForSingleObject(m.process, 0) == WAIT_OBJECT_0) {
        DWORD code = 1;
        GetExitCodeProcess(m.process, &code);
        m.exitCode = (int)code;
        ReadReady(m.outRead, out);   // what it wrote last
        m.Close();
        m.running = false;
    }
    return m.running;
}

bool Pty::Write(const std::string& bytes) {
    Impl& m = *impl_;
    if (!m.running || !m.inWrite) return false;
    std::size_t at = 0;
    while (at < bytes.size()) {
        DWORD put = 0;
        const DWORD n = (DWORD)std::min<std::size_t>(bytes.size() - at, 65536);
        if (!WriteFile(m.inWrite, bytes.data() + at, n, &put, nullptr) || put == 0) return false;
        at += put;
    }
    return true;
}

void Pty::Resize(int cols, int rows) {
    if (impl_->pc && Api().resize) Api().resize(impl_->pc, SizeOf(cols, rows));
}

void Pty::Stop() {
    Impl& m = *impl_;
    if (!m.running) return;
    m.stopped = true;
    if (m.job) TerminateJobObject(m.job, 1);
    else if (m.process) TerminateProcess(m.process, 1);
    if (m.process) WaitForSingleObject(m.process, 2000);
    m.exitCode = 1;
    m.Close();
    m.running = false;
}

bool Pty::Running() const { return impl_->running; }
bool Pty::Stopped() const { return impl_->stopped; }
int Pty::ExitCode() const { return impl_->exitCode; }

#else   // POSIX

namespace {
std::vector<char*> ArgvOf(const std::vector<std::string>& argv) {
    std::vector<char*> av;
    for (const std::string& s : argv) av.push_back(const_cast<char*>(s.c_str()));
    av.push_back(nullptr);
    return av;
}
void CloseFd(int& fd) { if (fd >= 0) { ::close(fd); fd = -1; } }
void ClosePipes(int (&a)[2], int (&b)[2], int (&c)[2]) {
    CloseFd(a[0]); CloseFd(a[1]); CloseFd(b[0]); CloseFd(b[1]); CloseFd(c[0]); CloseFd(c[1]);
}
/// In the child after fork: go to `cwd` and become `exe`; on failure, send errno through the close-on-exec pipe.
[[noreturn]] void ExecOrReport(const std::string& exe, std::vector<char*>& av, const std::string& cwd, int reportFd) {
    int e = 0;
    if (!cwd.empty() && ::chdir(cwd.c_str()) != 0) e = errno;
    else { ::execv(exe.c_str(), av.data()); e = errno; }
    ssize_t w = ::write(reportFd, &e, sizeof(e));
    (void)w;
    ::_exit(127);
}
/// In the parent: wait for the exec report. True when exec worked (EOF); else `childErr` holds its errno.
bool ExecWorked(int (&execP)[2], int& childErr) {
    ::close(execP[1]);
    childErr = 0;
    ssize_t got;
    do { got = ::read(execP[0], &childErr, sizeof(childErr)); } while (got < 0 && errno == EINTR);
    ::close(execP[0]);
    return got != (ssize_t)sizeof(childErr);
}
/// In the child after fork: stdout and stderr go to the two pipes' write ends, stdin is empty.
void PipesToStd(int (&outP)[2], int (&errP)[2]) {
    ::dup2(outP[1], 1);
    ::dup2(errP[1], 2);
    ::close(outP[0]); ::close(outP[1]); ::close(errP[0]); ::close(errP[1]);
    const int nul = ::open("/dev/null", O_RDONLY);
    if (nul >= 0) { ::dup2(nul, 0); if (nul > 2) ::close(nul); }
}
} // namespace

RunResult Run(const std::vector<std::string>& argv, const std::string& cwd, bool capture) {
    RunResult r;
    if (argv.empty()) return r;
    const std::string exe = ResolveProgram(argv[0]);   // a full path from PATH's absolute folders only
    if (exe.empty()) return r;
    int outP[2] = {-1, -1}, errP[2] = {-1, -1}, execP[2] = {-1, -1};
    if (capture && (::pipe(outP) != 0 || ::pipe(errP) != 0)) { CloseFd(outP[0]); CloseFd(outP[1]); return r; }
    // A close-on-exec pipe tells the parent whether exec worked: it reads EOF on success, an errno on failure.
    if (::pipe(execP) != 0) { ClosePipes(outP, errP, execP); return r; }
    ::fcntl(execP[1], F_SETFD, FD_CLOEXEC);
    std::vector<char*> av = ArgvOf(argv);
    const pid_t pid = ::fork();
    if (pid < 0) {
        ClosePipes(outP, errP, execP);
        return r;
    }
    if (pid == 0) {
        ::close(execP[0]);
        if (capture) PipesToStd(outP, errP);
        ExecOrReport(exe, av, cwd, execP[1]);
    }
    int childErr = 0;
    const bool execFailed = !ExecWorked(execP, childErr);
    if (capture) {
        CloseFd(outP[1]);
        CloseFd(errP[1]);
        char buf[4096];
        while (outP[0] >= 0 || errP[0] >= 0) {
            pollfd fds[2];
            int n = 0, idxOut = -1, idxErr = -1;
            if (outP[0] >= 0) { idxOut = n; fds[n].fd = outP[0]; fds[n].events = POLLIN; fds[n].revents = 0; ++n; }
            if (errP[0] >= 0) { idxErr = n; fds[n].fd = errP[0]; fds[n].events = POLLIN; fds[n].revents = 0; ++n; }
            const int pr = ::poll(fds, (nfds_t)n, -1);
            if (pr < 0) { if (errno == EINTR) continue; break; }
            for (int k = 0; k < n; ++k) {
                if (!fds[k].revents) continue;
                int& fd = k == idxOut ? outP[0] : errP[0];
                std::string& sink = k == idxOut ? r.out : r.err;
                const ssize_t rd = ::read(fd, buf, sizeof(buf));
                if (rd > 0) sink.append(buf, (std::size_t)rd);
                else if (rd == 0 || errno != EINTR) CloseFd(fd);
            }
            (void)idxErr;
        }
        CloseFd(outP[0]);
        CloseFd(errP[0]);
    }
    int status = 0;
    pid_t w;
    do { w = ::waitpid(pid, &status, 0); } while (w < 0 && errno == EINTR);
    if (execFailed) return r;   // not started: no such program, or the folder is missing
    r.started = true;
    if (w < 0) r.exitCode = -1;
    else if (WIFEXITED(status)) r.exitCode = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) r.exitCode = 128 + WTERMSIG(status);
    return r;
}

bool StartDetached(const std::vector<std::string>& argv, const std::string& cwd) { return StartDetached(argv, cwd, DetachOptions()); }

bool StartDetached(const std::vector<std::string>& argv, const std::string& cwd, const DetachOptions& options) {
    if (argv.empty()) return false;
    const std::string exe = ResolveProgram(argv[0]);
    if (exe.empty()) return false;
    std::vector<char*> av = ArgvOf(argv);
    std::string path;   // made before fork: the child only calls async-signal-safe functions and setenv
    if (!options.pathFirst.empty()) {
        const char* old = std::getenv("PATH");
        path = options.pathFirst + (old && *old ? std::string(":") + old : std::string());
    }
    const pid_t pid = ::fork();
    if (pid < 0) return false;
    if (pid == 0) {
        ::setsid();
        const pid_t again = ::fork();   // the grandchild is adopted by init, so nothing waits on us
        if (again != 0) ::_exit(again < 0 ? 1 : 0);
        if (!options.newConsole) {   // a terminal window keeps its own output; anything else is silenced
            const int nul = ::open("/dev/null", O_RDWR);
            if (nul >= 0) { ::dup2(nul, 0); ::dup2(nul, 1); ::dup2(nul, 2); if (nul > 2) ::close(nul); }
        }
        if (!cwd.empty() && ::chdir(cwd.c_str()) != 0) ::_exit(127);
        if (!path.empty()) ::setenv("PATH", path.c_str(), 1);
        ::execv(exe.c_str(), av.data());
        ::_exit(127);
    }
    int status = 0;
    pid_t w;
    do { w = ::waitpid(pid, &status, 0); } while (w < 0 && errno == EINTR);
    return w == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

// ---- Child (POSIX) -----------------------------------------------------------------------------------------------
struct Child::Impl {
    pid_t pid = -1;
    int outFd = -1, errFd = -1;
    bool running = false, stopped = false, exited = false;
    int exitCode = -1;
};

Child::Child() : impl_(new Impl()) {}
Child::~Child() {
    Stop();
    std::string a, b;
    for (int i = 0; i < 200 && Poll(a, b); ++i) ::usleep(5000);
    delete impl_;
}

bool Child::Start(const std::vector<std::string>& argv, const std::string& cwd, std::string& error) {
    if (impl_->running) { error = "A command is still running."; return false; }
    *impl_ = Impl();
    if (argv.empty()) { error = "Nothing to run."; return false; }
    const std::string exe = ResolveProgram(argv[0]);
    if (exe.empty()) { error = "Could not find " + argv[0] + "."; return false; }
    int outP[2] = {-1, -1}, errP[2] = {-1, -1}, execP[2] = {-1, -1};
    if (::pipe(outP) != 0 || ::pipe(errP) != 0 || ::pipe(execP) != 0) {
        ClosePipes(outP, errP, execP);
        error = "Could not make a pipe.";
        return false;
    }
    ::fcntl(execP[1], F_SETFD, FD_CLOEXEC);
    ::fcntl(outP[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(errP[0], F_SETFD, FD_CLOEXEC);
    std::vector<char*> av = ArgvOf(argv);
    const pid_t pid = ::fork();
    if (pid < 0) {
        ClosePipes(outP, errP, execP);
        error = "Could not start " + exe + ".";
        return false;
    }
    if (pid == 0) {
        ::setpgid(0, 0);   // its own process group: Stop ends it and what it started
        ::close(execP[0]);
        PipesToStd(outP, errP);
        ExecOrReport(exe, av, cwd, execP[1]);
    }
    ::setpgid(pid, pid);   // also from here, so Stop works even if it runs before the child got that far
    CloseFd(outP[1]);
    CloseFd(errP[1]);
    int childErr = 0;
    if (!ExecWorked(execP, childErr)) {
        int st = 0;
        while (::waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
        CloseFd(outP[0]); CloseFd(errP[0]);
        error = "Could not start " + exe + ": " + std::strerror(childErr) + ".";
        return false;
    }
    ::fcntl(outP[0], F_SETFL, ::fcntl(outP[0], F_GETFL) | O_NONBLOCK);
    ::fcntl(errP[0], F_SETFL, ::fcntl(errP[0], F_GETFL) | O_NONBLOCK);
    impl_->pid = pid;
    impl_->outFd = outP[0];
    impl_->errFd = errP[0];
    impl_->running = true;
    return true;
}

namespace {
/// Read what a non-blocking pipe holds now. Closes it at end of file.
void ReadReady(int& fd, std::string& sink) {
    char buf[4096];
    while (fd >= 0) {
        const ssize_t rd = ::read(fd, buf, sizeof(buf));
        if (rd > 0) { sink.append(buf, (std::size_t)rd); continue; }
        if (rd < 0 && errno == EINTR) continue;
        if (rd < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        CloseFd(fd);   // end of file, or an error
    }
}
} // namespace

bool Child::Poll(std::string& out, std::string& err) {
    Impl& m = *impl_;
    if (!m.running) return false;
    ReadReady(m.outFd, out);
    ReadReady(m.errFd, err);
    if (!m.exited) {
        int st = 0;
        pid_t w;
        do { w = ::waitpid(m.pid, &st, WNOHANG); } while (w < 0 && errno == EINTR);
        if (w == m.pid || w < 0) {
            m.exited = true;
            if (w < 0) m.exitCode = -1;
            else if (WIFEXITED(st)) m.exitCode = WEXITSTATUS(st);
            else if (WIFSIGNALED(st)) m.exitCode = 128 + WTERMSIG(st);
        }
    }
    if (m.exited) {
        // Our program ended: take what is left, and do not wait on a program it left running with the pipes open.
        ReadReady(m.outFd, out);
        ReadReady(m.errFd, err);
        CloseFd(m.outFd);
        CloseFd(m.errFd);
        m.running = false;
    }
    return m.running;
}

bool Child::Running() const { return impl_->running; }
bool Child::Stopped() const { return impl_->stopped; }
int Child::ExitCode() const { return impl_->exitCode; }

void Child::Stop() {
    Impl& m = *impl_;
    if (!m.running || m.exited || m.pid <= 0) return;
    m.stopped = true;
    ::kill(-m.pid, SIGKILL);   // the whole group: the program and what it started
    ::kill(m.pid, SIGKILL);
}

// ---- Pty (POSIX): a pseudo terminal ------------------------------------------------------------------------------
// Written from POSIX posix_openpt, grantpt, unlockpt, ptsname, setsid and the termios TIOCSWINSZ / TIOCSCTTY requests.
struct Pty::Impl {
    pid_t pid = -1;
    int master = -1;
    bool running = false, stopped = false, exited = false;
    int exitCode = -1;
};

Pty::Pty() : impl_(new Impl()) {}
Pty::~Pty() {
    Stop();
    delete impl_;
}

namespace {
/// The program's environment: ours, with TERM and COLORTERM saying what the emulator understands, PATH with a folder
/// first, and no LINES / COLUMNS left over from the terminal the launcher was started in. Made before fork.
std::vector<std::string> PtyEnvironment(const std::string& pathFirst) {
    std::vector<std::string> env;
    std::string path;
    for (char** e = environ; e && *e; ++e) {
        const std::string kv = *e;
        const std::string key = kv.substr(0, kv.find('='));
        if (key == "TERM" || key == "COLORTERM" || key == "LINES" || key == "COLUMNS") continue;
        if (key == "PATH") { path = kv.substr(5); continue; }
        env.push_back(kv);
    }
    if (!pathFirst.empty()) path = pathFirst + (path.empty() ? std::string() : ":" + path);
    if (!path.empty()) env.push_back("PATH=" + path);
    env.push_back("TERM=xterm-256color");
    env.push_back("COLORTERM=truecolor");
    return env;
}
void SetWinSize(int fd, int cols, int rows) {
    struct winsize ws = {};
    ws.ws_col = (unsigned short)(cols < 2 ? 2 : cols);
    ws.ws_row = (unsigned short)(rows < 1 ? 1 : rows);
    ::ioctl(fd, TIOCSWINSZ, &ws);
}
} // namespace

bool Pty::Start(const Options& o, std::string& error) {
    Impl& m = *impl_;
    if (m.running) { error = "The shell is still running."; return false; }
    m = Impl();
    if (o.argv.empty()) { error = "Nothing to run."; return false; }
    const std::string exe = o.argv[0];
    if (::access(exe.c_str(), X_OK) != 0) { error = "Could not find " + exe + "."; return false; }
    const int master = ::posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) { error = "Could not open a pseudo terminal."; return false; }
    if (::grantpt(master) != 0 || ::unlockpt(master) != 0) { ::close(master); error = "Could not set up the pseudo terminal."; return false; }
    const char* sn = ::ptsname(master);
    if (!sn) { ::close(master); error = "Could not name the pseudo terminal."; return false; }
    const std::string slaveName = sn;
    ::fcntl(master, F_SETFD, FD_CLOEXEC);
    SetWinSize(master, o.cols, o.rows);
    std::vector<std::string> env = PtyEnvironment(o.pathFirst);
    std::vector<char*> envp;
    for (std::string& e : env) envp.push_back(&e[0]);
    envp.push_back(nullptr);
    std::vector<char*> av = ArgvOf(o.argv);
    int execP[2] = {-1, -1};
    if (::pipe(execP) != 0) { ::close(master); error = "Could not make a pipe."; return false; }
    ::fcntl(execP[1], F_SETFD, FD_CLOEXEC);
    ::fcntl(execP[0], F_SETFD, FD_CLOEXEC);
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(master); ::close(execP[0]); ::close(execP[1]);
        error = "Could not start " + exe + ".";
        return false;
    }
    if (pid == 0) {
        // A session of its own; opening the terminal then makes it the controlling one (TIOCSCTTY for systems that
        // need asking), so Ctrl+C on it reaches the program in the foreground.
        ::setsid();
        int e = 0;
        const int slave = ::open(slaveName.c_str(), O_RDWR);
        if (slave < 0) e = errno;
        else {
#ifdef TIOCSCTTY
            ::ioctl(slave, TIOCSCTTY, 0);
#endif
            ::dup2(slave, 0); ::dup2(slave, 1); ::dup2(slave, 2);
            if (slave > 2) ::close(slave);
            ::signal(SIGINT, SIG_DFL); ::signal(SIGQUIT, SIG_DFL); ::signal(SIGPIPE, SIG_DFL);
            ::signal(SIGCHLD, SIG_DFL); ::signal(SIGTSTP, SIG_DFL); ::signal(SIGHUP, SIG_DFL);
            sigset_t none;
            sigemptyset(&none);
            ::sigprocmask(SIG_SETMASK, &none, nullptr);
            if (!o.cwd.empty() && ::chdir(o.cwd.c_str()) != 0) e = errno;
            else { ::execve(exe.c_str(), av.data(), envp.data()); e = errno; }
        }
        ssize_t w = ::write(execP[1], &e, sizeof(e));
        (void)w;
        ::_exit(127);
    }
    int childErr = 0;
    if (!ExecWorked(execP, childErr)) {
        int st = 0;
        while (::waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
        ::close(master);
        error = "Could not start " + exe + ": " + std::strerror(childErr) + ".";
        return false;
    }
    ::fcntl(master, F_SETFL, ::fcntl(master, F_GETFL) | O_NONBLOCK);
    m.pid = pid;
    m.master = master;
    m.running = true;
    return true;
}

bool Pty::Read(std::string& out) {
    Impl& m = *impl_;
    if (!m.running) return false;
    char buf[8192];
    bool eof = false;
    for (;;) {
        const ssize_t rd = ::read(m.master, buf, sizeof(buf));
        if (rd > 0) { out.append(buf, (std::size_t)rd); if (out.size() > (1u << 20)) break; continue; }
        if (rd < 0 && errno == EINTR) continue;
        if (rd < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        eof = true;   // EIO: every program on the terminal has closed it
        break;
    }
    if (!m.exited) {
        int st = 0;
        pid_t w;
        do { w = ::waitpid(m.pid, &st, WNOHANG); } while (w < 0 && errno == EINTR);
        if (w == m.pid || w < 0) {
            m.exited = true;
            if (w < 0) m.exitCode = -1;
            else if (WIFEXITED(st)) m.exitCode = WEXITSTATUS(st);
            else if (WIFSIGNALED(st)) m.exitCode = 128 + WTERMSIG(st);
        }
    }
    if (m.exited || eof) {
        if (!m.exited) {   // the terminal closed but the shell has not been reaped yet: give it a moment
            int st = 0;
            for (int i = 0; i < 100 && !m.exited; ++i) {
                const pid_t w = ::waitpid(m.pid, &st, WNOHANG);
                if (w == m.pid) { m.exited = true; m.exitCode = WIFEXITED(st) ? WEXITSTATUS(st) : WIFSIGNALED(st) ? 128 + WTERMSIG(st) : -1; }
                else ::usleep(2000);
            }
            if (!m.exited) return true;
        }
        for (;;) {   // what is left; a program the shell left running does not keep it "running"
            const ssize_t rd = ::read(m.master, buf, sizeof(buf));
            if (rd > 0) { out.append(buf, (std::size_t)rd); continue; }
            if (rd < 0 && errno == EINTR) continue;
            break;
        }
        CloseFd(m.master);
        m.running = false;
    }
    return m.running;
}

bool Pty::Write(const std::string& bytes) {
    Impl& m = *impl_;
    if (!m.running || m.master < 0) return false;
    std::size_t at = 0;
    int waits = 0;
    while (at < bytes.size()) {
        const ssize_t w = ::write(m.master, bytes.data() + at, bytes.size() - at);
        if (w > 0) { at += (std::size_t)w; continue; }
        if (w < 0 && errno == EINTR) continue;
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && waits++ < 50) {   // full: wait a little (at most 0.5 s)
            pollfd p = {m.master, POLLOUT, 0};
            ::poll(&p, 1, 10);
            continue;
        }
        return false;
    }
    return true;
}

void Pty::Resize(int cols, int rows) {
    if (impl_->master >= 0) SetWinSize(impl_->master, cols, rows);   // the kernel tells the program (SIGWINCH)
}

void Pty::Stop() {
    Impl& m = *impl_;
    if (!m.running) return;
    m.stopped = true;
    if (!m.exited && m.pid > 0) {
        ::kill(-m.pid, SIGHUP);   // its session's group, as closing a terminal window does
        ::kill(-m.pid, SIGKILL);
        ::kill(m.pid, SIGKILL);
        int st = 0;
        pid_t w;
        do { w = ::waitpid(m.pid, &st, 0); } while (w < 0 && errno == EINTR);
        m.exited = true;
        m.exitCode = 128 + SIGKILL;
    }
    CloseFd(m.master);
    m.running = false;
}

bool Pty::Running() const { return impl_->running; }
bool Pty::Stopped() const { return impl_->stopped; }
int Pty::ExitCode() const { return impl_->exitCode; }

#endif

} // namespace okaycli
