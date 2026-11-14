// The real download behind the Versions page. See VersionsNet.hpp.
#include "VersionsNet.hpp"

#include <ctime>

#include "AppRestart.hpp"   // Utf8, QuoteWindowsArg, the posix_spawn headers
#if !defined(_WIN32)
#  include "okay/Platform/NativeHttp.hpp"   // our own HTTP client, only when built with OKAY_OWN_HTTP and run with OKAY_OWN_HTTP=1
#endif

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
#if defined(OKAY_OWN_HTTP_ACTIVE)
    // Our own client first (opt-in, docs/own_http_client.md). Anything that is not a clear answer (libssl missing,
    // certificate refused, proxy trouble, a malformed reply) falls through to curl below, as before.
    if (okay::http::NativeAvailable()) {
        enum class Why { None, Cancelled, TooBig } why = Why::None;
        okay::http::RequestOptions ro;
        ro.maxBytes = maxBytes;
        ro.maxRedirects = 5;
        ro.connectMs = 20000;
        ro.ioMs = 60000;
        ro.totalMs = 6 * 60 * 60 * 1000;
        ro.cancel = [&]() { if (cancelled && cancelled()) { why = Why::Cancelled; return true; } return false; };
        ro.progress = [&](std::uint64_t done, std::uint64_t) {
            if (onBytes) onBytes(done);
            if (cancelled && cancelled()) { why = Why::Cancelled; return false; }
            if (maxBytes > 0 && done > maxBytes) { why = Why::TooBig; return false; }
            return true;
        };
        const okay::http::ExResult r = okay::http::RequestEx("GET", url, {"User-Agent: OkaySpace-Launcher"}, {}, {}, okay::restart::Utf8(dest), ro);
        if (r.reached) {
            if (r.status == 200 && fs::is_regular_file(dest, ec)) {
                const std::uintmax_t sz = fs::file_size(dest, ec);
                if (onBytes && !ec) onBytes((std::uint64_t)sz);
                out.status = DownloadStatus::Ok;
                return out;
            }
            fs::remove(dest, ec);
            out.error = r.status >= 400 ? "The download failed (the server does not have that file)."
                                        : "The download failed (the server answered " + std::to_string(r.status) + ").";
            return out;
        }
        fs::remove(dest, ec);
        if (why == Why::Cancelled) { out.status = DownloadStatus::Cancelled; out.error = "Cancelled."; return out; }
        if (why == Why::TooBig || r.error.rfind("too-large", 0) == 0) { out.error = "The server sent more data than the file should have."; return out; }
        if (r.writeFailed) { out.status = DownloadStatus::DiskFull; out.error = "The disk is full."; return out; }
        // otherwise: no usable answer; try curl
    }
#endif
    std::vector<std::string> args = {"curl", "-L", "-s", "-f", "--connect-timeout", "20", "--max-redirs", "5",
                                     "--speed-limit", "1024", "--speed-time", "60", "-A", "OkaySpace-Launcher"};
    if (url.compare(0, 8, "https://") == 0) { args.push_back("--proto-redir"); args.push_back("=https"); }
    args.push_back("-o");
    args.push_back(okay::restart::Utf8(dest));
    args.push_back(url);

    enum class Why { None, Cancelled, TooBig } why = Why::None;
    std::uint64_t last = 0;
    const RunResult run = RunProcess(args, [&]() {
        std::error_code e;
        const std::uintmax_t sz = fs::exists(dest, e) ? fs::file_size(dest, e) : 0;
        const std::uint64_t now = e ? 0 : (std::uint64_t)sz;
        if (now != last) { last = now; if (onBytes) onBytes(now); }
        if (cancelled && cancelled()) { why = Why::Cancelled; return false; }
        if (maxBytes > 0 && now > maxBytes) { why = Why::TooBig; return false; }
        return true;
    });
    if (!run.started) { out.error = "The download program (curl) could not be started."; return out; }
    if (run.stopped) {
        fs::remove(dest, ec);
        if (why == Why::Cancelled) { out.status = DownloadStatus::Cancelled; out.error = "Cancelled."; }
        else out.error = "The server sent more data than the file should have.";
        return out;
    }
    if (run.exitCode == 0 && fs::is_regular_file(dest, ec)) {
        const std::uintmax_t sz = fs::file_size(dest, ec);
        if (onBytes && !ec) onBytes((std::uint64_t)sz);
        out.status = DownloadStatus::Ok;
        return out;
    }
    fs::remove(dest, ec);
    if (run.exitCode == 23) { out.status = DownloadStatus::DiskFull; out.error = "The disk is full."; }
    else out.error = "The download failed (" + std::string(run.exitCode == 22 ? "the server does not have that file" : "curl exit code " + std::to_string(run.exitCode)) + ").";
    return out;
}

}  // namespace okayversions::net
