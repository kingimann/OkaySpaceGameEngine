// okay CLI: the one place that starts another program. See Process.hpp.
// Written from the Win32 CreateProcessW / pipe documentation and POSIX fork, execv, pipe and poll. Our own code.
// The Windows quoting, UTF-8 to UTF-16 and this program's path come from shared/AppRestart.hpp.
#include "Process.hpp"

#include "AppRestart.hpp"
#include "okay/Core/StringUtil.hpp"   // shared/: QuoteWindowsArg, Utf8ToWide, CurrentExecutable (one copy of each)

#include <cstdlib>
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
#include <sys/wait.h>
#include <unistd.h>
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
        int e = 0;
        if (!cwd.empty() && ::chdir(cwd.c_str()) != 0) e = errno;
        else { ::execv(exe.c_str(), av.data()); e = errno; }
        ssize_t w = ::write(execP[1], &e, sizeof(e));
        (void)w;
        ::_exit(127);
    }
    ::close(execP[1]);
    int childErr = 0;
    ssize_t got;
    do { got = ::read(execP[0], &childErr, sizeof(childErr)); } while (got < 0 && errno == EINTR);
    ::close(execP[0]);
    const bool execFailed = got == (ssize_t)sizeof(childErr);
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
        int e = 0;
        if (!cwd.empty() && ::chdir(cwd.c_str()) != 0) e = errno;
        else { ::execv(exe.c_str(), av.data()); e = errno; }
        ssize_t w = ::write(execP[1], &e, sizeof(e));
        (void)w;
        ::_exit(127);
    }
    ::setpgid(pid, pid);   // also from here, so Stop works even if it runs before the child got that far
    CloseFd(outP[1]);
    CloseFd(errP[1]);
    ::close(execP[1]);
    int childErr = 0;
    ssize_t got;
    do { got = ::read(execP[0], &childErr, sizeof(childErr)); } while (got < 0 && errno == EINTR);   // EOF once exec worked
    ::close(execP[0]);
    if (got == (ssize_t)sizeof(childErr)) {
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

#endif

} // namespace okaycli
