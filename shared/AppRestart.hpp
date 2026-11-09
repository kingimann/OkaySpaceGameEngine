#pragma once
// Detached install-and-relaunch for the editor and the launcher.
//
// The old updater started the new process while this process still held the
// single-instance lock (and, on Windows, launched a cmd.exe batch through
// `start` with the exit code ignored). A second instance then exited because
// the lock was still held, or the helper never survived to launch anything.
// This helper is a separate process: it waits until the old process has
// exited, swaps the staged file only then, and starts the new process once
// with the original arguments and working directory.
//
// Integrity checks stay with the caller (size, and the editor's MZ check).
// A failed swap puts the previous file back. Nothing here deletes a project,
// settings, or any path other than the staged file, the install target, and
// that target's backup.

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <cerrno>
#  include <csignal>
#  include <fcntl.h>
#  include <spawn.h>
#  include <sys/stat.h>
#  include <sys/wait.h>
#  include <unistd.h>
extern char** environ;
#endif

#if defined(__APPLE__)
#  include <mach-o/dyld.h>
#endif

namespace okay::restart {
namespace fs = std::filesystem;

// GetModuleFileNameA / a MAX_PATH buffer truncates and drops characters that
// are not in the ANSI code page. The Windows path buffer is much larger and
// the wide API is what CurrentExecutable() uses.
inline constexpr std::size_t kWindowsModulePathCapacity = 32768;

// Set to "1" only from tests. The swap moves the live file aside and then
// refuses to install, so the previous file is restored. It does not skip a
// check or accept a bad payload.
inline constexpr const char* kFailAfterBackupEnv = "OKAY_RESTART_TEST_FAIL_AFTER_BACKUP";

inline std::string Utf8(const fs::path& p) {
#if defined(_WIN32)
    const std::wstring w = p.wstring();
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
#else
    return p.string();
#endif
}

inline fs::path FromUtf8(const std::string& s) {
    if (s.empty()) return {};
#if defined(_WIN32)
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), w.data(), n);
    return fs::path(w);
#else
    return fs::path(s);
#endif
}

#if defined(_WIN32)
inline std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return {};
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), w.data(), n);
    return w;
}
#endif

inline fs::path CurrentExecutable() {
#if defined(_WIN32)
    std::wstring buf(kWindowsModulePathCapacity, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
    if (n == 0 || n >= buf.size()) return {};
    buf.resize(n);
    return fs::path(buf);
#elif defined(__APPLE__)
    char small[1024];
    std::uint32_t size = sizeof(small);
    if (_NSGetExecutablePath(small, &size) == 0) return fs::path(small);
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
    return fs::path(buf.c_str());
#else
    std::string buf(512, '\0');
    for (;;) {
        const ssize_t n = ::readlink("/proc/self/exe", buf.data(), buf.size());
        if (n < 0) return {};
        if (static_cast<std::size_t>(n) < buf.size()) {
            buf.resize(static_cast<std::size_t>(n));
            return fs::path(buf);
        }
        if (buf.size() >= kWindowsModulePathCapacity) return {};
        buf.assign(buf.size() * 2, '\0');
    }
#endif
}

inline unsigned long CurrentPid() {
#if defined(_WIN32)
    return (unsigned long)GetCurrentProcessId();
#else
    return (unsigned long)::getpid();
#endif
}

inline std::vector<std::string>& StoredArguments() {
    static std::vector<std::string> args;
    return args;
}

// argv[1..] of this process, so a relaunch can keep `--launcher <pid>` and
// anything else the user opened with. Not the helper's own flag.
inline void RememberArguments(int argc, char** argv) {
    StoredArguments().clear();
    for (int i = 1; i < argc; ++i) {
        if (!argv[i]) continue;
        if (std::strcmp(argv[i], "--okay-restart-helper") == 0) continue;
        StoredArguments().emplace_back(argv[i]);
    }
}

inline const std::vector<std::string>& CurrentArguments() { return StoredArguments(); }

// CommandLineToArgvW quoting (backslash and quote). Used so a path with
// spaces, parentheses, '&', or a trailing slash stays one argument — the
// cmd.exe `start` line this replaces did not.
inline std::wstring QuoteWindowsArg(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
    std::wstring out;
    out.push_back(L'"');
    std::size_t i = 0;
    while (i < arg.size()) {
        std::size_t bs = 0;
        while (i < arg.size() && arg[i] == L'\\') { ++bs; ++i; }
        if (i == arg.size()) {
            out.append(bs * 2, L'\\');
            break;
        }
        if (arg[i] == L'"') {
            out.append(bs * 2 + 1, L'\\');
            out.push_back(L'"');
            ++i;
        } else {
            out.append(bs, L'\\');
            out.push_back(arg[i]);
            ++i;
        }
    }
    out.push_back(L'"');
    return out;
}

// What `cmd /c` does with quotes, from `cmd /?`. Exactly two quotes are kept
// only when the text between them has whitespace, has none of &<>()@^|, and
// cmd decides that text names an executable. Otherwise a leading quote is
// stripped along with the last quote, and `C:\Program Files\...` becomes the
// program `C:\Program`.
struct LegacyCmdQuote {
    bool preserved = false;
    std::string command;
};

inline LegacyCmdQuote LegacyCmdSlashC(const std::string& remainder, bool betweenQuotesIsExecutable) {
    int quotes = 0;
    for (unsigned char c : remainder) if (c == '"') ++quotes;
    if (quotes == 2) {
        const auto a = remainder.find('"');
        const auto b = remainder.find('"', a + 1);
        const std::string mid = remainder.substr(a + 1, b - a - 1);
        const bool whitespace = mid.find_first_of(" \t") != std::string::npos;
        const bool special = mid.find_first_of("&<>()@^|") != std::string::npos;
        if (!special && whitespace && betweenQuotesIsExecutable) return {true, remainder};
    }
    if (!remainder.empty() && remainder.front() == '"') {
        std::string s = remainder.substr(1);
        const auto last = s.rfind('"');
        if (last != std::string::npos) s.erase(last, 1);
        return {false, s};
    }
    return {false, remainder};
}

// The editor used to write okay_update.bat and run this, ignoring the exit code.
inline std::string LegacyEditorRestartCommand(const std::string& batPath) {
    return "start \"\" /min cmd /c \"" + batPath + "\"";
}
inline std::string LegacyInnerCmdRemainder(const std::string& batPath) {
    return "\"" + batPath + "\"";
}

struct Request {
    fs::path executable;
    std::vector<std::string> arguments;
    fs::path workingDirectory;
    unsigned long waitForPid = 0;
    fs::path logFile;
    fs::path replaceTarget;
    fs::path replaceSource;
    fs::path replaceBackup;
    std::uint64_t minBytes = 0;
    int timeoutMs = 120000;
    bool keepBackup = true;
};

struct SwapResult {
    bool ok = false;
    std::string error;
};

inline std::string Stamp() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

inline void AppendLog(const fs::path& log, const std::string& message) {
    const std::string line = Stamp() + " [" + std::to_string(CurrentPid()) + "] " + message + "\n";
    if (!log.empty()) {
        std::error_code ec;
        if (!log.parent_path().empty()) fs::create_directories(log.parent_path(), ec);
        std::ofstream o(log, std::ios::app | std::ios::binary);
        if (o) { o << line; o.flush(); return; }
    }
    std::fputs(line.c_str(), stderr);
}

inline std::string Escape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (unsigned char c : s) {
        if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else o += (char)c;
    }
    return o;
}

inline std::string Unescape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            const char n = s[++i];
            if (n == 'n') o += '\n';
            else if (n == 'r') o += '\r';
            else o += n;
        } else o += s[i];
    }
    return o;
}

inline bool WriteRequest(const Request& req, fs::path& outPath, std::string& error) {
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec);
    if (ec || dir.empty()) { error = "no temp directory for the restart request"; return false; }
    const auto n = std::chrono::steady_clock::now().time_since_epoch().count();
    outPath = dir / ("okay_restart_" + std::to_string(CurrentPid()) + "_" + std::to_string((long long)n) + ".req");
    std::ofstream o(outPath, std::ios::binary);
    if (!o) { error = "couldn't write the restart request"; return false; }
    auto field = [&](const char* key, const std::string& value) {
        o << key << '=' << Escape(value) << '\n';
    };
    field("wait_pid", std::to_string(req.waitForPid));
    field("timeout_ms", std::to_string(req.timeoutMs));
    field("min_bytes", std::to_string(req.minBytes));
    field("keep_backup", req.keepBackup ? "1" : "0");
    field("cwd", Utf8(req.workingDirectory));
    field("exe", Utf8(req.executable));
    field("log", Utf8(req.logFile));
    field("replace_target", Utf8(req.replaceTarget));
    field("replace_source", Utf8(req.replaceSource));
    field("replace_backup", Utf8(req.replaceBackup));
    for (const auto& a : req.arguments) field("arg", a);
    o.flush();
    if (!o) { error = "couldn't finish the restart request"; fs::remove(outPath, ec); return false; }
    return true;
}

inline bool LoadRequest(const fs::path& path, Request& req, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { error = "couldn't read the restart request"; return false; }
    req = Request{};
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string value = Unescape(line.substr(eq + 1));
        if (key == "wait_pid") req.waitForPid = std::strtoul(value.c_str(), nullptr, 10);
        else if (key == "timeout_ms") req.timeoutMs = std::atoi(value.c_str());
        else if (key == "min_bytes") req.minBytes = std::strtoull(value.c_str(), nullptr, 10);
        else if (key == "keep_backup") req.keepBackup = value != "0";
        else if (key == "cwd") req.workingDirectory = FromUtf8(value);
        else if (key == "exe") req.executable = FromUtf8(value);
        else if (key == "log") req.logFile = FromUtf8(value);
        else if (key == "replace_target") req.replaceTarget = FromUtf8(value);
        else if (key == "replace_source") req.replaceSource = FromUtf8(value);
        else if (key == "replace_backup") req.replaceBackup = FromUtf8(value);
        else if (key == "arg") req.arguments.push_back(value);
    }
    if (req.executable.empty() && req.replaceTarget.empty()) {
        error = "restart request has nothing to install or launch";
        return false;
    }
    return true;
}

inline SwapResult SwapFile(const fs::path& dest, const fs::path& staged, const fs::path& backup,
                           std::uint64_t minBytes, bool keepBackup) {
    std::error_code ec;
    if (staged.empty() || !fs::is_regular_file(staged, ec) || ec)
        return {false, "staged update is missing"};
    const auto sz = fs::file_size(staged, ec);
    if (ec) return {false, "couldn't read the staged update: " + ec.message()};
    if (sz < minBytes) return {false, "staged update is too small (" + std::to_string(sz) + " bytes)"};

    bool moved = false;
    if (fs::exists(dest, ec)) {
        if (backup.empty()) return {false, "no backup path for the existing program"};
        fs::remove(backup, ec);
        ec.clear();
        if (fs::exists(backup, ec))
            return {false, "couldn't move the previous backup aside (" + Utf8(backup) + ")"};
        ec.clear();
        fs::rename(dest, backup, ec);
        if (ec) return {false, "couldn't move the existing program aside: " + ec.message()};
        moved = true;
    }
    if (const char* inj = std::getenv(kFailAfterBackupEnv); inj && inj[0] && inj[0] != '0') {
        std::error_code ec2;
        if (moved) fs::rename(backup, dest, ec2);
        if (ec2) return {false, "restore after a failed install failed: " + ec2.message()};
        return {false, moved ? "install aborted; restored the previous program"
                             : "install aborted; the existing program was not changed"};
    }
    ec.clear();
    fs::rename(staged, dest, ec);
    if (ec) {
        std::error_code ec2;
        std::string extra;
        if (moved) {
            fs::rename(backup, dest, ec2);
            extra = ec2 ? " and couldn't restore the previous program: " + ec2.message()
                        : "; restored the previous program";
        }
        return {false, "couldn't install the update: " + ec.message() + extra};
    }
#if !defined(_WIN32)
    fs::permissions(dest,
                    fs::perms::owner_read | fs::perms::owner_exec |
                    fs::perms::group_read | fs::perms::group_exec |
                    fs::perms::others_read | fs::perms::others_exec,
                    fs::perm_options::add, ec);
    std::error_code stEc;
    const auto perms = fs::status(dest, stEc).permissions();
    const bool executable = !stEc && (perms & fs::perms::owner_exec) != fs::perms::none;
    if (!executable) {
        std::error_code ec2, ec3;
        if (!fs::exists(staged, ec2)) fs::rename(dest, staged, ec2);
        if (moved) fs::rename(backup, dest, ec3);
        if ((moved && ec3) || ec2)
            return {false, "updated program isn't executable and restoring the previous one failed"};
        return {false, "couldn't mark the updated program executable; restored the previous program"};
    }
#endif
    if (!keepBackup && moved) fs::remove(backup, ec);
    return {true, {}};
}

inline bool PidAlive(unsigned long pid) {
    if (pid == 0) return false;
#if defined(_WIN32)
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    if (!h) return GetLastError() == ERROR_ACCESS_DENIED;
    const DWORD w = WaitForSingleObject(h, 0);
    CloseHandle(h);
    return w == WAIT_TIMEOUT;
#else
    if (::kill(static_cast<pid_t>(pid), 0) == 0) return true;
    return errno == EPERM;
#endif
}

inline bool WaitForExit(unsigned long pid, int timeoutMs, std::string& error) {
    if (pid == 0) return true;
    const auto start = std::chrono::steady_clock::now();
    while (PidAlive(pid)) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= timeoutMs) {
            error = "timed out after " + std::to_string(timeoutMs) +
                    "ms waiting for process " + std::to_string(pid) +
                    " to exit; no files were changed";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return true;
}

#if !defined(_WIN32)
inline bool LaunchOnce(const Request& req, std::string& error) {
    int fds[2] = {-1, -1};
    if (::pipe(fds) != 0) { error = std::string("pipe: ") + std::strerror(errno); return false; }
    if (::fcntl(fds[1], F_SETFD, FD_CLOEXEC) != 0) {
        error = std::string("fcntl: ") + std::strerror(errno);
        ::close(fds[0]); ::close(fds[1]);
        return false;
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        error = std::string("fork: ") + std::strerror(errno);
        ::close(fds[0]); ::close(fds[1]);
        return false;
    }
    if (pid == 0) {
        ::close(fds[0]);
        if (!req.workingDirectory.empty()) {
            const std::string cwd = Utf8(req.workingDirectory);
            if (::chdir(cwd.c_str()) != 0) {
                const int e = errno;
                ssize_t wr = ::write(fds[1], &e, sizeof(e));
                (void)wr;
                _exit(126);
            }
        }
        ::unsetenv("OKAY_RESTART_REQUEST");
        std::vector<std::string> store;
        store.push_back(Utf8(req.executable));
        for (const auto& a : req.arguments) store.push_back(a);
        std::vector<char*> av;
        av.reserve(store.size() + 1);
        for (auto& s : store) av.push_back(s.data());
        av.push_back(nullptr);
        ::execv(store[0].c_str(), av.data());
        const int e = errno;
        ssize_t wr = ::write(fds[1], &e, sizeof(e));
        (void)wr;
        _exit(127);
    }
    ::close(fds[1]);
    int e = 0;
    const ssize_t n = ::read(fds[0], &e, sizeof(e));
    ::close(fds[0]);
    if (n == (ssize_t)sizeof(e)) {
        int st = 0;
        while (::waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
        error = std::strerror(e);
        return false;
    }
    return true;
}
#else
inline std::wstring EnvironmentBlock(const std::wstring& dropKey, const std::wstring& addKey, const std::wstring& addValue) {
    std::wstring block;
    LPWCH env = GetEnvironmentStringsW();
    if (env) {
        for (LPWCH p = env; *p;) {
            const std::wstring e = p;
            p += e.size() + 1;
            auto skip = [&](const std::wstring& key) {
                if (key.empty() || e.size() < key.size() + 1) return false;
                if (_wcsnicmp(e.c_str(), key.c_str(), (int)key.size()) != 0) return false;
                return e[key.size()] == L'=';
            };
            if (skip(dropKey) || skip(addKey)) continue;
            block += e;
            block.push_back(L'\0');
        }
        FreeEnvironmentStringsW(env);
    }
    if (!addKey.empty()) {
        block += addKey + L"=" + addValue;
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

inline bool LaunchOnce(const Request& req, std::string& error) {
    const std::wstring exe = req.executable.wstring();
    if (exe.empty()) { error = "no program path"; return false; }
    std::wstring cmd = QuoteWindowsArg(exe);
    for (const auto& a : req.arguments) {
        cmd.push_back(L' ');
        cmd += QuoteWindowsArg(Utf8ToWide(a));
    }
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    std::wstring env = EnvironmentBlock(L"OKAY_RESTART_REQUEST", L"", L"");
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    const std::wstring cwd = req.workingDirectory.wstring();
    DWORD flags = CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS | CREATE_BREAKAWAY_FROM_JOB;
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, flags,
                             env.data(), cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    if (!ok && (flags & CREATE_BREAKAWAY_FROM_JOB) && GetLastError() == ERROR_ACCESS_DENIED) {
        flags &= ~CREATE_BREAKAWAY_FROM_JOB;
        ok = CreateProcessW(exe.c_str(), buf.data(), nullptr, nullptr, FALSE, flags,
                            env.data(), cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    }
    if (!ok) {
        error = "CreateProcess failed (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}
#endif

inline int RunHelper(const Request& req) {
    auto log = [&](const std::string& m) { AppendLog(req.logFile, m); };
    log("helper started; waiting for process " + std::to_string(req.waitForPid));
    std::string err;
    if (!WaitForExit(req.waitForPid, req.timeoutMs, err)) {
        log(err);
        return 1;
    }
    if (req.waitForPid != 0)
        log("process " + std::to_string(req.waitForPid) + " has exited");

    if (!req.replaceTarget.empty()) {
        const SwapResult sw = SwapFile(req.replaceTarget, req.replaceSource, req.replaceBackup,
                                       req.minBytes, req.keepBackup);
        if (!sw.ok) {
            log("install failed: " + sw.error);
            return 1;
        }
        log("replaced " + Utf8(req.replaceTarget));
    }
    if (req.executable.empty()) {
        log("no program to launch");
        return 1;
    }
    // Launch the path we were given. When a swap was requested that path is
    // the install target, which now holds the staged file.
    if (!LaunchOnce(req, err)) {
        log("launch failed: " + err);
        if (!req.replaceBackup.empty())
            log("previous version kept at " + Utf8(req.replaceBackup));
        return 1;
    }
    log("launched " + Utf8(req.executable) + " once");
    return 0;
}

#if !defined(_WIN32)
inline bool SpawnHelperProcess(const fs::path& self, const fs::path& reqPath, const fs::path& logFile, std::string& error) {
    std::string exe = Utf8(self);
    std::string flag = "--okay-restart-helper";
    std::string reqEnv = "OKAY_RESTART_REQUEST=" + Utf8(reqPath);
    std::string log = Utf8(logFile);
    std::vector<std::string> envStore;
    for (char** e = environ; e && *e; ++e) {
        std::string s(*e);
        if (s.rfind("OKAY_RESTART_REQUEST=", 0) == 0) continue;
        envStore.push_back(std::move(s));
    }
    envStore.push_back(std::move(reqEnv));
    std::vector<char*> envp;
    envp.reserve(envStore.size() + 1);
    for (auto& s : envStore) envp.push_back(s.data());
    envp.push_back(nullptr);
    char* argv[] = {exe.data(), flag.data(), nullptr};

    const pid_t mid = ::fork();
    if (mid < 0) { error = std::string("fork: ") + std::strerror(errno); return false; }
    if (mid == 0) {
        ::setsid();
        const pid_t g = ::fork();
        if (g < 0) _exit(127);
        if (g > 0) _exit(0);
        sigset_t set;
        ::sigfillset(&set);
        ::sigprocmask(SIG_UNBLOCK, &set, nullptr);
        for (int fd = 0; fd < 1024; ++fd) {
            const int fl = ::fcntl(fd, F_GETFD);
            if (fl >= 0) ::fcntl(fd, F_SETFD, fl | FD_CLOEXEC);
        }
        ::execve(exe.c_str(), argv, envp.data());
        int fd = -1;
        if (!log.empty()) fd = ::open(log.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        const char msg[] = "helper failed to start (execve)\n";
        if (fd >= 0) {
            ssize_t wr = ::write(fd, msg, sizeof(msg) - 1);
            (void)wr;
        }
        _exit(127);
    }
    int st = 0;
    while (::waitpid(mid, &st, 0) < 0 && errno == EINTR) {}
    return true;
}
#else
inline bool SpawnHelperProcess(const fs::path& self, const fs::path& reqPath, const fs::path& logFile, std::string& error) {
    (void)logFile;
    std::wstring cmd = QuoteWindowsArg(self.wstring()) + L" --okay-restart-helper";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    std::wstring env = EnvironmentBlock(L"OKAY_RESTART_REQUEST", L"OKAY_RESTART_REQUEST", reqPath.wstring());
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    DWORD flags = CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP | DETACHED_PROCESS | CREATE_BREAKAWAY_FROM_JOB;
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(self.wstring().c_str(), buf.data(), nullptr, nullptr, FALSE, flags,
                             env.data(), nullptr, &si, &pi);
    if (!ok && GetLastError() == ERROR_ACCESS_DENIED) {
        flags &= ~CREATE_BREAKAWAY_FROM_JOB;
        ok = CreateProcessW(self.wstring().c_str(), buf.data(), nullptr, nullptr, FALSE, flags,
                            env.data(), nullptr, &si, &pi);
    }
    if (!ok) {
        error = "couldn't start the updater helper (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}
#endif

// Starts the helper and returns. The caller must exit so the wait can finish.
// On false, nothing was replaced and the caller should stay open.
inline bool StartHelper(const Request& req, std::string& error) {
    error.clear();
    const fs::path self = CurrentExecutable();
    if (self.empty()) { error = "couldn't find this program to run the updater helper"; return false; }
    if (req.executable.empty()) { error = "no program to relaunch"; return false; }
    fs::path reqPath;
    if (!WriteRequest(req, reqPath, error)) return false;
    AppendLog(req.logFile, "starting detached helper for " + Utf8(req.executable));
    if (!SpawnHelperProcess(self, reqPath, req.logFile, error)) {
        std::error_code ec;
        fs::remove(reqPath, ec);
        AppendLog(req.logFile, "helper did not start: " + error);
        return false;
    }
    return true;
}

// Run as the first thing in main. Returns -1 when this is a normal launch.
// Otherwise the process is the helper and the return value is its exit code.
inline int DispatchHelper(int argc, char** argv) {
    bool flag = false;
    for (int i = 1; i < argc; ++i)
        if (argv[i] && std::strcmp(argv[i], "--okay-restart-helper") == 0) flag = true;
    if (!flag) return -1;
    fs::path reqPath;
#if defined(_WIN32)
    std::wstring w(kWindowsModulePathCapacity, L'\0');
    const DWORD n = GetEnvironmentVariableW(L"OKAY_RESTART_REQUEST", w.data(), (DWORD)w.size());
    if (n == 0 || n >= w.size()) {
        std::fputs("okay restart helper: missing request\n", stderr);
        return 2;
    }
    w.resize(n);
    reqPath = fs::path(w);
#else
    const char* e = std::getenv("OKAY_RESTART_REQUEST");
    if (!e || !*e) {
        std::fputs("okay restart helper: missing request\n", stderr);
        return 2;
    }
    reqPath = FromUtf8(e);
#endif
    Request req;
    std::string err;
    if (!LoadRequest(reqPath, req, err)) {
        std::fprintf(stderr, "okay restart helper: %s\n", err.c_str());
        return 2;
    }
    std::error_code ec;
    fs::remove(reqPath, ec);
    return RunHelper(req);
}

// Blocks until the program exits. argv is not passed through a shell.
// Returns the exit code, or -1 if it could not be started.
inline int RunAndWait(const std::vector<std::string>& args) {
    if (args.empty() || args[0].empty()) return -1;
#if defined(_WIN32)
    std::wstring cmd;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i) cmd.push_back(L' ');
        cmd += QuoteWindowsArg(Utf8ToWide(args[i]));
    }
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
                        nullptr, nullptr, &si, &pi))
        return -1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
#else
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) return -1;
    const bool opened =
        posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDWR, 0) == 0 &&
        posix_spawn_file_actions_adddup2(&actions, STDIN_FILENO, STDOUT_FILENO) == 0 &&
        posix_spawn_file_actions_adddup2(&actions, STDIN_FILENO, STDERR_FILENO) == 0;
    std::vector<char*> av;
    av.reserve(args.size() + 1);
    for (const auto& s : args) av.push_back(const_cast<char*>(s.c_str()));
    av.push_back(nullptr);
    pid_t pid = 0;
    const int rc = opened ? posix_spawnp(&pid, av[0], &actions, nullptr, av.data(), environ) : -1;
    posix_spawn_file_actions_destroy(&actions);
    if (!opened || rc != 0) return -1;
    int st = 0;
    while (::waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    return -1;
#endif
}

}  // namespace okay::restart
