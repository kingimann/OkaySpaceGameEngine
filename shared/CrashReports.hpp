#pragma once
// Editor crash reports, as the editor's crash handlers write them next to the
// editor executable: okay_crash.txt (the latest report) and okay_crashlog.txt (every
// report, each after a "========== crash @ <when> (v<version>) ==========" line).
// Header-only so the launcher's Crash reports page and the tests share one parser.
//
// The layout (shared/CrashContext.hpp writes it, docs/online/crash_reports.md describes it): "Key: value" lines, then
// "Backtrace:" and one line per frame, then sections that start with "--- Name ---" (Breadcrumbs, Console). A report
// written by an older editor has no sections and fewer keys; everything here accepts both.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace okaycrash {

struct Report {
    std::string when;       // "2026-10-23 21:14:05" (local time on the crashing machine)
    std::string version;    // editor version
    std::string lastStep;   // the editor's last breadcrumb ("UI.Inspector", "Play.Tick", ...)
    std::string exception;  // "0xC0000005" (Windows) or "SIGSEGV (signal 11)"
    std::string module;     // fault module (+ offset on Windows)
    std::string text;       // the full report, for copying / support
    int frames = 0;         // backtrace lines
    // Added with the detailed reports (all empty for a report from an older editor):
    std::string build;      // build id: "pe-<link time>-<size>, built <date>"
    std::string os;         // "Windows 10.0.19045 (x64)"
    std::vector<std::pair<std::string, std::string>> info;   // every other "Key: value" line before the backtrace (CPU, GPU, Scene, ...)
    std::vector<std::string> backtrace;                       // the frame lines, trimmed (at most kMaxFrames)
    std::vector<std::string> breadcrumbs;                     // "frame 1200: UI.Inspector"
    std::vector<std::string> console;                         // "[-3s] E message"
    std::string signature;                                    // 16 hex digits: the same crash gives the same signature (Signature())
    std::string id;                                           // 16 hex digits: a hash of the whole text, one per report (what the uploader remembers)
    // The value of a header line ("GPU", "Scene", "Uptime", ...), "" if the report has none.
    std::string Info(const std::string& key) const {
        for (const auto& kv : info) if (kv.first == key) return kv.second;
        return std::string();
    }
};

constexpr std::size_t kMaxFrames = 64;

inline std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

// ---- Signature: "is this the same crash as one we already know?" ---------------------------------------------------------
// FNV-1a, 64 bit, as 16 lower-case hex digits. Not a security hash: it names a crash (and a report) and nothing else.
inline std::string HashHex(const std::string& s) {
    std::uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    static const char* hex = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) { out[(std::size_t)i] = hex[h & 15]; h >>= 4; }
    return out;
}

inline std::string LowerCopy(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
inline std::string BaseName(const std::string& p) {
    const std::size_t k = p.find_last_of("/\\");
    return k == std::string::npos ? p : p.substr(k + 1);
}

// A backtrace line -> "module+0xoffset" (no symbol) or "module!function" (symbol known); "" if the line is not a frame.
// Understands both layouts the handlers write:
//   Windows: "  #00 OkayEngine.exe + 0x1A2B3C  DrawInspector +0x40  (C:\src\x.cpp:12)"
//   Linux:   "./OkayEngine(main+0x8ac)[0x55d0]"  or  "/lib/x86_64-linux-gnu/libc.so.6(+0x2a1ca)[0x7f1c]"
// The offset is lower-case hex without leading zeros; a function name drops its displacement, so the same function is the same
// frame whichever instruction in it faulted.
inline std::string NormalizeFrame(const std::string& raw) {
    std::string line = Trim(raw);
    if (line.empty()) return std::string();
    auto hexTail = [](const std::string& s, std::size_t at) {   // "0x1A2B" or "1A2B" at `at` -> "1a2b"
        if (s.compare(at, 2, "0x") == 0 || s.compare(at, 2, "0X") == 0) at += 2;
        std::string h;
        while (at < s.size() && std::isxdigit((unsigned char)s[at])) h += (char)std::tolower((unsigned char)s[at++]);
        const std::size_t nz = h.find_first_not_of('0');
        h = nz == std::string::npos ? std::string("0") : h.substr(nz);
        return h;
    };
    if (line[0] == '#') {   // "#00 " numbering
        const std::size_t sp = line.find(' ');
        if (sp == std::string::npos) return std::string();
        line = Trim(line.substr(sp));
    }
    const std::size_t plus = line.find(" + 0x");
    if (plus != std::string::npos && line.find('(') != 0 && line.find('[') != 0) {   // Windows layout
        const std::string module = BaseName(Trim(line.substr(0, plus)));
        std::size_t p = plus + 3;
        const std::string off = hexTail(line, p);
        p += 2; while (p < line.size() && std::isxdigit((unsigned char)line[p])) ++p;
        std::string rest = Trim(line.substr(p));
        std::string fn;
        if (!rest.empty() && rest[0] != '(') {
            const std::size_t disp = rest.rfind(" +0x");
            if (disp != std::string::npos) fn = Trim(rest.substr(0, disp));
            else { const std::size_t paren = rest.find("  ("); fn = Trim(paren == std::string::npos ? rest : rest.substr(0, paren)); }
        }
        return fn.empty() ? module + "+0x" + off : module + "!" + fn;
    }
    const std::size_t open = line.find('('), close = line.find(')', open == std::string::npos ? 0 : open);
    if (open != std::string::npos && close != std::string::npos && close > open) {   // Linux layout
        const std::string module = BaseName(line.substr(0, open));
        const std::string inner = line.substr(open + 1, close - open - 1);
        const std::size_t at = inner.rfind("+0x");
        const std::string fn = at == std::string::npos ? inner : inner.substr(0, at);
        if (module.empty()) return std::string();
        if (!fn.empty()) return module + "!" + fn;
        return at == std::string::npos ? std::string() : module + "+0x" + hexTail(inner, at + 1);
    }
    return std::string();
}

// A frame that is the crash machinery or the operating system, not the program's own code: skipped at the top of the stack
// so the signature (and the "top frame" shown to people) is the first frame that is ours.
inline bool IsSystemFrame(const std::string& normalized) {
    const std::string low = LowerCopy(normalized);
    static const char* const kModules[] = {"libc.so", "libc-", "libstdc++", "libgcc_s", "ld-linux", "ld-2", "libpthread", "libm.so", "libm-",
        "linux-vdso", "[vdso]", "ntdll", "kernel32", "kernelbase", "msvcrt", "ucrtbase", "libwinpthread", "libgcc_s_seh", "libgcc_s_dw2"};
    for (const char* m : kModules) if (low.compare(0, std::strlen(m), m) == 0) return true;
    const std::size_t bang = low.find('!');
    if (bang != std::string::npos) {
        std::string fn = low.substr(bang + 1);
        const std::size_t paren = fn.find('(');
        if (paren != std::string::npos) fn.resize(paren);
        static const char* const kExact[] = {"raise", "abort", "gsignal", "pthread_kill", "__pthread_kill_implementation", "__restore_rt",
                                             "_zst9terminatev", "std::terminate", "terminate", "__cxa_throw", "__cxa_rethrow"};
        for (const char* n : kExact) if (fn == n) return true;
        if (fn.find("okaycrash") != std::string::npos) return true;   // the handler itself (OkayCrashSignal, okaycrash::handler::...)
        static const char* const kPrefix[] = {"unhandledexceptionfilter", "kiuserexceptiondispatcher", "rtldispatchexception", "_unwind_",
                                              "__cxa_", "__gnu_cxx::__verbose", "_zn9__gnu_cxx27__verbose"};
        for (const char* n : kPrefix) if (fn.compare(0, std::strlen(n), n) == 0) return true;
    }
    return false;
}

// "0xC0000005", "SIGSEGV", "TERMINATE": the part of the Exception line that names the kind of fault.
inline std::string ExceptionClass(const std::string& exc) {
    const std::string low = LowerCopy(exc);
    if (low.find("terminate") != std::string::npos || low.find("c++ exception") != std::string::npos) return "TERMINATE";
    std::string out;
    std::size_t i = 0;
    while (i < exc.size() && exc[i] == ' ') ++i;
    for (; i < exc.size() && (std::isalnum((unsigned char)exc[i]) || exc[i] == '_'); ++i) out += (char)std::toupper((unsigned char)exc[i]);
    if (out.size() > 1 && out[0] == '0' && out[1] == 'X') out = "0x" + out.substr(2);
    return out.empty() ? std::string("UNKNOWN") : out;
}

// First frame that is the program's own: "OkayEngine.exe+0x1a2b3c" / "OkayEngine!Scene::Update", "" if none could be read.
inline std::string TopFrame(const Report& r) {
    for (const std::string& f : r.backtrace) {
        const std::string n = NormalizeFrame(f);
        if (!n.empty() && !IsSystemFrame(n)) return n;
    }
    return std::string();
}

// A name for the crash: exception kind plus the top three of the program's own frames (frames inside the operating system
// or the C++ runtime are left out wherever they are: their offsets differ between Windows versions). The same crash gives the
// same signature on every machine running the same build. A fault that comes out of abort() or an uncaught C++ exception has
// the same few frames whatever the cause, so those also count the last step. A report with no readable frame falls back
// to exception + last step + version (so two builds do not collapse into one).
inline std::string Signature(const Report& r) {
    const std::string cls = ExceptionClass(r.exception);
    std::string key = cls;
    int taken = 0;
    for (const std::string& f : r.backtrace) {
        const std::string n = NormalizeFrame(f);
        if (n.empty() || IsSystemFrame(n)) continue;
        key += "|" + n;
        if (++taken == 3) break;
    }
    const bool abortLike = cls == "SIGABRT" || cls == "TERMINATE" || cls == "0xC0000409";
    if (taken == 0) key += "|nostack|" + r.lastStep + "|" + r.version;
    else if (abortLike) key += "|step:" + r.lastStep;
    return HashHex(key);
}

// One report's text -> fields ("Key: value" lines, then the backtrace, then the "--- Name ---" sections).
inline Report ParseOne(const std::string& text) {
    Report r;
    r.text = Trim(text);
    std::istringstream in(text);
    std::string line;
    enum class Part { Header, Backtrace, Breadcrumbs, Console, Ignored } part = Part::Header;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (part != Part::Header && line.compare(0, 4, "--- ") == 0) {   // a section starts (or the report ends)
            if (line.compare(4, 11, "Breadcrumbs") == 0) part = Part::Breadcrumbs;
            else if (line.compare(4, 7, "Console") == 0) part = Part::Console;
            else part = Part::Ignored;
            continue;
        }
        if (part == Part::Backtrace) {
            if (!Trim(line).empty()) { ++r.frames; if (r.backtrace.size() < kMaxFrames) r.backtrace.push_back(Trim(line)); }
            continue;
        }
        if (part == Part::Breadcrumbs) { if (!Trim(line).empty() && r.breadcrumbs.size() < 256) r.breadcrumbs.push_back(Trim(line)); continue; }
        if (part == Part::Console) { if (!line.empty() && r.console.size() < 1000) r.console.push_back(line); continue; }
        if (part == Part::Ignored) continue;
        // header
        if (line.compare(0, 10, "Backtrace:") == 0) { part = Part::Backtrace; continue; }
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos || colon == 0 || colon > 24 || line[0] == ' ') continue;
        bool keyOk = true;   // letters and single spaces only: "Last step", "Fault module", "OS", "CPU"
        for (std::size_t i = 0; i < colon; ++i) if (!std::isalpha((unsigned char)line[i]) && line[i] != ' ') { keyOk = false; break; }
        if (!keyOk) continue;
        const std::string key = line.substr(0, colon), value = Trim(line.substr(colon + 1));
        if (key == "Version") r.version = value;
        else if (key == "When") r.when = value;
        else if (key == "Last step") r.lastStep = value;
        else if (key == "Exception") r.exception = value;
        else if (key == "Fault module") r.module = value;
        else if (key == "Build") r.build = value;
        else if (key == "OS") r.os = value;
        else if (r.info.size() < 64) r.info.emplace_back(key, value);
    }
    r.signature = Signature(r);
    r.id = HashHex(r.text);
    return r;
}

// A whole okay_crashlog.txt -> reports, newest first.
inline std::vector<Report> ParseLog(const std::string& log) {
    std::vector<Report> out;
    const std::string mark = "========== crash @ ";
    std::size_t pos = log.find(mark);
    while (pos != std::string::npos) {
        const std::size_t eol = log.find('\n', pos);
        const std::size_t next = log.find(mark, eol == std::string::npos ? log.size() : eol);
        const std::string header = log.substr(pos, (eol == std::string::npos ? log.size() : eol) - pos);
        const std::string body = eol == std::string::npos ? std::string()
                               : log.substr(eol + 1, (next == std::string::npos ? log.size() : next) - eol - 1);
        Report r = ParseOne(body);
        // The header carries when / version even if the body was cut short.
        const std::size_t at = mark.size(), open = header.find(" (v", at);
        if (r.when.empty()) r.when = Trim(header.substr(at, open == std::string::npos ? std::string::npos : open - at));
        if (r.version.empty() && open != std::string::npos) {
            const std::size_t close = header.find(')', open);
            r.version = header.substr(open + 3, close == std::string::npos ? std::string::npos : close - open - 3);
        }
        if (!r.text.empty() || !r.when.empty()) out.push_back(std::move(r));
        pos = next;
    }
    std::reverse(out.begin(), out.end());
    return out;
}

// A plain-English name for the exception code / signal.
inline std::string ExceptionName(const std::string& exc) {
    std::string u = exc;
    for (char& c : u) c = (char)std::toupper((unsigned char)c);
    if (u.find("SIGSEGV") != std::string::npos || u.find("C0000005") != std::string::npos) return "Access violation";
    if (u.find("C00000FD") != std::string::npos) return "Stack overflow";
    if (u.find("C0000094") != std::string::npos || u.find("SIGFPE") != std::string::npos) return "Divide by zero";
    if (u.find("C000001D") != std::string::npos || u.find("SIGILL") != std::string::npos) return "Illegal instruction";
    if (u.find("C0000409") != std::string::npos || u.find("SIGABRT") != std::string::npos) return "Aborted";
    if (u.find("80000003") != std::string::npos) return "Breakpoint";
    if (u.find("C0000017") != std::string::npos) return "Out of memory";
    if (u.find("SIGBUS") != std::string::npos) return "Bus error";
    if (u.find("TERMINATE") != std::string::npos || u.find("C++ EXCEPTION") != std::string::npos) return "Unhandled C++ exception";
    return exc.empty() ? std::string("Crash") : exc;
}

// The reports beside the editor at `editorDir`: the history if there is one, else the
// single latest report (editors from before the history existed).
inline std::vector<Report> Load(const std::filesystem::path& editorDir) {
    auto slurp = [](const std::filesystem::path& p) {
        std::ifstream f(p, std::ios::binary);
        std::ostringstream ss; ss << f.rdbuf();
        return ss.str();
    };
    std::error_code ec;
    if (std::filesystem::exists(editorDir / "okay_crashlog.txt", ec)) {
        auto v = ParseLog(slurp(editorDir / "okay_crashlog.txt"));
        if (!v.empty()) return v;
    }
    if (std::filesystem::exists(editorDir / "okay_crash.txt", ec)) {
        Report r = ParseOne(slurp(editorDir / "okay_crash.txt"));
        if (!r.text.empty()) return {r};
    }
    return {};
}

// Delete both report files. Returns false if either couldn't be removed.
inline bool Clear(const std::filesystem::path& editorDir) {
    std::error_code ec1, ec2;
    std::filesystem::remove(editorDir / "okay_crashlog.txt", ec1);
    std::filesystem::remove(editorDir / "okay_crash.txt", ec2);
    return !ec1 && !ec2;
}

} // namespace okaycrash
