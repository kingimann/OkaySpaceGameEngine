// The real download behind the Versions page. See VersionsNet.hpp.
#include "VersionsNet.hpp"

#include <ctime>

#include "AppRestart.hpp"   // Utf8, QuoteWindowsArg, the posix_spawn headers
#include "okay/Platform/NativeFetch.hpp"   // the HTTP seam: WinHTTP / our own client

// (No std::thread / std::mutex here: the launcher is built with the MinGW win32 thread model. A native sleep is all this file needs.)

namespace okayversions::net {
namespace fs = std::filesystem;
namespace {
void SleepMs(int ms) {
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    timespec ts{ms / 1000, (long)(ms % 1000) * 1000000L};
    while (::nanosleep(&ts, &ts) != 0 && errno == EINTR) {}
#endif
}
}  // namespace

RunResult RunProcess(const std::vector<std::string>& args, const std::function<bool()>& keepGoing, int pollMs) {
    RunResult res;
    if (args.empty() || args[0].empty()) return res;
    if (pollMs < 10) pollMs = 10;
#if defined(_WIN32)
    std::wstring cmd;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i) cmd.push_back(L' ');
        cmd += okay::restart::QuoteWindowsArg(okay::restart::Utf8ToWide(args[i]));
    }
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &si, &pi))
        return res;
    res.started = true;
    for (;;) {
        if (WaitForSingleObject(pi.hProcess, (DWORD)pollMs) == WAIT_OBJECT_0) {
            DWORD code = 1;
            GetExitCodeProcess(pi.hProcess, &code);
            res.exitCode = (int)code;
            break;
        }
        if (keepGoing && !keepGoing()) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
            res.stopped = true;
            break;
        }
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return res;
#else
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) return res;
    const bool opened =
        posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDWR, 0) == 0 &&
        posix_spawn_file_actions_adddup2(&actions, STDIN_FILENO, STDOUT_FILENO) == 0 &&
        posix_spawn_file_actions_adddup2(&actions, STDIN_FILENO, STDERR_FILENO) == 0;
    std::vector<char*> av;
    av.reserve(args.size() + 1);
    for (const std::string& s : args) av.push_back(const_cast<char*>(s.c_str()));
    av.push_back(nullptr);
    pid_t pid = 0;
    const int rc = opened ? posix_spawnp(&pid, av[0], &actions, nullptr, av.data(), environ) : -1;
    posix_spawn_file_actions_destroy(&actions);
    if (!opened || rc != 0) return res;
    res.started = true;
    for (;;) {
        int st = 0;
        const pid_t w = ::waitpid(pid, &st, WNOHANG);
        if (w == pid) { res.exitCode = WIFEXITED(st) ? WEXITSTATUS(st) : -1; break; }
        if (w < 0 && errno != EINTR) break;
        if (keepGoing && !keepGoing()) {
            ::kill(pid, SIGTERM);
            for (int i = 0; i < 40; ++i) {                      // up to two seconds to leave, then it is killed
                if (::waitpid(pid, &st, WNOHANG) == pid) { res.stopped = true; return res; }
                SleepMs(50);
            }
            ::kill(pid, SIGKILL);
            while (::waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
            res.stopped = true;
            return res;
        }
        SleepMs(pollMs);
    }
    return res;
#endif
}

DownloadResult DownloadFile(const std::string& url, const fs::path& dest, std::uint64_t maxBytes,
                            const std::function<void(std::uint64_t)>& onBytes, const std::function<bool()>& cancelled) {
    DownloadResult out;
    if (!IsAcceptableUrl(url)) { out.error = "That download address is not allowed."; return out; }
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    fs::remove(dest, ec);
    // The HTTP seam (NativeFetch.hpp): WinHTTP on Windows, our own client on Linux (an https -> http redirect is always
    // refused). The file goes to a temporary "<dest>.<...>.part" first and is renamed when complete; a dropped connection
    // is resumed with Range (two more tries, with backoff). The caller still checks the SHA-256 against versions.json.
    okay::http::OwnDownloadSpec sp;
    sp.userAgent = "OkaySpace-Launcher";
    sp.maxBytes = maxBytes;
    sp.maxRedirects = 5;
    sp.connectMs = 20000;
    sp.ioMs = 60000;
    sp.totalMs = 6 * 60 * 60 * 1000;
    sp.retries = 2;
    sp.onBytes = onBytes;
    sp.cancelled = cancelled;
    const okay::http::OwnDownloadResult r = okay::http::FetchToFile(url, dest, sp);
    switch (r.outcome) {
    case okay::http::OwnOutcome::Ok: out.status = DownloadStatus::Ok; return out;
    case okay::http::OwnOutcome::Cancelled: out.status = DownloadStatus::Cancelled; out.error = "Cancelled."; break;
    case okay::http::OwnOutcome::TooBig: out.error = "The server sent more data than the file should have."; break;
    case okay::http::OwnOutcome::DiskFull: out.status = DownloadStatus::DiskFull; out.error = "The disk is full."; break;
    case okay::http::OwnOutcome::ChecksumMismatch: out.error = "The download is damaged (its checksum is wrong)."; break;
    case okay::http::OwnOutcome::HttpError:
        out.error = r.status >= 400 ? "The download failed (the server does not have that file)."
                                    : "The download failed (the server answered " + std::to_string(r.status) + ").";
        break;
    case okay::http::OwnOutcome::NotHandled:
        out.error = "The download failed (" + (r.error.empty() ? std::string("no answer from the server") : r.error) + ").";
        break;
    }
    fs::remove(dest, ec);
    return out;
}

}  // namespace okayversions::net
