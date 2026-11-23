#pragma once
// ---------------------------------------------------------------------------
// CurlFallback - the ONE place the shipped programs may start the external `curl` program.
//
// Every HTTP call of the launcher, editor, player and engine goes through the seam in NativeFetch.hpp
// (FetchToFile, HeadDump, FetchPrefix, Send). The seam uses the native client first: WinHTTP on Windows, our own
// HTTP/1.1 client on Linux (OKAY_OWN_HTTP). Only when that client is not there or gives no clear answer does the seam
// call RunCurlProcess below. tests/check_curl_seam_wiring.cmake fails if curl is started anywhere else, or if
// anything but NativeFetch.hpp calls RunCurlProcess.
//
// Why curl is kept at all (claude/third-party-1, 2026-11-23): on Linux our own client is still opt-in
// (OKAY_OWN_HTTP=1, rule 17: the default flips only with the owner's approval), so curl is still the default path
// there. On Windows WinHTTP answers every call; curl only runs when WinHTTP could not reach the server at all (an odd
// proxy) and the call is safe to repeat. OKAY_NO_CURL=1 turns the fallback off everywhere (the native client only).
//
// No shell: the arguments go straight to the program, so paths and addresses with spaces, quotes or unicode stay one
// argument each and cmd.exe never re-reads them. Windows: CreateProcessW, hidden (CREATE_NO_WINDOW). POSIX:
// posix_spawnp. The UWP and web (Emscripten) builds cannot start programs at all, so there the fallback is never started
// (RunCurlProcess reports "not started", and posix_spawnp is not referenced, so wasm-ld has nothing to resolve).
// Written from the Win32 CreateProcess and CommandLineToArgvW documentation and the POSIX posix_spawn manual page.
// No <mutex> / <thread> (rule 21a): the poll loop sleeps with Sleep / nanosleep. Windows: only the three standard handles
// are inherited (PROC_THREAD_ATTRIBUTE_HANDLE_LIST, from the UpdateProcThreadAttribute documentation).
// ---------------------------------------------------------------------------
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#if defined(_WIN32)
#  include <windows.h>
#elif defined(__EMSCRIPTEN__)
#  include <cerrno>
#  include <ctime>   // (the web build cannot start programs: no spawn headers, no posix_spawnp)
#else
#  include <cerrno>
#  include <csignal>
#  include <ctime>
#  include <fcntl.h>
#  include <spawn.h>
#  include <sys/types.h>
#  include <sys/wait.h>
extern char** environ;
#endif

namespace okay::http::curlfb {

/// OKAY_NO_CURL=1: never start curl (the native client's answer is final).
inline bool Disabled() {
    const char* e = std::getenv("OKAY_NO_CURL");
    return e && e[0] == '1' && e[1] == '\0';
}

struct RunSpec {
    std::function<bool()> keepGoing;   ///< polled about every pollMs; false ends the program (RunResult::stopped)
    int pollMs = 100;
    unsigned timeoutMs = 0;            ///< 0 = no limit; otherwise the program is ended after this long (stopped, timedOut)
    std::string stdoutFile;            ///< "" = discard; otherwise curl's standard output goes to this file (for -w)
};

struct RunResult {
    bool started = false;    ///< curl was found and started
    bool stopped = false;    ///< keepGoing said stop (or the time ran out) and curl was ended
    bool timedOut = false;
    int exitCode = -1;       ///< curl's exit code when it ran to its end
};

namespace detail {
inline void SleepMs(int ms) {
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    timespec ts{ms / 1000, (long)(ms % 1000) * 1000000L};
    while (::nanosleep(&ts, &ts) != 0 && errno == EINTR) {}
#endif
}
#if defined(_WIN32)
inline std::wstring Wide(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
/// One argument as the C runtime's command-line parser reads it back (CommandLineToArgvW rules): quoted when it is
/// empty or has a space, tab or quote; a quote becomes \" and the backslashes in front of a quote (or of the closing
/// quote) are doubled.
inline std::wstring QuoteArg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\n\v\"") == std::wstring::npos) return a;
    std::wstring o = L"\"";
    size_t slashes = 0;
    for (wchar_t c : a) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'"') { o.append(slashes * 2 + 1, L'\\'); o.push_back(L'"'); }
        else { o.append(slashes, L'\\'); o.push_back(c); }
        slashes = 0;
    }
    o.append(slashes * 2, L'\\');
    o.push_back(L'"');
    return o;
}
#endif
} // namespace detail

/// Start `curl` with `args` (without the program name), hidden, and wait for it. Standard input and error are the null
/// device; standard output is RunSpec::stdoutFile or the null device. Blocks until curl has ended.
inline RunResult RunCurlProcess(const std::vector<std::string>& args, const RunSpec& rs = RunSpec()) {
    RunResult res;
    if (Disabled()) return res;
    const int pollMs = rs.pollMs < 10 ? 10 : rs.pollMs;
#if defined(OKAY_XBOX_UWP) || defined(__EMSCRIPTEN__)
    (void)args; (void)pollMs;
    return res;   // a UWP app or a web page cannot start other programs: "not started", the seam's answer is final
#elif defined(_WIN32)
    std::wstring cmd = L"curl";
    for (const std::string& a : args) { cmd.push_back(L' '); cmd += detail::QuoteArg(detail::Wide(a)); }
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    HANDLE outH = INVALID_HANDLE_VALUE, nulIn = INVALID_HANDLE_VALUE, nulErr = INVALID_HANDLE_VALUE;
    BOOL inherit = FALSE;
    if (!rs.stdoutFile.empty()) {
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        outH = CreateFileW(detail::Wide(rs.stdoutFile).c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
        nulIn = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
        nulErr = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
        if (outH == INVALID_HANDLE_VALUE || nulIn == INVALID_HANDLE_VALUE || nulErr == INVALID_HANDLE_VALUE) {
            if (outH != INVALID_HANDLE_VALUE) CloseHandle(outH);
            if (nulIn != INVALID_HANDLE_VALUE) CloseHandle(nulIn);
            if (nulErr != INVALID_HANDLE_VALUE) CloseHandle(nulErr);
            return res;
        }
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdInput = nulIn; si.hStdOutput = outH; si.hStdError = nulErr;
        inherit = TRUE;
    }
    // Only these three handles may be inherited (PROC_THREAD_ATTRIBUTE_HANDLE_LIST, Windows Vista and later): without the
    // list a curl started on another thread at the same moment would also inherit this call's open, inheritable handles
    // (and keep this call's temporary files open and locked until it ends). If the list cannot be set up, curl is not
    // started with inherited handles at all (this call reports not started; the seam treats that as "no curl").
    STARTUPINFOEXW six{};
    six.StartupInfo = si;
    six.StartupInfo.cb = sizeof(six);
    std::vector<unsigned char> attrBuf;
    DWORD flags = CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT;
    HANDLE inheritList[3] = {nulIn, outH, nulErr};
    bool listOk = !inherit;
    if (inherit) {
        SIZE_T need = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &need);
        if (need > 0) {
            attrBuf.resize((size_t)need);
            six.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
            if (InitializeProcThreadAttributeList(six.lpAttributeList, 1, 0, &need)) {
                if (UpdateProcThreadAttribute(six.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inheritList,
                                              sizeof(inheritList), nullptr, nullptr)) {
                    listOk = true;
                    flags |= EXTENDED_STARTUPINFO_PRESENT;
                } else {
                    DeleteProcThreadAttributeList(six.lpAttributeList);
                }
            }
            if (!listOk) six.lpAttributeList = nullptr;
        }
    }
    PROCESS_INFORMATION pi{};
    const BOOL ok = listOk && CreateProcessW(nullptr, buf.data(), nullptr, nullptr, inherit, flags, nullptr, nullptr,
                                             (flags & EXTENDED_STARTUPINFO_PRESENT) ? &six.StartupInfo : &si, &pi);
    if (six.lpAttributeList) DeleteProcThreadAttributeList(six.lpAttributeList);
    if (outH != INVALID_HANDLE_VALUE) CloseHandle(outH);
    if (nulIn != INVALID_HANDLE_VALUE) CloseHandle(nulIn);
    if (nulErr != INVALID_HANDLE_VALUE) CloseHandle(nulErr);
    if (!ok) return res;
    res.started = true;
    const DWORD t0 = GetTickCount();   // (not GetTickCount64: older _WIN32_WINNT headers lack it; these intervals are far below the 49-day wrap)
    for (;;) {
        if (WaitForSingleObject(pi.hProcess, (DWORD)pollMs) == WAIT_OBJECT_0) {
            DWORD code = 1;
            GetExitCodeProcess(pi.hProcess, &code);
            res.exitCode = (int)code;
            break;
        }
        const bool late = rs.timeoutMs && (DWORD)(GetTickCount() - t0) >= rs.timeoutMs;
        if (late || (rs.keepGoing && !rs.keepGoing())) {
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 5000);
            res.stopped = true;
            res.timedOut = late;
            break;
        }
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return res;
#else
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) return res;
    bool opened = posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0) == 0 &&
                  posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0) == 0;
    if (opened) {
        opened = rs.stdoutFile.empty()
            ? posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0) == 0
            : posix_spawn_file_actions_addopen(&actions, 1, rs.stdoutFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600) == 0;
    }
    std::vector<std::string> argv;
    argv.reserve(args.size() + 1);
    argv.push_back("curl");
    for (const std::string& a : args) argv.push_back(a);
    std::vector<char*> av;
    av.reserve(argv.size() + 1);
    for (std::string& s : argv) av.push_back(&s[0]);
    av.push_back(nullptr);
    pid_t pid = 0;
    const int rc = opened ? posix_spawnp(&pid, av[0], &actions, nullptr, av.data(), environ) : -1;
    posix_spawn_file_actions_destroy(&actions);
    if (!opened || rc != 0) return res;
    res.started = true;
    unsigned waited = 0;
    for (;;) {
        int st = 0;
        const pid_t w = ::waitpid(pid, &st, WNOHANG);
        if (w == pid) {
            res.exitCode = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
            // posix_spawnp reports "not found" through the child on some systems: exit 127 = the program is not there
            if (res.exitCode == 127) res.started = false;
            return res;
        }
        if (w < 0 && errno != EINTR) return res;
        const bool late = rs.timeoutMs && waited >= rs.timeoutMs;
        if (late || (rs.keepGoing && !rs.keepGoing())) {
            ::kill(pid, SIGTERM);
            for (int i = 0; i < 40; ++i) {   // up to two seconds to leave, then it is killed
                if (::waitpid(pid, &st, WNOHANG) == pid) { res.stopped = true; res.timedOut = late; return res; }
                detail::SleepMs(50);
            }
            ::kill(pid, SIGKILL);
            while (::waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
            res.stopped = true;
            res.timedOut = late;
            return res;
        }
        detail::SleepMs(pollMs);
        waited += (unsigned)pollMs;
    }
#endif
}

} // namespace okay::http::curlfb
