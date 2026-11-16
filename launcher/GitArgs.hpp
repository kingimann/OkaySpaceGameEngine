// GitArgs — the launcher's Git buttons (Clone, Pull, Commit & Push, Publish,
// "git init" for a new project) as argument lists, plus the rules that keep
// user text away from any shell. Header-only and std-only (no SDL, no ImGui)
// so tests/test_git_args.cpp can test it directly.
//
// Why: the launcher used to build one shell string per step with plain "..."
// quoting and hand it to std::system / `cmd /c`. A commit message, URL or
// project path with a backtick, $( ), $name, ", a trailing \ (POSIX) or
// %VAR%, &, |, ^ (Windows) was re-parsed by the shell: commands could run, and
// the message could change or the commit fail silently. Also only the last
// step's exit code decided the toast, so a failed commit read "Pushed".
//
// Now:
//   - every step is an argv list run WITHOUT a shell (okay::restart::RunAndWait:
//     posix_spawnp on POSIX, CreateProcessW with per-argument quoting on Windows);
//   - the commit message goes through `git commit -F <private temp file>`, so no
//     command line ever carries it;
//   - a URL is refused if it starts with '-' (would be read as a git option) or
//     uses git's command-running transports (ext::, fd::), and "--" ends the
//     options before user text where git allows it;
//   - RunSteps stops at the first failing step and says which one failed.
//
// PosixShellQuote / WindowsCmdQuote are kept for the places that still need a
// printable or shell form (logging the failed step). Written from the POSIX
// sh "Quoting" section (single quotes keep every character literal; a single
// quote cannot appear inside, so it is closed, written as \' and reopened),
// from `cmd /?` plus the documented cmd.exe parse order (percent expansion
// first, then ^ escapes and quote toggling), and from Microsoft's "Parsing C
// command-line arguments" page for the program's own argv rules.
#pragma once

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <initializer_list>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#  include <fcntl.h>
#  include <io.h>
#  include <share.h>
#  include <sys/stat.h>
#else
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <unistd.h>
#endif

namespace okay::gitargs {

using Argv = std::vector<std::string>;

// {"git", "-C", dir, rest...}: run git in `dir` with `rest` as separate arguments.
inline Argv GitIn(const std::string& dir, std::initializer_list<std::string> rest) {
    Argv a{"git", "-C", dir};
    a.insert(a.end(), rest.begin(), rest.end());
    return a;
}

// A clone / publish URL we are willing to hand to git. Not a shell check (no
// shell is used); this keeps user text from being read as a git option or a
// transport that runs commands.
inline bool UrlOk(const std::string& url) {
    if (url.empty() || url[0] == '-') return false;
    for (unsigned char c : url)
        if (c < 0x20 || c == 0x7f) return false;   // newlines and other control bytes
    auto starts = [&](const char* p) {
        std::size_t n = std::char_traits<char>::length(p);
        if (url.size() < n) return false;
        for (std::size_t i = 0; i < n; ++i) {
            char c = url[i];
            if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
            if (c != p[i]) return false;
        }
        return true;
    };
    if (starts("ext::") || starts("fd::")) return false;
    return true;
}

// The folder name "git clone" would make from a URL: the last path part, ".git"
// dropped, characters a folder name cannot hold replaced by '-'. Never empty,
// never "." or "..", never starts with '-'.
inline std::string CloneFolderName(const std::string& url) {
    std::string nm = url;
    while (!nm.empty() && (nm.back() == '/' || nm.back() == '\\')) nm.pop_back();
    if (auto sl = nm.find_last_of("/\\:"); sl != std::string::npos) nm = nm.substr(sl + 1);
    if (nm.size() > 4 && nm.compare(nm.size() - 4, 4, ".git") == 0) nm.erase(nm.size() - 4);
    for (char& ch : nm)
        if (ch == '"' || ch == '\\' || ch == '/' || ch == ':' || ch == '*' || ch == '?' || ch == '<' ||
            ch == '>' || ch == '|' || (unsigned char)ch < 0x20)
            ch = '-';
    if (nm.empty() || nm == "." || nm == "..") nm = "ClonedProject";
    if (nm[0] == '-') nm[0] = '_';
    return nm;
}

// ---- The steps each button runs ------------------------------------------------
inline std::vector<Argv> InitSteps(const std::string& root) { return {GitIn(root, {"init"})}; }

inline std::vector<Argv> PullSteps(const std::string& root) { return {GitIn(root, {"pull"})}; }

// Empty when the URL is refused (see UrlOk).
inline std::vector<Argv> CloneSteps(const std::string& parentDir, const std::string& url, const std::string& folder) {
    if (!UrlOk(url)) return {};
    return {GitIn(parentDir, {"clone", "--", url, folder})};
}

// messageFile holds the commit message (WriteMessageFile).
inline std::vector<Argv> CommitPushSteps(const std::string& root, const std::string& messageFile) {
    return {GitIn(root, {"add", "-A"}),
            GitIn(root, {"commit", "-F", messageFile}),
            GitIn(root, {"push"})};
}

// Empty when the URL is refused (see UrlOk).
inline std::vector<Argv> PublishSteps(const std::string& root, const std::string& url, const std::string& messageFile) {
    if (!UrlOk(url)) return {};
    return {GitIn(root, {"init"}),
            GitIn(root, {"add", "-A"}),
            GitIn(root, {"commit", "-F", messageFile}),
            GitIn(root, {"branch", "-M", "main"}),
            GitIn(root, {"remote", "add", "origin", "--", url}),
            GitIn(root, {"push", "-u", "origin", "main"})};
}

// ---- Running a sequence -----------------------------------------------------------
struct StepsResult {
    bool ok = true;
    std::size_t failedIndex = static_cast<std::size_t>(-1);   // the step that failed, when !ok
    int rc = 0;                                                // its exit code
    std::size_t ran = 0;                                       // how many steps were started
};

// Runs the steps in order with run(argv) -> exit code, and stops at the first
// non-zero code. An empty list is a failure (nothing was done).
template <class RunFn>
inline StepsResult RunSteps(const std::vector<Argv>& steps, RunFn&& run) {
    StepsResult r;
    if (steps.empty()) { r.ok = false; r.failedIndex = 0; r.rc = -1; return r; }
    for (std::size_t i = 0; i < steps.size(); ++i) {
        ++r.ran;
        const int c = run(steps[i]);
        if (c != 0) { r.ok = false; r.failedIndex = i; r.rc = c; return r; }
    }
    return r;
}

// "git commit" for a step like {"git","-C",dir,"commit","-F",f}: the words a
// toast can name without repeating paths or user text.
inline std::string StepName(const Argv& a) {
    std::string s = a.empty() ? std::string() : a[0];
    std::size_t i = 1;
    if (a.size() > 2 && a[1] == "-C") i = 3;
    if (i < a.size()) s += " " + a[i];
    return s;
}

// ---- Commit message file --------------------------------------------------------------
// Writes the message to a new private file in `dir` (0600 on POSIX; on
// Windows the per-user temp folder; created only if it does not exist yet) and returns its path, or "" on failure. The
// caller deletes it when the job ends.
inline std::string WriteMessageFile(const std::filesystem::path& dir, const std::string& message) {
    std::random_device rd;
    for (int attempt = 0; attempt < 16; ++attempt) {
        char name[64];
        std::snprintf(name, sizeof(name), "okay_git_msg_%08x%08x.txt", (unsigned)rd(), (unsigned)rd());
        const std::filesystem::path p = dir / name;
#if defined(_WIN32)
        int fd = -1;
        if (_wsopen_s(&fd, p.wstring().c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY | _O_NOINHERIT,
                      _SH_DENYWR, _S_IREAD | _S_IWRITE) != 0 || fd < 0)
            continue;
        std::size_t done = 0;
        bool ok = true;
        while (done < message.size()) {
            const unsigned chunk = (unsigned)((message.size() - done) > 65536 ? 65536 : (message.size() - done));
            const int w = _write(fd, message.data() + done, chunk);
            if (w <= 0) { ok = false; break; }
            done += (std::size_t)w;
        }
        if (_close(fd) != 0) ok = false;
        if (ok) {
            const auto u8 = p.u8string();   // UTF-8 for RunAndWait (it widens each argument)
            return std::string(reinterpret_cast<const char*>(u8.c_str()), u8.size());
        }
        std::error_code ec; std::filesystem::remove(p, ec);
        return {};
#else
        const int fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (fd < 0) continue;
        std::size_t done = 0;
        bool ok = true;
        while (done < message.size()) {
            const ssize_t w = ::write(fd, message.data() + done, message.size() - done);
            if (w <= 0) { ok = false; break; }
            done += (std::size_t)w;
        }
        if (::close(fd) != 0) ok = false;
        if (ok) return p.string();
        std::error_code ec; std::filesystem::remove(p, ec);
        return {};
#endif
    }
    return {};
}

// ---- Shell forms (for logging, or any place a shell string is unavoidable) ------------
// POSIX sh: one word, every character literal. ' becomes '\''.
inline std::string PosixShellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

// One argument for a `cmd /c` command line, read back by the program with the
// usual Windows argv rules. Returns false (out left empty) for text cmd cannot
// carry at all: CR, LF and NUL end or cut the command line.
// Two layers, innermost first:
//   1. The program's argv rules: wrap in "...", write a " inside as \", and
//      double the backslashes that come right before a " (n backslashes then "
//      means n/2 backslashes; an odd count makes the " literal).
//   2. cmd.exe: put ^ before every character cmd treats specially, the quotes
//      included: ( ) % ! ^ " < > & |. With every " escaped cmd never thinks it
//      is inside quotes, so every special character is escaped and none is
//      acted on. A %NAME% becomes ^%NAME^%: percent expansion runs before ^ is
//      removed and looks up the name "NAME^", which no variable has, so the
//      text is kept; then the carets go. ! only matters with /v:on.
inline bool WindowsCmdQuote(const std::string& s, std::string& out) {
    out.clear();
    for (char c : s)
        if (c == '\r' || c == '\n' || c == '\0') return false;
    std::string argv = "\"";
    std::size_t bs = 0;
    for (char c : s) {
        if (c == '\\') { ++bs; continue; }
        if (c == '"') { argv.append(bs * 2 + 1, '\\'); argv += '"'; bs = 0; continue; }
        argv.append(bs, '\\'); bs = 0;
        argv += c;
    }
    argv.append(bs * 2, '\\');
    argv += '"';
    std::string r;
    for (char c : argv) {
        switch (c) {
            case '(': case ')': case '%': case '!': case '^': case '"': case '<': case '>': case '&': case '|':
                r += '^';
                break;
            default: break;
        }
        r += c;
    }
    out = r;
    return true;
}

// A whole argv as one printable line for the log.
inline std::string DisplayLine(const Argv& a) {
    std::string s;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (i) s += ' ';
#if defined(_WIN32)
        std::string q;
        s += WindowsCmdQuote(a[i], q) ? q : std::string("<unprintable>");
#else
        s += PosixShellQuote(a[i]);
#endif
    }
    return s;
}

}  // namespace okay::gitargs
