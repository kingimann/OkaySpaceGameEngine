#pragma once
// The real disk and the vswhere call for XboxSdkCheck.hpp. UNVERIFIED: never run on Windows.
// Off Windows it reports "vswhere not found", so Xbox export is simply unavailable there.
#include "XboxAsyncProbe.hpp"
#include "XboxSdkCheck.hpp"
#include "XboxVswhereJson.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>
#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace okay::xbox {

inline FsView RealFs() {
    FsView v;
    v.list = [](const std::string& dir) {
        std::vector<std::string> out;
        std::error_code ec;
        std::filesystem::directory_iterator it(std::filesystem::u8path(dir), ec), end;
        for (; !ec && it != end; it.increment(ec)) out.push_back(it->path().filename().u8string());
        return out;
    };
    return v;
}

#if defined(_WIN32)
namespace detail {
inline std::wstring WidenUtf8(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((std::size_t)(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
/// Runs `exe args` with no window, collects stdout, and KILLS it after `timeoutMs`. Returns false (and `timedOut`) when it did not finish in time
/// or could not start. Never blocks the caller longer than the timeout. Native CreateProcessW + an anonymous pipe, nothing from the user in the command.
inline bool RunWithTimeout(const std::string& exe, const std::string& args, unsigned timeoutMs, std::string& out, bool& timedOut) {
    out.clear(); timedOut = false;
    SECURITY_ATTRIBUTES sa = {sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si = {}; si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi = {};
    std::wstring cmd = L"\"" + WidenUtf8(exe) + L"\" " + WidenUtf8(args);
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(L'\0');
    const BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); return false; }
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    bool finished = false;
    for (;;) {
        DWORD avail = 0;
        while (PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr) && avail > 0) {
            char tmp[4096]; DWORD got = 0;
            if (!ReadFile(rd, tmp, sizeof tmp < avail ? (DWORD)sizeof tmp : avail, &got, nullptr) || got == 0) break;
            out.append(tmp, got);
        }
        if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {   // finished: drain what is left
            DWORD a2 = 0;
            while (PeekNamedPipe(rd, nullptr, 0, nullptr, &a2, nullptr) && a2 > 0) { char tmp[4096]; DWORD got = 0; if (!ReadFile(rd, tmp, sizeof tmp < a2 ? (DWORD)sizeof tmp : a2, &got, nullptr) || got == 0) break; out.append(tmp, got); }
            finished = true; break;
        }
        if (GetTickCount64() >= deadline) { TerminateProcess(pi.hProcess, 1); timedOut = true; break; }
        Sleep(10);
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); CloseHandle(rd);
    return finished;
}
inline std::string ReadWholeFile(const std::filesystem::path& p) {
    std::string s; if (FILE* f = _wfopen(p.c_str(), L"rb")) { char b[8192]; std::size_t n; while ((n = std::fread(b, 1, sizeof b, f)) > 0) s.append(b, n); std::fclose(f); }
    return s;
}
inline void WriteWholeFile(const std::filesystem::path& p, const std::string& s) {
    std::error_code ec; std::filesystem::create_directories(p.parent_path(), ec);
    const std::filesystem::path tmp = p.wstring() + L".tmp";
    if (FILE* f = _wfopen(tmp.c_str(), L"wb")) { const bool ok = std::fwrite(s.data(), 1, s.size(), f) == s.size(); std::fclose(f); if (ok) { std::filesystem::rename(tmp, p, ec); if (ec) std::filesystem::remove(tmp, ec); } else std::filesystem::remove(tmp, ec); }
}
}
#endif

/// Asks this PC what is installed. Runs on a WORKER thread (AsyncSdkProbe), never on the editor's UI thread. On Windows: ONE vswhere call
/// (`-format json`, killed after 5 seconds), whose answer is kept in %LOCALAPPDATA%\OkaySpace\xbox_vswhere_cache.txt keyed by vswhere.exe's size and
/// modified time and the modified time of VS's instance folder, so the next editor start does not call vswhere at all. `fresh` (Check again) skips the cache.
inline SdkInputs ProbeThisPc(bool fresh = false) {
    SdkInputs in;
    in.fs = RealFs();
    (void)fresh;
#if defined(_WIN32)
    const char* pf = std::getenv("ProgramFiles(x86)");
    if (!pf || !*pf) pf = std::getenv("ProgramFiles");
    std::string base = pf && *pf ? pf : "C:/Program Files (x86)";
    for (char& c : base) if (c == '\\') c = '/';
    in.programFilesX86 = base;
    const std::string vswhere = base + "/Microsoft Visual Studio/Installer/vswhere.exe";
    std::error_code ec;
    in.vswhereFound = std::filesystem::exists(std::filesystem::u8path(vswhere), ec);
    if (in.vswhereFound) {
        // Fixed program, fixed arguments, nothing from the user: no injection surface. Every install of any version (2017 and newer).
        // (The old fourth call, `-requires ...VC.Tools`, only set an informational flag: the folders decide, so it is gone.)
        std::string key;
        {
            std::error_code e1, e2, e3;
            const std::filesystem::path vp = std::filesystem::u8path(vswhere);
            const auto sz = std::filesystem::file_size(vp, e1);
            const auto mt = std::filesystem::last_write_time(vp, e2).time_since_epoch().count();
            const char* pd = std::getenv("ProgramData");
            std::string inst = (pd && *pd ? std::string(pd) : std::string("C:/ProgramData")) + "/Microsoft/VisualStudio/Packages/_Instances";
            const auto im = std::filesystem::last_write_time(std::filesystem::u8path(inst), e3).time_since_epoch().count();
            key = std::to_string(e1 ? 0 : (long long)sz) + "|" + std::to_string(e2 ? 0 : (long long)mt) + "|" + std::to_string(e3 ? 0 : (long long)im);
        }
        std::filesystem::path cacheFile;
        if (const char* la = std::getenv("LOCALAPPDATA")) if (*la) cacheFile = std::filesystem::u8path(std::string(la)) / "OkaySpace" / "xbox_vswhere_cache.txt";
        std::string json; bool fromCache = false;
        if (!fresh && !cacheFile.empty()) { std::string f = detail::ReadWholeFile(cacheFile), j; if (ReadProbeCache(f, key, j)) { std::vector<VsInstall> t; if (ParseVswhereJson(j, t) && !t.empty()) { json = j; fromCache = true; } } }
        if (!fromCache) {
            bool timedOut = false;
            const bool ran = detail::RunWithTimeout(vswhere, "-products * -prerelease -sort -utf8 -format json", 5000, json, timedOut);
            if (timedOut) in.probeNote = "vswhere did not answer within 5 seconds (it was stopped); press Check again, or start Visual Studio Installer once";
            else if (!ran) in.probeNote = "vswhere.exe could not be started";
            std::vector<VsInstall> parsed;
            if (ran && !ParseVswhereJson(json, parsed)) in.probeNote = "vswhere gave an answer that could not be read";
            else if (ran) {
                in.installs = parsed;
                if (!parsed.empty() && !cacheFile.empty()) detail::WriteWholeFile(cacheFile, MakeProbeCache(key, json));   // only a useful answer is kept
            }
        } else ParseVswhereJson(json, in.installs);
    }
#endif
    return in;
}

/// The editor's answer, without ever blocking its UI thread. The first call starts the probe on a worker thread and returns a report with
/// `checking == true` ("Checking your PC for the Xbox tools..."); later calls return the finished report. `refresh` (the "Check again" button) asks
/// again unless a probe is already running. UI thread only. Nothing starts at editor start-up: the first call comes from the Xbox page / platform.
inline AsyncSdkProbe& SdkProbeJob() {
    static AsyncSdkProbe* job = new AsyncSdkProbe([](bool fresh) { return ProbeThisPc(fresh); });   // never freed: a worker may still be finishing at exit
    return *job;
}
inline const SdkReport& CachedSdkReport(bool refresh = false) { return SdkProbeJob().Get(refresh); }

} // namespace okay::xbox
