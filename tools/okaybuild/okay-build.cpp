// okay-build: prototype of our own build tool (docs/own_build_plan.md). OFF by default, dev tool only.
// Written from general knowledge of how build systems work (action graph, compiler-made depfiles,
// content hashes, a ready queue with worker threads). No code from any other build tool was used.
// POSIX only (fork/exec). Not built by CMake or CI.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <sys/stat.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>
#include <vector>

namespace fs = std::filesystem;
using std::string;
using std::vector;

struct Fail : std::runtime_error { using std::runtime_error::runtime_error; };

// ---------------------------------------------------------------- manifest
struct Toolchain {
    string name = "gcc", cc = "gcc", cxx = "g++", ar = "ar", obj = ".o", exe, libp = "lib", libs = ".a";
    vector<string> flags, linkFlags; size_t rspThreshold = 30000;
};
struct RawAction { string dir, cmd, output, depfile; vector<string> inputs, after; int line = 0; };
struct Target {
    string name; bool isLib = false; int line = 0;
    vector<string> sources, includes, exportIncludes, defines, flags, linkFlags, libs, deps;
};
struct Manifest { Toolchain tc; string outdir = "build"; vector<Target> targets; vector<RawAction> actions; };

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

static Manifest parseManifest(const string& file) {
    std::ifstream in(file);
    if (!in) throw Fail("cannot open manifest " + file);
    Manifest m; string line; int ln = 0;
    enum { TOP, TC, TARGET, ACTION } mode = TOP;
    auto err = [&](const string& msg) { return Fail(file + ":" + std::to_string(ln) + ": " + msg); };
    while (std::getline(in, line)) {
        ++ln;
        auto h = line.find('#'); if (h != string::npos) line.resize(h);
        auto t = tokens(line); if (t.empty()) continue;
        if (mode == ACTION && t[0] == "cmd") { // raw rest of line, kept exactly
            auto p = line.find('='); if (p == string::npos) throw err("expected: cmd = command");
            size_t b = line.find_first_not_of(" \t", p + 1); m.actions.back().cmd = b == string::npos ? "" : line.substr(b);
            while (!m.actions.back().cmd.empty() && isspace((unsigned char)m.actions.back().cmd.back())) m.actions.back().cmd.pop_back();
            continue;
        }
        if (t[0] == "end") { if (mode == TOP) throw err("'end' outside a block"); mode = TOP; continue; }
        if (mode == TOP) {
            if (t[0] == "outdir" && t.size() == 3 && t[1] == "=") { m.outdir = t[2]; continue; }
            if (t[0] == "toolchain") {
                mode = TC; Toolchain& c = m.tc;
                if (t.size() > 1) {
                    c.name = t[1];
                    if (t[1] == "gcc") {}
                    else if (t[1] == "clang") { c.cc = "clang"; c.cxx = "clang++"; }
                    else if (t[1] == "mingw") { c.cc = "x86_64-w64-mingw32-gcc-win32"; c.cxx = "x86_64-w64-mingw32-g++-win32";
                                                c.ar = "x86_64-w64-mingw32-ar"; c.exe = ".exe"; }
                    else throw err("unknown toolchain preset '" + t[1] + "' (gcc, clang, mingw)");
                }
                continue;
            }
            if (t[0] == "action") { m.actions.push_back(RawAction()); m.actions.back().line = ln; mode = ACTION; continue; }
            if (t[0] == "target") {
                if (t.size() != 3 || (t[2] != "exe" && t[2] != "lib")) throw err("expected: target NAME exe|lib");
                for (auto& x : m.targets) if (x.name == t[1]) throw err("duplicate target '" + t[1] + "'");
                Target x; x.name = t[1]; x.isLib = t[2] == "lib"; x.line = ln; m.targets.push_back(x); mode = TARGET; continue;
            }
            throw err("unexpected '" + t[0] + "' (expected toolchain, target or outdir)");
        }
        if (t.size() < 2 || t[1] != "=") throw err("expected: key = values");
        vector<string> v(t.begin() + 2, t.end()); string k = t[0];
        auto add = [&](vector<string>& d) { d.insert(d.end(), v.begin(), v.end()); };
        if (mode == TC) {
            Toolchain& c = m.tc; auto one = [&](string& d) { if (v.size() != 1 && !(v.empty() && (k == "exe_suffix" || k == "lib_prefix"))) throw err(k + " takes one value"); d = v.empty() ? "" : v[0]; };
            if (k == "cc") one(c.cc); else if (k == "cxx") one(c.cxx); else if (k == "ar") one(c.ar);
            else if (k == "obj_suffix") one(c.obj); else if (k == "exe_suffix") one(c.exe);
            else if (k == "lib_prefix") one(c.libp); else if (k == "lib_suffix") one(c.libs);
            else if (k == "flags") add(c.flags); else if (k == "link_flags") add(c.linkFlags);
            else if (k == "rsp_threshold") { if (v.size() != 1) throw err("rsp_threshold takes one value"); c.rspThreshold = (size_t)atoll(v[0].c_str()); }
            else throw err("unknown toolchain key '" + k + "'");
        } else if (mode == ACTION) {
            RawAction& x = m.actions.back();
            if (k == "dir") { if (v.size() != 1) throw err("dir takes one value"); x.dir = v[0]; }
            else if (k == "output") { if (v.size() != 1) throw err("output takes one value"); x.output = v[0]; }
            else if (k == "depfile") { if (v.size() != 1) throw err("depfile takes one value"); x.depfile = v[0]; }
            else if (k == "inputs") add(x.inputs); else if (k == "after") add(x.after);
            else throw err("unknown action key '" + k + "'");
        } else {
            Target& x = m.targets.back();
            if (k == "sources") add(x.sources); else if (k == "include") add(x.includes);
            else if (k == "export_include") add(x.exportIncludes); else if (k == "define") add(x.defines);
            else if (k == "flags") add(x.flags); else if (k == "link_flags") add(x.linkFlags);
            else if (k == "libs") add(x.libs); else if (k == "deps") add(x.deps);
            else throw err("unknown target key '" + k + "'");
        }
    }
    if (mode != TOP) throw Fail(file + ": missing 'end' at end of file");
    for (auto& a : m.actions) if (a.cmd.empty()) throw Fail(file + ":" + std::to_string(a.line) + ": action has no cmd");
    return m;
}

// ---------------------------------------------------------------- hashing + stat shortcut
struct FileSig { string hash; long long size = 0, mtime = 0, at = 0; };
static string hex64(uint64_t h) { char b[17]; snprintf(b, sizeof b, "%016llx", (unsigned long long)h); return b; }
static uint64_t fnv(const char* p, size_t n, uint64_t h = 1469598103934665603ull) {
    for (size_t i = 0; i < n; ++i) { h ^= (unsigned char)p[i]; h *= 1099511628211ull; }
    return h;
}
static std::mutex gHashMu; static std::unordered_map<string, FileSig> gCache, gPrev; // this run / loaded from state
static std::atomic<int> gHashed{0}, gTrusted{0};
static bool gUseStat = true;
// Shortcut: if size and mtime equal the recorded ones, and the file was already 2 s older than when it was recorded
// (so a later same-timestamp edit is impossible), reuse the recorded hash and do not read the file.
static bool hashFile(const string& p, FileSig& out) {
    FileSig prev; bool havePrev = false;
    { std::lock_guard<std::mutex> l(gHashMu); auto it = gCache.find(p); if (it != gCache.end()) { out = it->second; return true; }
      auto pv = gPrev.find(p); if (pv != gPrev.end()) { prev = pv->second; havePrev = true; } }
    struct stat st; if (::stat(p.c_str(), &st) != 0) return false; // one system call instead of two filesystem calls
    FileSig s; s.size = (long long)st.st_size; s.mtime = (long long)st.st_mtim.tv_sec * 1000000000ll + st.st_mtim.tv_nsec;
    if (gUseStat && havePrev && prev.size == s.size && prev.mtime == s.mtime && prev.mtime + 2000000000ll < prev.at) {
        s = prev; ++gTrusted;
    } else {
        std::ifstream f(p, std::ios::binary); if (!f) return false;
        uint64_t h = 1469598103934665603ull; char buf[65536];
        while (f.read(buf, sizeof buf) || f.gcount()) { h = fnv(buf, (size_t)f.gcount(), h); if (f.eof()) break; }
        s.hash = hex64(h); { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); s.at = (long long)ts.tv_sec * 1000000000ll + ts.tv_nsec; } ++gHashed;
    }
    out = s; std::lock_guard<std::mutex> l(gHashMu); gCache[p] = s; return true;
}
static void forgetHash(const string& p) { std::lock_guard<std::mutex> l(gHashMu); gCache.erase(p); }
static string hashStr(const string& s) { return hex64(fnv(s.data(), s.size())); }
static string hashArgv(const vector<string>& a) { uint64_t h = 1469598103934665603ull; for (auto& s : a) { h = fnv(s.data(), s.size(), h); h = fnv("\0", 1, h); } return hex64(h); }

// ---------------------------------------------------------------- graph
struct Action {
    int id = 0; string kind, desc, output, depfile, dir, rawCmd; vector<string> argv, inputs; vector<int> deps, rdeps;
    string target; size_t rspHead = 0, rspThreshold = 0; string cmdHash;
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
    auto add = [&](Action a) { a.id = (int)g.acts.size(); g.acts.push_back(a); return a.id; };
    for (auto& n : order) {
        const Target& t = *byName[n]; vector<string> libs = transitive(t);
        vector<string> objs; vector<int> objActs;
        for (auto& s : t.sources) {
            if (!fs::exists(s)) throw Fail("target '" + t.name + "': source '" + s + "' does not exist");
            bool isC = fs::path(s).extension() == ".c";
            string rel = fs::path(s).lexically_normal().generic_string();
            for (size_t p; (p = rel.find("..")) != string::npos;) rel.replace(p, 2, "__");
            Action a; a.kind = "CC"; a.target = t.name; a.desc = s;
            a.output = under(".okaybuild/obj/" + t.name + "/" + rel + m.tc.obj); a.depfile = a.output + ".d"; a.inputs = {s};
            a.argv.push_back(isC ? m.tc.cc : m.tc.cxx);
            for (auto& f : m.tc.flags) a.argv.push_back(f);
            for (auto& f : t.flags) a.argv.push_back(f);
            for (auto& i : t.includes) a.argv.push_back("-I" + i);
            for (auto& i : t.exportIncludes) a.argv.push_back("-I" + i);
            for (auto& l : libs) for (auto& i : byName[l]->exportIncludes) a.argv.push_back("-I" + i);
            for (auto& d : t.defines) a.argv.push_back("-D" + d);
            for (auto& x : {string("-MMD"), string("-MF"), a.depfile, string("-MT"), a.output, string("-c"), s, string("-o"), a.output}) a.argv.push_back(x);
            objs.push_back(a.output); objActs.push_back(add(a));
        }
        Action a; a.target = t.name; a.output = outPath(t); a.deps = objActs; a.inputs = objs; a.desc = a.output; a.rspThreshold = m.tc.rspThreshold;
        if (t.isLib) { a.kind = "AR"; a.argv = {m.tc.ar, "rcs", a.output}; a.rspHead = 3; for (auto& o : objs) a.argv.push_back(o); }
        else {
            a.kind = "LD"; a.argv = {m.tc.cxx}; a.rspHead = 1; for (auto& o : objs) a.argv.push_back(o);
            for (auto& f : m.tc.linkFlags) a.argv.push_back(f);
            for (auto& f : t.linkFlags) a.argv.push_back(f);
            vector<string> sys = t.libs;
            for (auto& l : libs) { a.argv.push_back(outPath(*byName[l])); a.inputs.push_back(outPath(*byName[l])); a.deps.push_back(finalAct[l]);
                                   for (auto& s : byName[l]->libs) sys.push_back(s); }
            for (auto& s : sys) a.argv.push_back("-l" + s);
            a.argv.push_back("-o"); a.argv.push_back(a.output);
        }
        finalAct[n] = add(a); g.targetFinal.push_back({n, finalAct[n]});
    }
    // raw actions (imported commands): run as `cd DIR && CMD` through sh, edges inferred from inputs/outputs
    std::map<string, int> byOut, byRawName;
    for (auto& r : m.actions) {
        Action a; a.kind = "RUN"; a.dir = r.dir; a.rawCmd = r.cmd; a.desc = r.output.empty() ? r.cmd.substr(0, 60) : r.output;
        a.output = r.output.empty() ? "" : resolve(r.dir, r.output); a.depfile = r.depfile.empty() ? "" : resolve(r.dir, r.depfile);
        for (auto& i : r.inputs) a.inputs.push_back(resolve(r.dir, i));
        string full = (r.dir.empty() ? "" : "cd " + quote(r.dir) + " && ") + r.cmd;
        a.argv = {"sh", "-c", full};
        int id = add(a); if (!a.output.empty()) { byOut[a.output] = id; byRawName[r.output] = id; byRawName[a.output] = id; }
    }
    for (size_t i = 0; i < m.actions.size(); ++i) {
        Action& a = g.acts[g.acts.size() - m.actions.size() + i];
        for (auto& in : a.inputs) { auto it = byOut.find(in); if (it != byOut.end() && it->second != a.id) a.deps.push_back(it->second); }
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
struct Rec { string cmd; vector<std::pair<string, FileSig>> inputs; };
static std::map<string, Rec> gState; static std::mutex gStateMu;
static string statePath() { return under(".okaybuild/state"); }
static void loadState() {
    std::ifstream f(statePath(), std::ios::binary); string l; Rec* cur = nullptr;
    if (!std::getline(f, l) || l != "okaybuild-state 3") return; // unknown/missing: rebuild everything
    gPrev.reserve(4096);
    while (std::getline(f, l)) {
        const char* p = l.c_str(); const char* e = p + l.size(); const char* fld[6]; int n = 0; fld[n++] = p;
        for (const char* q = p; q < e && n < 6; ++q) if (*q == '\t') fld[n++] = q + 1;
        auto len = [&](int i) { return (size_t)((i + 1 < n ? fld[i + 1] - 1 : e) - fld[i]); };
        if (n == 3 && l[0] == 'O') cur = &(gState[string(fld[1], len(1))] = Rec{string(fld[2], len(2)), {}});
        else if (n == 6 && l[0] == 'I' && cur) { FileSig s; s.hash.assign(fld[2], len(2)); s.size = atoll(fld[3]); s.mtime = atoll(fld[4]); s.at = atoll(fld[5]);
                                                   string path(fld[1], len(1)); cur->inputs.push_back({path, s}); gPrev[path] = s; }
    }
}
static void saveState() {
    fs::create_directories(under(".okaybuild")); string tmp = statePath() + ".tmp";
    { std::ofstream f(tmp, std::ios::trunc); f << "okaybuild-state 3\n";
      for (auto& [o, r] : gState) { f << "O\t" << o << "\t" << r.cmd << "\n"; for (auto& [p, s] : r.inputs) f << "I\t" << p << "\t" << s.hash << "\t" << s.size << "\t" << s.mtime << "\t" << s.at << "\n"; } }
    fs::rename(tmp, statePath());
}

static bool dirty(const Action& a, string& why) {
    if (a.output.empty()) { why = "no declared output (always runs)"; return true; }
    if (!fs::exists(a.output)) { why = "output " + a.output + " is missing"; return true; }
    const Rec* rp; { std::lock_guard<std::mutex> l(gStateMu); auto it = gState.find(a.output); if (it == gState.end()) { why = "no record in state file (never built by okay-build)"; return true; } rp = &it->second; } // map nodes are stable
    const Rec& r = *rp;
    if (r.cmd != a.cmdHash) { why = "command line changed"; return true; }
    std::set<string> have; for (auto& [p, h] : r.inputs) have.insert(p);
    for (auto& i : a.inputs) if (!have.count(i)) { why = "new input " + i; return true; }
    for (auto& [p, h] : r.inputs) {
        FileSig now; if (!hashFile(p, now)) { why = "input " + p + " is gone"; return true; }
        if (now.hash != h.hash) { why = "input " + p + " changed"; return true; }
    }
    return false;
}

// ---------------------------------------------------------------- running commands
static int runCmd(const vector<string>& argv, string& out) {
    int fd[2]; if (pipe(fd)) return 127;
    pid_t pid = fork();
    if (pid == 0) {
        dup2(fd[1], 1); dup2(fd[1], 2); close(fd[0]); close(fd[1]);
        vector<char*> av; for (auto& s : argv) av.push_back(const_cast<char*>(s.c_str())); av.push_back(nullptr);
        execvp(av[0], av.data()); dprintf(2, "okay-build: cannot run '%s': %s\n", av[0], strerror(errno)); _exit(127);
    }
    close(fd[1]); char b[4096]; ssize_t n; while ((n = read(fd[0], b, sizeof b)) > 0) out.append(b, (size_t)n);
    close(fd[0]); int st = 0; waitpid(pid, &st, 0); return WIFEXITED(st) ? WEXITSTATUS(st) : 128;
}

static std::atomic<long long> gNsDirty{0}, gNsSpawn{0}, gNsPost{0}, gNsPrint{0};
static long long nowNs() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
struct Opts { int jobs = 0; bool dry = false, explain = false, verbose = false, profile = false; string file = "build.okay"; };

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
        else if (hasO) { p.kind = "link"; for (auto& x : w) if (x != p.output && (endsWith(x, ".o") || endsWith(x, ".a") || endsWith(x, ".so"))) p.inputs.push_back(x); ++nLink; }
        else { // archive: "... ar qc OUT.a objs..." (no -o)
            size_t ar = w.size(); for (size_t i = 0; i < w.size(); ++i) if (fs::path(w[i]).filename() == "ar") ar = i;
            if (ar < w.size()) for (size_t i = ar + 1; i < w.size(); ++i) if (endsWith(w[i], ".a")) { p.output = w[i]; for (size_t j = i + 1; j < w.size(); ++j) if (endsWith(w[j], ".o")) p.inputs.push_back(w[j]); break; }
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

static int build(const Manifest& m, const vector<string>& want, const Opts& o) {
    long long tStart = nowNs(); Graph g = plan(m, want); loadState();
    long long tPlan = nowNs() - tStart;
    int total = (int)g.acts.size(), ran = 0, upToDate = 0; bool failed = false; int running = 0, finished = 0;
    vector<int> pending(total); vector<char> wouldRun(total, 0); std::vector<int> ready;
    for (auto& a : g.acts) { pending[a.id] = (int)a.deps.size(); if (a.deps.empty()) ready.push_back(a.id); }
    std::mutex mu; std::condition_variable cv; string firstErr;
    int jobs = o.jobs > 0 ? o.jobs : (int)std::max(1u, std::thread::hardware_concurrency());
    auto worker = [&] {
        std::unique_lock<std::mutex> lk(mu);
        for (;;) {
            cv.wait(lk, [&] { return !ready.empty() || running == 0; });
            if (ready.empty() || failed) { if (running == 0) { cv.notify_all(); return; } if (failed) { cv.wait(lk, [&] { return running == 0; }); return; } continue; }
            int id = ready.back(); ready.pop_back(); ++running; const Action& a = g.acts[id]; lk.unlock();
            string why; long long t0 = nowNs(); bool d = dirty(a, why); gNsDirty += nowNs() - t0;
            if (!d && o.dry) for (int dep : a.deps) if (wouldRun[dep]) { d = true; why = "input " + g.acts[dep].output + " would be rebuilt"; }
            bool ok = true; string out;
            if (d) {
                { std::lock_guard<std::mutex> pl(mu); ++ran; printf("[%d/%d] %s %s\n", ran, total, a.kind.c_str(), a.desc.c_str());
                  if (o.explain) { printf("        because: %s\n", why.c_str()); }
                  if (o.verbose || o.dry) { printf("        %s\n", joined(a.argv).c_str()); }
                  fflush(stdout); }
                if (!o.dry) {
                    if (!a.output.empty()) fs::create_directories(fs::path(a.output).parent_path());
                    if (a.kind == "AR") fs::remove(a.output);
                    vector<string> argv = a.argv;
                    if (a.rspHead > 0 && joined(a.argv).size() > a.rspThreshold) { // long line: put the arguments in a response file
                        string rf = under(".okaybuild/rsp/" + hashStr(a.output) + ".rsp"); fs::create_directories(fs::path(rf).parent_path());
                        std::ofstream r(rf, std::ios::trunc);
                        for (size_t i = a.rspHead; i < a.argv.size(); ++i) { for (char c : a.argv[i]) { if (c == '\\' || c == '"' || c == ' ') r << '\\'; r << c; } r << '\n'; }
                        argv.resize(a.rspHead); argv.push_back("@" + rf);
                    }
                    long long ts = nowNs(); int rc = runCmd(argv, out); ok = rc == 0; gNsSpawn += nowNs() - ts; ts = nowNs();
                    if (ok && !a.output.empty()) {
                        Rec r; r.cmd = a.cmdHash; vector<string> ins = a.inputs;
                        if (!a.depfile.empty()) for (auto& h : parseDepfile(a.depfile)) { string n = resolve(a.dir, h); if (std::find(ins.begin(), ins.end(), n) == ins.end()) ins.push_back(n); }
                        forgetHash(a.output);
                        for (auto& p : ins) { FileSig h; if (!hashFile(p, h)) { ok = false; out += "okay-build: cannot hash input " + p + "\n"; break; } r.inputs.push_back({p, h}); }
                        if (ok) { std::lock_guard<std::mutex> sl(gStateMu); gState[a.output] = r; }
                        gNsPost += nowNs() - ts;
                    } else out = "okay-build: FAILED: " + joined(a.argv) + "\n" + out;
                } else wouldRun[id] = 1;
            } else { std::lock_guard<std::mutex> pl(mu); ++upToDate; if (o.verbose) printf("[up to date] %s\n", a.output.c_str()); }
            lk.lock(); --running; ++finished;
            if (!ok) { if (!failed) firstErr = out; failed = true; if (!out.empty()) { fflush(stdout); fputs(out.c_str(), stderr); } }
            else if (!failed) for (int r : a.rdeps) if (--pending[r] == 0) ready.push_back(r);
            cv.notify_all();
        }
    };
    vector<std::thread> th; for (int i = 0; i < jobs; ++i) th.emplace_back(worker);
    for (auto& t : th) t.join();
    if (!o.dry) saveState();
    if (failed) { fprintf(stderr, "okay-build: build FAILED (%d step(s) ran, %d of %d finished)\n", ran, finished, total); return 1; }
    printf("okay-build: ran %d of %d steps (%d up to date)%s\n", ran, total, upToDate, o.dry ? " [dry run]" : "");
    if (o.profile) printf("okay-build: plan+state load %lld ms\n", tPlan / 1000000);
    if (o.profile) printf("okay-build: profile (summed over workers, ms): dirty-check %lld, run command %lld, depfile+hash %lld; wall %lld\n", gNsDirty / 1000000, gNsSpawn / 1000000, gNsPost / 1000000, (nowNs() - tStart) / 1000000);
    if (o.verbose) printf("okay-build: hashed %d input file(s), trusted size+mtime for %d\n", gHashed.load(), gTrusted.load());
    return 0;
}

static string jsonEsc(const string& s) { string r; for (char c : s) { if (c == '"' || c == '\\') { r += '\\'; r += c; } else if (c == '\n') r += "\\n"; else r += c; } return r; }
static void writeCompdb(const Manifest& m) {
    Graph g = plan(m, {}); string dir = fs::current_path().string(); std::ofstream f("compile_commands.json"); f << "[\n"; bool first = true;
    for (auto& a : g.acts) if (a.kind == "CC") {
        f << (first ? "" : ",\n") << "  {\"directory\": \"" << jsonEsc(dir) << "\", \"file\": \"" << jsonEsc(a.inputs[0]) << "\", \"arguments\": [";
        for (size_t i = 0; i < a.argv.size(); ++i) { f << (i ? ", " : "") << "\"" << jsonEsc(a.argv[i]) << "\""; }
        f << "]}"; first = false;
    }
    f << "\n]\n"; printf("okay-build: wrote compile_commands.json\n");
}

int main(int argc, char** argv) {
    Opts o; string cmd = "build", runDir, importOut = "build.okay"; vector<string> want;
    try {
        for (int i = 1; i < argc; ++i) {
            string a = argv[i];
            if (a == "-f" && i + 1 < argc) o.file = argv[++i];
            else if (a == "-j" && i + 1 < argc) o.jobs = atoi(argv[++i]);
            else if (a.rfind("-j", 0) == 0 && a.size() > 2) o.jobs = atoi(a.c_str() + 2);
            else if (a == "--dry-run" || a == "-n") o.dry = true;
            else if (a == "--explain") o.explain = true;
            else if (a == "-v") o.verbose = true;
            else if (a == "--profile") o.profile = true;
            else if (a == "--build-dir" && i + 1 < argc) gBuildDir = argv[++i];
            else if (a == "--no-stat") gUseStat = false;
            else if (a == "--run-dir" && i + 1 < argc) runDir = argv[++i];
            else if (a == "-o" && i + 1 < argc) importOut = argv[++i];
            else if (a == "build" || a == "clean" || a == "compdb" || a == "import") cmd = a;
            else if (a == "-h" || a == "--help") { puts("usage: okay-build [build|clean|compdb|import] [-f build.okay] [-j N] [--build-dir DIR] [--no-stat] [--dry-run] [--explain] [-v] [target...]"); return 0; }
            else if (!a.empty() && a[0] == '-') throw Fail("unknown option " + a);
            else want.push_back(a);
        }
        if (cmd == "import") { // okay-build import BUILD_DIR [--run-dir DIR] [-o build.okay] [ninja targets...]
            if (want.empty()) throw Fail("usage: okay-build import <cmake build dir> [--run-dir DIR] [-o FILE] [ninja targets...]");
            string bd = want[0]; want.erase(want.begin()); return importCmake(bd, runDir, importOut, want);
        }
        if (gBuildDir != ".") gBuildDir = fs::absolute(gBuildDir).string();
        fs::path mp = fs::absolute(o.file); Manifest m = parseManifest(mp.string()); fs::current_path(mp.parent_path());
        if (cmd == "clean") {
            Graph g = plan(m, {}); for (auto& a : g.acts) if (a.kind != "CC" && !a.output.empty()) fs::remove(a.output);
            fs::remove_all(under(".okaybuild")); fs::remove("compile_commands.json"); printf("okay-build: cleaned\n"); return 0;
        }
        if (cmd == "compdb") { writeCompdb(m); return 0; }
        return build(m, want, o);
    } catch (const Fail& e) { fprintf(stderr, "okay-build: error: %s\n", e.what()); return 2; }
    catch (const std::exception& e) { fprintf(stderr, "okay-build: internal error: %s\n", e.what()); return 3; }
}
