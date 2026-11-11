// FileDialog: see FileDialog.hpp. System dialogs per platform + the pure helpers + the built-in
// browser's model. The ImGui drawing of the browser is in FileDialogUI.cpp.
#include "FileDialog.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <utility>

#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <objbase.h>
#  include <shobjidl.h>
#else
#  include <fcntl.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  include <cerrno>
#endif

namespace okay::filedialog {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------------------------
// Pure helpers
// ---------------------------------------------------------------------------------------------
static std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
static std::string Upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
static std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
static bool IsAbsolutePath(const std::string& p) {
    if (!p.empty() && (p[0] == '/' || p[0] == '\\')) return true;
    return p.size() >= 2 && std::isalpha((unsigned char)p[0]) && p[1] == ':';
}
static std::string JoinPath(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    if (dir.back() == '/') return dir + name;
    return dir + "/" + name;
}

std::string NormalizePath(const std::string& in) {
    if (in.empty()) return {};
    std::string s = in;
    for (char& c : s) if (c == '\\') c = '/';
    std::string prefix;
    size_t pos = 0;
    if (s.size() >= 2 && s[0] == '/' && s[1] == '/' && !(s.size() >= 3 && s[2] == '/')) { prefix = "//"; pos = 2; }   // UNC
    else if (s[0] == '/') { prefix = "/"; pos = 1; }
    else if (s.size() >= 2 && std::isalpha((unsigned char)s[0]) && s[1] == ':') {
        prefix = s.substr(0, 2);
        pos = 2;
        if (pos < s.size() && s[pos] == '/') { prefix += "/"; ++pos; }
    }
    const bool absolute = !prefix.empty() && prefix.back() == '/';
    std::vector<std::string> parts;
    while (pos <= s.size()) {
        size_t slash = s.find('/', pos);
        std::string seg = s.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
        pos = slash == std::string::npos ? s.size() + 1 : slash + 1;
        if (seg.empty() || seg == ".") continue;
        if (seg == "..") {
            if (!parts.empty() && parts.back() != "..") parts.pop_back();
            else if (!absolute) parts.push_back("..");
            continue;
        }
        parts.push_back(seg);
    }
    std::string out = prefix;
    for (size_t i = 0; i < parts.size(); ++i) { if (i) out += '/'; out += parts[i]; }
    if (out.empty()) return ".";   // "a/.." and friends
    return out;
}

std::string FileNameOf(const std::string& path) {
    std::string n = NormalizePath(path);
    size_t s = n.find_last_of('/');
    if (n == "/" || (s != std::string::npos && s + 1 == n.size())) return {};   // a root
    return s == std::string::npos ? n : n.substr(s + 1);
}

std::string ParentOf(const std::string& path) {
    std::string n = NormalizePath(path);
    if (n.empty() || n == "/" ) return n;
    size_t s = n.find_last_of('/');
    if (s == std::string::npos) {
        if (n.size() == 2 && n[1] == ':') return n;
        if (n.size() > 2 && n[1] == ':') return n.substr(0, 2) + "/";   // "C:a" is rare; keep it sane
        return {};
    }
    if (s == 0) return "/";
    if (s == 2 && n[1] == ':') return n.substr(0, 3);   // "C:/x" -> "C:/"
    if (n.compare(0, 2, "//") == 0 && n.find('/', 2) == s) return n;   // "//server/share" is its own root
    return n.substr(0, s);
}

std::string ExtensionOf(const std::string& path) {
    std::string name = FileNameOf(path);
    size_t dot = name.find_last_of('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == name.size()) return {};
    return Lower(name.substr(dot));
}

std::vector<std::string> ParsePatterns(const std::string& text) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] { std::string t = Trim(cur); if (!t.empty()) out.push_back(t); cur.clear(); };
    for (char c : text) { if (c == ';' || c == ',' || c == ' ' || c == '\t' || c == '|') flush(); else cur += c; }
    flush();
    return out;
}

bool MatchPattern(const std::string& pattern, const std::string& name) {
    if (pattern == "*" || pattern == "*.*") return true;
    const std::string p = Lower(pattern), n = Lower(name);
    size_t pi = 0, ni = 0, star = std::string::npos, mark = 0;
    while (ni < n.size()) {
        if (pi < p.size() && (p[pi] == '?' || p[pi] == n[ni])) { ++pi; ++ni; }
        else if (pi < p.size() && p[pi] == '*') { star = pi++; mark = ni; }
        else if (star != std::string::npos) { pi = star + 1; ni = ++mark; }
        else return false;
    }
    while (pi < p.size() && p[pi] == '*') ++pi;
    return pi == p.size();
}

bool MatchesAny(const std::vector<std::string>& patterns, const std::string& name) {
    if (patterns.empty()) return true;
    for (const auto& p : patterns) if (MatchPattern(p, name)) return true;
    return false;
}

bool MatchesFilter(const Filter& f, const std::string& name) { return MatchesAny(f.patterns, name); }

std::string LiteralExtension(const std::string& pattern) {
    if (pattern.size() < 3 || pattern[0] != '*' || pattern[1] != '.') return {};
    std::string rest = pattern.substr(1);   // ".png"
    if (rest.find_first_of("*?") != std::string::npos) return {};
    return Lower(rest);
}

std::string EnforceExtension(const std::string& path, const Filter* filter) {
    if (path.empty() || !filter || filter->patterns.size() != 1) return path;
    const std::string ext = LiteralExtension(filter->patterns[0]);
    if (ext.empty() || !ExtensionOf(path).empty()) return path;
    if (path.back() == '.') return path + ext.substr(1);
    return path + ext;
}

StartSplit SplitStart(const std::string& startPath, bool isFolder) {
    StartSplit r;
    if (startPath.empty()) return r;
    const char last = startPath.back();
    if (isFolder || last == '/' || last == '\\') { r.dir = NormalizePath(startPath); if (r.dir == ".") r.dir.clear(); return r; }
    std::string n = NormalizePath(startPath);
    if (n.find('/') == std::string::npos && !(n.size() == 2 && n[1] == ':')) { r.name = n; return r; }
    r.name = FileNameOf(n);
    r.dir = ParentOf(n);
    return r;
}

// The start path split into folder + name; an existing folder without a trailing slash counts as a folder.
static StartSplit StartFor(const Request& r) {
    std::error_code ec;
    const bool onDisk = !r.startPath.empty() && fs::is_directory(fs::u8path(r.startPath), ec);
    return SplitStart(r.startPath, onDisk || r.kind == Kind::Folder);
}

// The patterns a helper program should be given: each pattern plus an upper-case twin, because
// zenity and kdialog match case-sensitively and "PHOTO.PNG" should still be offered.
static std::vector<std::string> WithCaseTwins(const std::vector<std::string>& pats) {
    std::vector<std::string> out;
    auto add = [&](const std::string& p) { if (std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p); };
    for (const auto& p : pats) { add(p); add(Upper(p)); }
    return out;
}
static std::string JoinWith(const std::vector<std::string>& v, const char* sep) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) { if (i) s += sep; s += v[i]; }
    return s;
}
static std::string OneLine(std::string s) {
    for (char& c : s) if (c == '\n' || c == '\r' || c == '|' || c == '\t') c = ' ';
    return s;
}

std::vector<std::string> ZenityArgs(const Request& r) {
    std::vector<std::string> a = {"zenity", "--file-selection"};
    if (!r.title.empty()) a.push_back("--title=" + r.title);
    if (r.kind == Kind::SaveFile) { a.push_back("--save"); a.push_back("--confirm-overwrite"); }
    if (r.kind == Kind::Folder) a.push_back("--directory");
    if (r.kind == Kind::OpenFile && r.multi) { a.push_back("--multiple"); a.push_back("--separator=\n"); }
    const StartSplit st = StartFor(r);
    if (r.kind == Kind::SaveFile) {
        if (!st.dir.empty() || !st.name.empty()) a.push_back("--filename=" + (st.dir.empty() ? st.name : JoinPath(st.dir, st.name)));
    } else if (!st.dir.empty()) {
        a.push_back("--filename=" + JoinPath(st.dir, ""));
    }
    if (r.kind != Kind::Folder && !r.filters.empty()) {
        for (const auto& f : r.filters) {
            if (f.patterns.empty()) continue;
            a.push_back("--file-filter=" + OneLine(f.name.empty() ? JoinWith(f.patterns, " ") : f.name) + " | " + JoinWith(WithCaseTwins(f.patterns), " "));
        }
        a.push_back("--file-filter=All files | *");
    }
    return a;
}

static std::string GuardDash(const std::string& s) { return !s.empty() && s[0] == '-' ? "./" + s : s; }

std::vector<std::string> KdialogArgs(const Request& r) {
    std::vector<std::string> a = {"kdialog"};
    if (!r.title.empty()) { a.push_back("--title"); a.push_back(r.title[0] == '-' ? " " + r.title : r.title); }
    switch (r.kind) {
        case Kind::OpenFile:
            if (r.multi) { a.push_back("--multiple"); a.push_back("--separate-output"); }
            a.push_back("--getopenfilename");
            break;
        case Kind::SaveFile: a.push_back("--getsavefilename"); break;
        case Kind::Folder:   a.push_back("--getexistingdirectory"); break;
    }
    const StartSplit st = StartFor(r);
    std::string start = r.kind == Kind::SaveFile ? (st.dir.empty() ? st.name : JoinPath(st.dir, st.name)) : JoinPath(st.dir, "");
    if (start.empty()) start = ".";
    a.push_back(GuardDash(start));
    if (r.kind != Kind::Folder && !r.filters.empty()) {
        std::string spec;
        for (const auto& f : r.filters) {
            if (f.patterns.empty()) continue;
            if (!spec.empty()) spec += "\n";
            spec += JoinWith(WithCaseTwins(f.patterns), " ") + "|" + OneLine(f.name);
        }
        spec += (spec.empty() ? "" : "\n") + std::string("*|All files");
        a.push_back(spec);
    }
    return a;
}

std::string AppleScriptQuote(const std::string& text) {
    std::string o = "\"";
    for (char c : text) {
        if (c == '\\') o += "\\\\";
        else if (c == '"') o += "\\\"";
        else if (c == '\n' || c == '\r') o += ' ';
        else o += c;
    }
    return o + "\"";
}

std::vector<std::string> OsascriptArgs(const Request& r) {
    std::vector<std::string> a = {"osascript"};
    auto line = [&](const std::string& s) { a.push_back("-e"); a.push_back(s); };
    const std::string prompt = r.title.empty() ? "" : " with prompt " + AppleScriptQuote(r.title);
    const StartSplit st = StartFor(r);
    const std::string where = st.dir.empty() ? "" : " default location (POSIX file " + AppleScriptQuote(st.dir) + ")";
    // "of type {"png", "jpg"}" only when every pattern is a plain "*.ext" (a UTI list cannot say more).
    std::string types;
    bool typed = !r.filters.empty();
    std::vector<std::string> exts;
    for (const auto& f : r.filters) {
        for (const auto& p : f.patterns) {
            std::string e = LiteralExtension(p);
            if (e.empty() || e.find('.', 1) != std::string::npos) { typed = false; continue; }
            exts.push_back(AppleScriptQuote(e.substr(1)));
        }
    }
    if (typed && !exts.empty()) types = " of type {" + JoinWith(exts, ", ") + "}";
    if (r.kind == Kind::Folder) {
        line("return POSIX path of (choose folder" + prompt + where + ")");
    } else if (r.kind == Kind::SaveFile) {
        const std::string def = st.name.empty() ? "" : " default name " + AppleScriptQuote(st.name);
        line("return POSIX path of (choose file name" + prompt + def + where + ")");
    } else if (r.multi) {
        line("set theFiles to choose file" + prompt + types + where + " with multiple selections allowed");
        line("set out to \"\"");
        line("repeat with f in theFiles");
        line("set out to out & POSIX path of f & linefeed");
        line("end repeat");
        line("return out");
    } else {
        line("return POSIX path of (choose file" + prompt + types + where + ")");
    }
    return a;
}

std::vector<std::string> ParseOutput(const std::string& out, bool multi) {
    std::vector<std::string> r;
    std::string cur;
    auto flush = [&] {
        while (!cur.empty() && (cur.back() == '\r')) cur.pop_back();
        if (!cur.empty()) r.push_back(cur);
        cur.clear();
    };
    for (char c : out) { if (c == '\n') flush(); else cur += c; }
    flush();
    if (!multi && r.size() > 1) r.resize(1);
    return r;
}

static std::string StripTrailingSeparators(std::string p) {
    while (p.size() > 1 && (p.back() == '/' || p.back() == '\\')) {
        if (p.size() == 3 && p[1] == ':') break;   // "C:/"
        p.pop_back();
    }
    return p;
}

std::vector<std::string> FinishResults(const Request& r, std::vector<std::string> paths) {
    for (auto& p : paths) {
        if (r.kind == Kind::Folder) p = StripTrailingSeparators(p);
        else if (r.kind == Kind::SaveFile && r.filters.size() == 1) p = EnforceExtension(p, &r.filters[0]);
    }
    return paths;
}

// ---------------------------------------------------------------------------------------------
// Running helper programs (POSIX)
// ---------------------------------------------------------------------------------------------
#ifndef _WIN32
bool FindOnPath(const std::string& program, std::string* fullPath) {
    if (program.empty()) return false;
    const char* env = std::getenv("PATH");
    std::string path = env ? env : "/usr/local/bin:/usr/bin:/bin";
    size_t pos = 0;
    while (pos <= path.size()) {
        size_t colon = path.find(':', pos);
        std::string dir = path.substr(pos, colon == std::string::npos ? std::string::npos : colon - pos);
        pos = colon == std::string::npos ? path.size() + 1 : colon + 1;
        if (dir.empty()) dir = ".";
        std::string full = dir + "/" + program;
        std::error_code ec;
        if (fs::is_regular_file(full, ec) && ::access(full.c_str(), X_OK) == 0) { if (fullPath) *fullPath = full; return true; }
    }
    return false;
}

bool RunProcess(const std::vector<std::string>& argv, std::string& out, int& exitCode) {
    out.clear();
    exitCode = -1;
    if (argv.empty()) return false;
    int fds[2];
    if (::pipe(fds) != 0) return false;
    std::vector<char*> av;
    for (const auto& s : argv) av.push_back(const_cast<char*>(s.c_str()));
    av.push_back(nullptr);
    pid_t pid = ::fork();
    if (pid < 0) { ::close(fds[0]); ::close(fds[1]); return false; }
    if (pid == 0) {   // child: stdout -> pipe, stdin/stderr -> /dev/null, no shell
        ::dup2(fds[1], 1);
        ::close(fds[0]); ::close(fds[1]);
        int nul = ::open("/dev/null", O_RDWR);
        if (nul >= 0) { ::dup2(nul, 0); ::dup2(nul, 2); if (nul > 2) ::close(nul); }
        ::execvp(av[0], av.data());
        ::_exit(127);
    }
    ::close(fds[1]);
    char buf[4096];
    for (;;) {
        ssize_t n = ::read(fds[0], buf, sizeof(buf));
        if (n > 0) out.append(buf, (size_t)n);
        else if (n < 0 && errno == EINTR) continue;
        else break;
    }
    ::close(fds[0]);
    int status = 0;
    pid_t w;
    do { w = ::waitpid(pid, &status, 0); } while (w < 0 && errno == EINTR);
    if (w < 0) { exitCode = out.empty() ? 1 : 0; return true; }   // SIGCHLD ignored: guess from the output
    exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return exitCode != 127;   // 127: exec failed
}
#else
bool FindOnPath(const std::string&, std::string*) { return false; }
bool RunProcess(const std::vector<std::string>&, std::string&, int&) { return false; }
#endif

// ---------------------------------------------------------------------------------------------
// Backend choice
// ---------------------------------------------------------------------------------------------
const char* BackendName(Backend b) {
    switch (b) {
        case Backend::Win32: return "win32";
        case Backend::Zenity: return "zenity";
        case Backend::Kdialog: return "kdialog";
        case Backend::Osascript: return "osascript";
        default: return "builtin";
    }
}

static bool g_decided = false;
static Backend g_backend = Backend::Builtin;

static Backend Decide() {
    const char* force = std::getenv("OKAY_FILE_DIALOG");
    if (force && *force) {
        const std::string f = Lower(force);
        if (f == "builtin") return Backend::Builtin;
#ifdef _WIN32
        if (f == "win32") return Backend::Win32;
#else
        if (f == "zenity") return Backend::Zenity;
        if (f == "kdialog") return Backend::Kdialog;
        if (f == "osascript") return Backend::Osascript;
#endif
    }
#ifdef _WIN32
    return Backend::Win32;
#elif defined(__APPLE__)
    return FindOnPath("osascript") ? Backend::Osascript : Backend::Builtin;
#else
    const char* disp = std::getenv("DISPLAY");
    const char* way = std::getenv("WAYLAND_DISPLAY");
    if ((!disp || !*disp) && (!way || !*way)) return Backend::Builtin;
    if (FindOnPath("zenity")) return Backend::Zenity;
    if (FindOnPath("kdialog")) return Backend::Kdialog;
    return Backend::Builtin;
#endif
}

Backend CurrentBackend() {
    if (!g_decided) { g_backend = Decide(); g_decided = true; }
    return g_backend;
}
void ResetBackendForTests() { g_decided = false; }

// ---------------------------------------------------------------------------------------------
// System dialogs
// ---------------------------------------------------------------------------------------------
#ifdef _WIN32
static std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
static std::string ToUtf8(const wchar_t* w) {
    if (!w || !*w) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string s((size_t)(n > 0 ? n - 1 : 0), '\0');
    if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}
static std::wstring Backslashes(std::wstring w) { for (auto& c : w) if (c == L'/') c = L'\\'; return w; }

// The shell's own ids, spelled out so no extra import library is needed.
static const GUID kClsidFileOpen  = {0xDC1C5A9C, 0xE88A, 0x4DDE, {0xA5, 0xA1, 0x60, 0xF8, 0x2A, 0x20, 0xAE, 0xF7}};
static const GUID kClsidFileSave  = {0xC0B4E2F3, 0xBA21, 0x4773, {0x8D, 0xBA, 0x33, 0x5E, 0xC9, 0x46, 0xEB, 0x8B}};
static const GUID kIidFileDialog  = {0x42F85136, 0xDB7E, 0x439C, {0x85, 0xF1, 0xE4, 0x07, 0x5D, 0x13, 0x5F, 0xC8}};
static const GUID kIidFileOpenDlg = {0xD57C7288, 0xD4AD, 0x4768, {0xBE, 0x02, 0x9D, 0x96, 0x95, 0x32, 0xA6, 0x60}};
static const GUID kIidShellItem   = {0x43826D1E, 0xE718, 0x42EE, {0xBC, 0x55, 0xA1, 0xE2, 0x61, 0xC3, 0x7B, 0xFE}};

static bool RunWin32(const Request& r, std::vector<std::string>& out) {
    out.clear();
    const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool uninit = SUCCEEDED(hrInit);   // S_FALSE (already initialised) still needs the matching call
    bool handled = false;
    IFileDialog* dlg = nullptr;
    if (SUCCEEDED(CoCreateInstance(r.kind == Kind::SaveFile ? kClsidFileSave : kClsidFileOpen, nullptr, CLSCTX_INPROC_SERVER,
                                   kIidFileDialog, reinterpret_cast<void**>(&dlg))) && dlg) {
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        opts |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR;
        if (r.kind == Kind::Folder) opts |= FOS_PICKFOLDERS;
        if (r.kind == Kind::OpenFile) opts |= FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST | (r.multi ? (DWORD)FOS_ALLOWMULTISELECT : (DWORD)0);
        if (r.kind == Kind::SaveFile) opts |= FOS_OVERWRITEPROMPT | FOS_PATHMUSTEXIST;
        dlg->SetOptions(opts);
        if (!r.title.empty()) { std::wstring t = ToWide(r.title); dlg->SetTitle(t.c_str()); }

        std::vector<std::wstring> names, specs;
        std::vector<COMDLG_FILTERSPEC> fsv;
        if (r.kind != Kind::Folder && !r.filters.empty()) {
            for (const auto& f : r.filters) {
                if (f.patterns.empty()) continue;
                names.push_back(ToWide(f.name.empty() ? JoinWith(f.patterns, ", ") : f.name));
                specs.push_back(ToWide(JoinWith(f.patterns, ";")));
            }
            names.push_back(L"All files"); specs.push_back(L"*.*");
            for (size_t i = 0; i < names.size(); ++i) fsv.push_back({names[i].c_str(), specs[i].c_str()});
            dlg->SetFileTypes((UINT)fsv.size(), fsv.data());
            dlg->SetFileTypeIndex(1);
            if (r.kind == Kind::SaveFile && r.filters.size() == 1) {
                const std::string ext = r.filters[0].patterns.size() == 1 ? LiteralExtension(r.filters[0].patterns[0]) : std::string();
                if (!ext.empty()) { std::wstring e = ToWide(ext.substr(1)); dlg->SetDefaultExtension(e.c_str()); }
            }
        }
        const StartSplit st = StartFor(r);
        if (!st.dir.empty()) {
            IShellItem* item = nullptr;
            std::wstring wd = Backslashes(ToWide(st.dir));
            if (SUCCEEDED(SHCreateItemFromParsingName(wd.c_str(), nullptr, kIidShellItem, reinterpret_cast<void**>(&item))) && item) {
                dlg->SetFolder(item);
                item->Release();
            }
        }
        if (r.kind == Kind::SaveFile && !st.name.empty()) { std::wstring n = ToWide(st.name); dlg->SetFileName(n.c_str()); }

        const HRESULT hr = dlg->Show(GetActiveWindow());
        if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
            handled = true;   // cancel: empty result
        } else if (SUCCEEDED(hr)) {
            handled = true;
            auto take = [&](IShellItem* it) {
                PWSTR p = nullptr;
                if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) { out.push_back(ToUtf8(p)); CoTaskMemFree(p); }
            };
            if (r.kind == Kind::OpenFile && r.multi) {
                IFileOpenDialog* od = nullptr;
                if (SUCCEEDED(dlg->QueryInterface(kIidFileOpenDlg, reinterpret_cast<void**>(&od))) && od) {
                    IShellItemArray* arr = nullptr;
                    if (SUCCEEDED(od->GetResults(&arr)) && arr) {
                        DWORD n = 0;
                        arr->GetCount(&n);
                        for (DWORD i = 0; i < n; ++i) { IShellItem* it = nullptr; if (SUCCEEDED(arr->GetItemAt(i, &it)) && it) { take(it); it->Release(); } }
                        arr->Release();
                    }
                    od->Release();
                }
            } else {
                IShellItem* it = nullptr;
                if (SUCCEEDED(dlg->GetResult(&it)) && it) { take(it); it->Release(); }
            }
        }
        dlg->Release();
    }
    if (uninit) CoUninitialize();
    return handled;
}
#endif

bool RunNative(const Request& r, std::vector<std::string>& paths) {
    paths.clear();
    const Backend b = CurrentBackend();
    if (b == Backend::Builtin) return false;
#ifdef _WIN32
    if (!RunWin32(r, paths)) { g_backend = Backend::Builtin; return false; }
    paths = FinishResults(r, paths);
    return true;
#else
    std::vector<std::string> argv = b == Backend::Zenity ? ZenityArgs(r) : b == Backend::Kdialog ? KdialogArgs(r) : OsascriptArgs(r);
    std::string out;
    int code = -1;
    if (!RunProcess(argv, out, code) || code < 0 || code > 1) { g_backend = Backend::Builtin; return false; }   // broken helper: use the built-in browser from now on
    if (code == 1) return true;   // the user cancelled
    paths = FinishResults(r, ParseOutput(out, r.multi && r.kind == Kind::OpenFile));
    return true;
#endif
}

// ---------------------------------------------------------------------------------------------
// Built-in browser: model
// ---------------------------------------------------------------------------------------------
static std::string HomeDir() {
#ifdef _WIN32
    const char* h = std::getenv("USERPROFILE");
#else
    const char* h = std::getenv("HOME");
#endif
    return h ? NormalizePath(h) : std::string();
}

bool BrowserModel::List() {
    std::error_code ec;
    fs::directory_iterator it(fs::u8path(dir_), fs::directory_options::skip_permission_denied, ec);
    if (ec) return false;
    entries_.clear();
    const Filter* flt = (req_.kind != Kind::Folder && filterIndex_ >= 0 && filterIndex_ < (int)req_.filters.size()) ? &req_.filters[(size_t)filterIndex_] : nullptr;
    for (fs::directory_iterator end; it != end; it.increment(ec)) {
        if (ec) break;
        std::string name = it->path().filename().u8string();
        if (name.empty() || (!showHidden_ && name[0] == '.')) continue;
        std::error_code e2;
        BrowserEntry be;
        be.name = name;
        be.isDir = it->is_directory(e2);
        if (!be.isDir) {
            if (req_.kind == Kind::Folder) continue;
            if (flt && !MatchesFilter(*flt, name)) continue;
            std::error_code e3;
            auto sz = it->file_size(e3);
            be.size = e3 ? 0 : (std::uint64_t)sz;
        }
        entries_.push_back(std::move(be));
    }
    std::sort(entries_.begin(), entries_.end(), [](const BrowserEntry& a, const BrowserEntry& b) {
        if (a.isDir != b.isDir) return a.isDir;
        return Lower(a.name) < Lower(b.name);
    });
    return true;
}

void BrowserModel::Begin(const Request& r) {
    req_ = r;
    selected_.clear();
    typed.clear();
    why_.clear();
    filterIndex_ = 0;
    std::error_code ec;
    const bool isDir = !r.startPath.empty() && fs::is_directory(fs::u8path(r.startPath), ec);
    const StartSplit st = SplitStart(r.startPath, isDir);
    if (r.kind == Kind::SaveFile) typed = st.name;
    std::string d = st.dir;
    // Walk up to the nearest folder that exists, then fall back to home, then the current folder.
    while (!d.empty() && !fs::is_directory(fs::u8path(d), ec)) {
        std::string up = ParentOf(d);
        if (up == d) { d.clear(); break; }
        d = up;
    }
    if (d.empty()) d = HomeDir();
    if (d.empty() || !fs::is_directory(fs::u8path(d), ec)) d = NormalizePath(fs::current_path(ec).u8string());
    dir_ = d;
    if (!List()) { dir_ = "/"; List(); }
}

bool BrowserModel::GoTo(const std::string& dir) {
    std::string n = NormalizePath(dir);
    if (n.empty()) return false;
    std::error_code ec;
    if (!fs::is_directory(fs::u8path(n), ec)) return false;
    std::string old = dir_;
    dir_ = n;
    if (!List()) { dir_ = old; List(); return false; }
    selected_.clear();
    return true;
}
bool BrowserModel::Up() { std::string p = ParentOf(dir_); return !p.empty() && p != dir_ && GoTo(p); }
bool BrowserModel::Refresh() { return List(); }

bool BrowserModel::Enter(const std::string& name) {
    for (const auto& e : entries_) {
        if (e.name != name) continue;
        if (e.isDir) return GoTo(JoinPath(dir_, name));
        Select(name, false);
        return false;
    }
    return false;
}

void BrowserModel::SetFilterIndex(int i) { filterIndex_ = i; List(); }
void BrowserModel::SetShowHidden(bool v) { showHidden_ = v; List(); }

bool BrowserModel::IsSelected(const std::string& name) const { return std::find(selected_.begin(), selected_.end(), name) != selected_.end(); }

void BrowserModel::Select(const std::string& name, bool additive) {
    bool isDir = false, found = false;
    for (const auto& e : entries_) if (e.name == name) { found = true; isDir = e.isDir; }
    if (!found) return;
    if (isDir && req_.kind != Kind::Folder) return;   // folders are entered, not picked, when choosing files
    const bool multi = req_.kind == Kind::OpenFile && req_.multi;
    auto it = std::find(selected_.begin(), selected_.end(), name);
    if (multi && additive) { if (it != selected_.end()) selected_.erase(it); else selected_.push_back(name); }
    else { selected_.assign(1, name); }
    if (req_.kind == Kind::SaveFile && !isDir) typed = name;
}

std::vector<std::string> BrowserModel::Result() {
    why_.clear();
    std::vector<std::string> out;
    std::error_code ec;
    if (req_.kind == Kind::Folder) {
        out.push_back(selected_.empty() ? dir_ : JoinPath(dir_, selected_[0]));
        return out;
    }
    const std::string name = Trim(typed);
    if (req_.kind == Kind::OpenFile) {
        if (!selected_.empty()) { for (const auto& s : selected_) out.push_back(JoinPath(dir_, s)); return out; }
        if (name.empty()) { why_ = "Pick a file"; return out; }
        std::string p = IsAbsolutePath(name) ? NormalizePath(name) : NormalizePath(JoinPath(dir_, name));
        if (!fs::is_regular_file(fs::u8path(p), ec)) { why_ = "That file does not exist"; return out; }
        out.push_back(p);
        return out;
    }
    // Save
    if (name.empty()) { why_ = "Type a file name"; return out; }
    if (name == "." || name == "..") { why_ = "That is not a file name"; return out; }
    std::string p = IsAbsolutePath(name) ? NormalizePath(name) : NormalizePath(JoinPath(dir_, name));
    const Filter* flt = (filterIndex_ >= 0 && filterIndex_ < (int)req_.filters.size()) ? &req_.filters[(size_t)filterIndex_] : nullptr;
    if (fs::is_directory(fs::u8path(p), ec)) { why_ = "That is a folder"; return out; }
    p = EnforceExtension(p, flt);
    if (fs::is_directory(fs::u8path(p), ec)) { why_ = "That is a folder"; return out; }
    if (!fs::is_directory(fs::u8path(ParentOf(p)), ec)) { why_ = "That folder does not exist"; return out; }
    out.push_back(p);
    return out;
}

bool BrowserModel::ResultExists() {
    if (req_.kind != Kind::SaveFile) return false;
    auto r = Result();
    std::error_code ec;
    return !r.empty() && fs::exists(fs::u8path(r[0]), ec);
}

std::vector<std::pair<std::string, std::string>> BrowserModel::Places() const {
    std::vector<std::pair<std::string, std::string>> p;
    std::error_code ec;
    const std::string home = HomeDir();
    if (!home.empty()) {
        p.push_back({"Home", home});
        for (const char* sub : {"Desktop", "Documents", "Downloads", "Pictures"}) {
            std::string d = home + "/" + sub;
            if (fs::is_directory(fs::u8path(d), ec)) p.push_back({sub, d});
        }
    }
#ifdef _WIN32
    for (char c = 'A'; c <= 'Z'; ++c) {
        std::string d = std::string(1, c) + ":/";
        if (fs::is_directory(fs::u8path(d), ec)) p.push_back({std::string(1, c) + ":", d});
    }
#else
    p.push_back({"Computer", "/"});
#endif
    return p;
}

// ---------------------------------------------------------------------------------------------
// Built-in browser: the queue
// ---------------------------------------------------------------------------------------------
namespace {
struct Pending { Request req; Callback cb; };
struct Builtin {
    bool active = false;
    BrowserModel model;
    Callback cb;
    std::deque<Pending> queue;
};
Builtin& B() { static Builtin b; return b; }
void StartNext() {
    Builtin& b = B();
    if (b.queue.empty()) { b.active = false; b.cb = nullptr; return; }
    Pending p = std::move(b.queue.front());
    b.queue.pop_front();
    b.model.Begin(p.req);
    b.cb = std::move(p.cb);
    b.active = true;
}
}  // namespace

bool BuiltinOpen() { return B().active || !B().queue.empty(); }
BrowserModel* BuiltinModel() { return B().active ? &B().model : nullptr; }

void FinishBuiltin(bool accepted) {
    Builtin& b = B();
    if (!b.active) return;
    std::vector<std::string> res;
    if (accepted) res = b.model.Result();
    Callback cb = std::move(b.cb);
    StartNext();   // before the callback, so a callback that asks for another dialog queues cleanly
    if (accepted && !res.empty() && cb) cb(res);
}

void CancelBuiltin() { B().queue.clear(); B().active = false; B().cb = nullptr; }

bool AnswerBuiltinForTests(const std::vector<std::string>& paths) {
    Builtin& b = B();
    if (!b.active) return false;
    Callback cb = std::move(b.cb);
    StartNext();
    if (!paths.empty() && cb) cb(paths);
    return true;
}

// ---------------------------------------------------------------------------------------------
// The calls
// ---------------------------------------------------------------------------------------------
void Show(const Request& r, Callback cb) {
    if (!cb) return;
    std::vector<std::string> paths;
    if (!BuiltinOpen() && RunNative(r, paths)) {
        if (!paths.empty()) cb(paths);
        return;
    }
    Builtin& b = B();
    b.queue.push_back({r, std::move(cb)});
    if (!b.active) StartNext();
}

void Open(const std::string& title, const std::string& startPath, const std::vector<Filter>& filters, bool multi, Callback cb) {
    Request r; r.kind = Kind::OpenFile; r.title = title; r.startPath = startPath; r.filters = filters; r.multi = multi;
    Show(r, std::move(cb));
}
void Save(const std::string& title, const std::string& startPath, const std::vector<Filter>& filters, Callback cb) {
    Request r; r.kind = Kind::SaveFile; r.title = title; r.startPath = startPath; r.filters = filters;
    Show(r, std::move(cb));
}
void Folder(const std::string& title, const std::string& startPath, Callback cb) {
    Request r; r.kind = Kind::Folder; r.title = title; r.startPath = startPath;
    Show(r, std::move(cb));
}
static Callback First(std::function<void(const std::string&)> cb) {
    return [cb](const std::vector<std::string>& v) { if (!v.empty() && !v[0].empty()) cb(v[0]); };
}
void OpenOne(const std::string& t, const std::string& s, const std::vector<Filter>& f, std::function<void(const std::string&)> cb) { Open(t, s, f, false, First(std::move(cb))); }
void SaveOne(const std::string& t, const std::string& s, const std::vector<Filter>& f, std::function<void(const std::string&)> cb) { Save(t, s, f, First(std::move(cb))); }
void FolderOne(const std::string& t, const std::string& s, std::function<void(const std::string&)> cb) { Folder(t, s, First(std::move(cb))); }

}  // namespace okay::filedialog
