// okay-build: prototype of our own build tool (docs/own_build_plan.md). OFF by default, dev tool only.
// Written from general knowledge of how build systems work (action graph, compiler-made depfiles,
// content hashes, a ready queue). No code from any other build tool was used.
// POSIX uses fork/exec and worker threads. Windows (_WIN32) uses CreateProcessW and waits on
// process handles from one thread, so it does not include <thread>, <mutex> or <condition_variable>.
// Not built by CMake or CI.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <deque>
#include <map>
#include <set>
#include <unordered_map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <condition_variable>
#include <mutex>
#include <thread>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <poll.h>
#include <csignal>
#endif

namespace fs = std::filesystem;
using std::string;
using std::string_view;
using std::vector;

#ifdef _WIN32
static std::wstring widen(const string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w((size_t)n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}
// Absolute Unix paths (Wine maps / to Z:) become Z:\... when that file exists. Relative paths stay relative.
static std::wstring widePath(const string& p) {
    string s = p;
    for (char& c : s) if (c == '/') c = '\\';
    if (!(s.size() >= 2 && s[1] == ':') && !s.empty() && s[0] == '\\') {
        string z = "Z:" + s;
        if (GetFileAttributesW(widen(z).c_str()) != INVALID_FILE_ATTRIBUTES) s = z;
    }
    return widen(s);
}
static int gHashMu = 0, gStateMu = 0;
#define OK_LOCK(name, mu) int name = ((void)(mu), 0); (void)name
#else
static std::mutex gHashMu, gStateMu;
#define OK_LOCK(name, mu) std::lock_guard<std::mutex> name(mu)
#endif

struct Fail : std::runtime_error { using std::runtime_error::runtime_error; };

// ---------------------------------------------------------------- manifest
struct Toolchain {
    string name = "gcc", cc = "gcc", cxx = "g++", ld, ar = "ar", obj = ".o", exe, libp = "lib", libs = ".a";
    vector<string> flags, linkFlags; size_t rspThreshold = 30000; bool msvc = false; bool rspUtf16 = false;
    bool autoPick = false; vector<string> setKeys; // 'toolchain auto'; tool fields the manifest set by hand
};
static bool applyPreset(Toolchain& c, const string& name) {
    Toolchain d; d.flags = c.flags; d.linkFlags = c.linkFlags; d.rspThreshold = c.rspThreshold; d.autoPick = c.autoPick; d.setKeys = c.setKeys;
    if (name == "gcc") {}
    else if (name == "clang") { d.cc = "clang"; d.cxx = "clang++"; }
    else if (name == "mingw") { d.cc = "x86_64-w64-mingw32-gcc-win32"; d.cxx = "x86_64-w64-mingw32-g++-win32"; d.ar = "x86_64-w64-mingw32-ar"; d.exe = ".exe"; }
    else if (name == "msvc") { d.cc = "cl.exe"; d.cxx = "cl.exe"; d.ld = "link.exe"; d.ar = "lib.exe"; d.obj = ".obj"; d.exe = ".exe"; d.libp = ""; d.libs = ".lib"; d.msvc = true; d.rspUtf16 = true; }
    else return false;
    d.name = name; c = d; return true;
}
struct PoolDecl { string name; int depth = 1; int line = 0; };
struct RawAction {
    string dir, cmd, output, depfile, pool; vector<string> inputs, after, outputs; bool restat = false; int memory = 0; int line = 0;
};
struct Target {
    string name; bool isLib = false; bool isTest = false; int line = 0; string pool;
    vector<string> sources, includes, exportIncludes, defines, flags, linkFlags, libs, deps, needs, runtime, assets;
};
struct ConfigBlock {
    string name; int line = 0; int optimize = -1; int debug = -1;
    vector<string> flags, linkFlags, defines;
};
struct OptionDecl { string name, type, deflt; int line = 0; };
struct CondBlock {
    string opt, equals; bool hasEquals = false; int line = 0;
    vector<string> flags, linkFlags, defines;
};
struct PackageDecl {
    string name, path, version; int line = 0;
    vector<std::pair<string, string>> depends;
};
struct DistDecl {
    bool on = false; string dir = "dist", launcher, dll, readme, version, textures, fonts;
    vector<string> tools, examples;
};
struct Manifest {
    Toolchain tc; string outdir = "build"; vector<Target> targets; vector<RawAction> actions; vector<PoolDecl> pools;
    vector<ConfigBlock> configs; vector<OptionDecl> options; vector<CondBlock> conds; vector<PackageDecl> packages;
    DistDecl dist;
    bool hasBuildfile = false; string buildCmd, buildDepfile; vector<string> buildInputs; int buildLine = 0;
};

// split on whitespace; "double quoted" words may contain spaces
static vector<string> tokens(const string& line) {
    vector<string> t; string cur; bool inq = false, have = false;
    for (char c : line) {
        if (c == '"') { inq = !inq; have = true; }
        else if (!inq && isspace((unsigned char)c)) { if (have) { t.push_back(cur); cur.clear(); have = false; } }
        else { cur += c; have = true; }
    }
    if (have) t.push_back(cur);
    return t;
}
static string qtok(const string& s) { return s.find_first_of(" \t") == string::npos && !s.empty() ? s : "\"" + s + "\""; }

// '#' starts a comment only outside quotes. A cmd line is kept whole, because the command text
// itself may contain '#' (a preprocessor line, a quoted path). A trailing comment on a cmd line
// is therefore part of the command.
static bool isCmdLine(const string& line) {
    size_t i = 0; while (i < line.size() && isspace((unsigned char)line[i])) ++i;
    if (line.compare(i, 3, "cmd") != 0) return false;
    size_t j = i + 3; return j >= line.size() || isspace((unsigned char)line[j]) || line[j] == '=';
}
static string stripComment(const string& line) {
    bool inq = false;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '"') inq = !inq;
        else if (line[i] == '#' && !inq) return line.substr(0, i);
    }
    return line;
}

static Manifest parseManifest(const string& file) {
    std::ifstream in(file);
    if (!in) throw Fail("cannot open manifest " + file);
    Manifest m; string line; int ln = 0;
    enum { TOP, TC, TARGET, ACTION, POOL, BUILDFILE, CONFIG, COND, PACKAGE, DIST } mode = TOP;
    auto err = [&](const string& msg) { return Fail(file + ":" + std::to_string(ln) + ": " + msg); };
    auto oneFlag = [&](const vector<string>& v, const string& k) {
        if (v.size() != 1) throw err(k + " takes one value");
        if (v[0] == "1" || v[0] == "true" || v[0] == "yes" || v[0] == "on") return true;
        if (v[0] == "0" || v[0] == "false" || v[0] == "no" || v[0] == "off") return false;
        throw err(k + " is on or off");
    };
    while (std::getline(in, line)) {
        ++ln;
        if (!isCmdLine(line)) line = stripComment(line);
        auto t = tokens(line); if (t.empty()) continue;
        if ((mode == ACTION || mode == BUILDFILE) && t[0] == "cmd") { // raw rest of line, kept exactly
            auto p = line.find('='); if (p == string::npos) throw err("expected: cmd = command");
            size_t b = line.find_first_not_of(" \t", p + 1); string cmd = b == string::npos ? "" : line.substr(b);
            while (!cmd.empty() && isspace((unsigned char)cmd.back())) cmd.pop_back();
            (mode == ACTION ? m.actions.back().cmd : m.buildCmd) = cmd; continue;
        }
        if (t[0] == "end") { if (mode == TOP) throw err("'end' outside a block"); mode = TOP; continue; }
        if (mode == TOP) {
            if (t[0] == "outdir" && t.size() == 3 && t[1] == "=") { m.outdir = t[2]; continue; }
            if (t[0] == "toolchain") {
                mode = TC; Toolchain& c = m.tc;
                if (t.size() > 1) {
                    if (t[1] == "auto") { c.autoPick = true; c.name = "auto"; }
                    else if (!applyPreset(c, t[1])) throw err("unknown toolchain preset '" + t[1] + "' (auto, gcc, clang, mingw, msvc)");
                }
                continue;
            }
            if (t[0] == "pool") {
                if (t.size() != 2) throw err("expected: pool NAME");
                for (auto& p : m.pools) if (p.name == t[1]) throw err("duplicate pool '" + t[1] + "'");
                m.pools.push_back(PoolDecl{t[1], 1, ln}); mode = POOL; continue;
            }
            if (t[0] == "buildfile") {
                if (t.size() != 1) throw err("expected: buildfile");
                if (m.hasBuildfile) throw err("only one buildfile block is allowed");
                m.hasBuildfile = true; m.buildLine = ln; mode = BUILDFILE; continue;
            }
            if (t[0] == "action") { m.actions.push_back(RawAction()); m.actions.back().line = ln; mode = ACTION; continue; }
            if (t[0] == "dist") {
                if (m.dist.on) throw err("only one dist block is allowed");
                m.dist.on = true; mode = DIST; continue;
            }
            if (t[0] == "package") {
                if (t.size() != 2) throw err("expected: package NAME");
                for (auto& p : m.packages) if (p.name == t[1]) throw err("duplicate package '" + t[1] + "'");
                PackageDecl p; p.name = t[1]; p.line = ln; m.packages.push_back(p); mode = PACKAGE; continue;
            }
            if (t[0] == "target") {
                if (t.size() != 3 || (t[2] != "exe" && t[2] != "lib")) throw err("expected: target NAME exe|lib");
                for (auto& x : m.targets) if (x.name == t[1]) throw err("duplicate target '" + t[1] + "'");
                Target x; x.name = t[1]; x.isLib = t[2] == "lib"; x.line = ln; m.targets.push_back(x); mode = TARGET; continue;
            }
            if (t[0] == "option") {
                if (t.size() < 5 || t[3] != "=") throw err("expected: option NAME bool|int|string = VALUE");
                string name = t[1], type = t[2];
                if (type != "bool" && type != "int" && type != "string") throw err("option type is bool, int or string");
                for (auto& o : m.options) if (o.name == name) throw err("duplicate option '" + name + "'");
                string value;
                if (type == "string") { for (size_t i = 4; i < t.size(); ++i) { if (!value.empty()) value += ' '; value += t[i]; } }
                else if (t.size() != 5) throw err("option '" + name + "' takes one value");
                else value = t[4];
                if (type == "bool" && !(value == "1" || value == "0" || value == "true" || value == "false" || value == "yes" || value == "no" || value == "on" || value == "off"))
                    throw err("option '" + name + "' default is not a bool");
                if (type == "int") {
                    size_t i = (!value.empty() && (value[0] == '-' || value[0] == '+')) ? 1 : 0;
                    if (i >= value.size()) throw err("option '" + name + "' default is not an int");
                    for (; i < value.size(); ++i) if (!isdigit((unsigned char)value[i])) throw err("option '" + name + "' default is not an int");
                }
                m.options.push_back(OptionDecl{name, type, value, ln}); continue;
            }
            if (t[0] == "config") {
                if (t.size() != 2) throw err("expected: config NAME");
                for (auto& c : m.configs) if (c.name == t[1]) throw err("duplicate config '" + t[1] + "'");
                ConfigBlock c; c.name = t[1]; c.line = ln; m.configs.push_back(c); mode = CONFIG; continue;
            }
            if (t[0] == "if") {
                if (!(t.size() == 2 || (t.size() == 4 && t[2] == "=="))) throw err("expected: if OPTION   or   if OPTION == VALUE");
                CondBlock c; c.opt = t[1]; c.line = ln; if (t.size() == 4) { c.hasEquals = true; c.equals = t[3]; }
                m.conds.push_back(c); mode = COND; continue;
            }
            throw err("unexpected '" + t[0] + "' (expected toolchain, target, package, config, option, if, pool, action, buildfile or outdir)");
        }
        if (t.size() < 2 || t[1] != "=") throw err("expected: key = values");
        vector<string> v(t.begin() + 2, t.end()); string k = t[0];
        auto add = [&](vector<string>& d) { d.insert(d.end(), v.begin(), v.end()); };
        if (mode == TC) {
            Toolchain& c = m.tc; auto one = [&](string& d) { c.setKeys.push_back(k); if (v.size() != 1 && !(v.empty() && (k == "exe_suffix" || k == "lib_prefix"))) throw err(k + " takes one value"); d = v.empty() ? "" : v[0]; };
            if (k == "cc") one(c.cc); else if (k == "cxx") one(c.cxx); else if (k == "ld") one(c.ld); else if (k == "ar") one(c.ar);
            else if (k == "obj_suffix") one(c.obj); else if (k == "exe_suffix") one(c.exe);
            else if (k == "lib_prefix") one(c.libp); else if (k == "lib_suffix") one(c.libs);
            else if (k == "flags") add(c.flags); else if (k == "link_flags") add(c.linkFlags);
            else if (k == "rsp_threshold") { if (v.size() != 1) throw err("rsp_threshold takes one value"); c.rspThreshold = (size_t)atoll(v[0].c_str()); }
            else if (k == "rsp_utf16") c.rspUtf16 = oneFlag(v, "rsp_utf16");
            else throw err("unknown toolchain key '" + k + "'");
        } else if (mode == POOL) {
            PoolDecl& p = m.pools.back();
            if (k == "depth") {
                if (v.size() != 1) throw err("depth takes one value");
                p.depth = atoi(v[0].c_str()); if (p.depth < 1) throw err("pool depth must be at least 1");
            } else throw err("unknown pool key '" + k + "'");
        } else if (mode == ACTION) {
            RawAction& x = m.actions.back();
            if (k == "dir") { if (v.size() != 1) throw err("dir takes one value"); x.dir = v[0]; }
            else if (k == "output") { if (v.size() != 1) throw err("output takes one value"); x.output = v[0]; }
            else if (k == "outputs") add(x.outputs);
            else if (k == "depfile") { if (v.size() != 1) throw err("depfile takes one value"); x.depfile = v[0]; }
            else if (k == "pool") { if (v.size() != 1) throw err("pool takes one value"); x.pool = v[0]; }
            else if (k == "restat") x.restat = oneFlag(v, k);
            else if (k == "memory") { if (v.size() != 1) throw err("memory takes one value"); x.memory = atoi(v[0].c_str()); if (x.memory < 0) throw err("memory must be 0 or more"); }
            else if (k == "inputs") add(x.inputs); else if (k == "after") add(x.after);
            else throw err("unknown action key '" + k + "'");
        } else if (mode == BUILDFILE) {
            if (k == "inputs") add(m.buildInputs);
            else if (k == "depfile") { if (v.size() != 1) throw err("depfile takes one value"); m.buildDepfile = v[0]; }
            else throw err("unknown buildfile key '" + k + "'");
        } else if (mode == CONFIG || mode == COND) {
            vector<string>& flags = mode == CONFIG ? m.configs.back().flags : m.conds.back().flags;
            vector<string>& lflags = mode == CONFIG ? m.configs.back().linkFlags : m.conds.back().linkFlags;
            vector<string>& defs = mode == CONFIG ? m.configs.back().defines : m.conds.back().defines;
            if (k == "flags") add(flags);
            else if (k == "link_flags") add(lflags);
            else if (k == "define") add(defs);
            else if (mode == CONFIG && k == "optimize") {
                if (v.size() != 1) throw err("optimize takes one value");
                size_t i = 0; if (!v[0].empty() && (v[0][0] == '-' || v[0][0] == '+')) i = 1;
                bool digits = i < v[0].size();
                for (; i < v[0].size(); ++i) if (!isdigit((unsigned char)v[0][i])) digits = false;
                int n = atoi(v[0].c_str());
                if (!digits || n < 0 || n > 3) throw err("optimize is 0, 1, 2 or 3");
                m.configs.back().optimize = n;
            } else if (mode == CONFIG && k == "debug") {
                m.configs.back().debug = oneFlag(v, "debug") ? 1 : 0;
            }             else throw err(string("unknown ") + (mode == CONFIG ? "config" : "if") + " key '" + k + "'");
        } else if (mode == PACKAGE) {
            PackageDecl& p = m.packages.back();
            if (k == "path") { if (v.size() != 1) throw err("path takes one value"); p.path = v[0]; }
            else if (k == "version") { if (v.size() != 1) throw err("version takes one value"); p.version = v[0]; }
            else if (k == "depend") { if (v.size() != 2) throw err("depend takes a package name and a version"); p.depends.push_back({v[0], v[1]}); }
            else throw err("unknown package key '" + k + "'");
        } else if (mode == DIST) {
            DistDecl& d = m.dist;
            if (k == "dir") { if (v.size() != 1) throw err("dir takes one value"); d.dir = v[0]; }
            else if (k == "launcher") { if (v.size() != 1) throw err("launcher takes one value"); d.launcher = v[0]; }
            else if (k == "dll") { if (v.size() != 1) throw err("dll takes one value"); d.dll = v[0]; }
            else if (k == "tool") { if (v.size() != 1) throw err("tool takes one value"); d.tools.push_back(v[0]); }
            else if (k == "version") { if (v.size() != 1) throw err("version takes one value"); d.version = v[0]; }
            else if (k == "textures") { if (v.size() != 1) throw err("textures takes one value"); d.textures = v[0]; }
            else if (k == "fonts") { if (v.size() != 1) throw err("fonts takes one value"); d.fonts = v[0]; }
            else if (k == "example") { if (v.size() != 1) throw err("example takes one value"); d.examples.push_back(v[0]); }
            else if (k == "readme") { if (v.size() != 1) throw err("readme takes one value"); d.readme = v[0]; }
            else throw err("unknown dist key '" + k + "'");
        } else {
            Target& x = m.targets.back();
            if (k == "sources") add(x.sources); else if (k == "include") add(x.includes);
            else if (k == "export_include") add(x.exportIncludes); else if (k == "define") add(x.defines);
            else if (k == "flags") add(x.flags); else if (k == "link_flags") add(x.linkFlags);
            else if (k == "libs") add(x.libs); else if (k == "deps") add(x.deps);
            else if (k == "needs") add(x.needs);
            else if (k == "runtime") add(x.runtime);
            else if (k == "assets") add(x.assets);
            else if (k == "pool") { if (v.size() != 1) throw err("pool takes one value"); x.pool = v[0]; }
            else if (k == "test") x.isTest = oneFlag(v, "test");
            else throw err("unknown target key '" + k + "'");
        }
    }
    if (mode != TOP) throw Fail(file + ": missing 'end' at end of file");
    for (auto& a : m.actions) {
        if (a.cmd.empty()) throw Fail(file + ":" + std::to_string(a.line) + ": action has no cmd");
        if (a.restat && a.output.empty() && a.outputs.empty()) throw Fail(file + ":" + std::to_string(a.line) + ": restat action has no output");
    }
    if (m.hasBuildfile && m.buildCmd.empty()) throw Fail(file + ":" + std::to_string(m.buildLine) + ": buildfile has no cmd");
    for (auto& t : m.targets) if (t.isTest && t.isLib) throw Fail(file + ":" + std::to_string(t.line) + ": test target '" + t.name + "' is a library");
    for (auto& p : m.packages) {
        if (p.path.empty()) throw Fail(file + ":" + std::to_string(p.line) + ": package '" + p.name + "' has no path");
        if (p.version.empty()) throw Fail(file + ":" + std::to_string(p.line) + ": package '" + p.name + "' has no version");
    }
    return m;
}

// ---------------------------------------------------------------- hashing + stat shortcut
struct FileSig { uint64_t hash = 0; long long size = 0, mtime = 0, at = 0; };
static string hex64(uint64_t h) { char b[17]; snprintf(b, sizeof b, "%016llx", (unsigned long long)h); return b; }
static uint64_t fnv(const char* p, size_t n, uint64_t h = 1469598103934665603ull) {
    for (size_t i = 0; i < n; ++i) { h ^= (unsigned char)p[i]; h *= 1099511628211ull; }
    return h;
}
static std::unordered_map<string, FileSig> gCache, gPrev; // this run / loaded from state
static std::atomic<int> gHashed{0}, gTrusted{0};
static bool gUseStat = true;
static long long clockNs() {
#ifdef _WIN32
    FILETIME ft; GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    return (long long)u.QuadPart * 100; // nanoseconds since 1601, same epoch as file stamps below
#else
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000000000ll + ts.tv_nsec;
#endif
}
static bool stampOf(const string& p, long long& size, long long& mtime) {
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(widePath(p).c_str(), GetFileExInfoStandard, &d)) return false;
    size = ((long long)d.nFileSizeHigh << 32) | d.nFileSizeLow;
    ULARGE_INTEGER u; u.LowPart = d.ftLastWriteTime.dwLowDateTime; u.HighPart = d.ftLastWriteTime.dwHighDateTime;
    mtime = (long long)u.QuadPart * 100;
    return true;
#else
    struct stat st; if (::stat(p.c_str(), &st) != 0) return false;
    size = (long long)st.st_size; mtime = (long long)st.st_mtim.tv_sec * 1000000000ll + st.st_mtim.tv_nsec;
    return true;
#endif
}
static bool hashContents(const string& p, uint64_t& h) {
    h = 1469598103934665603ull;
#ifdef _WIN32
    HANDLE f = CreateFileW(widePath(p).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    char buf[65536];
    for (;;) {
        DWORD n = 0;
        if (!ReadFile(f, buf, sizeof buf, &n, nullptr)) { CloseHandle(f); return false; }
        if (n == 0) break;
        h = fnv(buf, n, h);
    }
    CloseHandle(f);
    return true;
#else
    int fd = ::open(p.c_str(), O_RDONLY);
    if (fd < 0) return false;
    char buf[65536];
    for (;;) {
        ssize_t n = ::read(fd, buf, sizeof buf);
        if (n < 0) { if (errno == EINTR) continue; ::close(fd); return false; }
        if (n == 0) break;
        h = fnv(buf, (size_t)n, h);
    }
    ::close(fd);
    return true;
#endif
}
// Shortcut: same size and mtime as the record means the bytes did not change, so reuse the stored hash.
// A stamp with sub-millisecond digits cannot survive an ordinary later write, so it is trusted at once.
// A whole-second or whole-millisecond stamp might be shared by two writes (coarse clocks), so it is trusted
// only once it was already 2 s old when it was recorded. Forged timestamps with the same size are not detected.
static bool hashFile(string_view pv, FileSig& out) {
    string p(pv);
    FileSig prev; bool havePrev = false;
    { OK_LOCK(l, gHashMu); auto it = gCache.find(p); if (it != gCache.end()) { out = it->second; return true; }
      auto it2 = gPrev.find(p); if (it2 != gPrev.end()) { prev = it2->second; havePrev = true; } }
    FileSig s; if (!stampOf(p, s.size, s.mtime)) return false;
    long long nsec = s.mtime % 1000000000ll;
    bool fine = nsec % 1000000ll != 0;
    bool settled = prev.mtime + 2000000000ll < prev.at;
    if (gUseStat && havePrev && prev.size == s.size && prev.mtime == s.mtime && (fine || settled)) {
        s = prev; ++gTrusted;
    } else {
        uint64_t h = 0; if (!hashContents(p, h)) return false;
        s.hash = h; s.at = clockNs(); ++gHashed;
    }
    out = s; OK_LOCK(l, gHashMu); gCache[p] = s; return true;
}
static void forgetHash(const string& p) { OK_LOCK(l, gHashMu); gCache.erase(p); }
static string hashStr(const string& s) { return hex64(fnv(s.data(), s.size())); }
static string hashArgv(const vector<string>& a) { uint64_t h = 1469598103934665603ull; for (auto& s : a) { h = fnv(s.data(), s.size(), h); h = fnv("\0", 1, h); } return hex64(h); }

// ---------------------------------------------------------------- graph
struct Action {
    int id = 0; string kind, desc, output, depfile, dir, rawCmd, pool; vector<string> argv, inputs, outputs, waitFor;
    vector<int> deps, rdeps; string target; size_t rspHead = 0, rspThreshold = 0; string cmdHash; bool restat = false; int memory = 0;
};
struct Graph { vector<Action> acts; vector<std::pair<string, int>> targetFinal; };
static string gBuildDir = ".";
static string under(const string& rel) { return gBuildDir == "." ? rel : gBuildDir + "/" + rel; }
static string resolve(const string& dir, const string& p) {
    fs::path x(p); if (x.is_relative() && !dir.empty()) x = fs::path(dir) / x;
    return x.lexically_normal().generic_string();
}

static string quote(const string& s) { return s.find_first_of(" \"'$\\") == string::npos ? s : "'" + s + "'"; }
static string joined(const vector<string>& a) { string r; for (auto& s : a) { if (!r.empty()) r += ' '; r += quote(s); } return r; }

static string spellMsvc(const string& f, bool link) {
    if (f.size() >= 2 && f[0] == '-' && f[1] == 'D') return "/D" + f.substr(2);
    if (f.size() >= 2 && f[0] == '-' && f[1] == 'I') return "/I" + f.substr(2);
    if (f == "-g" || f == "-g1" || f == "-ggdb") return "/Zi";
    if (f == "-O0") return "/Od";
    if (f == "-O1") return "/O1";
    if (f == "-O2") return "/O2";
    if (f == "-O3") return "/Ox";
    if (link && f.size() >= 2 && f[0] == '-' && f[1] == 'l') return f.substr(2) + ".lib";
    if (link && f.size() >= 2 && f[0] == '-' && f[1] == 'L') return "/LIBPATH:" + f.substr(2);
    return f;
}
static bool msvcLinkerIsCl(const string& ld) {
    string b = ld; auto p = b.find_last_of("/\\"); if (p != string::npos) b = b.substr(p + 1);
    if (b.size() > 4 && b.compare(b.size() - 4, 4, ".exe") == 0) b.resize(b.size() - 4);
    return b == "cl";
}
static Graph plan(const Manifest& m, const vector<string>& want) {
    std::map<string, const Target*> byName;
    for (auto& t : m.targets) byName[t.name] = &t;
    for (auto& t : m.targets) {
        for (auto& d : t.deps) {
            if (!byName.count(d)) throw Fail("target '" + t.name + "' (line " + std::to_string(t.line) + ") depends on unknown target '" + d + "'");
            if (!byName[d]->isLib) throw Fail("target '" + t.name + "' depends on '" + d + "', which is not a lib");
        }
        if (t.sources.empty()) throw Fail("target '" + t.name + "' has no sources");
    }
    // cycle detection (colour DFS), reports the loop
    std::map<string, int> col; vector<string> stack;
    std::function<void(const string&)> visit = [&](const string& n) {
        col[n] = 1; stack.push_back(n);
        for (auto& d : byName[n]->deps) {
            if (col[d] == 1) {
                string loop; bool on = false;
                for (auto& s : stack) { if (s == d) on = true; if (on) loop += s + " -> "; }
                throw Fail("dependency cycle between targets: " + loop + d);
            }
            if (col[d] == 0) visit(d);
        }
        stack.pop_back(); col[n] = 2;
    };
    for (auto& t : m.targets) if (col[t.name] == 0) visit(t.name);

    Graph g; std::map<string, int> finalAct;
    auto outPath = [&](const Target& t) { return under(m.outdir + "/" + (t.isLib ? m.tc.libp + t.name + m.tc.libs : t.name + m.tc.exe)); };
    std::set<string> done; vector<string> order;
    std::function<void(const string&)> post = [&](const string& n) { if (!done.insert(n).second) return; for (auto& d : byName[n]->deps) post(d); order.push_back(n); };
    for (auto& t : m.targets) post(t.name);

    auto transitive = [&](const Target& t) { // dependents first (link order)
        vector<string> po; std::set<string> seen;
        std::function<void(const string&)> go = [&](const string& n) { if (!seen.insert(n).second) return; for (auto& d : byName[n]->deps) go(d); po.push_back(n); };
        for (auto& d : t.deps) go(d);
        std::reverse(po.begin(), po.end()); return po;
    };
    std::set<string> produced; // outputs of raw actions, so a generated source need not exist yet
    for (auto& r : m.actions) {
        if (!r.output.empty()) produced.insert(resolve(r.dir, r.output));
        for (auto& o : r.outputs) produced.insert(resolve(r.dir, o));
    }
    auto add = [&](Action a) { a.id = (int)g.acts.size(); g.acts.push_back(a); return a.id; };
    for (auto& n : order) {
        const Target& t = *byName[n]; vector<string> libs = transitive(t);
        vector<string> objs; vector<int> objActs; vector<string> needKeys;
        for (auto& nd : t.needs) needKeys.push_back(resolve("", nd));
        for (auto& s : t.sources) {
            string srcKey = resolve("", s);
            if (!produced.count(srcKey) && !fs::exists(s)) throw Fail("target '" + t.name + "': source '" + s + "' does not exist");
            bool isC = fs::path(s).extension() == ".c";
            string rel = fs::path(s).lexically_normal().generic_string();
            for (size_t p; (p = rel.find("..")) != string::npos;) rel.replace(p, 2, "__");
            Action a; a.kind = "CC"; a.target = t.name; a.desc = s;
            a.output = under(".okaybuild/obj/" + t.name + "/" + rel + m.tc.obj); a.depfile = a.output + ".d"; a.inputs = {srcKey}; a.outputs = {a.output};
            a.waitFor = needKeys; if (produced.count(srcKey)) a.waitFor.push_back(srcKey);
            if (m.tc.msvc) {
                a.argv.push_back(isC ? m.tc.cc : m.tc.cxx);
                a.argv.push_back("/nologo"); a.argv.push_back("/showIncludes");
                for (auto& f : m.tc.flags) a.argv.push_back(spellMsvc(f, false));
                for (auto& f : t.flags) a.argv.push_back(spellMsvc(f, false));
                for (auto& i : t.includes) a.argv.push_back("/I" + i);
                for (auto& i : t.exportIncludes) a.argv.push_back("/I" + i);
                for (auto& l : libs) for (auto& i : byName[l]->exportIncludes) a.argv.push_back("/I" + i);
                for (auto& d : t.defines) a.argv.push_back("/D" + d);
                a.argv.push_back("/c"); a.argv.push_back("/Fo" + a.output); a.argv.push_back(s);
                a.rspHead = 1; a.rspThreshold = m.tc.rspThreshold;
            } else {
                a.argv.push_back(isC ? m.tc.cc : m.tc.cxx);
                for (auto& f : m.tc.flags) a.argv.push_back(f);
                for (auto& f : t.flags) a.argv.push_back(f);
                for (auto& i : t.includes) a.argv.push_back("-I" + i);
                for (auto& i : t.exportIncludes) a.argv.push_back("-I" + i);
                for (auto& l : libs) for (auto& i : byName[l]->exportIncludes) a.argv.push_back("-I" + i);
                for (auto& d : t.defines) a.argv.push_back("-D" + d);
                for (auto& x : {string("-MMD"), string("-MF"), a.depfile, string("-MT"), a.output, string("-c"), s, string("-o"), a.output}) a.argv.push_back(x);
            }
            objs.push_back(a.output); objActs.push_back(add(a));
        }
        Action a; a.target = t.name; a.output = outPath(t); a.outputs = {a.output}; a.deps = objActs; a.inputs = objs; a.desc = a.output; a.rspThreshold = m.tc.rspThreshold; a.pool = t.pool; a.waitFor = needKeys;
        if (t.isLib) {
            a.kind = "AR";
            if (m.tc.msvc) { a.argv = {m.tc.ar, "/nologo", "/OUT:" + a.output}; a.rspHead = 3; }
            else { a.argv = {m.tc.ar, "rcs", a.output}; a.rspHead = 3; }
            for (auto& o : objs) a.argv.push_back(o);
        } else {
            a.kind = "LD";
            string linker = m.tc.ld.empty() ? m.tc.cxx : m.tc.ld;
            a.argv = {linker}; a.rspHead = 1;
            if (m.tc.msvc) a.argv.push_back("/nologo");
            for (auto& o : objs) a.argv.push_back(o);
            for (auto& f : m.tc.linkFlags) a.argv.push_back(m.tc.msvc ? spellMsvc(f, true) : f);
            for (auto& f : t.linkFlags) a.argv.push_back(m.tc.msvc ? spellMsvc(f, true) : f);
            vector<string> sys = t.libs;
            for (auto& l : libs) { a.argv.push_back(outPath(*byName[l])); a.inputs.push_back(outPath(*byName[l])); a.deps.push_back(finalAct[l]);
                                   for (auto& s : byName[l]->libs) sys.push_back(s); }
            for (auto& s : sys) a.argv.push_back(m.tc.msvc ? s + ".lib" : "-l" + s);
            if (m.tc.msvc && (msvcLinkerIsCl(linker) || linker == m.tc.cc || linker == m.tc.cxx)) { a.argv.push_back("/Fe" + a.output); }
            else if (m.tc.msvc) { a.argv.push_back("/OUT:" + a.output); }
            else { a.argv.push_back("-o"); a.argv.push_back(a.output); }
        }
        finalAct[n] = add(a); g.targetFinal.push_back({n, finalAct[n]});
    }
    // raw actions (imported commands and generators): `cd DIR && CMD` through sh.
    // Every declared output is an edge source. `after` names the spelling used in the manifest.
    std::map<string, int> byOut, byRawName;
    for (auto& r : m.actions) {
        Action a; a.kind = "RUN"; a.dir = r.dir; a.rawCmd = r.cmd; a.pool = r.pool; a.restat = r.restat; a.memory = r.memory;
        vector<string> outs;
        if (!r.output.empty()) outs.push_back(resolve(r.dir, r.output));
        for (auto& o : r.outputs) { string p = resolve(r.dir, o); if (std::find(outs.begin(), outs.end(), p) == outs.end()) outs.push_back(p); }
        a.outputs = outs; a.output = outs.empty() ? "" : outs[0];
        a.desc = a.output.empty() ? r.cmd.substr(0, 60) : a.output;
        a.depfile = r.depfile.empty() ? "" : resolve(r.dir, r.depfile);
        for (auto& i : r.inputs) a.inputs.push_back(resolve(r.dir, i));
        string full = (r.dir.empty() ? "" : "cd " + quote(r.dir) + " && ") + r.cmd;
        a.argv = {"sh", "-c", full};
        int id = add(a);
        if (!r.output.empty()) byRawName[r.output] = id;
        for (auto& o : outs) byRawName[o] = id;
    }
    for (auto& a : g.acts) for (auto& o : a.outputs) {
        auto it = byOut.find(o);
        if (it != byOut.end() && it->second != a.id) throw Fail("two actions produce '" + o + "'");
        byOut[o] = a.id; byRawName[o] = a.id;
    }
    for (auto& a : g.acts) {
        for (auto& in : a.inputs) { auto it = byOut.find(in); if (it != byOut.end() && it->second != a.id) a.deps.push_back(it->second); }
        for (auto& w : a.waitFor) {
            auto it = byOut.find(w);
            if (it == byOut.end()) throw Fail("'" + a.desc + "' waits for '" + w + "', which no action produces");
            if (it->second != a.id) a.deps.push_back(it->second);
        }
    }
    for (size_t i = 0; i < m.actions.size(); ++i) {
        Action& a = g.acts[g.acts.size() - m.actions.size() + i];
        for (auto& x : m.actions[i].after) { auto it = byRawName.find(x); if (it == byRawName.end()) throw Fail("action '" + a.desc + "': 'after' names unknown action '" + x + "'"); a.deps.push_back(it->second); }
    }
    // cycle check on the final graph (Kahn)
    { vector<int> indeg(g.acts.size()); vector<vector<int>> out(g.acts.size());
      for (auto& a : g.acts) { std::sort(a.deps.begin(), a.deps.end()); a.deps.erase(std::unique(a.deps.begin(), a.deps.end()), a.deps.end()); for (int d : a.deps) { out[d].push_back(a.id); ++indeg[a.id]; } }
      vector<int> q; for (auto& a : g.acts) if (!indeg[a.id]) q.push_back(a.id);
      size_t seen = 0; while (!q.empty()) { int x = q.back(); q.pop_back(); ++seen; for (int y : out[x]) if (--indeg[y] == 0) q.push_back(y); }
      if (seen != g.acts.size()) { string n; for (auto& a : g.acts) if (indeg[a.id]) { n = a.desc; break; } throw Fail("dependency cycle between actions (involves " + n + ")"); } }
    // keep only wanted targets/actions and what they need
    if (!want.empty()) {
        vector<char> keep(g.acts.size(), 0); std::function<void(int)> mark = [&](int i) { if (keep[i]) return; keep[i] = 1; for (int d : g.acts[i].deps) mark(d); };
        for (auto& w : want) {
            bool ok = false; for (auto& tf : g.targetFinal) if (tf.first == w) { mark(tf.second); ok = true; }
            if (byRawName.count(w)) { mark(byRawName[w]); ok = true; }
            if (!ok) throw Fail("unknown target or output '" + w + "'");
        }
        vector<int> remap(g.acts.size(), -1); Graph h;
        for (auto& a : g.acts) if (keep[a.id]) { remap[a.id] = (int)h.acts.size(); h.acts.push_back(a); }
        for (auto& a : h.acts) { a.id = remap[a.id]; for (int& d : a.deps) d = remap[d]; }
        for (auto& tf : g.targetFinal) if (keep[tf.second]) h.targetFinal.push_back({tf.first, remap[tf.second]});
        g = h;
    }
    for (auto& a : g.acts) { a.rdeps.clear(); a.cmdHash = a.rawCmd.empty() ? hashArgv(a.argv) : hashStr(a.dir + "\n" + a.rawCmd); }
    for (auto& a : g.acts) for (int d : a.deps) g.acts[d].rdeps.push_back(a.id);
    return g;
}

// ---------------------------------------------------------------- depfile parser (make syntax)
static vector<string> parseDepfile(const string& path) {
    std::ifstream f(path); if (!f) return {};
    std::stringstream ss; ss << f.rdbuf(); string s = ss.str(), cur; vector<string> words; bool sawColon = false;
    auto flush = [&] { if (!cur.empty()) { if (sawColon) words.push_back(cur); cur.clear(); } };
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '\\' && i + 1 < s.size()) {
            if (s[i + 1] == '\n') { ++i; flush(); continue; }
            if (s[i + 1] == '\r' && i + 2 < s.size() && s[i + 2] == '\n') { i += 2; flush(); continue; }
            if (s[i + 1] == ' ' || s[i + 1] == '#' || s[i + 1] == '\\') { cur += s[++i]; continue; }
        }
        if (c == '$' && i + 1 < s.size() && s[i + 1] == '$') { cur += '$'; ++i; continue; }
        if (c == ':' && !sawColon && (i + 1 >= s.size() || isspace((unsigned char)s[i + 1]))) { cur.clear(); sawColon = true; continue; }
        if (isspace((unsigned char)c)) { flush(); continue; }
        cur += c;
    }
    flush(); return words;
}

// ---------------------------------------------------------------- state
struct Rec { string cmd; vector<std::pair<string_view, FileSig>> inputs, produced; };
static std::deque<string> gPool;
static string_view poolStr(const char* s, size_t n) { gPool.emplace_back(s, n); return gPool.back(); }
static uint64_t parseHex64(const char* s, size_t n) {
    uint64_t h = 0;
    for (size_t i = 0; i < n && i < 16; ++i) {
        unsigned char c = (unsigned char)s[i];
        int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) break;
        h = (h << 4) | (uint64_t)d;
    }
    return h;
}
static void putU32(string& o, uint32_t v) { o.push_back(char(v & 255)); o.push_back(char((v >> 8) & 255)); o.push_back(char((v >> 16) & 255)); o.push_back(char((v >> 24) & 255)); }
static void putU64(string& o, uint64_t v) { for (int i = 0; i < 8; ++i) o.push_back(char((v >> (8 * i)) & 255)); }
static bool rdU32(const char*& p, const char* e, uint32_t& v) {
    if (e - p < 4) return false;
    v = (uint32_t)(unsigned char)p[0] | ((uint32_t)(unsigned char)p[1] << 8) | ((uint32_t)(unsigned char)p[2] << 16) | ((uint32_t)(unsigned char)p[3] << 24);
    p += 4; return true;
}
static bool rdU64(const char*& p, const char* e, uint64_t& v) {
    if (e - p < 8) return false;
    v = 0; for (int i = 0; i < 8; ++i) v |= (uint64_t)(unsigned char)p[i] << (8 * i);
    p += 8; return true;
}
static bool rdI64(const char*& p, const char* e, long long& v) { uint64_t u; if (!rdU64(p, e, u)) return false; v = (long long)u; return true; }
static std::map<string, Rec> gState;
static std::atomic<int> gCancel{0};
#ifdef _WIN32
static BOOL WINAPI onCtrl(DWORD) { gCancel.store(1); return TRUE; }
#else
static void onInt(int) { gCancel.store(1); }
#endif
static string statePath() { return under(".okaybuild/state"); }
static string journalPath() { return under(".okaybuild/state.journal"); }
static void ingestStateLine(const string& l, Rec*& cur) {
    const char* p = l.c_str(); const char* e = p + l.size(); const char* fld[6]; int n = 0; fld[n++] = p;
    for (const char* q = p; q < e && n < 6; ++q) if (*q == '\t') fld[n++] = q + 1;
    auto len = [&](int i) { return (size_t)((i + 1 < n ? fld[i + 1] - 1 : e) - fld[i]); };
    if (n == 3 && l[0] == 'O') cur = &(gState[string(fld[1], len(1))] = Rec{string(fld[2], len(2)), {}, {}});
    else if (n == 6 && (l[0] == 'I' || l[0] == 'P') && cur) {
        FileSig s; s.hash = parseHex64(fld[2], len(2)); s.size = atoll(fld[3]); s.mtime = atoll(fld[4]); s.at = atoll(fld[5]);
        string_view path = poolStr(fld[1], len(1));
        (l[0] == 'I' ? cur->inputs : cur->produced).push_back({path, s});
        if (l[0] == 'I') gPrev.emplace(string(path), s);
    }
}
static bool loadTextState(const char* data, size_t n) {
    if (n < 17 || memcmp(data, "okaybuild-state ", 17) != 0) return false;
    string all(data, n); std::istringstream in(all); string l; Rec* cur = nullptr;
    if (!std::getline(in, l) || (l != "okaybuild-state 3" && l != "okaybuild-state 4")) return false;
    while (std::getline(in, l)) ingestStateLine(l, cur);
    return true;
}
// OKB1: little-endian path table, then records of command hash text and file signatures.
// A file that is not OKB1 is tried as the older text format. A truncated OKB1 is ignored.
static bool loadBinaryState(const char* data, size_t n) {
    if (n < 16 || memcmp(data, "OKB1", 4) != 0) return false;
    const char* p = data + 4; const char* e = data + n;
    uint32_t ver = 0, pc = 0, rc = 0;
    if (!rdU32(p, e, ver) || ver != 1 || !rdU32(p, e, pc) || !rdU32(p, e, rc)) return true;
    if (pc > n || rc > n) return true;
    vector<string_view> paths; paths.reserve(pc);
    for (uint32_t i = 0; i < pc; ++i) {
        uint32_t len = 0; if (!rdU32(p, e, len) || p + len > e) return true;
        paths.push_back(poolStr(p, len)); p += len;
    }
    gPrev.reserve(pc);
    for (uint32_t i = 0; i < rc; ++i) {
        uint32_t outId = 0, cmdLen = 0, nIn = 0, nProd = 0;
        if (!rdU32(p, e, outId) || outId >= pc || !rdU32(p, e, cmdLen) || p + cmdLen > e) return true;
        string cmd(p, cmdLen); p += cmdLen;
        if (!rdU32(p, e, nIn) || !rdU32(p, e, nProd)) return true;
        Rec rec; rec.cmd = std::move(cmd);
        auto one = [&](vector<std::pair<string_view, FileSig>>& dst, bool input) {
            uint32_t id = 0; FileSig s;
            if (!rdU32(p, e, id) || id >= pc || !rdU64(p, e, s.hash) || !rdI64(p, e, s.size) || !rdI64(p, e, s.mtime) || !rdI64(p, e, s.at)) return false;
            dst.push_back({paths[id], s});
            if (input) gPrev.emplace(string(paths[id]), s);
            return true;
        };
        for (uint32_t k = 0; k < nIn; ++k) if (!one(rec.inputs, true)) return true;
        for (uint32_t k = 0; k < nProd; ++k) if (!one(rec.produced, false)) return true;
        gState[string(paths[outId])] = std::move(rec);
    }
    return true;
}
static bool mapWhole(const string& path, string& storage, const char*& data, size_t& n) {
    data = nullptr; n = 0;
#ifdef _WIN32
    HANDLE f = CreateFileW(widePath(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz; if (!GetFileSizeEx(f, &sz) || sz.QuadPart < 0) { CloseHandle(f); return false; }
    HANDLE map = CreateFileMappingW(f, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!map) {
        storage.resize((size_t)sz.QuadPart);
        DWORD got = 0; BOOL ok = sz.QuadPart == 0 || ReadFile(f, storage.data(), (DWORD)storage.size(), &got, nullptr);
        CloseHandle(f);
        if (!ok) return false;
        data = storage.data(); n = storage.size(); return true;
    }
    void* view = MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(map); CloseHandle(f);
    if (!view) return false;
    storage.assign((const char*)view, (size_t)sz.QuadPart);
    UnmapViewOfFile(view);
    data = storage.data(); n = storage.size(); return true;
#else
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    struct stat st; if (fstat(fd, &st) != 0 || st.st_size < 0) { ::close(fd); return false; }
    n = (size_t)st.st_size;
    if (n == 0) { ::close(fd); data = ""; return true; }
    void* view = mmap(nullptr, n, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (view == MAP_FAILED) return false;
    storage.assign((const char*)view, n);
    munmap(view, n);
    data = storage.data(); return true;
#endif
}
static void loadState() {
    gPrev.reserve(8192);
    const char* data = nullptr; size_t n = 0; string storage;
    if (mapWhole(statePath(), storage, data, n) && n > 0) {
        if (!loadBinaryState(data, n)) loadTextState(data, n);
    }
    std::ifstream j(journalPath(), std::ios::binary); string l; Rec* cur = nullptr;
    while (std::getline(j, l)) ingestStateLine(l, cur);
}
// A file whose bytes still match its record but whose size or mtime moved (an object restored from the cache, a touch)
// gets its new stamp written back, so the next run trusts size+mtime and does not read the file again.
static int refreshStamps() {
    int n = 0;
    for (auto& [o, r] : gState) for (auto& [p, sg] : r.inputs) {
        auto it = gCache.find(string(p));
        if (it != gCache.end() && it->second.hash == sg.hash && (it->second.mtime != sg.mtime || it->second.size != sg.size)) { sg = it->second; ++n; }
    }
    return n;
}
static void saveState() {
    fs::create_directories(under(".okaybuild"));
    std::unordered_map<string, uint32_t> id;
    vector<string_view> paths;
    auto add = [&](string_view s) -> uint32_t {
        string key(s); auto it = id.find(key);
        if (it != id.end()) return it->second;
        uint32_t n = (uint32_t)paths.size(); paths.push_back(s); id.emplace(std::move(key), n); return n;
    };
    for (auto& [o, r] : gState) { add(o); for (auto& [p, s] : r.inputs) add(p); for (auto& [p, s] : r.produced) add(p); }
    string out; out.append("OKB1", 4);
    putU32(out, 1); putU32(out, (uint32_t)paths.size()); putU32(out, (uint32_t)gState.size());
    for (auto sv : paths) { putU32(out, (uint32_t)sv.size()); out.append(sv.data(), sv.size()); }
    for (auto& [o, r] : gState) {
        putU32(out, add(o)); putU32(out, (uint32_t)r.cmd.size()); out.append(r.cmd);
        putU32(out, (uint32_t)r.inputs.size()); putU32(out, (uint32_t)r.produced.size());
        auto dump = [&](const vector<std::pair<string_view, FileSig>>& v) {
            for (auto& [p, s] : v) { putU32(out, add(p)); putU64(out, s.hash); putU64(out, (uint64_t)s.size); putU64(out, (uint64_t)s.mtime); putU64(out, (uint64_t)s.at); }
        };
        dump(r.inputs); dump(r.produced);
    }
    string tmp = statePath() + ".tmp";
    { std::ofstream f(tmp, std::ios::binary | std::ios::trunc); f << out; }
    fs::rename(tmp, statePath());
    std::error_code ec; fs::remove(journalPath(), ec);
}
static void appendJournal(const string& output) {
    OK_LOCK(l, gStateMu);
    auto it = gState.find(output); if (it == gState.end()) return;
    fs::create_directories(under(".okaybuild"));
    std::ofstream f(journalPath(), std::ios::app);
    f << "O\t" << output << "\t" << it->second.cmd << "\n";
    for (auto& [p, s] : it->second.inputs) f << "I\t" << p << "\t" << hex64(s.hash) << "\t" << s.size << "\t" << s.mtime << "\t" << s.at << "\n";
    for (auto& [p, s] : it->second.produced) f << "P\t" << p << "\t" << hex64(s.hash) << "\t" << s.size << "\t" << s.mtime << "\t" << s.at << "\n";
}

static bool fileMissing(const string& p) {
#ifdef _WIN32
    return GetFileAttributesW(widePath(p).c_str()) == INVALID_FILE_ATTRIBUTES;
#else
    struct stat st; return ::stat(p.c_str(), &st) != 0;
#endif
}
// restat: the command rewrote an output with the same bytes. Put the previous mtime back so dependents,
// and anything else looking at mtimes, see no change. Content hashes already skip the rebuild.
static void restoreMtime(const string& p, long long mtime) {
#ifdef _WIN32
    HANDLE h = CreateFileW(widePath(p).c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    ULARGE_INTEGER u; u.QuadPart = (unsigned long long)(mtime / 100);
    FILETIME ft; ft.dwLowDateTime = u.LowPart; ft.dwHighDateTime = u.HighPart;
    SetFileTime(h, nullptr, nullptr, &ft);
    CloseHandle(h);
#else
    struct timespec ts[2];
    ts[0].tv_sec = 0; ts[0].tv_nsec = UTIME_OMIT;
    ts[1].tv_sec = (time_t)(mtime / 1000000000ll); ts[1].tv_nsec = (long)(mtime % 1000000000ll);
    utimensat(AT_FDCWD, p.c_str(), ts, 0);
#endif
}
static void noteOutputs(const Action& a, Rec& rec, const vector<std::pair<string_view, FileSig>>& oldProduced, string& out, bool& ok) {
    for (auto& o : a.outputs) {
        forgetHash(o); FileSig now;
        if (!hashFile(o, now)) { ok = false; out += "okay-build: output missing after the command: " + o + "\n"; return; }
        bool same = false; FileSig prev{};
        for (auto& [p, s] : oldProduced) if (p == o) { prev = s; same = s.hash == now.hash; break; }
        if (a.restat && same) { restoreMtime(o, prev.mtime); now = prev; forgetHash(o); }
        rec.produced.push_back({poolStr(o.data(), o.size()), now});
    }
}
static bool dirty(const Action& a, string& why) {
    if (a.outputs.empty()) { why = "no declared output (always runs)"; return true; }
    for (auto& o : a.outputs) if (fileMissing(o)) { why = "output " + o + " is missing"; return true; }
    const Rec* rp; { OK_LOCK(l, gStateMu); auto it = gState.find(a.output); if (it == gState.end()) { why = "no record in state file (never built by okay-build)"; return true; } rp = &it->second; } // map nodes are stable
    const Rec& r = *rp;
    if (r.cmd != a.cmdHash) { why = "command line changed"; return true; }
    for (auto& i : a.inputs) {
        bool known = false; for (auto& [p, h] : r.inputs) if (p == i) { known = true; break; }
        if (!known) { why = "new input " + i; return true; }
    }
    for (auto& [p, h] : r.inputs) {
        FileSig now; if (!hashFile(p, now)) { why = "input " + string(p) + " is gone"; return true; }
        if (now.hash != h.hash) { why = "input " + string(p) + " changed"; return true; }
    }
    return false;
}

// ---------------------------------------------------------------- running commands
static long long nowNs();
#ifdef _WIN32
struct Live {
    HANDLE proc = nullptr;
    HANDLE pipe = nullptr;
    int id = -1;
    string out, cacheKey, cacheTag;
    bool stream = false;
    long long started = 0, began = 0;
    int jobKind = 0;
};
static string winQuote(const string& s) {
    bool need = s.empty();
    for (char c : s) if (c == ' ' || c == '\t' || c == '"') need = true;
    if (!need) return s;
    string r = "\"";
    int bs = 0;
    for (char c : s) {
        if (c == '\\') { ++bs; r += c; continue; }
        if (c == '"') { for (int i = 0; i < bs; ++i) r += '\\'; bs = 0; r += '\\'; r += '"'; continue; }
        bs = 0; r += c;
    }
    for (int i = 0; i < bs; ++i) r += '\\';
    r += '"';
    return r;
}
static string winJoin(const vector<string>& argv) {
    string r; for (auto& s : argv) { if (!r.empty()) r += ' '; r += winQuote(s); } return r;
}
static void drainLive(Live& L) {
    if (L.stream || !L.pipe) return;
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(L.pipe, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) break;
        char buf[4096]; DWORD n = 0; DWORD chunk = avail > sizeof buf ? (DWORD)sizeof buf : avail;
        if (!ReadFile(L.pipe, buf, chunk, &n, nullptr) || n == 0) break;
        L.out.append(buf, n);
    }
}
static bool spawnLive(const string& cmdline, const string& dir, bool stream, Live& L, string& err) {
    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof sa; sa.bInheritHandle = TRUE;
    HANDLE rd = nullptr, wr = nullptr;
    if (!stream) {
        if (!CreatePipe(&rd, &wr, &sa, 1 << 20)) { err = "okay-build: CreatePipe failed\n"; return false; }
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    }
    STARTUPINFOW si{}; si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = stream ? GetStdHandle(STD_OUTPUT_HANDLE) : wr;
    si.hStdError = stream ? GetStdHandle(STD_ERROR_HANDLE) : wr;
    std::wstring wcmd = widen(cmdline);
    std::vector<wchar_t> buf(wcmd.begin(), wcmd.end()); buf.push_back(0);
    string dirNative = dir;
    for (char& c : dirNative) if (c == '/') c = '\\';
    if (!dirNative.empty() && dirNative[0] == '\\' && !(dirNative.size() >= 2 && dirNative[1] == ':')) {
        string z = "Z:" + dirNative;
        if (GetFileAttributesW(widen(z).c_str()) != INVALID_FILE_ATTRIBUTES) dirNative = z;
    }
    std::wstring wdir = widen(dirNative);
    PROCESS_INFORMATION pi{};
    DWORD flags = stream ? 0 : CREATE_NO_WINDOW;
    BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, flags, nullptr, wdir.empty() ? nullptr : wdir.c_str(), &si, &pi);
    if (wr) CloseHandle(wr);
    if (!ok) {
        if (rd) CloseHandle(rd);
        err = "okay-build: CreateProcessW failed (" + std::to_string(GetLastError()) + ")\n";
        return false;
    }
    CloseHandle(pi.hThread);
    L.proc = pi.hProcess; L.pipe = rd; L.stream = stream; L.started = nowNs();
    return true;
}
static int runCmd(const vector<string>& argv, string& out, bool stream = false) {
    Live L; string err;
    if (!spawnLive(winJoin(argv), "", stream, L, err)) { out += err; return 127; }
    for (;;) {
        drainLive(L);
        if (gCancel.load()) { TerminateProcess(L.proc, 130); WaitForSingleObject(L.proc, 1000); break; }
        if (WaitForSingleObject(L.proc, 20) == WAIT_OBJECT_0) break;
    }
    drainLive(L);
    DWORD code = 1; GetExitCodeProcess(L.proc, &code);
    CloseHandle(L.proc); if (L.pipe) CloseHandle(L.pipe);
    out += L.out;
    return (int)code;
}
static int captureStdout(const vector<string>& argv, string& out, string& err) {
    SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof sa; sa.bInheritHandle = TRUE;
    HANDLE orr = nullptr, owr = nullptr, errr = nullptr, ewr = nullptr;
    if (!CreatePipe(&orr, &owr, &sa, 1 << 20) || !CreatePipe(&errr, &ewr, &sa, 1 << 20)) return 127;
    SetHandleInformation(orr, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(errr, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{}; si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE); si.hStdOutput = owr; si.hStdError = ewr;
    std::wstring wcmd = widen(winJoin(argv));
    std::vector<wchar_t> buf(wcmd.begin(), wcmd.end()); buf.push_back(0);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(owr); CloseHandle(ewr);
    if (!ok) { CloseHandle(orr); CloseHandle(errr); return 127; }
    CloseHandle(pi.hThread);
    auto drain = [&](HANDLE h, string& dest) {
        for (;;) {
            DWORD avail = 0;
            if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr) || avail == 0) break;
            char b[4096]; DWORD n = 0; DWORD chunk = avail > sizeof b ? (DWORD)sizeof b : avail;
            if (!ReadFile(h, b, chunk, &n, nullptr) || n == 0) break;
            dest.append(b, n);
        }
    };
    for (;;) {
        drain(orr, out); drain(errr, err);
        if (gCancel.load()) { TerminateProcess(pi.hProcess, 130); WaitForSingleObject(pi.hProcess, 1000); CloseHandle(pi.hProcess); CloseHandle(orr); CloseHandle(errr); return 130; }
        if (WaitForSingleObject(pi.hProcess, 20) == WAIT_OBJECT_0) break;
    }
    drain(orr, out); drain(errr, err);
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(orr); CloseHandle(errr);
    return (int)code;
}
#else
static int waitChild(pid_t pid) {
    for (;;) {
        int st = 0; pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) return WIFEXITED(st) ? WEXITSTATUS(st) : 128;
        if (r < 0 && errno != EINTR) return 128;
        if (gCancel.load()) { kill(-pid, SIGTERM); kill(pid, SIGTERM); waitpid(pid, &st, 0); return 130; }
        struct timespec ts; ts.tv_sec = 0; ts.tv_nsec = 20000000; nanosleep(&ts, nullptr);
    }
}
static int runCmd(const vector<string>& argv, string& out, bool stream = false, long long timeoutNs = 0) {
    auto child = [&]() {
        vector<char*> av; for (auto& s : argv) av.push_back(const_cast<char*>(s.c_str())); av.push_back(nullptr);
        execvp(av[0], av.data()); dprintf(2, "okay-build: cannot run '%s': %s\n", av[0] ? av[0] : "(none)", strerror(errno)); _exit(127);
    };
    if (stream) { // console pool: the command owns the terminal, one at a time
        pid_t pid = fork(); if (pid < 0) return 127;
        if (pid == 0) { setpgid(0, 0); child(); }
        setpgid(pid, pid); return waitChild(pid);
    }
    int fd[2]; if (pipe(fd)) return 127;
    pid_t pid = fork();
    if (pid < 0) { close(fd[0]); close(fd[1]); return 127; }
    if (pid == 0) { setpgid(0, 0); dup2(fd[1], 1); dup2(fd[1], 2); close(fd[0]); close(fd[1]); child(); }
    setpgid(pid, pid);
    close(fd[1]); fcntl(fd[0], F_SETFL, O_NONBLOCK);
    long long startedAt = timeoutNs ? nowNs() : 0;
    for (;;) {
        pollfd pf{}; pf.fd = fd[0]; pf.events = POLLIN; poll(&pf, 1, 20);
        for (;;) { char b[4096]; ssize_t n = read(fd[0], b, sizeof b); if (n > 0) out.append(b, (size_t)n); else break; }
        int st = 0; pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) { close(fd[0]); return WIFEXITED(st) ? WEXITSTATUS(st) : 128; }
        if (r < 0 && errno != EINTR) { close(fd[0]); return 128; }
        if (gCancel.load()) { kill(-pid, SIGTERM); kill(pid, SIGTERM); while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {} close(fd[0]); return 130; }
        if (timeoutNs && nowNs() - startedAt > timeoutNs) {
            kill(-pid, SIGKILL); kill(pid, SIGKILL); while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
            for (;;) { char b[4096]; ssize_t n = read(fd[0], b, sizeof b); if (n > 0) out.append(b, (size_t)n); else break; }
            close(fd[0]); return 124;
        }
    }
}
static int captureStdout(const vector<string>& argv, string& out, string& err) {
    int op[2], ep[2];
    if (pipe(op) || pipe(ep)) return 127;
    pid_t pid = fork();
    if (pid < 0) { close(op[0]); close(op[1]); close(ep[0]); close(ep[1]); return 127; }
    if (pid == 0) {
        dup2(op[1], 1); dup2(ep[1], 2);
        close(op[0]); close(op[1]); close(ep[0]); close(ep[1]);
        vector<char*> av; for (auto& s : argv) av.push_back(const_cast<char*>(s.c_str())); av.push_back(nullptr);
        execvp(av[0], av.data()); _exit(127);
    }
    close(op[1]); close(ep[1]);
    fcntl(op[0], F_SETFL, O_NONBLOCK); fcntl(ep[0], F_SETFL, O_NONBLOCK);
    int openPipes = 2;
    while (openPipes > 0) {
        pollfd pf[2]; int nfd = 0, map[2];
        if (op[0] >= 0) { pf[nfd].fd = op[0]; pf[nfd].events = POLLIN; map[nfd] = 0; ++nfd; }
        if (ep[0] >= 0) { pf[nfd].fd = ep[0]; pf[nfd].events = POLLIN; map[nfd] = 1; ++nfd; }
        if (poll(pf, (nfds_t)nfd, -1) < 0) { if (errno == EINTR) continue; break; }
        for (int i = 0; i < nfd; ++i) {
            if (!(pf[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            string& dest = map[i] == 0 ? out : err;
            char b[4096]; ssize_t k = read(pf[i].fd, b, sizeof b);
            if (k > 0) dest.append(b, (size_t)k);
            else { close(pf[i].fd); if (map[i] == 0) op[0] = -1; else ep[0] = -1; --openPipes; }
        }
    }
    return waitChild(pid);
}
#endif

static std::atomic<long long> gNsDirty{0}, gNsSpawn{0}, gNsPost{0}, gNsPrint{0}, gNsPre{0};
static std::atomic<int> gCacheHit{0}, gCacheMiss{0};
static bool gUseCache = true;
static bool gRspUtf16 = false;
static void writeArgsFile(const string& path, const vector<string>& args, size_t head) {
    fs::create_directories(fs::path(path).parent_path());
    std::ofstream r(path, std::ios::binary | std::ios::trunc);
    if (gRspUtf16) { r.put((char)0xFF); r.put((char)0xFE); }
    for (size_t i = head; i < args.size(); ++i) {
        if (!gRspUtf16) {
            for (char c : args[i]) { if (c == '\\' || c == '"' || c == ' ') r << '\\'; r << c; }
            r << '\n';
        } else {
            string line = args[i] + "\r\n";
            for (unsigned char c : line) { r.put((char)c); r.put('\0'); }
        }
    }
}
static uint64_t gCacheMax = 0;
static int gMemLimit = 0;
static bool gWantJobserver = false, gJobOn = false;
static int gJsR = -1, gJsW = -1;
static std::atomic<int> gImplicit{1};
#ifdef _WIN32
static HANDLE gJsSem = nullptr;
#endif
struct JsToken { int kind = 0; }; // 0 not in use, 1 implicit slot, 2 a pipe or semaphore token, -1 cancelled, -2 none free
static JsToken tryTakeJob() {
    JsToken t;
    if (!gJobOn) return t;
    if (gCancel.load()) { t.kind = -1; return t; }
    int prev = gImplicit.fetch_sub(1);
    if (prev > 0) { t.kind = 1; return t; }
    gImplicit.fetch_add(1);
#ifdef _WIN32
    if (gJsSem && WaitForSingleObject(gJsSem, 0) == WAIT_OBJECT_0) { t.kind = 2; return t; }
#else
    if (gJsR >= 0) {
        pollfd pf{}; pf.fd = gJsR; pf.events = POLLIN;
        if (poll(&pf, 1, 0) > 0) {
            char b = 0; ssize_t n = read(gJsR, &b, 1);
            if (n == 1) { t.kind = 2; return t; }
        }
    }
#endif
    t.kind = -2; return t;
}
static void giveJob(JsToken t) {
    if (t.kind == 1) gImplicit.fetch_add(1);
    else if (t.kind == 2) {
#ifdef _WIN32
        if (gJsSem) ReleaseSemaphore(gJsSem, 1, nullptr);
#else
        char b = '+';
        while (gJsW >= 0 && write(gJsW, &b, 1) < 0 && errno == EINTR) {}
#endif
    }
}
static bool parseJobAuth(const string& spec) {
    if (spec.rfind("fifo:", 0) == 0) {
#ifdef _WIN32
        return false;
#else
        int fd = ::open(spec.substr(5).c_str(), O_RDWR);
        if (fd < 0) return false;
        gJsR = gJsW = fd;
        return true;
#endif
    }
    if (spec.rfind("sem:", 0) == 0) {
#ifdef _WIN32
        gJsSem = OpenSemaphoreW(SYNCHRONIZE | SEMAPHORE_MODIFY_STATE, FALSE, widen(spec.substr(4)).c_str());
        return gJsSem != nullptr;
#else
        return false;
#endif
    }
    auto c = spec.find(','); if (c == string::npos || c == 0) return false;
    gJsR = atoi(spec.c_str()); gJsW = atoi(spec.c_str() + c + 1);
    return gJsR >= 0 && gJsW >= 0;
}
static void adoptJobserver() {
    if (gJobOn) return;
    const char* own = getenv("OKAY_JOBSERVER");
    if (own && parseJobAuth(own)) { gJobOn = true; return; }
    const char* mf = getenv("MAKEFLAGS"); if (!mf) return;
    string s = mf;
    auto p = s.find("--jobserver-auth="); if (p == string::npos) p = s.find("--jobserver-fds=");
    if (p == string::npos) return;
    auto eq = s.find('=', p); if (eq == string::npos) return;
    string spec = s.substr(eq + 1);
    auto sp = spec.find_first_of(" \t"); if (sp != string::npos) spec = spec.substr(0, sp);
    if (parseJobAuth(spec)) gJobOn = true;
}
static void ensureJobserver(int jobs) {
    if (gJobOn || !gWantJobserver) return;
    if (jobs < 1) jobs = 1;
#ifdef _WIN32
    string name = "Local\\okaybuild-" + std::to_string(GetCurrentProcessId());
    gJsSem = CreateSemaphoreW(nullptr, jobs > 1 ? jobs - 1 : 0, jobs, widen(name).c_str());
    if (!gJsSem) return;
    string spec = "sem:" + name;
    SetEnvironmentVariableA("OKAY_JOBSERVER", spec.c_str());
#else
    int fd[2]; if (pipe(fd) != 0) return;
    for (int i = 0; i + 1 < jobs; ++i) { char b = '+'; if (write(fd[1], &b, 1) != 1) break; }
    gJsR = fd[0]; gJsW = fd[1];
    string spec = std::to_string(gJsR) + "," + std::to_string(gJsW);
    setenv("OKAY_JOBSERVER", spec.c_str(), 1);
    const char* old = getenv("MAKEFLAGS");
    string flags = old ? string(old) : string();
    flags += " --jobserver-auth=" + spec;
    setenv("MAKEFLAGS", flags.c_str(), 1);
#endif
    gJobOn = true;
}
static void rejectOversized(const Graph& g) {
    if (gMemLimit <= 0) return;
    for (auto& a : g.acts) if (a.memory > gMemLimit)
        throw Fail("step '" + a.desc + "' needs " + std::to_string(a.memory) + " memory but the limit is " + std::to_string(gMemLimit));
}
static long long nowNs() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
struct Opts { int jobs = 0; bool dry = false, explain = false, verbose = false, profile = false, keep = false; string file = "build.okay"; };

// Compiler cache. The key is FNV-1a of the preprocessed source, the command line, and the compiler
// file's bytes. FNV-1a is not collision-safe: two different inputs can share a key. This is a speed
// cache, not a security check. A miss stores the object and its depfile. A hit copies them back.
// A changed source, command, or compiler file is a different key, so the old entry is left unused.
static string resolveTool(const string& bin) {
    if (bin.find('/') != string::npos || bin.find('\\') != string::npos) return bin;
    const char* env = getenv("PATH"); if (!env) return bin;
    string cur; char sep =
#ifdef _WIN32
        ';';
#else
        ':';
#endif
    auto found = [&](const string& dir) -> string {
        string p = dir; if (!p.empty() && p.back() != '/' && p.back() != '\\') p += '/'; p += bin;
#ifdef _WIN32
        std::error_code ec; return fs::is_regular_file(p, ec) ? p : string();
#else
        return access(p.c_str(), X_OK) == 0 ? p : string();
#endif
    };
    for (char c : string(env)) {
        if (c == sep) { if (!cur.empty()) { string p = found(cur); if (!p.empty()) return p; } cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) { string p = found(cur); if (!p.empty()) return p; }
    return bin;
}
static string compilerIdentity(const string& bin) {
    string path = resolveTool(bin); FileSig sig;
    if (!hashFile(path, sig)) return "missing\n" + bin;
    return path + "\n" + hex64(sig.hash);
}
static vector<string> preprocessArgv(const Action& a) {
    vector<string> p; if (a.argv.empty()) return p;
    p.push_back(a.argv[0]); p.push_back("-E");
    for (size_t i = 1; i < a.argv.size(); ++i) {
        const string& s = a.argv[i];
        if (s == "-MMD") continue;
        if (s == "-c") continue;
        if (s == "-MF" || s == "-MT" || s == "-o") { if (i + 1 < a.argv.size()) ++i; continue; }
        p.push_back(s);
    }
    return p;
}
static string gCacheDir; // --cache-dir or OKAY_CACHE_DIR: one cache shared by many build trees
static string cacheRoot() { return gCacheDir.empty() ? under(".okaybuild/cache") : gCacheDir; }
static string cacheDirOf(const string& key) { return cacheRoot() + "/" + key; }
static long long fileSizeOf(const string& p) { std::error_code ec; auto n = fs::file_size(p, ec); return ec ? -1 : (long long)n; }
// The key must not depend on where the object goes: leave out the values of -o, -MF, -MT and the cl /Fo form.
static string hashArgvForCache(const vector<string>& a) {
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < a.size(); ++i) {
        const string& s = a[i];
        if ((s == "-o" || s == "-MF" || s == "-MT") && i + 1 < a.size()) { ++i; continue; }
        if (s.rfind("/Fo", 0) == 0 || s.rfind("/Fd", 0) == 0) continue;
        h = fnv(s.data(), s.size(), h); h = fnv("\0", 1, h);
    }
    return hex64(h);
}
// An entry is usable only when meta names this key and both stored files still have the recorded size.
// A truncated or half-written entry is removed and counts as a miss.
static bool cacheEntryOk(const string& key, bool needDep) {
    string dir = cacheDirOf(key);
    std::ifstream in(dir + "/meta"); string l1, l2, l3, l4;
    bool ok = in && std::getline(in, l1) && std::getline(in, l2) && std::getline(in, l3) && std::getline(in, l4);
    if (ok) ok = l1 == "okaybuild-cache 2" && l2 == key && fileSizeOf(dir + "/obj") == atoll(l3.c_str());
    if (ok && needDep) ok = fileSizeOf(dir + "/dep") == atoll(l4.c_str()) && atoll(l4.c_str()) >= 0;
    if (!ok && fs::exists(dir)) { std::error_code ec; fs::remove_all(dir, ec); }
    return ok;
}
static void rememberCache(const string&);
// 1 hit, 0 miss, -1 could not preprocess.
static int lookupCache(const Action& a, string& key, string& errOut) {
    vector<string> pp = preprocessArgv(a);
    string out, err; long long tPre = nowNs(); int rc = captureStdout(pp, out, err); gNsPre += nowNs() - tPre;
    if (rc != 0) { errOut += err.empty() ? out : err; if (errOut.empty() || errOut.back() != '\n') errOut += '\n'; errOut += "okay-build: preprocess failed\n"; return -1; }
    string id = compilerIdentity(a.argv[0]); string cmd = hashArgvForCache(a.argv);
    uint64_t h = fnv(out.data(), out.size()); h = fnv(cmd.data(), cmd.size(), h); h = fnv(id.data(), id.size(), h);
    key = hex64(h);
    string dir = cacheDirOf(key);
    if (!cacheEntryOk(key, !a.depfile.empty())) return 0;
    std::error_code ec;
    fs::copy_file(dir + "/obj", a.output, fs::copy_options::overwrite_existing, ec);
    if (ec) { errOut += "okay-build: cache copy failed: " + ec.message() + "\n"; return -1; }
    if (!a.depfile.empty()) { fs::copy_file(dir + "/dep", a.depfile, fs::copy_options::overwrite_existing, ec); if (ec) { errOut += "okay-build: cache depfile copy failed\n"; return -1; } }
    rememberCache(key);
    return 1;
}
static void rememberCache(const string&);
static int ownPid() {
#ifdef _WIN32
    return (int)GetCurrentProcessId();
#else
    return (int)getpid();
#endif
}
static void storeCache(const Action& a, const string& key) {
    if (key.empty() || a.output.empty() || !fs::is_regular_file(a.output)) return;
    // Build the entry beside the final one and rename it in, so a killed or racing build never leaves a half entry that looks whole.
    static std::atomic<int> seq{0};
    string dir = cacheDirOf(key), tmp = dir + ".tmp" + std::to_string(ownPid()) + "_" + std::to_string(seq++);
    std::error_code ec; fs::remove_all(tmp, ec); fs::create_directories(tmp, ec);
    fs::copy_file(a.output, tmp + "/obj", fs::copy_options::overwrite_existing, ec);
    if (ec) { fs::remove_all(tmp, ec); return; }
    long long depSize = -1;
    if (!a.depfile.empty() && fs::is_regular_file(a.depfile)) { fs::copy_file(a.depfile, tmp + "/dep", fs::copy_options::overwrite_existing, ec); if (!ec) depSize = fileSizeOf(tmp + "/dep"); }
    { std::ofstream m(tmp + "/meta", std::ios::trunc); m << "okaybuild-cache 2\n" << key << "\n" << fileSizeOf(tmp + "/obj") << "\n" << depSize << "\n"; }
    fs::remove_all(dir, ec);
    fs::rename(tmp, dir, ec);
    if (ec) { std::error_code e2; fs::remove_all(tmp, e2); }
    rememberCache(key);
}
struct CacheRow { string key; uint64_t bytes = 0, used = 0; };
static uint64_t gCacheTick = 1;
static uint64_t cacheDirBytes(const string& dir) {
    uint64_t n = 0; std::error_code ec;
    if (!fs::exists(dir, ec)) return 0;
    for (auto& e : fs::directory_iterator(dir, ec)) if (!ec && e.is_regular_file()) n += (uint64_t)e.file_size();
    return n;
}
static string cacheIndexPath() { return cacheRoot() + "/index"; }
static vector<CacheRow> loadCacheIndex() {
    vector<CacheRow> rows; std::ifstream in(cacheIndexPath()); string l;
    if (!std::getline(in, l) || l != "okaybuild-cache-index 1") return rows;
    while (std::getline(in, l)) {
        auto a = l.find('\t'); auto b = a == string::npos ? string::npos : l.find('\t', a + 1);
        if (a == string::npos || b == string::npos) continue;
        CacheRow r; r.key = l.substr(0, a); r.bytes = strtoull(l.c_str() + a + 1, nullptr, 10); r.used = strtoull(l.c_str() + b + 1, nullptr, 10);
        if (r.used >= gCacheTick) gCacheTick = r.used + 1;
        rows.push_back(r);
    }
    return rows;
}
static void saveCacheIndex(const vector<CacheRow>& rows) {
    fs::create_directories(cacheRoot());
    string tmp = cacheIndexPath() + ".tmp" + std::to_string(ownPid());
    { std::ofstream f(tmp, std::ios::trunc); f << "okaybuild-cache-index 1\n";
      f << "limit\t" << gCacheMax << "\n";
      for (auto& r : rows) f << r.key << '\t' << r.bytes << '\t' << r.used << '\n'; }
    fs::rename(tmp, cacheIndexPath());
}
static void evictCache(vector<CacheRow>& rows) {
    if (gCacheMax == 0) return;
    auto total = [&]() { uint64_t s = 0; for (auto& r : rows) s += r.bytes; return s; };
    while (rows.size() > 1 && total() > gCacheMax) {
        size_t oldest = 0;
        for (size_t i = 1; i < rows.size(); ++i) if (rows[i].used < rows[oldest].used) oldest = i;
        std::error_code ec; fs::remove_all(cacheDirOf(rows[oldest].key), ec);
        rows.erase(rows.begin() + oldest);
    }
}
static void rememberCache(const string& key) {
    if (key.empty()) return;
    OK_LOCK(l, gStateMu);
    auto rows = loadCacheIndex();
    uint64_t bytes = cacheDirBytes(cacheDirOf(key));
    bool found = false;
    for (auto& r : rows) if (r.key == key) { r.bytes = bytes; r.used = gCacheTick++; found = true; break; }
    if (!found) rows.push_back(CacheRow{key, bytes, gCacheTick++});
    evictCache(rows);
    saveCacheIndex(rows);
}
static int cmdCache(const string& sub) {
    if (sub == "clean") {
        std::error_code ec; fs::remove_all(cacheRoot(), ec);
        printf("okay-build cache: removed\n"); return 0;
    }
    if (sub != "stats") throw Fail("usage: okay-build cache stats|clean");
    int entries = 0; uint64_t bytes = 0; std::error_code ec;
    string root = cacheRoot();
    if (fs::exists(root, ec)) for (auto& e : fs::directory_iterator(root, ec)) if (!ec && e.is_directory() && e.path().filename().string().find(".tmp") == string::npos) { ++entries; bytes += cacheDirBytes(e.path().generic_string()); }
    uint64_t limit = gCacheMax;
    if (limit == 0) {
        std::ifstream in(cacheIndexPath()); string l;
        while (std::getline(in, l)) if (l.compare(0, 6, "limit\t") == 0) { limit = strtoull(l.c_str() + 6, nullptr, 10); break; }
    }
    printf("entries: %d\nbytes: %llu\nlimit: ", entries, (unsigned long long)bytes);
    if (limit == 0) printf("none\n"); else printf("%llu\n", (unsigned long long)limit);
    return 0;
}
static bool hasShowIncludes(const Action& a) {
    for (auto& s : a.argv) if (s == "/showIncludes") return true;
    return false;
}
// cl.exe prints "Note: including file: <path>" (deeper includes have more spaces). We turn that into
// the same depfile the gcc path already reads. This is not a copy of any other parser.
static bool okShow(const Action& a) { return a.kind == "CC" && !a.depfile.empty() && hasShowIncludes(a); }
static void writeShowIncludesDepFile(const Action& a, const string& captured) {
    if (!okShow(a)) return;
    vector<string> hdrs;
    size_t i = 0;
    while (i < captured.size()) {
        size_t nl = captured.find('\n', i); if (nl == string::npos) nl = captured.size();
        string line = captured.substr(i, nl - i); if (!line.empty() && line.back() == '\r') line.pop_back();
        auto p = line.find("including file:");
        if (p != string::npos) {
            string path = line.substr(p + 15);
            size_t a0 = path.find_first_not_of(" \t"); size_t b0 = path.find_last_not_of(" \t");
            if (a0 != string::npos) hdrs.push_back(path.substr(a0, b0 - a0 + 1));
        }
        i = nl + 1; if (nl == captured.size()) break;
    }
    fs::path parent = fs::path(a.depfile).parent_path(); if (!parent.empty()) fs::create_directories(parent);
    std::ofstream f(a.depfile, std::ios::trunc);
    f << a.output << ":";
    for (auto& h : hdrs) { f << ' '; for (char c : h) { if (c == ' ' || c == '\\' || c == '#') f << '\\'; f << c; } }
    f << '\n';
}
static void commitStep(const Action& a, string& out, bool& ok, const vector<std::pair<string_view, FileSig>>& oldProduced) {
    if (ok) writeShowIncludesDepFile(a, out);
    if (!ok || a.outputs.empty()) { if (!ok) out = "okay-build: FAILED: " + joined(a.argv) + "\n" + out; return; }
    long long ts = nowNs();
    Rec rec; rec.cmd = a.cmdHash; vector<string> ins = a.inputs;
    if (!a.depfile.empty()) for (auto& h : parseDepfile(a.depfile)) { string np = resolve(a.dir, h); if (std::find(ins.begin(), ins.end(), np) == ins.end()) ins.push_back(np); }
    for (auto& pth : a.outputs) forgetHash(pth);
    for (auto& pth : ins) { FileSig sig; if (!hashFile(pth, sig)) { ok = false; out += "okay-build: cannot hash input " + pth + "\n"; break; } rec.inputs.push_back({poolStr(pth.data(), pth.size()), sig}); }
    if (ok) noteOutputs(a, rec, oldProduced, out, ok);
    if (ok) { OK_LOCK(l, gStateMu); gState[a.output] = rec; }
    else out = "okay-build: FAILED: " + joined(a.argv) + "\n" + out;
    gNsPost += nowNs() - ts;
    if (ok && !a.output.empty()) appendJournal(a.output);
}
static void printCache() {
    int h = gCacheHit.load(), m = gCacheMiss.load();
    if (h + m > 0) printf("okay-build: cache %d hit, %d miss\n", h, m);
}
struct StepNote { long long ns = 0; string kind, cache, desc; };
static vector<StepNote> gSteps;
static void noteStep(const string& kind, const string& desc, long long ns, const string& cache) {
    string d = desc; for (char& c : d) if (c == '\t' || c == '\n') c = ' ';
    OK_LOCK(l, gStateMu); gSteps.push_back(StepNote{ns, kind, cache, d});
}
static void archiveReport();
static void writeReport() {
    fs::create_directories(under(".okaybuild"));
    string tmp = under(".okaybuild/report.tmp");
    { std::ofstream f(tmp, std::ios::trunc);
      f << "okaybuild-report 1\n";
      for (auto& s : gSteps) f << s.ns << '\t' << s.kind << '\t' << s.cache << '\t' << s.desc << '\n';
      f << "hits\t" << gCacheHit.load() << "\nmisses\t" << gCacheMiss.load() << "\n"; }
    fs::rename(tmp, under(".okaybuild/report"));
    archiveReport();
}
static int gReportKeep = 20;
static void archiveReport() {
    if (gReportKeep < 1) return;
    string dir = under(".okaybuild/reports");
    fs::create_directories(dir);
    int seq = 1; { std::ifstream in(dir + "/seq"); if (in) in >> seq; if (seq < 1) seq = 1; }
    fs::copy_file(under(".okaybuild/report"), dir + "/" + std::to_string(seq) + ".txt", fs::copy_options::overwrite_existing);
    { std::ofstream o(dir + "/seq", std::ios::trunc); o << (seq + 1) << "\n"; }
    vector<int> ids;
    for (auto& e : fs::directory_iterator(dir)) {
        string n = e.path().filename().generic_string();
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".txt") == 0) ids.push_back(atoi(n.c_str()));
    }
    std::sort(ids.begin(), ids.end());
    while ((int)ids.size() > gReportKeep) { fs::remove(dir + "/" + std::to_string(ids.front()) + ".txt"); ids.erase(ids.begin()); }
}
static int showHistory() {
    string dir = under(".okaybuild/reports");
    vector<int> ids; std::error_code ec;
    if (fs::exists(dir, ec)) for (auto& e : fs::directory_iterator(dir, ec)) {
        string n = e.path().filename().generic_string();
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".txt") == 0) ids.push_back(atoi(n.c_str()));
    }
    std::sort(ids.begin(), ids.end());
    printf("history: %zu\n", ids.size());
    for (int id : ids) printf("%d\n", id);
    return 0;
}
static string jsonEsc(const string& s);
struct DiffStep { long long ms = 0; string cache; };
static std::map<string, DiffStep> loadReportSteps(const string& path) {
    std::ifstream in(path); string line;
    if (!std::getline(in, line) || line != "okaybuild-report 1") throw Fail("not a report: " + path);
    std::map<string, DiffStep> steps;
    while (std::getline(in, line)) {
        if (line.compare(0, 5, "hits\t") == 0 || line.compare(0, 7, "misses\t") == 0) continue;
        size_t a = line.find('\t'), b = a == string::npos ? string::npos : line.find('\t', a + 1), c = b == string::npos ? string::npos : line.find('\t', b + 1);
        if (a == string::npos || c == string::npos) continue;
        string desc = line.substr(c + 1);
        steps[desc] = DiffStep{atoll(line.c_str()) / 1000000, line.substr(b + 1, c - b - 1)};
    }
    return steps;
}
static int diffReports(const string& a, const string& b) {
    auto resolve = [](const string& name) {
        string hist = under(".okaybuild/reports/" + name + ".txt");
        if (fs::is_regular_file(hist)) return hist;
        if (fs::is_regular_file(name)) return name;
        throw Fail("no report '" + name + "'");
    };
    auto A = loadReportSteps(resolve(a)), B = loadReportSteps(resolve(b));
    for (auto& [d, st] : A) if (!B.count(d)) printf("only %s: %s (%lld ms)\n", a.c_str(), d.c_str(), st.ms);
    for (auto& [d, st] : B) if (!A.count(d)) printf("only %s: %s (%lld ms)\n", b.c_str(), d.c_str(), st.ms);
    for (auto& [d, st] : A) {
        auto it = B.find(d); if (it == B.end()) continue;
        if (it->second.ms != st.ms) printf("changed: %s %lld ms -> %lld ms\n", d.c_str(), st.ms, it->second.ms);
        if (it->second.cache != st.cache) printf("cache: %s %s -> %s\n", d.c_str(), st.cache.c_str(), it->second.cache.c_str());
    }
    return 0;
}
static int showReport(bool json = false) {
    std::ifstream in(under(".okaybuild/report"));
    if (!in) throw Fail("no report yet; run a build first");
    string line; if (!std::getline(in, line) || line != "okaybuild-report 1") throw Fail("report is not from this okay-build");
    struct Row { long long ns; string kind, cache, desc; };
    vector<Row> rows; int hits = 0, misses = 0;
    while (std::getline(in, line)) {
        if (line.compare(0, 5, "hits\t") == 0) { hits = atoi(line.c_str() + 5); continue; }
        if (line.compare(0, 7, "misses\t") == 0) { misses = atoi(line.c_str() + 7); continue; }
        size_t a = line.find('\t'), b = a == string::npos ? string::npos : line.find('\t', a + 1), c = b == string::npos ? string::npos : line.find('\t', b + 1);
        if (a == string::npos || b == string::npos || c == string::npos) continue;
        rows.push_back(Row{atoll(line.c_str()), line.substr(a + 1, b - a - 1), line.substr(b + 1, c - b - 1), line.substr(c + 1)});
    }
    std::sort(rows.begin(), rows.end(), [](const Row& x, const Row& y) { return x.ns > y.ns; });
    long long sum = 0; for (auto& r : rows) sum += r.ns;
    if (json) {
        printf("{\"steps\": %zu, \"hits\": %d, \"misses\": %d, \"total_ms\": %lld, \"items\": [", rows.size(), hits, misses, sum / 1000000);
        for (size_t i = 0; i < rows.size(); ++i)
            printf("%s{\"ms\": %lld, \"kind\": \"%s\", \"cache\": \"%s\", \"desc\": \"%s\"}", i ? ", " : "", rows[i].ns / 1000000, jsonEsc(rows[i].kind).c_str(), jsonEsc(rows[i].cache).c_str(), jsonEsc(rows[i].desc).c_str());
        printf("]}\n"); return 0;
    }
    printf("steps: %zu\n", rows.size());
    int denom = hits + misses;
    if (denom == 0) printf("cache: no compiles\n");
    else printf("cache: %d hit, %d miss, %d%%\n", hits, misses, hits * 100 / denom);
    printf("slowest:\n");
    size_t n = rows.size() < 10 ? rows.size() : 10;
    for (size_t i = 0; i < n; ++i) printf("  %lld ms\t%s\t%s\t%s\n", rows[i].ns / 1000000, rows[i].kind.c_str(), rows[i].cache.c_str(), rows[i].desc.c_str());
    std::map<string, std::pair<int, long long>> byKind;
    for (auto& r : rows) { auto& k = byKind[r.kind]; ++k.first; k.second += r.ns; }
    printf("by kind:\n");
    for (auto& [k, v] : byKind) printf("  %s\t%d step(s)\t%lld ms\n", k.c_str(), v.first, v.second / 1000000);
    printf("total: %lld ms\n", sum / 1000000);
    return 0;
}

// ---------------------------------------------------------------- importer (Stage B)
// Source of commands: `ninja -t commands [targets]` of an already configured CMake build dir (every compile, archive, link and
// custom command, one per line, in dependency order). compile_commands.json is only used as a cross-check of the compile count,
// because it has no link steps and no depfile flag. Commands are kept as the exact text CMake generated.
static vector<string> shellWords(const string& c) { // enough shell lexing to find -o/-c/-MF/inputs; quotes and backslashes honoured
    vector<string> w; string cur; bool have = false; char q = 0;
    for (size_t i = 0; i < c.size(); ++i) {
        char ch = c[i];
        if (q) { if (ch == q) q = 0; else if (q == '"' && ch == '\\' && i + 1 < c.size()) cur += c[++i]; else cur += ch; }
        else if (ch == '"' || ch == '\'') { q = ch; have = true; }
        else if (ch == '\\' && i + 1 < c.size()) { cur += c[++i]; have = true; }
        else if (isspace((unsigned char)ch)) { if (have) { w.push_back(cur); cur.clear(); have = false; } }
        else { cur += ch; have = true; }
    }
    if (have) { w.push_back(cur); }
    return w;
}
static bool endsWith(const string& s, const char* x) { size_t n = strlen(x); return s.size() >= n && s.compare(s.size() - n, n, x) == 0; }
static string toolBase(string s) {
    auto p = s.find_last_of("/\\"); if (p != string::npos) s = s.substr(p + 1);
    if (endsWith(s, ".exe")) s.resize(s.size() - 4);
    return s;
}
static bool gccLike(const string& b) {
    return b == "gcc" || b == "g++" || b == "clang" || b == "clang++" || b == "cc" || b == "c++" || b.find("gcc") != string::npos || b.find("g++") != string::npos || b.find("clang") != string::npos;
}
// 2 = not a compile. Otherwise the same codes as lookupCache. Imported gcc/clang/cl -c commands
// are keyed by the preprocessed text, the original words, and the compiler file.
static int lookupImported(const Action& a, string& key, string& errOut) {
    auto w = shellWords(a.rawCmd);
    if (w.size() < 2) return 2;
    string base = toolBase(w[0]);
    bool gcc = gccLike(base), cl = base == "cl";
    if (!gcc && !cl) return 2;
    bool compile = false; string output, dep;
    vector<string> pp; pp.push_back(w[0]); pp.push_back(gcc ? "-E" : "/E");
    for (size_t i = 1; i < w.size(); ++i) {
        const string& s = w[i];
        if (gcc && (s == "-c" || s == "-MMD" || s == "-MD")) { if (s == "-c") compile = true; continue; }
        if (gcc && (s == "-o" || s == "-MF" || s == "-MT") && i + 1 < w.size()) {
            if (s == "-o") output = w[++i]; else if (s == "-MF") dep = w[++i]; else ++i;
            continue;
        }
        if (cl && (s == "/c" || s == "-c")) { compile = true; continue; }
        if (cl && (s.rfind("/Fo", 0) == 0 || s.rfind("/Fe", 0) == 0)) { output = s.substr(3); continue; }
        pp.push_back(s);
    }
    if (!compile || (output.empty() && a.output.empty())) return 2;
    string dest = a.output.empty() ? output : a.output;
    string depfile = a.depfile.empty() ? dep : a.depfile;
    string out, err; long long tPre = nowNs(); int rc = captureStdout(pp, out, err); gNsPre += nowNs() - tPre;
    if (rc != 0) { errOut += err.empty() ? out : err; if (errOut.empty() || errOut.back() != '\n') errOut += '\n'; errOut += "okay-build: preprocess failed\n"; return -1; }
    string id = compilerIdentity(w[0]); string cmd = hashArgvForCache(w);
    uint64_t h = fnv(out.data(), out.size()); h = fnv(cmd.data(), cmd.size(), h); h = fnv(id.data(), id.size(), h);
    key = hex64(h);
    string dir = cacheDirOf(key);
    if (!cacheEntryOk(key, !depfile.empty())) return 0;
    std::error_code ec;
    fs::copy_file(dir + "/obj", dest, fs::copy_options::overwrite_existing, ec);
    if (ec) { errOut += "okay-build: cache copy failed: " + ec.message() + "\n"; return -1; }
    if (!depfile.empty()) fs::copy_file(dir + "/dep", depfile, fs::copy_options::overwrite_existing, ec);
    rememberCache(key);
    return 1;
}
// --- build.ninja reader: only what we need (build statements with outputs, rule, inputs, implicit and order-only inputs)
struct NinjaEdge { string rule; vector<string> outs, ins, implicit, orderOnly; };
static vector<string> ninjaSplit(const string& s) { // split on unescaped spaces; '$ ' '$:' '$$' unescaped; ':' | '||' kept as words
    vector<string> w; string cur; bool have = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '$' && i + 1 < s.size()) { cur += s[++i]; have = true; if (s[i] == ':') { /* escaped colon stays part of the word */ } continue; }
        if (c == ' ') { if (have) { w.push_back(cur); cur.clear(); have = false; } continue; }
        if (c == ':') { if (have) { w.push_back(cur); cur.clear(); have = false; } w.push_back(":"); continue; } // unescaped colon separates outputs from rule
        cur += c; have = true;
    }
    if (have) { w.push_back(cur); }
    return w;
}
static std::map<string, NinjaEdge> readNinja(const string& file) {
    std::ifstream in(file); std::map<string, NinjaEdge> m; if (!in) return m; string line, acc;
    auto flush = [&](const string& l) {
        if (l.compare(0, 6, "build ") != 0) return;
        auto w = ninjaSplit(l.substr(6)); NinjaEdge e; size_t i = 0; int mode = 0; bool colon = false;
        for (; i < w.size(); ++i) {
            if (w[i] == ":") { colon = true; ++i; break; }
            if (w[i] == "|") continue; // implicit outputs are treated as outputs too
            e.outs.push_back(w[i]);
        }
        if (!colon || i >= w.size()) return;
        e.rule = w[i++];
        for (; i < w.size(); ++i) { if (w[i] == "|") mode = 1; else if (w[i] == "||") mode = 2; else (mode == 0 ? e.ins : mode == 1 ? e.implicit : e.orderOnly).push_back(w[i]); }
        for (auto& o : e.outs) m[o] = e;
    };
    while (std::getline(in, line)) {
        auto continued = [](const string& l) { size_t n = 0; for (size_t i = l.size(); i > 0 && l[i - 1] == '$'; --i) ++n; return n % 2 == 1; };
        while (continued(line)) { // '$' at end of line joins the next line (leading blanks dropped)
            line.pop_back(); string next; if (!std::getline(in, next)) break;
            size_t nb = next.find_first_not_of(' '); line += nb == string::npos ? "" : next.substr(nb);
        }
        flush(line);
    }
    return m;
}
static int importCmake(const string& buildDir, const string& runDir, const string& outFile, const vector<string>& targets) {
    string ninjaCmd = "ninja"; vector<string> argv = {ninjaCmd, "-C", buildDir, "-t", "commands"}; for (auto& t : targets) argv.push_back(t);
    string out; if (runCmd(argv, out) != 0) throw Fail("ninja -t commands failed in " + buildDir + ":\n" + out);
    string dir = runDir.empty() ? fs::absolute(buildDir).string() : fs::absolute(runDir).string();
    auto edges = readNinja(buildDir + "/build.ninja");
    struct Imp { string line, kind, output, depfile; vector<string> inputs, after; };
    vector<Imp> imps; std::istringstream in(out); string line; int nCompile = 0, nArchive = 0, nLink = 0, nOther = 0, nGuess = 0; vector<string> others; std::set<string> outputs;
    string absBuild = fs::absolute(buildDir).lexically_normal().generic_string(); while (absBuild.size() > 1 && absBuild.back() == '/') absBuild.pop_back();
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (dir != absBuild) { // commands that name the build tree by absolute path (cd, -I for generated headers) must use the run tree
            string r; size_t pos = 0;
            for (size_t i; (i = line.find(absBuild, pos)) != string::npos;) {
                size_t e = i + absBuild.size(); bool endOk = e >= line.size() || strchr("/ \"'\\;&", line[e]) != nullptr;
                bool startOk = i == 0 || strchr(" \"'=:,I", line[i - 1]) != nullptr;
                r += line.substr(pos, i - pos); r += (endOk && startOk) ? dir : absBuild; pos = e;
            }
            line = r + line.substr(pos);
        }
        auto w = shellWords(line); Imp p; p.line = line; string src; bool hasC = false, hasO = false;
        for (size_t i = 0; i < w.size(); ++i) {
            if (w[i] == "-c" && i + 1 < w.size()) { hasC = true; src = w[i + 1]; }
            else if (w[i] == "-o" && i + 1 < w.size()) { hasO = true; p.output = w[i + 1]; }
            else if (w[i] == "-MF" && i + 1 < w.size()) p.depfile = w[i + 1];
        }
        if (hasC && hasO) { p.kind = "compile"; p.inputs = {src}; ++nCompile; }
        else if (hasO) { p.kind = "link"; for (auto& x : w) if (x != p.output && (endsWith(x, ".o") || endsWith(x, ".obj") || endsWith(x, ".a") || endsWith(x, ".so") || endsWith(x, ".dll"))) p.inputs.push_back(x); ++nLink; }
        else { // archive: "... ar qc OUT.a objs..." (no -o)
            size_t ar = w.size(); for (size_t i = 0; i < w.size(); ++i) { string fn = fs::path(w[i]).filename().string(); if (fn == "ar" || endsWith(fn, "-ar")) ar = i; } // ar, gcc-ar, x86_64-w64-mingw32-ar, llvm-ar
            if (ar < w.size()) for (size_t i = ar + 1; i < w.size(); ++i) if (endsWith(w[i], ".a")) { p.output = w[i]; for (size_t j = i + 1; j < w.size(); ++j) if (endsWith(w[j], ".o") || endsWith(w[j], ".obj")) p.inputs.push_back(w[j]); break; }
            if (!p.output.empty()) { p.kind = "archive"; ++nArchive; }
            else { // custom command: find the build.ninja custom edge whose output appears as a word of the command
                p.kind = "custom"; ++nOther;
                for (auto& x : w) { auto it = edges.find(x); if (it != edges.end() && it->second.rule.find("CUSTOM_COMMAND") != string::npos) { p.output = x; ++nGuess; break; } }
                others.push_back(line.substr(0, 100) + (p.output.empty() ? "   [no output found: always runs]" : "   [output " + p.output + "]"));
            }
        }
        if (!p.output.empty() && !outputs.insert(p.output).second) continue; // duplicate command for the same output
        imps.push_back(p);
    }
    // order-only / implicit edges: resolve through phony nodes to outputs of imported actions (build.ninja is the only place they live)
    std::set<string> have; for (auto& p : imps) if (!p.output.empty()) have.insert(p.output);
    long nAfter = 0;
    for (auto& p : imps) { // custom steps: their real file inputs (absolute, existing, not made by another imported action) are tracked by content
        if (p.kind != "custom" || p.output.empty()) continue;
        auto e = edges.find(p.output); if (e == edges.end()) continue;
        for (auto* v : {&e->second.ins, &e->second.implicit}) for (auto& x : *v) if (fs::path(x).is_absolute() && !have.count(x) && fs::exists(x)) p.inputs.push_back(x);
    }
    for (auto& p : imps) {
        if (p.output.empty()) continue;
        auto e = edges.find(p.output); if (e == edges.end()) continue;
        std::set<string> seen, res; std::function<void(const string&)> walk = [&](const string& n) {
            if (!seen.insert(n).second) return;
            if (have.count(n)) { if (n != p.output) res.insert(n); return; }
            auto it = edges.find(n); if (it == edges.end() || it->second.rule != "phony") return;
            for (auto* v : {&it->second.ins, &it->second.implicit, &it->second.orderOnly}) for (auto& x : *v) walk(x);
        };
        for (auto* v : {&e->second.ins, &e->second.implicit, &e->second.orderOnly}) for (auto& x : *v) walk(x);
        for (auto& x : res) { if (std::find(p.inputs.begin(), p.inputs.end(), x) == p.inputs.end()) p.after.push_back(x); }
        nAfter += (long)p.after.size();
    }
    std::ofstream f(outFile, std::ios::trunc); f << "# generated by okay-build import from " << fs::absolute(buildDir).string() << " (raw CMake/Ninja commands)\n";
    for (auto& p : imps) {
        f << "action\n  dir = " << qtok(dir) << "\n  cmd = " << p.line << "\n";
        if (!p.output.empty()) f << "  output = " << qtok(p.output) << "\n";
        if (!p.depfile.empty()) f << "  depfile = " << qtok(p.depfile) << "\n";
        if (!p.inputs.empty()) { f << "  inputs ="; for (auto& x : p.inputs) f << " " << qtok(x); f << "\n"; }
        if (!p.after.empty()) { f << "  after ="; for (auto& x : p.after) f << " " << qtok(x); f << "\n"; }
        f << "end\n";
    }
    printf("okay-build import: %d compile, %d archive, %d link, %d other (custom) command(s) -> %s\n", nCompile, nArchive, nLink, nOther, outFile.c_str());
    printf("okay-build import: %zu build.ninja edges read, %ld order-only/implicit edge(s) kept, %d custom output(s) found\n", edges.size(), nAfter, nGuess);
    for (auto& x : others) printf("  custom: %s\n", x.c_str());
    return 0;
}

struct PoolUse { string name; int depth = 1; int used = 0; };
static int findPool(const vector<PoolUse>& pools, const string& name) {
    for (int i = 0; i < (int)pools.size(); ++i) if (pools[i].name == name) return i;
    return -1;
}
static string effectivePool(const Action& a) {
    if (a.pool == "none") return "-";
    if (!a.pool.empty()) return a.pool;
    return a.kind == "LD" ? "link" : "-";
}

#ifdef _WIN32
static string writeRsp(const string& key, const vector<string>& args, size_t head) {
    string rf = under(".okaybuild/rsp/" + hashStr(key.empty() ? "cmd" : key) + ".rsp");
    fs::create_directories(fs::path(rf).parent_path());
    writeArgsFile(rf, args, head);
    return rf;
}
static bool shellMeta(const string& body) {
    bool q = false;
    for (size_t i = 0; i < body.size(); ++i) {
        if (body[i] == '"') q = !q;
        if (q) continue;
        if (body[i] == '>' || body[i] == '<' || body[i] == '|') return true;
        if (body[i] == '&' && i + 1 < body.size() && body[i + 1] == '&') return true;
    }
    return false;
}
static string trimCopy(const string& s) {
    size_t a = s.find_first_not_of(" \t"); if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t"); return s.substr(a, b - a + 1);
}
// A cmd.exe line dies around 8191 characters. A long tool invocation (no shell operators) is
// rewritten to `tool @response` so CreateProcess stays under the 32767-character limit.
static string shortenShell(const string& body) {
    if (body.size() < 7000) return body;
    vector<string> segs; string cur; bool q = false;
    for (size_t i = 0; i < body.size(); ++i) {
        if (body[i] == '"') q = !q;
        if (!q && i + 1 < body.size() && body[i] == '&' && body[i + 1] == '&') { segs.push_back(cur); cur.clear(); ++i; continue; }
        cur += body[i];
    }
    segs.push_back(cur);
    string out;
    for (size_t si = 0; si < segs.size(); ++si) {
        string t = trimCopy(segs[si]);
        if (t.size() > 7000 && !shellMeta(t)) {
            auto w = shellWords(t);
            if (w.size() > 1) {
                string rf = writeRsp(t, w, 1);
                for (char& c : rf) if (c == '/') c = '\\';
                t = winQuote(w[0]) + " @" + winQuote(rf);
            }
        }
        if (si) out += " && ";
        out += t;
    }
    return out;
}
static string commandLineFor(const Action& a, string& err) {
    if (!a.rawCmd.empty()) {
        string body = shortenShell(a.rawCmd);
        string script = under(".okaybuild/cmd/" + hashStr(body) + ".cmd");
        fs::create_directories(fs::path(script).parent_path());
        { std::ofstream o(script, std::ios::binary); o << "@echo off\r\n" << body << "\r\n"; }
        for (char& c : script) if (c == '/') c = '\\';
        return "cmd.exe /C " + winQuote(script);
    }
    vector<string> av = a.argv;
    size_t limit = a.rspThreshold ? a.rspThreshold : 30000;
    if (limit > 30000) limit = 30000;
    size_t head = a.rspHead > 0 ? a.rspHead : 1;
    if (av.size() > head && winJoin(av).size() > limit) {
        string rf = writeRsp(a.output.empty() ? a.desc : a.output, av, head);
        av.resize(head);
        for (char& c : rf) if (c == '/') c = '\\';
        av.push_back("@" + rf);
    }
    string line = winJoin(av);
    if (line.size() > 32000) err = "okay-build: command line exceeds the Windows 32767-character limit\n";
    return line;
}
static int buildOnWindows(const Manifest& m, const vector<string>& want, const Opts& o) {
    gRspUtf16 = m.tc.rspUtf16;
    long long tStart = nowNs(); Graph g = plan(m, want); rejectOversized(g);
    long long tPlanOnly = nowNs() - tStart; tStart = nowNs(); loadState();
    long long tState = nowNs() - tStart; long long tPlan = tPlanOnly + tState; tStart = nowNs() - tPlan;
    int total = (int)g.acts.size(), ran = 0, upToDate = 0, finished = 0;
    bool anyFail = false, stop = false;
    vector<PoolUse> pools = {{"link", 1, 0}, {"console", 1, 0}};
    for (auto& d : m.pools) { int i = findPool(pools, d.name); if (i < 0) pools.push_back({d.name, d.depth, 0}); else pools[i].depth = d.depth; }
    vector<int> poolIdx(total, -1);
    for (auto& a : g.acts) {
        if (a.pool == "none") continue;
        string n = a.pool.empty() ? (a.kind == "LD" ? "link" : "") : a.pool;
        if (n.empty()) continue;
        int i = findPool(pools, n);
        if (i < 0) throw Fail("action '" + a.desc + "' uses unknown pool '" + n + "'");
        poolIdx[a.id] = i;
    }
    if (!o.dry) {
        bool anyDirty = false; long long t0 = nowNs();
        for (auto& a : g.acts) { string why; if (dirty(a, why)) { anyDirty = true; break; } }
        gNsDirty += nowNs() - t0;
        if (!anyDirty) {
            upToDate = total;
            if (gHashed.load() > 0 && refreshStamps() > 0) saveState();
            if (o.verbose) for (auto& a : g.acts) if (!a.output.empty()) printf("[up to date] %s\n", a.output.c_str());
            printf("okay-build: ran 0 of %d steps (%d up to date)\n", total, upToDate);
            if (o.profile) printf("okay-build: plan %lld ms, state %lld ms\n", tPlanOnly / 1000000, tState / 1000000);
            if (o.profile) printf("okay-build: profile (summed over workers, ms): dirty-check %lld, run command 0, depfile+hash 0; wall %lld\n", gNsDirty / 1000000, (nowNs() - tStart) / 1000000);
            if (o.verbose) printf("okay-build: hashed %d input file(s), trusted size+mtime for %d\n", gHashed.load(), gTrusted.load());
            return 0;
        }
    }
    vector<int> pending(total); vector<char> wouldRun(total, 0); vector<int> ready;
    for (auto& a : g.acts) { pending[a.id] = (int)a.deps.size(); if (a.deps.empty()) ready.push_back(a.id); }
    SYSTEM_INFO info; GetSystemInfo(&info);
    int jobs = o.jobs > 0 ? o.jobs : (int)info.dwNumberOfProcessors;
    if (jobs > total && total > 0) jobs = total;
    if (jobs < 1) jobs = 1;
    if (jobs > 64) jobs = 64; // WaitForMultipleObjects accepts at most 64 handles
    ensureJobserver(jobs);
    int memUsed = 0;
    auto blocked = [&](int id) {
        int p = poolIdx[id]; if (p >= 0 && pools[p].used >= pools[p].depth) return true;
        return gMemLimit > 0 && memUsed + g.acts[id].memory > gMemLimit;
    };
    auto take = [&]() -> int {
        for (int i = (int)ready.size() - 1; i >= 0; --i) if (!blocked(ready[i])) { int id = ready[i]; ready.erase(ready.begin() + i); return id; }
        return -1;
    };
    auto done = [&](int id, bool ok, const string& msg, bool held) {
        if (held) { if (poolIdx[id] >= 0) --pools[poolIdx[id]].used; memUsed -= g.acts[id].memory; }
        ++finished;
        if (!ok) { anyFail = true; if (!o.keep) stop = true; if (!msg.empty()) { fflush(stdout); fputs(msg.c_str(), stderr); } }
        else if (!stop) for (int r : g.acts[id].rdeps) if (--pending[r] == 0) ready.push_back(r);
    };
    vector<Live> live;
    for (;;) {
        while ((int)live.size() < jobs && !stop && !gCancel.load()) {
            int id = take();
            if (id < 0) break;
            const Action& a = g.acts[id];
            string why, out;
            try {
                long long t0 = nowNs(); bool d = dirty(a, why); gNsDirty += nowNs() - t0;
                if (!d && o.dry) for (int dep : a.deps) if (wouldRun[dep]) { d = true; why = "input " + g.acts[dep].output + " would be rebuilt"; }
                if (!d) { ++upToDate; if (o.verbose) printf("[up to date] %s\n", a.output.c_str()); done(id, true, "", false); continue; }
                ++ran; printf("[%d/%d] %s %s\n", ran, total, a.kind.c_str(), a.desc.c_str());
                if (o.explain) printf("        because: %s\n", why.c_str());
                if (o.verbose || o.dry) printf("        %s\n", joined(a.argv).c_str());
                fflush(stdout);
                if (o.dry) { wouldRun[id] = 1; done(id, true, "", false); continue; }
                for (auto& pth : a.outputs) { fs::path parent = fs::path(pth).parent_path(); if (!parent.empty()) fs::create_directories(parent); }
                if (a.kind == "AR" && !a.output.empty()) fs::remove(a.output);
                string err; string line = commandLineFor(a, err);
                if (!err.empty()) { done(id, false, err, false); continue; }
                long long tStep = nowNs();
                string cacheKey, tag = "-";
                int cr = 2;
                if (gUseCache && a.kind == "CC" && !hasShowIncludes(a)) cr = lookupCache(a, cacheKey, out);
                else if (gUseCache && a.kind == "RUN") cr = lookupImported(a, cacheKey, out);
                if (cr != 2) {
                    if (cr < 0) { noteStep(a.kind, a.desc, nowNs() - tStep, "fail"); done(id, false, out, false); continue; }
                    if (cr == 1) {
                        ++gCacheHit; tag = "hit"; printf("        cache hit\n"); fflush(stdout);
                        bool ok = true; vector<std::pair<string_view, FileSig>> old;
                        if (a.restat && !a.output.empty()) { auto it = gState.find(a.output); if (it != gState.end()) old = it->second.produced; }
                        commitStep(a, out, ok, old);
                        noteStep(a.kind, a.desc, nowNs() - tStep, tag);
                        done(id, ok, out, false); continue;
                    }
                    tag = "miss"; ++gCacheMiss; printf("        cache miss\n"); fflush(stdout);
                }
                bool stream = poolIdx[id] >= 0 && pools[poolIdx[id]].name == "console";
                Live L; L.id = id; L.cacheKey = cacheKey; L.cacheTag = tag; L.began = tStep;
                JsToken tok = tryTakeJob();
                if (tok.kind == -2) { ready.push_back(id); break; }
                if (tok.kind < 0) { gCancel.store(1); done(id, false, "okay-build: cancelled\n", false); continue; }
                if (!spawnLive(line, a.dir, stream, L, err)) { giveJob(tok); done(id, false, err, false); continue; }
                L.jobKind = tok.kind;
                if (poolIdx[id] >= 0) ++pools[poolIdx[id]].used;
                memUsed += a.memory;
                live.push_back(L);
            } catch (const std::exception& e) { done(id, false, string("okay-build: ") + e.what() + "\n", false); }
        }
        if (gCancel.load()) {
            for (auto& L : live) { TerminateProcess(L.proc, 130); WaitForSingleObject(L.proc, 1000); CloseHandle(L.proc); if (L.pipe) CloseHandle(L.pipe); }
            live.clear(); break;
        }
        if (live.empty()) {
            if (gJobOn && !ready.empty() && !stop && !gCancel.load()) { Sleep(20); continue; }
            if (!ready.empty() && !stop) { anyFail = true; stop = true; fprintf(stderr, "okay-build: steps are waiting on a pool but none is free\n"); }
            break;
        }
        HANDLE hs[64]; int n = (int)live.size(); if (n > 64) n = 64;
        for (int i = 0; i < n; ++i) hs[i] = live[(size_t)i].proc;
        for (auto& L : live) drainLive(L);
        DWORD w = WaitForMultipleObjects((DWORD)n, hs, FALSE, 20);
        for (auto& L : live) drainLive(L);
        if (w < WAIT_OBJECT_0 || w >= WAIT_OBJECT_0 + (DWORD)n) continue;
        int idx = (int)(w - WAIT_OBJECT_0);
        Live L = live[(size_t)idx]; live.erase(live.begin() + idx);
        drainLive(L);
        DWORD code = 1; GetExitCodeProcess(L.proc, &code);
        CloseHandle(L.proc); if (L.pipe) CloseHandle(L.pipe);
        gNsSpawn += nowNs() - L.started;
        giveJob(JsToken{L.jobKind});
        const Action& a = g.acts[L.id];
        bool ok = code == 0; string out = L.out;
        if (ok && !L.cacheKey.empty()) storeCache(a, L.cacheKey);
        vector<std::pair<string_view, FileSig>> oldProduced;
        if (ok && a.restat && !a.output.empty()) { auto it = gState.find(a.output); if (it != gState.end()) oldProduced = it->second.produced; }
        commitStep(a, out, ok, oldProduced);
        noteStep(a.kind, a.desc, nowNs() - (L.began ? L.began : L.started), L.cacheTag.empty() ? "-" : L.cacheTag);
        done(L.id, ok, out, true);
    }
    if (!o.dry) { refreshStamps(); saveState(); }
    if (!o.dry) writeReport();
    if (gCancel.load()) return 130;
    if (anyFail) { printCache(); fprintf(stderr, "okay-build: build FAILED (%d step(s) ran, %d of %d finished)\n", ran, finished, total); return 1; }
    printCache();
    printf("okay-build: ran %d of %d steps (%d up to date)%s\n", ran, total, upToDate, o.dry ? " [dry run]" : "");
    if (o.profile) printf("okay-build: plan %lld ms, state %lld ms\n", tPlanOnly / 1000000, tState / 1000000);
    if (o.profile) printf("okay-build: profile (summed over workers, ms): dirty-check %lld, preprocess %lld, run command %lld, depfile+hash %lld; wall %lld\n", gNsDirty / 1000000, gNsPre / 1000000, gNsSpawn / 1000000, gNsPost / 1000000, (nowNs() - tStart) / 1000000);
    if (o.verbose) printf("okay-build: hashed %d input file(s), trusted size+mtime for %d\n", gHashed.load(), gTrusted.load());
    return 0;
}
#endif
static int build(const Manifest& m, const vector<string>& want, const Opts& o) {
#ifdef _WIN32
    return buildOnWindows(m, want, o);
#else
    gRspUtf16 = m.tc.rspUtf16;
    long long tStart = nowNs(); Graph g = plan(m, want); rejectOversized(g);
    long long tPlanOnly = nowNs() - tStart; tStart = nowNs(); loadState();
    long long tState = nowNs() - tStart; long long tPlan = tPlanOnly + tState; tStart = nowNs() - tPlan;
    int total = (int)g.acts.size(), ran = 0, upToDate = 0, running = 0, finished = 0;
    bool anyFail = false, stop = false;
    vector<PoolUse> pools = {{"link", 1, 0}, {"console", 1, 0}};
    for (auto& d : m.pools) { int i = findPool(pools, d.name); if (i < 0) pools.push_back({d.name, d.depth, 0}); else pools[i].depth = d.depth; }
    vector<int> poolIdx(total, -1);
    for (auto& a : g.acts) {
        if (a.pool == "none") continue;
        string n = a.pool.empty() ? (a.kind == "LD" ? "link" : "") : a.pool;
        if (n.empty()) continue;
        int i = findPool(pools, n);
        if (i < 0) throw Fail("action '" + a.desc + "' uses unknown pool '" + n + "'");
        poolIdx[a.id] = i;
    }
    // A clean tree does not need worker threads: one pass of stat checks, then stop.
    // A dry run still walks the pool so "would run" can spread from a step that would rebuild.
    if (!o.dry) {
        bool anyDirty = false; long long t0 = nowNs();
        for (auto& a : g.acts) { string why; if (dirty(a, why)) { anyDirty = true; break; } }
        gNsDirty += nowNs() - t0;
        if (!anyDirty) {
            upToDate = total;
            if (gHashed.load() > 0 && refreshStamps() > 0) saveState();
            if (o.verbose) for (auto& a : g.acts) if (!a.output.empty()) printf("[up to date] %s\n", a.output.c_str());
            printf("okay-build: ran 0 of %d steps (%d up to date)\n", total, upToDate);
            if (o.profile) printf("okay-build: plan %lld ms, state %lld ms\n", tPlanOnly / 1000000, tState / 1000000);
            if (o.profile) printf("okay-build: profile (summed over workers, ms): dirty-check %lld, run command 0, depfile+hash 0; wall %lld\n", gNsDirty / 1000000, (nowNs() - tStart) / 1000000);
            if (o.verbose) printf("okay-build: hashed %d input file(s), trusted size+mtime for %d\n", gHashed.load(), gTrusted.load());
            return 0;
        }
    }
    vector<int> pending(total); vector<char> wouldRun(total, 0); vector<int> ready;
    for (auto& a : g.acts) { pending[a.id] = (int)a.deps.size(); if (a.deps.empty()) ready.push_back(a.id); }
    std::mutex mu; std::condition_variable cv; string firstErr;
    int jobs = o.jobs > 0 ? o.jobs : (int)std::max(1u, std::thread::hardware_concurrency());
    if (jobs > total && total > 0) jobs = total;
    if (jobs < 1) jobs = 1;
    ensureJobserver(jobs);
    int memUsed = 0;
    auto blocked = [&](int id) {
        int p = poolIdx[id]; if (p >= 0 && pools[p].used >= pools[p].depth) return true;
        return gMemLimit > 0 && memUsed + g.acts[id].memory > gMemLimit;
    };
    auto take = [&]() -> int {
        for (int i = (int)ready.size() - 1; i >= 0; --i) if (!blocked(ready[i])) { int id = ready[i]; ready.erase(ready.begin() + i); return id; }
        return -1;
    };
    auto worker = [&] {
        std::unique_lock<std::mutex> lk(mu);
        for (;;) {
            cv.wait(lk, [&] {
                if (gCancel.load() || stop || ready.empty()) return running == 0;
                for (int id : ready) if (!blocked(id)) return true;
                return running == 0;
            });
            if (gCancel.load() || stop || ready.empty()) { if (running == 0) { cv.notify_all(); return; } continue; }
            int id = take();
            if (id < 0) {
                if (running == 0) { anyFail = true; stop = true; firstErr = "okay-build: steps are waiting on a pool but none is free\n"; cv.notify_all(); return; }
                continue;
            }
            ++running; bool stream = false;
            if (poolIdx[id] >= 0) { ++pools[poolIdx[id]].used; stream = pools[poolIdx[id]].name == "console"; }
            memUsed += g.acts[id].memory;
            const Action& a = g.acts[id]; lk.unlock();
            string why, out; bool d = false, ok = true; vector<std::pair<string_view, FileSig>> oldProduced;
            try {
                if (a.restat && !a.output.empty()) { std::lock_guard<std::mutex> sl(gStateMu); auto it = gState.find(a.output); if (it != gState.end()) oldProduced = it->second.produced; }
                long long t0 = nowNs(); d = dirty(a, why); gNsDirty += nowNs() - t0;
                if (!d && o.dry) for (int dep : a.deps) if (wouldRun[dep]) { d = true; why = "input " + g.acts[dep].output + " would be rebuilt"; }
                if (d) {
                    { std::lock_guard<std::mutex> pl(mu); ++ran; printf("[%d/%d] %s %s\n", ran, total, a.kind.c_str(), a.desc.c_str());
                      if (o.explain) printf("        because: %s\n", why.c_str());
                      if (o.verbose || o.dry) printf("        %s\n", joined(a.argv).c_str());
                      fflush(stdout); }
                    if (!o.dry) {
                        for (auto& pth : a.outputs) { fs::path parent = fs::path(pth).parent_path(); if (!parent.empty()) fs::create_directories(parent); }
                        if (a.kind == "AR" && !a.output.empty()) fs::remove(a.output);
                        vector<string> argv = a.argv;
                        if (a.rspHead > 0 && joined(a.argv).size() > a.rspThreshold) {
                            string rf = under(".okaybuild/rsp/" + hashStr(a.output.empty() ? a.desc : a.output) + ".rsp");
                            writeArgsFile(rf, a.argv, a.rspHead);
                            argv.resize(a.rspHead); argv.push_back("@" + rf);
                        }
                        long long tStep = nowNs();
                        string cacheKey, tag = "-"; bool cached = false;
                        int cr = 2;
                        if (gUseCache && a.kind == "CC" && !hasShowIncludes(a)) cr = lookupCache(a, cacheKey, out);
                        else if (gUseCache && a.kind == "RUN") cr = lookupImported(a, cacheKey, out);
                        if (cr != 2) {
                            if (cr < 0) { ok = false; tag = "fail"; }
                            else if (cr == 1) { cached = true; tag = "hit"; ++gCacheHit; std::lock_guard<std::mutex> pl(mu); printf("        cache hit\n"); fflush(stdout); }
                            else { tag = "miss"; ++gCacheMiss; std::lock_guard<std::mutex> pl(mu); printf("        cache miss\n"); fflush(stdout); }
                        }
                        long long ts = nowNs();
                        if (ok && !cached) {
                            JsToken tok;
                            for (;;) {
                                tok = tryTakeJob();
                                if (tok.kind != -2) break;
                                if (gCancel.load()) { tok.kind = -1; break; }
                                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                            }
                            if (tok.kind < 0) ok = false;
                            else { int rc = runCmd(argv, out, stream); ok = rc == 0; giveJob(tok); }
                        }
                        gNsSpawn += nowNs() - ts;
                        if (ok && !cached && !cacheKey.empty()) storeCache(a, cacheKey);
                        commitStep(a, out, ok, oldProduced);
                        noteStep(a.kind, a.desc, nowNs() - tStep, tag);
                    } else wouldRun[id] = 1;
                } else { std::lock_guard<std::mutex> pl(mu); ++upToDate; if (o.verbose) printf("[up to date] %s\n", a.output.c_str()); }
            } catch (const std::exception& e) { ok = false; out += string("okay-build: ") + e.what() + "\n"; }
            lk.lock(); --running; if (poolIdx[id] >= 0) --pools[poolIdx[id]].used; memUsed -= g.acts[id].memory; ++finished;
            if (!ok) { anyFail = true; if (!o.keep) stop = true; if (firstErr.empty()) firstErr = out; if (!out.empty()) { fflush(stdout); fputs(out.c_str(), stderr); } }
            else if (!stop) for (int r : a.rdeps) if (--pending[r] == 0) ready.push_back(r);
            cv.notify_all();
        }
    };
    vector<std::thread> th; for (int i = 0; i < jobs; ++i) th.emplace_back(worker);
    for (auto& t : th) t.join();
    if (!o.dry) { refreshStamps(); saveState(); }
    if (!o.dry) writeReport();
    if (gCancel.load()) return 130;
    if (anyFail) { printCache(); fprintf(stderr, "okay-build: build FAILED (%d step(s) ran, %d of %d finished)\n", ran, finished, total); return 1; }
    printCache();
    printf("okay-build: ran %d of %d steps (%d up to date)%s\n", ran, total, upToDate, o.dry ? " [dry run]" : "");
    if (o.profile) printf("okay-build: plan %lld ms, state %lld ms\n", tPlanOnly / 1000000, tState / 1000000);
    if (o.profile) printf("okay-build: profile (summed over workers, ms): dirty-check %lld, preprocess %lld, run command %lld, depfile+hash %lld; wall %lld\n", gNsDirty / 1000000, gNsPre / 1000000, gNsSpawn / 1000000, gNsPost / 1000000, (nowNs() - tStart) / 1000000);
    if (o.verbose) printf("okay-build: hashed %d input file(s), trusted size+mtime for %d\n", gHashed.load(), gTrusted.load());
    return 0;
#endif
}

static int numberBefore(const string& s, const string& word) {
    auto p = s.find(word); if (p == string::npos) return -1;
    size_t i = p; while (i > 0 && isspace((unsigned char)s[i - 1])) --i;
    size_t end = i; while (i > 0 && isdigit((unsigned char)s[i - 1])) --i;
    if (i == end) return -1;
    return atoi(s.c_str() + i);
}
static long long gTestTimeoutSec = 0;
static int printTestReport(const vector<std::pair<string, string>>& tests, const vector<int>& code, const vector<string>& outs) {
    vector<int> order(tests.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
    std::sort(order.begin(), order.end(), [&](int a, int b) { return tests[a].first < tests[b].first; });
    int failed = 0;
    fs::create_directories(under(".okaybuild"));
    std::ofstream rep(under(".okaybuild/test-report"), std::ios::trunc);
    rep << "okaybuild-test 1\n";
    for (int i : order) {
        int checks = i < (int)outs.size() ? numberBefore(outs[i], "checks") : -1;
        int bad = i < (int)outs.size() ? numberBefore(outs[i], "failed") : -1;
        string extra;
        if (checks >= 0) extra += " checks " + std::to_string(checks);
        if (bad >= 0) extra += " failed " + std::to_string(bad);
        if (code[i] == 0) { printf("PASS %s%s\n", tests[i].first.c_str(), extra.c_str()); rep << "PASS\t" << tests[i].first << "\t0\t" << checks << "\t" << bad << "\n"; }
        else { printf("FAIL %s exit %d%s\n", tests[i].first.c_str(), code[i], extra.c_str()); rep << "FAIL\t" << tests[i].first << "\t" << code[i] << "\t" << checks << "\t" << bad << "\n"; ++failed;
            if (code[i] == 124) printf("  timed out after %lld s\n", gTestTimeoutSec);
            if (i < (int)outs.size() && !outs[i].empty()) { // the last 40 lines of what the test printed
                vector<string> ls; size_t pos = 0;
                while (pos < outs[i].size()) { size_t e = outs[i].find('\n', pos); if (e == string::npos) e = outs[i].size(); ls.push_back(outs[i].substr(pos, e - pos)); pos = e + 1; }
                for (size_t k = ls.size() > 40 ? ls.size() - 40 : 0; k < ls.size(); ++k) printf("  | %s\n", ls[k].c_str());
            }
        }
    }
    printf("%zu tests, %d failed\n", tests.size(), failed);
    return failed ? 1 : 0;
}
static int executeTests(const vector<std::pair<string, string>>& tests, int jobs) {
    if (tests.empty()) return 0;
    if (jobs < 1) jobs = 1;
    vector<int> code(tests.size(), 1);
    vector<string> outs(tests.size());
#ifdef _WIN32
    if (jobs > 64) jobs = 64;
    vector<int> left; for (int i = (int)tests.size() - 1; i >= 0; --i) left.push_back(i);
    struct Slot { Live proc; int index; long long start; };
    vector<Slot> live;
    while (!left.empty() || !live.empty()) {
        while ((int)live.size() < jobs && !left.empty()) {
            int index = left.back(); left.pop_back();
            string err; Live L;
            if (!spawnLive(winJoin({tests[index].second}), "", false, L, err)) { code[index] = 127; continue; }
            live.push_back(Slot{L, index, nowNs()});
        }
        if (live.empty()) break;
        HANDLE hs[64]; int n = (int)live.size();
        for (int i = 0; i < n; ++i) { drainLive(live[i].proc); hs[i] = live[i].proc.proc; }
        if (gTestTimeoutSec > 0) for (int i = 0; i < n; ++i) if (nowNs() - live[i].start > gTestTimeoutSec * 1000000000ll) { TerminateProcess(live[i].proc.proc, 124); }
        DWORD w = WaitForMultipleObjects((DWORD)n, hs, FALSE, 20);
        for (int i = 0; i < n; ++i) drainLive(live[i].proc);
        if (w < WAIT_OBJECT_0 || w >= WAIT_OBJECT_0 + (DWORD)n) continue;
        int idx = (int)(w - WAIT_OBJECT_0);
        DWORD c = 1; GetExitCodeProcess(live[idx].proc.proc, &c);
        CloseHandle(live[idx].proc.proc); if (live[idx].proc.pipe) CloseHandle(live[idx].proc.pipe);
        code[live[idx].index] = (int)c;
        outs[live[idx].index] = live[idx].proc.out;
        live.erase(live.begin() + idx);
    }
#else
    if (jobs > (int)tests.size()) jobs = (int)tests.size();
    std::atomic<int> next{0};
    vector<std::thread> th;
    for (int t = 0; t < jobs; ++t) th.emplace_back([&] {
        for (;;) {
            int i = next++; if (i >= (int)tests.size()) return;
            string out; code[i] = runCmd({tests[i].second}, out, false, gTestTimeoutSec * 1000000000ll); outs[i] = out;
        }
    });
    for (auto& t : th) t.join();
#endif
    return printTestReport(tests, code, outs);
}
static void copyTreeFiles(const fs::path& from, const fs::path& to, const string& prefix, vector<string>& listed) {
    fs::create_directories(to);
    for (auto& e : fs::recursive_directory_iterator(from)) {
        if (!e.is_regular_file()) continue;
        fs::path rel = fs::relative(e.path(), from);
        fs::path dest = to / rel;
        fs::create_directories(dest.parent_path());
        fs::copy_file(e.path(), dest, fs::copy_options::overwrite_existing);
        string shown = prefix.empty() ? rel.generic_string() : prefix + "/" + rel.generic_string();
        listed.push_back(shown);
    }
}
static void copyInto(const fs::path& from, const fs::path& to) {
    vector<string> ignore; copyTreeFiles(from, to, "", ignore);
}
static int packageDist(const Manifest& m) {
    const DistDecl& d = m.dist;
    if (d.launcher.empty() || d.dll.empty()) throw Fail("dist needs launcher and dll");
    string root = d.dir.empty() ? "dist" : d.dir;
    std::error_code ec; fs::remove_all(root, ec); fs::create_directories(root);
    auto fileTo = [&](const string& src, const string& dest) {
        if (!fs::is_regular_file(src)) throw Fail("dist is missing '" + src + "'");
        fs::create_directories(fs::path(dest).parent_path());
        fs::copy_file(src, dest, fs::copy_options::overwrite_existing);
    };
    string dllName = fs::path(d.dll).filename().generic_string();
    fileTo(d.launcher, root + "/" + fs::path(d.launcher).filename().generic_string());
    fileTo(d.dll, root + "/" + dllName);
    if (!d.readme.empty()) fileTo(d.readme, root + "/README.txt");
    fs::create_directories(root + "/Tools");
    fileTo(d.dll, root + "/Tools/" + dllName);
    for (auto& t : d.tools) fileTo(t, root + "/Tools/" + fs::path(t).filename().generic_string());
    if (!d.version.empty()) {
        { std::ofstream f(root + "/VERSION.txt", std::ios::trunc); f << d.version << "\n"; }
        { std::ofstream f(root + "/Tools/VERSION.txt", std::ios::trunc); f << d.version << "\n"; }
    }
    if (!d.textures.empty()) copyInto(d.textures, fs::path(root) / "Assets/Textures");
    if (!d.fonts.empty()) copyInto(d.fonts, fs::path(root) / "Assets/Fonts");
    for (auto& ex : d.examples) {
        string folder = fs::path(ex).filename().generic_string();
        copyInto(ex, fs::path(root) / "examples" / folder);
    }
    printf("okay-build package: %s\n", root.c_str());
    return 0;
}
static int cmdPackage(Manifest& m, const vector<string>& want, const Opts& o) {
    if (m.dist.on) return packageDist(m);
    vector<string> names;
    for (auto& t : m.targets) {
        if (t.isLib) continue;
        if (!want.empty() && std::find(want.begin(), want.end(), t.name) == want.end()) continue;
        names.push_back(t.name);
    }
    if (names.empty()) throw Fail("package needs an exe target");
    int rc = build(m, names, o);
    if (rc != 0) return rc;
    for (auto& n : names) {
        const Target* t = nullptr; for (auto& x : m.targets) if (x.name == n) t = &x;
        string exeName = n + m.tc.exe;
        string root = "dist/" + n;
        std::error_code ec; fs::remove_all(root, ec); fs::create_directories(root);
        fs::copy_file(under(m.outdir + "/" + exeName), root + "/" + exeName, fs::copy_options::overwrite_existing);
        vector<string> listed{exeName};
        for (auto& r : t->runtime) {
            if (!fs::is_regular_file(r)) throw Fail("runtime file '" + r + "' does not exist");
            string base = fs::path(r).filename().generic_string();
            fs::copy_file(r, root + "/" + base, fs::copy_options::overwrite_existing);
            listed.push_back(base);
        }
        for (auto& a : t->assets) {
            if (!fs::is_directory(a)) throw Fail("asset folder '" + a + "' does not exist");
            copyTreeFiles(a, fs::path(root) / fs::path(a).filename(), fs::path(a).filename().generic_string(), listed);
        }
        std::sort(listed.begin(), listed.end());
        { std::ofstream f(root + "/PACKAGE.txt", std::ios::trunc); f << "okay-package 1\n"; for (auto& s : listed) f << s << "\n"; }
        printf("okay-build package: %s (%zu files)\n", root.c_str(), listed.size());
    }
    return 0;
}
static int cmdTest(Manifest& m, const Opts& o, const vector<string>& only) {
    vector<string> names;
    for (auto& t : m.targets) if (t.isTest) names.push_back(t.name);
    if (names.empty()) for (auto& t : m.targets) if (!t.isLib && t.name.compare(0, 4, "test") == 0) names.push_back(t.name);
    if (!only.empty()) { // okay-build test NAME...: run just those, any exe target will do
        names.clear();
        for (auto& n : only) {
            bool ok = false; for (auto& t : m.targets) if (t.name == n && !t.isLib) ok = true;
            if (!ok) throw Fail("no test program named '" + n + "'");
            names.push_back(n);
        }
    }
    if (names.empty()) throw Fail("no tests; set 'test = on' on an exe, or start its name with 'test'");
    int rc = build(m, names, o);
    if (rc != 0) return rc;
    vector<std::pair<string, string>> runs;
    for (auto& n : names) runs.push_back({n, under(m.outdir + "/" + n + m.tc.exe)});
    int jobs = o.jobs;
    if (jobs <= 0) {
#ifdef _WIN32
        SYSTEM_INFO info; GetSystemInfo(&info); jobs = (int)info.dwNumberOfProcessors;
#else
        jobs = (int)std::max(1u, std::thread::hardware_concurrency());
#endif
    }
    return executeTests(runs, jobs);
}

static void listTargets(const Graph& g) {
    for (auto& tf : g.targetFinal) printf("%s\t%s\n", tf.first.c_str(), g.acts[tf.second].kind == "AR" ? "lib" : "exe");
    for (auto& a : g.acts) if (a.kind == "RUN") {
        if (a.outputs.empty()) printf("action\t(always)\t%s\n", a.desc.c_str());
        else for (auto& pth : a.outputs) printf("action\t%s\n", pth.c_str());
    }
}
static void listGraph(const Graph& g) {
    for (auto& a : g.acts) {
        printf("%d\t%s\t%s\t%s\tpool=%s%s\n", a.id, a.kind.c_str(), a.desc.c_str(), a.output.c_str(), effectivePool(a).c_str(), a.restat ? "\trestat" : "");
        if (!a.deps.empty()) { printf("  deps"); for (int d : a.deps) printf(" %d", d); printf("\n"); }
        for (size_t i = 1; i < a.outputs.size(); ++i) printf("  output %s\n", a.outputs[i].c_str());
    }
}

static string jsonEsc(const string& s) { string r; for (char c : s) { if (c == '"' || c == '\\') { r += '\\'; r += c; } else if (c == '\n') r += "\\n"; else r += c; } return r; }
// An imported command is a compile when it has a -c argument. Links and archives do not.
static bool importedCompile(const Action& a, string& file) {
    if (a.kind != "RUN" || a.rawCmd.empty()) return false;
    auto w = shellWords(a.rawCmd); file.clear();
    for (size_t i = 0; i < w.size(); ++i) if (w[i] == "-c" && i + 1 < w.size()) { file = w[i + 1]; return !file.empty(); }
    return false;
}
static void writeCompdb(const Manifest& m) {
    Graph g = plan(m, {}); string dir = fs::current_path().string(); std::ofstream f("compile_commands.json"); f << "[\n"; bool first = true;
    int n = 0;
    for (auto& a : g.acts) {
        if (a.kind == "CC") {
            f << (first ? "" : ",\n") << "  {\"directory\": \"" << jsonEsc(dir) << "\", \"file\": \"" << jsonEsc(a.inputs[0]) << "\", \"arguments\": [";
            for (size_t i = 0; i < a.argv.size(); ++i) { f << (i ? ", " : "") << "\"" << jsonEsc(a.argv[i]) << "\""; }
            f << "]}"; first = false; ++n;
        } else {
            string file; if (!importedCompile(a, file)) continue;
            string where = a.dir.empty() ? dir : a.dir;
            f << (first ? "" : ",\n") << "  {\"directory\": \"" << jsonEsc(where) << "\", \"file\": \"" << jsonEsc(file) << "\", \"command\": \"" << jsonEsc(a.rawCmd) << "\"}";
            first = false; ++n;
        }
    }
    f << "\n]\n"; printf("okay-build: wrote compile_commands.json (%d compile command(s))\n", n);
}

static vector<std::pair<string, string>> buildfileSigs(const Manifest& m) {
    vector<string> paths = m.buildInputs;
    if (!m.buildDepfile.empty() && !fileMissing(m.buildDepfile)) {
        for (auto& h : parseDepfile(m.buildDepfile)) {
            string p = resolve("", h);
            if (std::find(paths.begin(), paths.end(), p) == paths.end()) paths.push_back(p);
        }
    }
    vector<std::pair<string, string>> sigs;
    for (auto& p : paths) { forgetHash(p); FileSig s; sigs.push_back({p, hashFile(p, s) ? hex64(s.hash) : string("-")}); }
    std::sort(sigs.begin(), sigs.end());
    return sigs;
}
static vector<std::pair<string, string>> loadBuildfileState() {
    vector<std::pair<string, string>> s; std::ifstream f(under(".okaybuild/buildfile")); string l;
    if (!std::getline(f, l) || l != "okaybuild-buildfile 1") return {};
    while (std::getline(f, l)) { auto t = l.find('\t'); if (t != string::npos) s.push_back({l.substr(0, t), l.substr(t + 1)}); }
    return s;
}
static void saveBuildfileState(const vector<std::pair<string, string>>& sigs) {
    fs::create_directories(under(".okaybuild")); string tmp = under(".okaybuild/buildfile.tmp");
    { std::ofstream f(tmp, std::ios::trunc); f << "okaybuild-buildfile 1\n"; for (auto& [p, h] : sigs) f << p << "\t" << h << "\n"; }
    fs::rename(tmp, under(".okaybuild/buildfile"));
}
// A buildfile block reruns its command when an input (or a depfile input) changes, then the manifest is read again.
// One build invocation regenerates at most twice. graph, targets, clean and compdb use the file as it stands.
static Manifest loadWithRegen(const string& file) {
    Manifest m = parseManifest(file);
    for (int round = 0; m.hasBuildfile; ++round) {
        if (buildfileSigs(m) == loadBuildfileState()) return m;
        if (round >= 2) throw Fail("build file regeneration did not settle");
        printf("okay-build: regenerating %s\n", fs::path(file).filename().string().c_str());
        string out; int rc = runCmd({"sh", "-c", m.buildCmd}, out);
        if (rc != 0) throw Fail("build file regeneration failed:\n" + out);
        saveBuildfileState(buildfileSigs(m));
        m = parseManifest(file);
    }
    return m;
}

// A module file is a flat list of `key = values` lines. This is the first slice of stage C:
// turn a directory of *.okaymodule files into one build.okay the existing builder already understands.
static int writeModules(const string& dir, const string& outFile) {
    if (!fs::is_directory(dir)) throw Fail("modules directory does not exist: " + dir);
    vector<fs::path> files;
    for (auto& ent : fs::directory_iterator(dir)) if (ent.path().extension() == ".okaymodule") files.push_back(ent.path());
    std::sort(files.begin(), files.end());
    if (files.empty()) throw Fail("no .okaymodule files in " + dir);
    struct Mod { string name, kind, file; vector<string> sources, includes, exps, defines, deps; };
    vector<Mod> mods;
    for (auto& f : files) {
        std::ifstream in(f); if (!in) throw Fail("cannot open " + f.string());
        Mod m; m.file = f.generic_string(); string line; int ln = 0;
        while (std::getline(in, line)) {
            ++ln; line = stripComment(line); auto t = tokens(line); if (t.empty()) continue;
            if (t.size() < 2 || t[1] != "=") throw Fail(m.file + ":" + std::to_string(ln) + ": expected key = values");
            vector<string> v(t.begin() + 2, t.end()); string k = t[0];
            auto add = [&](vector<string>& d) { d.insert(d.end(), v.begin(), v.end()); };
            if (k == "name") { if (v.size() != 1) throw Fail(m.file + ":" + std::to_string(ln) + ": name takes one value"); m.name = v[0]; }
            else if (k == "kind") { if (v.size() != 1 || (v[0] != "lib" && v[0] != "exe")) throw Fail(m.file + ":" + std::to_string(ln) + ": kind is lib or exe"); m.kind = v[0]; }
            else if (k == "sources") add(m.sources);
            else if (k == "include") add(m.includes);
            else if (k == "export_include") add(m.exps);
            else if (k == "define") add(m.defines);
            else if (k == "deps") add(m.deps);
            else throw Fail(m.file + ":" + std::to_string(ln) + ": unknown module key '" + k + "'");
        }
        if (m.name.empty()) throw Fail(m.file + ": module has no name");
        if (m.kind.empty()) throw Fail(m.file + ": module has no kind");
        if (m.sources.empty()) throw Fail(m.file + ": module has no sources");
        mods.push_back(m);
    }
    std::map<string, const Mod*> byName;
    for (auto& m : mods) {
        if (byName.count(m.name)) throw Fail("duplicate module '" + m.name + "'");
        byName[m.name] = &m;
    }
    for (auto& m : mods) for (auto& d : m.deps) {
        auto it = byName.find(d);
        if (it == byName.end()) throw Fail("module '" + m.name + "' depends on unknown module '" + d + "'");
        if (it->second->kind != "lib") throw Fail("module '" + m.name + "' depends on '" + d + "', which is not a lib");
    }
    std::ofstream out(outFile, std::ios::trunc);
    if (!out) throw Fail("cannot write " + outFile);
    out << "# generated by okay-build modules from " << fs::absolute(dir).generic_string() << "\n";
    auto lineOf = [&](const string& key, const vector<string>& v) {
        if (v.empty()) return;
        out << "  " << key << " =";
        for (auto& s : v) out << " " << qtok(s);
        out << "\n";
    };
    for (auto& m : mods) {
        out << "target " << m.name << " " << m.kind << "\n";
        lineOf("sources", m.sources); lineOf("include", m.includes); lineOf("export_include", m.exps);
        lineOf("define", m.defines); lineOf("deps", m.deps);
        out << "end\n";
    }
    printf("okay-build modules: %zu module(s) -> %s\n", mods.size(), outFile.c_str());
    return 0;
}

static bool optBoolWord(const string& v) {
    return v == "1" || v == "0" || v == "true" || v == "false" || v == "yes" || v == "no" || v == "on" || v == "off";
}
static bool optIntWord(const string& v) {
    size_t i = (!v.empty() && (v[0] == '-' || v[0] == '+')) ? 1 : 0;
    if (i >= v.size()) return false;
    for (; i < v.size(); ++i) if (!isdigit((unsigned char)v[i])) return false;
    return true;
}
static bool optTruthy(const string& type, const string& v) {
    if (type == "bool") return v == "1" || v == "true" || v == "yes" || v == "on";
    if (type == "int") return v != "0";
    return !v.empty();
}
static void checkOptValue(const OptionDecl& o, const string& v, const string& where) {
    if (o.type == "bool" && !optBoolWord(v)) throw Fail(where + ": option '" + o.name + "' is not a bool");
    if (o.type == "int" && !optIntWord(v)) throw Fail(where + ": option '" + o.name + "' is not an int");
}

// config.okay assigns the values declared in the manifest. It is the same key = value shape, not a second language.
struct FileSets { string config; bool sawConfig = false; struct Row { string name, type, value; }; vector<Row> rows; };
static FileSets readConfigOkay(const string& file) {
    std::ifstream in(file); if (!in) throw Fail("cannot open " + file);
    FileSets s; string line; int ln = 0;
    while (std::getline(in, line)) {
        ++ln; line = stripComment(line); auto t = tokens(line); if (t.empty()) continue;
        if (t[0] == "config" && t.size() >= 2 && t[1] == "=") {
            if (t.size() != 3) throw Fail(file + ":" + std::to_string(ln) + ": expected: config = NAME");
            s.config = t[2]; s.sawConfig = true; continue;
        }
        if (t[0] == "option") {
            string name, type, value;
            if (t.size() >= 4 && t[2] == "=") {
                name = t[1]; for (size_t i = 3; i < t.size(); ++i) { if (!value.empty()) value += ' '; value += t[i]; }
            } else if (t.size() >= 5 && t[3] == "=") {
                name = t[1]; type = t[2]; for (size_t i = 4; i < t.size(); ++i) { if (!value.empty()) value += ' '; value += t[i]; }
            } else throw Fail(file + ":" + std::to_string(ln) + ": expected: option NAME = VALUE");
            s.rows.push_back(FileSets::Row{name, type, value}); continue;
        }
        throw Fail(file + ":" + std::to_string(ln) + ": expected: config = NAME   or   option NAME = VALUE");
    }
    return s;
}

static void applyConfiguration(Manifest& m, const string& cliConfig, const string& configPath, bool configRequired, const vector<std::pair<string, string>>& cliOpts) {
    std::map<string, const OptionDecl*> decl;
    for (auto& o : m.options) decl[o.name] = &o;
    std::map<string, string> val;
    for (auto& o : m.options) val[o.name] = o.deflt;
    string chosen = cliConfig;
    if (!configPath.empty() && (configRequired || fs::exists(configPath))) {
        FileSets f = readConfigOkay(configPath);
        if (f.sawConfig && chosen.empty()) chosen = f.config;
        for (auto& row : f.rows) {
            auto it = decl.find(row.name);
            if (it == decl.end()) throw Fail(configPath + ": unknown option '" + row.name + "'");
            if (!row.type.empty() && row.type != it->second->type)
                throw Fail(configPath + ": option '" + row.name + "' is " + it->second->type + ", not " + row.type);
            checkOptValue(*it->second, row.value, configPath);
            val[row.name] = row.value;
        }
    }
    for (auto& kv : cliOpts) {
        auto it = decl.find(kv.first);
        if (it == decl.end()) throw Fail("unknown option '" + kv.first + "'");
        checkOptValue(*it->second, kv.second, "--opt");
        val[kv.first] = kv.second;
    }
    const ConfigBlock* cfg = nullptr;
    if (!m.configs.empty()) {
        if (chosen.empty()) cfg = &m.configs[0];
        else { for (auto& c : m.configs) if (c.name == chosen) cfg = &c; if (!cfg) throw Fail("unknown config '" + chosen + "'"); }
    } else if (!chosen.empty()) throw Fail("unknown config '" + chosen + "'");
    auto addFlags = [&](const vector<string>& flags, const vector<string>& lflags, const vector<string>& defs) {
        m.tc.flags.insert(m.tc.flags.end(), flags.begin(), flags.end());
        m.tc.linkFlags.insert(m.tc.linkFlags.end(), lflags.begin(), lflags.end());
        for (auto& d : defs) m.tc.flags.push_back("-D" + d);
    };
    if (cfg) {
        if (cfg->optimize >= 0) m.tc.flags.push_back("-O" + std::to_string(cfg->optimize));
        if (cfg->debug == 1) m.tc.flags.push_back("-g");
        addFlags(cfg->flags, cfg->linkFlags, cfg->defines);
    }
    for (auto& c : m.conds) {
        auto it = decl.find(c.opt);
        if (it == decl.end()) throw Fail("if uses unknown option '" + c.opt + "'");
        bool on = c.hasEquals ? val[c.opt] == c.equals : optTruthy(it->second->type, val[c.opt]);
        if (on) addFlags(c.flags, c.linkFlags, c.defines);
    }
}

// Toolchain probe. Directories are searched in the order given, then PATH.
// MSVC is cl.exe plus link.exe. On Windows the roots also come from VSINSTALLDIR,
// VCToolsInstallDir, and the Visual Studio SxS registry key (no vswhere). This process
// only runs that registry read when it was itself built for Windows.
static vector<string> splitEnv(const string& s) {
    vector<string> o; string cur; char sep =
#ifdef _WIN32
        ';';
#else
        ':';
#endif
    for (char c : s) { if (c == sep) { if (!cur.empty()) o.push_back(cur); cur.clear(); } else cur += c; }
    if (!cur.empty()) o.push_back(cur);
    return o;
}
static bool isRunnable(const string& p) {
    std::error_code ec; if (!fs::is_regular_file(p, ec)) return false;
#ifdef _WIN32
    return true;
#else
    return access(p.c_str(), X_OK) == 0;
#endif
}
static string findNamed(const vector<string>& dirs, std::initializer_list<const char*> names) {
    for (auto& d : dirs) for (const char* n : names) {
        string p = d; if (!p.empty() && p.back() != '/' && p.back() != '\\') p += '/'; p += n;
        if (isRunnable(p)) return p;
    }
    return "";
}
static void considerMsvcBin(const string& bin, string& cl, string& link) {
    if (cl.empty()) cl = findNamed({bin}, {"cl.exe", "cl"});
    if (link.empty()) link = findNamed({bin}, {"link.exe", "link"});
}
static void findMsvcUnder(const string& root, string& cl, string& link) {
    considerMsvcBin(root, cl, link);
    considerMsvcBin(root + "/bin/Hostx64/x64", cl, link);
    string parent = root + "/VC/Tools/MSVC";
    std::error_code ec; if (!fs::is_directory(parent, ec)) return;
    vector<string> vers;
    for (auto& e : fs::directory_iterator(parent, ec)) if (!ec && e.is_directory()) vers.push_back(e.path().generic_string());
    std::sort(vers.begin(), vers.end());
    for (auto it = vers.rbegin(); it != vers.rend(); ++it) considerMsvcBin(*it + "/bin/Hostx64/x64", cl, link);
}
#ifdef _WIN32
static string narrow(const wchar_t* w) {
    if (!w || !*w) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return "";
    string s((size_t)n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}
static vector<string> vsRootsFromRegistry() {
    vector<string> roots;
    HMODULE adv = LoadLibraryW(L"advapi32.dll");
    if (!adv) return roots;
    typedef LONG (WINAPI *OpenFn)(HKEY, LPCWSTR, DWORD, REGSAM, PHKEY);
    typedef LONG (WINAPI *EnumFn)(HKEY, DWORD, LPWSTR, LPDWORD, LPDWORD, LPDWORD, LPBYTE, LPDWORD);
    typedef LONG (WINAPI *CloseFn)(HKEY);
    auto open = (OpenFn)GetProcAddress(adv, "RegOpenKeyExW");
    auto en = (EnumFn)GetProcAddress(adv, "RegEnumValueW");
    auto closeK = (CloseFn)GetProcAddress(adv, "RegCloseKey");
    if (!open || !en || !closeK) { FreeLibrary(adv); return roots; }
    const wchar_t* keys[] = {
        L"SOFTWARE\\WOW6432Node\\Microsoft\\VisualStudio\\SxS\\VS7",
        L"SOFTWARE\\Microsoft\\VisualStudio\\SxS\\VS7"
    };
    for (auto* key : keys) {
        HKEY h = nullptr;
        if (open(HKEY_LOCAL_MACHINE, key, 0, KEY_READ | KEY_WOW64_64KEY, &h) != 0) continue;
        for (DWORD i = 0; i < 64; ++i) {
            wchar_t name[256]; DWORD nlen = 255, typ = 0, dlen = 1024; BYTE data[1024];
            if (en(h, i, name, &nlen, nullptr, &typ, data, &dlen) != 0) break;
            if (typ == REG_SZ) { string s = narrow(reinterpret_cast<wchar_t*>(data)); if (!s.empty()) roots.push_back(s); }
            nlen = 255; dlen = 1024;
        }
        closeK(h);
    }
    FreeLibrary(adv);
    return roots;
}
#else
static vector<string> vsRootsFromRegistry() { return {}; }
#endif
struct Detected { string name, cc, cxx, ld, ar, probe, host; bool found = false; };
// Looks for every preset. Directories are searched in the order given, then PATH.
static vector<Detected> detectToolchains(const vector<string>& extra, const vector<string>& roots) {
    vector<string> dirs = extra;
    const char* env = getenv("PATH");
    if (env) { auto p = splitEnv(env); dirs.insert(dirs.end(), p.begin(), p.end()); }
    vector<Detected> out;
    { Detected d; d.name = "gcc"; d.cc = findNamed(dirs, {"gcc", "gcc.exe"}); d.cxx = findNamed(dirs, {"g++", "g++.exe"});
      d.ar = findNamed(dirs, {"ar", "ar.exe"}); d.found = !d.cc.empty() && !d.cxx.empty(); out.push_back(d); }
    { Detected d; d.name = "clang"; d.cc = findNamed(dirs, {"clang", "clang.exe"}); d.cxx = findNamed(dirs, {"clang++", "clang++.exe"});
      d.ar = findNamed(dirs, {"ar", "ar.exe"}); d.found = !d.cc.empty() && !d.cxx.empty(); out.push_back(d); }
    { Detected d; d.name = "mingw"; d.cxx = findNamed(dirs, {"x86_64-w64-mingw32-g++-win32", "x86_64-w64-mingw32-g++-win32.exe", "x86_64-w64-mingw32-g++", "x86_64-w64-mingw32-g++.exe"});
      d.cc = findNamed(dirs, {"x86_64-w64-mingw32-gcc-win32", "x86_64-w64-mingw32-gcc-win32.exe", "x86_64-w64-mingw32-gcc", "x86_64-w64-mingw32-gcc.exe"});
      d.ar = findNamed(dirs, {"x86_64-w64-mingw32-ar", "x86_64-w64-mingw32-ar.exe"}); d.found = !d.cxx.empty(); out.push_back(d); }
#ifdef _WIN32
    string cl = findNamed(dirs, {"cl.exe", "cl"}), link = findNamed(dirs, {"link.exe", "link"});
#else
    // Bare "link" is a different program on this host. MSVC's tools are cl.exe and link.exe.
    string cl = findNamed(dirs, {"cl.exe"}), link = findNamed(dirs, {"link.exe"});
#endif
    vector<string> all = roots;
#ifdef _WIN32
    if (const char* e = getenv("VSINSTALLDIR")) all.push_back(e);
    if (const char* e = getenv("VCToolsInstallDir")) all.push_back(e);
    string host = "windows", probe = roots.empty() ? "registry+path" : "root+registry+path";
#else
    string host = "posix", probe = roots.empty() ? "path" : "root+path";
#endif
    for (auto& r : vsRootsFromRegistry()) all.push_back(r);
    for (auto& r : all) findMsvcUnder(r, cl, link);
    Detected d; d.name = "msvc"; d.cc = cl; d.cxx = cl; d.ld = link; d.probe = probe; d.host = host; d.found = !cl.empty() && !link.empty();
    if (d.found) { string lib = findNamed({fs::path(link).parent_path().generic_string()}, {"lib.exe"}); d.ar = lib; }
    out.push_back(d);
    return out;
}
static int cmdToolchains(const vector<string>& extra, const vector<string>& roots) {
    for (auto& d : detectToolchains(extra, roots)) {
        if (d.name == "msvc") {
            if (d.found) printf("msvc\tfound\tcc=%s\tlink=%s\tprobe=%s\thost=%s\n", d.cc.c_str(), d.ld.c_str(), d.probe.c_str(), d.host.c_str());
            else printf("msvc\tmissing\tprobe=%s\thost=%s\n", d.probe.c_str(), d.host.c_str());
        } else if (!d.found) printf("%s\tmissing\n", d.name.c_str());
        else if (d.name == "mingw") printf("mingw\tfound\tcxx=%s\n", d.cxx.c_str());
        else printf("%s\tfound\tcc=%s\tcxx=%s\n", d.name.c_str(), d.cc.c_str(), d.cxx.c_str());
    }
    return 0;
}
// 'toolchain auto' in the manifest, or --toolchain NAME on the command line: detect the compilers and fill the toolchain from the
// first one found (or the named one). Tool fields the manifest set by hand are kept.
static void resolveToolchain(Manifest& m, const string& cliName, const vector<string>& extra, const vector<string>& roots) {
    string want = !cliName.empty() ? cliName : (m.tc.autoPick ? "auto" : "");
    if (want.empty()) return;
    auto found = detectToolchains(extra, roots);
    const Detected* pick = nullptr;
    if (want == "auto") {
#ifdef _WIN32
        const char* order[] = {"msvc", "clang", "gcc", "mingw"};
#else
        const char* order[] = {"gcc", "clang", "mingw", "msvc"};
#endif
        for (const char* n : order) { for (auto& d : found) if (d.name == n && d.found) { pick = &d; break; } if (pick) break; }
        if (!pick) throw Fail("toolchain auto: no compiler found (looked for gcc, clang, mingw and msvc; try --search DIR or --msvc-root DIR)");
    } else {
        for (auto& d : found) if (d.name == want) pick = &d;
        if (!pick) throw Fail("unknown toolchain '" + want + "' (auto, gcc, clang, mingw, msvc)");
        if (!pick->found) throw Fail("toolchain " + want + " was not found on this machine (see: okay-build toolchains)");
    }
    Toolchain& c = m.tc; bool autoFlag = c.autoPick;
    Toolchain hand = c;
    applyPreset(c, pick->name); c.autoPick = autoFlag;
    auto kept = [&](const char* k) { return std::find(hand.setKeys.begin(), hand.setKeys.end(), k) != hand.setKeys.end(); };
    if (kept("cc")) c.cc = hand.cc; else if (!pick->cc.empty()) c.cc = pick->cc;
    if (kept("cxx")) c.cxx = hand.cxx; else if (!pick->cxx.empty()) c.cxx = pick->cxx;
    if (kept("ld")) c.ld = hand.ld; else if (!pick->ld.empty()) c.ld = pick->ld;
    if (kept("ar")) c.ar = hand.ar; else if (!pick->ar.empty()) c.ar = pick->ar;
}

static uint64_t hashPackageTree(const string& root) {
    vector<std::pair<string, string>> files;
    std::error_code ec;
    if (fs::is_regular_file(root, ec)) files.push_back({"", root});
    else if (fs::is_directory(root, ec)) {
        for (auto& e : fs::recursive_directory_iterator(root, ec)) {
            if (ec || !e.is_regular_file()) continue;
            files.push_back({fs::relative(e.path(), root).generic_string(), e.path().string()});
        }
    } else throw Fail("package path '" + root + "' does not exist");
    std::sort(files.begin(), files.end());
    uint64_t h = 1469598103934665603ull;
    for (auto& [rel, abs] : files) {
        h = fnv(rel.data(), rel.size(), h); h = fnv("\n", 1, h);
        std::ifstream in(abs, std::ios::binary); char buf[65536];
        while (in) { in.read(buf, sizeof buf); auto n = in.gcount(); if (n > 0) h = fnv(buf, (size_t)n, h); }
    }
    return h;
}
static int syncLock(const Manifest& m, bool writeMissing) {
    if (m.packages.empty()) return 0;
    std::map<string, const PackageDecl*> by;
    for (auto& p : m.packages) {
        if (by.count(p.name)) throw Fail("duplicate package '" + p.name + "'");
        by[p.name] = &p;
    }
    for (auto& p : m.packages) for (auto& d : p.depends) {
        auto it = by.find(d.first);
        if (it == by.end()) throw Fail("package '" + p.name + "' depends on unknown package '" + d.first + "'");
        if (it->second->version != d.second)
            throw Fail("package '" + p.name + "' needs " + d.first + " " + d.second + " but " + d.first + " is " + it->second->version);
    }
    vector<string> want;
    std::map<string, string> treeHash;
    for (auto& p : m.packages) { string hx = hex64(hashPackageTree(p.path)); treeHash[p.name] = hx; want.push_back(p.name + "\t" + p.version + "\t" + p.path + "\t" + hx); }
    std::sort(want.begin(), want.end());
    string path = "okay.lock";
    if (!fs::exists(path)) {
        if (!writeMissing) throw Fail("okay.lock is missing");
        std::ofstream f(path, std::ios::trunc); f << "okay-lock 1\n";
        for (auto& l : want) f << l << "\n";
        printf("okay-build lock: wrote %zu package(s) to okay.lock\n", want.size());
        return 0;
    }
    std::ifstream in(path); string l;
    if (!std::getline(in, l) || l != "okay-lock 1") throw Fail("okay.lock is not from this okay-build");
    std::map<string, string> got;
    while (std::getline(in, l)) {
        if (l.empty()) continue;
        auto a = l.find('\t'); auto b = a == string::npos ? string::npos : l.find('\t', a + 1);
        auto c = b == string::npos ? string::npos : l.find('\t', b + 1);
        if (a == string::npos || b == string::npos || c == string::npos) throw Fail("okay.lock has a broken line");
        got[l.substr(0, a)] = l.substr(a + 1, b - a - 1);
        string recordedPath = l.substr(b + 1, c - b - 1);
        string recordedHash = l.substr(c + 1);
        string name = l.substr(0, a);
        auto it = by.find(name);
        if (it == by.end()) throw Fail("okay.lock lists '" + name + "', which the manifest does not");
        if (it->second->path != recordedPath) throw Fail("okay.lock has " + name + " at " + recordedPath + " but the manifest says " + it->second->path);
        auto th = treeHash.find(name);
        if (th == treeHash.end() || th->second != recordedHash)
            throw Fail("package '" + name + "' files changed (okay.lock has " + recordedHash + ", the tree is " + (th == treeHash.end() ? string("missing") : th->second) + ")");
    }
    for (auto& p : m.packages) {
        auto it = got.find(p.name);
        if (it == got.end()) throw Fail("okay.lock has no entry for '" + p.name + "'");
        if (it->second != p.version) throw Fail("okay.lock has " + p.name + " " + it->second + " but the manifest resolves " + p.name + " " + p.version);
    }
    return 0;
}

static int writeNative(const string& root, const string& outFile) {
    fs::path src = fs::path(root) / "engine/src";
    if (!fs::is_directory(src)) throw Fail("native: no engine/src under " + root);
    auto rel = [&](const fs::path& p) { return fs::relative(p, root).generic_string(); };
    auto base = [](const fs::path& p) { return p.filename().generic_string(); };
    std::set<string> skip = {"ModelImportAssimp.cpp", "SteamworksService.cpp", "OwnWin32.cpp"};
    std::set<string> platform = {"Platform.cpp", "SdlBackend.cpp", "OwnLinux.cpp", "OwnLinuxX11.cpp", "OwnLinuxAudio.cpp", "OwnLinuxEvdev.cpp"};
    vector<string> crypto, plat, engine;
    for (auto& e : fs::recursive_directory_iterator(src)) {
        if (!e.is_regular_file()) continue;
        string ext = e.path().extension().generic_string();
        if (ext != ".cpp" && ext != ".c") continue;
        string name = base(e.path());
        if (skip.count(name)) continue;
        string r = rel(e.path());
        if (r.find("/Crypto/") != string::npos || r.find("\\Crypto\\") != string::npos) crypto.push_back(r);
        else if (platform.count(name)) plat.push_back(r);
        else engine.push_back(r);
    }
    fs::path ufbx = fs::path(root) / "engine/third_party/ufbx/ufbx.c";
    bool haveUfbx = fs::is_regular_file(ufbx);
    fs::path math = fs::path(root) / "tests/test_math.cpp";
    if (!fs::is_regular_file(math)) throw Fail("native: tests/test_math.cpp is missing");
    if (crypto.empty() || engine.empty()) throw Fail("native: source scan found nothing to build");
    std::sort(crypto.begin(), crypto.end());
    std::sort(plat.begin(), plat.end());
    std::sort(engine.begin(), engine.end());
    std::ofstream out(outFile, std::ios::trunc);
    if (!out) throw Fail("cannot write " + outFile);
    out << "# generated by okay-build native from the source tree (not from a CMake build)\n";
    out << "toolchain\n";
    out << "  flags = -Wall -Wextra -Wpedantic\n";
    out << "end\n";
    auto block = [&](const string& name, const string& kind, const vector<string>& srcs, const string& extra) {
        out << "target " << name << " " << kind << "\n  sources =";
        for (auto& s : srcs) out << " " << qtok(s);
        out << "\n" << extra << "end\n";
    };
    // shared/ holds headers the network and font files include (AntiCheatChecks.hpp, RobotoFontOwn.h); the CMake build adds it per file
    string sharedDir = fs::is_directory(fs::path(root) / "shared") ? " shared" : "";
    string inc =
        "  flags = -std=c++17 -O0 -g0\n"
        "  include = engine/include engine/third_party/stb engine/third_party/ufbx" + sharedDir + " /usr/include/SDL2\n"
        "  define = OKAY_HAVE_SODIUM OKAY_NET_CRYPTO OKAY_PLATFORM_HAVE_SDL2 OKAY_PLATFORM_HAVE_OWN OKAY_PLATFORM_X11_HEADERS\n";
    block("okay_crypto", "lib", crypto, "  flags = -std=c++17 -O0 -g0\n  export_include = engine/include\n");
    if (!plat.empty()) block("okay_platform", "lib", plat, inc + "  export_include = engine/include\n");
    string eng = inc + "  export_include = engine/include\n  deps = okay_crypto";
    if (!plat.empty()) eng += " okay_platform";
    if (haveUfbx) eng += " ufbx";
    eng += "\n";
    if (haveUfbx) block("ufbx", "lib", {rel(ufbx)}, "  flags = -w -O2 -std=c11\n  include = engine/third_party/ufbx\n");
    block("okay_engine", "lib", engine, eng);
    block("test_math", "exe", {rel(math)},
          "  flags = -std=c++17 -O0 -g0\n  deps = okay_engine\n  libs = sodium SDL2 dl pthread\n");
    printf("okay-build native: %zu crypto, %zu platform, %zu engine, test_math -> %s\n",
           crypto.size(), plat.size(), engine.size() + (haveUfbx ? 1 : 0), outFile.c_str());
    return 0;
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleCtrlHandler(onCtrl, TRUE);
#else
    signal(SIGINT, onInt);
#endif
    adoptJobserver();
    if (const char* e = getenv("OKAY_CACHE_DIR")) if (*e) gCacheDir = fs::absolute(e).string();
    Opts o; string toolchainName, cmd = "build", runDir, importOut = "build.okay", configName, configFile; vector<string> want;
    vector<std::pair<string, string>> optSets; vector<string> searchDirs, msvcRoots; bool configFileSet = false;
    try {
        for (int i = 1; i < argc; ++i) {
            string a = argv[i];
            if (a == "-f" && i + 1 < argc) o.file = argv[++i];
            else if (a == "-j" && i + 1 < argc) o.jobs = atoi(argv[++i]);
            else if (a.rfind("-j", 0) == 0 && a.size() > 2) o.jobs = atoi(a.c_str() + 2);
            else if (a == "--dry-run" || a == "-n") o.dry = true;
            else if (a == "-k" || a == "--keep-going") o.keep = true;
            else if (a == "--explain") o.explain = true;
            else if (a == "-v") o.verbose = true;
            else if (a == "--profile") o.profile = true;
            else if (a == "--build-dir" && i + 1 < argc) gBuildDir = argv[++i];
            else if (a == "--no-stat") gUseStat = false;
            else if (a == "--no-cache") gUseCache = false;
            else if (a == "--cache-dir" && i + 1 < argc) gCacheDir = fs::absolute(argv[++i]).string();
            else if (a == "--cache-max" && i + 1 < argc) {
                string s = argv[++i]; uint64_t n = 0; size_t k = 0;
                if (s.empty()) throw Fail("--cache-max wants a number of bytes");
                for (; k < s.size() && isdigit((unsigned char)s[k]); ++k) n = n * 10 + (uint64_t)(s[k] - '0');
                if (k == 0) throw Fail("--cache-max wants a number of bytes");
                if (k < s.size()) {
                    char u = (char)tolower((unsigned char)s[k]);
                    if (k + 1 != s.size() || (u != 'k' && u != 'm' && u != 'g')) throw Fail("--cache-max wants a number of bytes");
                    if (u == 'k') n *= 1024ull; else if (u == 'm') n *= 1024ull * 1024ull; else n *= 1024ull * 1024ull * 1024ull;
                }
                gCacheMax = n;
            }
            else if (a == "--jobserver") gWantJobserver = true;
            else if (a == "--timeout" && i + 1 < argc) { gTestTimeoutSec = atoll(argv[++i]); if (gTestTimeoutSec < 1) throw Fail("--timeout wants a number of seconds"); }
            else if (a == "--memory" && i + 1 < argc) gMemLimit = atoi(argv[++i]);
            else if (a == "--run-dir" && i + 1 < argc) runDir = argv[++i];
            else if (a == "-o" && i + 1 < argc) importOut = argv[++i];
            else if (a == "--config" && i + 1 < argc) configName = argv[++i];
            else if (a == "--config-file" && i + 1 < argc) { configFile = argv[++i]; configFileSet = true; }
            else if (a == "--opt" && i + 1 < argc) {
                string s = argv[++i]; auto eq = s.find('=');
                if (eq == string::npos || eq == 0) throw Fail("--opt wants name=value");
                optSets.push_back({s.substr(0, eq), s.substr(eq + 1)});
            }
            else if (a == "--toolchain" && i + 1 < argc) toolchainName = argv[++i];
            else if (a == "--search" && i + 1 < argc) searchDirs.push_back(argv[++i]);
            else if (a == "--msvc-root" && i + 1 < argc) msvcRoots.push_back(argv[++i]);
            else if (cmd == "cache" && (a == "stats" || a == "clean")) want.push_back(a);
            else if (a == "build" || a == "clean" || a == "compdb" || a == "import" || a == "graph" || a == "targets" || a == "modules" || a == "toolchains" || a == "report" || a == "test" || a == "cache" || a == "lock" || a == "package" || a == "native") cmd = a;
            else if (a == "-h" || a == "--help") {
                puts("usage: okay-build [build|clean|compdb|import|graph|targets|modules|toolchains|report|test] [-f build.okay] [-j N] [-k] [--config NAME] [--toolchain auto|gcc|clang|mingw|msvc] [--opt name=value] [--build-dir DIR] [--no-stat] [--no-cache] [--cache-dir DIR] [--timeout SECONDS] [--dry-run] [--explain] [-v] [--profile] [target...]");
                puts("audit: docs/own_build_plan.md (12 subsystems; this tool is off by default)");
                return 0;
            }
            else if (!a.empty() && a[0] == '-') throw Fail("unknown option " + a);
            else want.push_back(a);
        }
        if (cmd == "toolchains") return cmdToolchains(searchDirs, msvcRoots);
        if (cmd == "native") {
            if (want.empty()) throw Fail("usage: okay-build native <repo root> [-o build.okay]");
            return writeNative(want[0], importOut);
        }
        if (cmd == "modules") {
            if (want.empty()) throw Fail("usage: okay-build modules <directory> [-o build.okay]");
            return writeModules(want[0], importOut);
        }
        if (cmd == "import") { // okay-build import BUILD_DIR [--run-dir DIR] [-o build.okay] [ninja targets...]
            if (want.empty()) throw Fail("usage: okay-build import <cmake build dir> [--run-dir DIR] [-o FILE] [ninja targets...]");
            string bd = want[0]; want.erase(want.begin()); return importCmake(bd, runDir, importOut, want);
        }
        if (gBuildDir != ".") gBuildDir = fs::absolute(gBuildDir).string();
        fs::path mp = fs::absolute(o.file); fs::current_path(mp.parent_path());
        Manifest m = cmd == "build" ? loadWithRegen(mp.string()) : parseManifest(mp.string());
        if (!configFileSet) configFile = "config.okay";
        if (cmd == "build" || cmd == "test" || cmd == "package" || cmd == "graph" || cmd == "compdb") resolveToolchain(m, toolchainName, searchDirs, msvcRoots);
        applyConfiguration(m, configName, configFile, configFileSet, optSets);
        if (cmd == "cache") return cmdCache(want.empty() ? "" : want[0]);
        if (cmd == "lock") return syncLock(m, true);
        if (cmd == "report") {
            if (!want.empty() && want[0] == "history") return showHistory();
            if (want.size() >= 3 && want[0] == "diff") return diffReports(want[1], want[2]);
            return showReport(!want.empty() && want[0] == "json");
        }
        if (cmd == "build" || cmd == "test" || cmd == "package") syncLock(m, true);
        if (cmd == "package") return cmdPackage(m, want, o);
        if (cmd == "test") return cmdTest(m, o, want);
        if (cmd == "clean") {
            Graph g = plan(m, {}); for (auto& a : g.acts) if (a.kind != "CC") for (auto& pth : a.outputs) fs::remove(pth);
            fs::remove_all(under(".okaybuild")); fs::remove("compile_commands.json"); printf("okay-build: cleaned\n"); return 0;
        }
        if (cmd == "compdb") { writeCompdb(m); return 0; }
        if (cmd == "targets") { listTargets(plan(m, {})); return 0; }
        if (cmd == "graph") { listGraph(plan(m, want)); return 0; }
        return build(m, want, o);
    } catch (const Fail& e) { fprintf(stderr, "okay-build: error: %s\n", e.what()); return 2; }
    catch (const std::exception& e) { fprintf(stderr, "okay-build: internal error: %s\n", e.what()); return 3; }
}
