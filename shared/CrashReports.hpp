#pragma once
// Editor crash reports, as the editor's crash handlers write them next to the
// editor executable: okay_crash.txt (the latest report) and okay_crashlog.txt (every
// report, each after a "========== crash @ <when> (v<version>) ==========" line).
// Header-only so the launcher's Crash reports page and the tests share one parser.
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
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
};

inline std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    return s.substr(i);
}

// One report's text -> fields ("Key: value" lines, then the backtrace).
inline Report ParseOne(const std::string& text) {
    Report r;
    r.text = Trim(text);
    std::istringstream in(text);
    std::string line;
    bool bt = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (bt) { if (!Trim(line).empty()) ++r.frames; continue; }
        auto field = [&](const char* key, std::string& out) {
            const std::size_t n = std::char_traits<char>::length(key);
            if (line.compare(0, n, key) == 0) { out = Trim(line.substr(n)); return true; }
            return false;
        };
        if (field("Version:", r.version) || field("When:", r.when) || field("Last step:", r.lastStep) ||
            field("Exception:", r.exception) || field("Fault module:", r.module)) continue;
        if (line.compare(0, 10, "Backtrace:") == 0) bt = true;
    }
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
