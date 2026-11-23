// okay CLI: the one place that starts another program. See Process.hpp.
// Written from the Win32 CreateProcessW / pipe documentation and POSIX fork, execvp, pipe and poll. Our own code.
// The Windows quoting, UTF-8 to UTF-16 and this program's path come from shared/AppRestart.hpp.
#include "Process.hpp"

#include "AppRestart.hpp"   // shared/: QuoteWindowsArg, Utf8ToWide, CurrentExecutable (one copy of each)

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
        if (dir.empty()) continue;
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
    }
    const BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, capture ? TRUE : FALSE, capture ? CREATE_NO_WINDOW : 0,
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

bool StartDetached(const std::vector<std::string>& argv, const std::string& cwd) {
    if (argv.empty()) return false;
    std::wstring cmd = CommandLine(argv);
    const std::wstring wd = okay::restart::Utf8ToWide(cwd);
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr,
                        wd.empty() ? nullptr : wd.c_str(), &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
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
} // namespace

RunResult Run(const std::vector<std::string>& argv, const std::string& cwd, bool capture) {
    RunResult r;
    if (argv.empty()) return r;
    int outP[2] = {-1, -1}, errP[2] = {-1, -1}, execP[2] = {-1, -1};
    if (capture && (::pipe(outP) != 0 || ::pipe(errP) != 0)) { CloseFd(outP[0]); CloseFd(outP[1]); return r; }
    // A close-on-exec pipe tells the parent whether exec worked: it reads EOF on success, an errno on failure.
    if (::pipe(execP) != 0) { CloseFd(outP[0]); CloseFd(outP[1]); CloseFd(errP[0]); CloseFd(errP[1]); return r; }
    ::fcntl(execP[1], F_SETFD, FD_CLOEXEC);
    std::vector<char*> av = ArgvOf(argv);
    const pid_t pid = ::fork();
    if (pid < 0) {
        CloseFd(outP[0]); CloseFd(outP[1]); CloseFd(errP[0]); CloseFd(errP[1]); CloseFd(execP[0]); CloseFd(execP[1]);
        return r;
    }
    if (pid == 0) {
        ::close(execP[0]);
        if (capture) {
            ::dup2(outP[1], 1);
            ::dup2(errP[1], 2);
            ::close(outP[0]); ::close(outP[1]); ::close(errP[0]); ::close(errP[1]);
            const int nul = ::open("/dev/null", O_RDONLY);
            if (nul >= 0) { ::dup2(nul, 0); if (nul > 2) ::close(nul); }
        }
        int e = 0;
        if (!cwd.empty() && ::chdir(cwd.c_str()) != 0) e = errno;
        else { ::execvp(av[0], av.data()); e = errno; }
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

bool StartDetached(const std::vector<std::string>& argv, const std::string& cwd) {
    if (argv.empty()) return false;
    if (FindOnPath(argv[0]).empty()) return false;
    std::vector<char*> av = ArgvOf(argv);
    const pid_t pid = ::fork();
    if (pid < 0) return false;
    if (pid == 0) {
        ::setsid();
        const pid_t again = ::fork();   // the grandchild is adopted by init, so nothing waits on us
        if (again != 0) ::_exit(again < 0 ? 1 : 0);
        const int nul = ::open("/dev/null", O_RDWR);
        if (nul >= 0) { ::dup2(nul, 0); ::dup2(nul, 1); ::dup2(nul, 2); if (nul > 2) ::close(nul); }
        if (!cwd.empty() && ::chdir(cwd.c_str()) != 0) ::_exit(127);
        ::execvp(av[0], av.data());
        ::_exit(127);
    }
    int status = 0;
    pid_t w;
    do { w = ::waitpid(pid, &status, 0); } while (w < 0 && errno == EINTR);
    return w == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

#endif

} // namespace okaycli
