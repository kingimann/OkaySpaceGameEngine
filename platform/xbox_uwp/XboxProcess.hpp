#pragma once
// Windows process spawning for the one-click Xbox build: CreateProcessW with a pipe for stdout+stderr, polled without blocking
// (PeekNamedPipe). No <thread>, no <mutex>. UNVERIFIED: compiled (syntax only) with MinGW on Linux, NEVER RUN. The existing
// shared/FileDialog.cpp RunProcess does not exist on Windows and blocks until exit, so it cannot stream a log.
#if defined(_WIN32)
#include "XboxBuildPlan.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace okay::xbox {

namespace detail {
inline std::wstring Widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((std::size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
}

class WinProcess : public ProcessHandle {
public:
    ~WinProcess() override { Close(); }
    bool Start(const Command& c, std::string& err) {
        SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
        HANDLE rd = nullptr, wr = nullptr;
        if (!CreatePipe(&rd, &wr, &sa, 0)) { err = "CreatePipe failed"; return false; }
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);   // the child must not inherit our read end
        STARTUPINFOW si = {}; si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        PROCESS_INFORMATION pi = {};
        std::wstring cmd = detail::Widen(c.Line());
        const std::wstring wd = detail::Widen(c.workDir);
        const BOOL ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, wd.empty() ? nullptr : wd.c_str(), &si, &pi);
        CloseHandle(wr);
        if (!ok) { CloseHandle(rd); err = "CreateProcess failed (error " + std::to_string((unsigned long)GetLastError()) + ") for " + c.exe; return false; }
        CloseHandle(pi.hThread);
        proc_ = pi.hProcess; pipe_ = rd;
        return true;
    }
    bool Poll(std::string& out, int& exitCode) override {
        Drain(out);
        if (!proc_) { exitCode = exit_; return true; }
        if (WaitForSingleObject(proc_, 0) == WAIT_OBJECT_0) {
            Drain(out);   // whatever was written just before it exited
            DWORD code = 1; GetExitCodeProcess(proc_, &code); exit_ = (int)code; exitCode = exit_;
            Close(); return true;
        }
        return false;
    }
    void Kill() override { if (proc_) TerminateProcess(proc_, 1); Close(); }
private:
    void Drain(std::string& out) {
        if (!pipe_) return;
        char buf[4096]; DWORD avail = 0;
        while (PeekNamedPipe(pipe_, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            DWORD got = 0;
            if (!ReadFile(pipe_, buf, avail < sizeof(buf) ? avail : (DWORD)sizeof(buf), &got, nullptr) || got == 0) break;
            out.append(buf, got);
        }
    }
    void Close() { if (pipe_) { CloseHandle(pipe_); pipe_ = nullptr; } if (proc_) { CloseHandle(proc_); proc_ = nullptr; } }
    HANDLE proc_ = nullptr, pipe_ = nullptr; int exit_ = 1;
};

inline std::unique_ptr<ProcessHandle> SpawnWindowsProcess(const Command& c, std::string& err) {
    std::unique_ptr<WinProcess> p(new WinProcess());
    if (!p->Start(c, err)) return nullptr;
    return p;
}

} // namespace okay::xbox
#endif
