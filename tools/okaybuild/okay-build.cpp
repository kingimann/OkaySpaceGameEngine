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
#include <cerrno>
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
#include <fcntl.h>
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
struct PoolDecl { string name; int depth = 1; int line = 0; };
struct RawAction {
    string dir, cmd, output, depfile, pool; vector<string> inputs, after, outputs; bool restat = false; int line = 0;
};
struct Target {
    string name; bool isLib = false; int line = 0; string pool;
    vector<string> sources, includes, exportIncludes, defines, flags, linkFlags, libs, deps, needs;
};
struct Manifest {
    Toolchain tc; string outdir = "build"; vector<Target> targets; vector<RawAction> actions; vector<PoolDecl> pools;
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
    enum { TOP, TC, TARGET, ACTION, POOL, BUILDFILE } mode = TOP;
    auto err = [&](const string& msg) { return Fail(file + ":" + std::to_string(ln) + ": " + msg); };
    auto oneFlag = [&](const vector<string>& v, const string& k) {
        if (v.size() != 1) throw err(k + " takes one value");
        if (v[0] == "1" || v[0] == "true" || v[0] == "yes") return true;
        if (v[0] == "0" || v[0] == "false" || v[0] == "no") return false;
        throw err(k + " is 1 or 0");
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
                    c.name = t[1];
                    if (t[1] == "gcc") {}
                    else if (t[1] == "clang") { c.cc = "clang"; c.cxx = "clang++"; }
                    else if (t[1] == "mingw") { c.cc = "x86_64-w64-mingw32-gcc-win32"; c.cxx = "x86_64-w64-mingw32-g++-win32";
                                                c.ar = "x86_64-w64-mingw32-ar"; c.exe = ".exe"; }
                    else throw err("unknown toolchain preset '" + t[1] + "' (gcc, clang, mingw)");
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
            if (t[0] == "target") {
                if (t.size() != 3 || (t[2] != "exe" && t[2] != "lib")) throw err("expected: target NAME exe|lib");
                for (auto& x : m.targets) if (x.name == t[1]) throw err("duplicate target '" + t[1] + "'");
                Target x; x.name = t[1]; x.isLib = t[2] == "lib"; x.line = ln; m.targets.push_back(x); mode = TARGET; continue;
            }
            throw err("unexpected '" + t[0] + "' (expected toolchain, target, pool, action, buildfile or outdir)");
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
            else if (k == "inputs") add(x.inputs); else if (k == "after") add(x.after);
            else throw err("unknown action key '" + k + "'");
        } else if (mode == BUILDFILE) {
            if (k == "inputs") add(m.buildInputs);
            else if (k == "depfile") { if (v.size() != 1) throw err("depfile takes one value"); m.buildDepfile = v[0]; }
            else throw err("unknown buildfile key '" + k + "'");
        } else {
            Target& x = m.targets.back();
            if (k == "sources") add(x.sources); else if (k == "include") add(x.includes);
            else if (k == "export_include") add(x.exportIncludes); else if (k == "define") add(x.defines);
            else if (k == "flags") add(x.flags); else if (k == "link_flags") add(x.linkFlags);
            else if (k == "libs") add(x.libs); else if (k == "deps") add(x.deps);
            else if (k == "needs") add(x.needs);
            else if (k == "pool") { if (v.size() != 1) throw err("pool takes one value"); x.pool = v[0]; }
            else throw err("unknown target key '" + k + "'");
        }
    }
    if (mode != TOP) throw Fail(file + ": missing 'end' at end of file");
    for (auto& a : m.actions) {
        if (a.cmd.empty()) throw Fail(file + ":" + std::to_string(a.line) + ": action has no cmd");
        if (a.restat && a.output.empty() && a.outputs.empty()) throw Fail(file + ":" + std::to_string(a.line) + ": restat action has no output");
    }
    if (m.hasBuildfile && m.buildCmd.empty()) throw Fail(file + ":" + std::to_string(m.buildLine) + ": buildfile has no cmd");
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
// Shortcut: same size and mtime as the record means the bytes did not change, so reuse the stored hash.
// A stamp with sub-millisecond digits cannot survive an ordinary later write, so it is trusted at once.
// A whole-second or whole-millisecond stamp might be shared by two writes (coarse clocks), so it is trusted
// only once it was already 2 s old when it was recorded. Forged timestamps with the same size are not detected.
static bool hashFile(const string& p, FileSig& out) {
    FileSig prev; bool havePrev = false;
    { std::lock_guard<std::mutex> l(gHashMu); auto it = gCache.find(p); if (it != gCache.end()) { out = it->second; return true; }
      auto pv = gPrev.find(p); if (pv != gPrev.end()) { prev = pv->second; havePrev = true; } }
    struct stat st; if (::stat(p.c_str(), &st) != 0) return false;
    FileSig s; s.size = (long long)st.st_size; s.mtime = (long long)st.st_mtim.tv_sec * 1000000000ll + st.st_mtim.tv_nsec;
    long long nsec = s.mtime % 1000000000ll;
    bool fine = nsec % 1000000ll != 0;
    bool settled = prev.mtime + 2000000000ll < prev.at;
    if (gUseStat && havePrev && prev.size == s.size && prev.mtime == s.mtime && (fine || settled)) {
        s = prev; ++gTrusted;
    } else {
        int fd = ::open(p.c_str(), O_RDONLY);
        if (fd < 0) return false;
        uint64_t h = 1469598103934665603ull; char buf[65536];
        for (;;) {
            ssize_t n = ::read(fd, buf, sizeof buf);
            if (n < 0) { if (errno == EINTR) continue; ::close(fd); return false; }
            if (n == 0) break;
            h = fnv(buf, (size_t)n, h);
        }
        ::close(fd);
        s.hash = hex64(h); { struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts); s.at = (long long)ts.tv_sec * 1000000000ll + ts.tv_nsec; } ++gHashed;
    }
    out = s; std::lock_guard<std::mutex> l(gHashMu); gCache[p] = s; return true;
}
static void forgetHash(const string& p) { std::lock_guard<std::mutex> l(gHashMu); gCache.erase(p); }
static string hashStr(const string& s) { return hex64(fnv(s.data(), s.size())); }
static string hashArgv(const vector<string>& a) { uint64_t h = 1469598103934665603ull; for (auto& s : a) { h = fnv(s.data(), s.size(), h); h = fnv("\0", 1, h); } return hex64(h); }

// ---------------------------------------------------------------- graph
struct Action {
    int id = 0; string kind, desc, output, depfile, dir, rawCmd, pool; vector<string> argv, inputs, outputs, waitFor;
    vector<int> deps, rdeps; string target; size_t rspHead = 0, rspThreshold = 0; string cmdHash; bool restat = false;
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
        Action a; a.target = t.name; a.output = outPath(t); a.outputs = {a.output}; a.deps = objActs; a.inputs = objs; a.desc = a.output; a.rspThreshold = m.tc.rspThreshold; a.pool = t.pool; a.waitFor = needKeys;
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
    // raw actions (imported commands and generators): `cd DIR && CMD` through sh.
    // Every declared output is an edge source. `after` names the spelling used in the manifest.
    std::map<string, int> byOut, byRawName;
    for (auto& r : m.actions) {
        Action a; a.kind = "RUN"; a.dir = r.dir; a.rawCmd = r.cmd; a.pool = r.pool; a.restat = r.restat;
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
struct Rec { string cmd; vector<std::pair<string, FileSig>> inputs, produced; };
static std::map<string, Rec> gState; static std::mutex gStateMu;
static string statePath() { return under(".okaybuild/state"); }
static void loadState() {
    std::ifstream f(statePath(), std::ios::binary); string l; Rec* cur = nullptr;
    if (!std::getline(f, l) || (l != "okaybuild-state 3" && l != "okaybuild-state 4")) return; // unknown/missing: rebuild everything
    gPrev.reserve(4096);
    while (std::getline(f, l)) {
        const char* p = l.c_str(); const char* e = p + l.size(); const char* fld[6]; int n = 0; fld[n++] = p;
        for (const char* q = p; q < e && n < 6; ++q) if (*q == '\t') fld[n++] = q + 1;
        auto len = [&](int i) { return (size_t)((i + 1 < n ? fld[i + 1] - 1 : e) - fld[i]); };
        if (n == 3 && l[0] == 'O') cur = &(gState[string(fld[1], len(1))] = Rec{string(fld[2], len(2)), {}, {}});
        else if (n == 6 && (l[0] == 'I' || l[0] == 'P') && cur) { FileSig s; s.hash.assign(fld[2], len(2)); s.size = atoll(fld[3]); s.mtime = atoll(fld[4]); s.at = atoll(fld[5]);
                                                   string path(fld[1], len(1)); (l[0] == 'I' ? cur->inputs : cur->produced).push_back({path, s}); if (l[0] == 'I') gPrev[path] = s; }
    }
}
static void saveState() {
    fs::create_directories(under(".okaybuild")); string tmp = statePath() + ".tmp";
    { std::ofstream f(tmp, std::ios::trunc); f << "okaybuild-state 4\n";
      for (auto& [o, r] : gState) {
          f << "O\t" << o << "\t" << r.cmd << "\n";
          for (auto& [p, s] : r.inputs) f << "I\t" << p << "\t" << s.hash << "\t" << s.size << "\t" << s.mtime << "\t" << s.at << "\n";
          for (auto& [p, s] : r.produced) f << "P\t" << p << "\t" << s.hash << "\t" << s.size << "\t" << s.mtime << "\t" << s.at << "\n";
      } }
    fs::rename(tmp, statePath());
}

static bool fileMissing(const string& p) { struct stat st; return ::stat(p.c_str(), &st) != 0; }
// restat: the command rewrote an output with the same bytes. Put the previous mtime back so dependents,
// and anything else looking at mtimes, see no change. Content hashes already skip the rebuild.
static void restoreMtime(const string& p, long long mtime) {
    struct timespec ts[2];
    ts[0].tv_sec = 0; ts[0].tv_nsec = UTIME_OMIT;
    ts[1].tv_sec = (time_t)(mtime / 1000000000ll); ts[1].tv_nsec = (long)(mtime % 1000000000ll);
    utimensat(AT_FDCWD, p.c_str(), ts, 0);
}
static void noteOutputs(const Action& a, Rec& rec, const vector<std::pair<string, FileSig>>& oldProduced, string& out, bool& ok) {
    for (auto& o : a.outputs) {
        forgetHash(o); FileSig now;
        if (!hashFile(o, now)) { ok = false; out += "okay-build: output missing after the command: " + o + "\n"; return; }
        bool same = false; FileSig prev{};
        for (auto& [p, s] : oldProduced) if (p == o) { prev = s; same = s.hash == now.hash; break; }
        if (a.restat && same) { restoreMtime(o, prev.mtime); now = prev; forgetHash(o); }
        rec.produced.push_back({o, now});
    }
}
static bool dirty(const Action& a, string& why) {
    if (a.outputs.empty()) { why = "no declared output (always runs)"; return true; }
    for (auto& o : a.outputs) if (fileMissing(o)) { why = "output " + o + " is missing"; return true; }
    const Rec* rp; { std::lock_guard<std::mutex> l(gStateMu); auto it = gState.find(a.output); if (it == gState.end()) { why = "no record in state file (never built by okay-build)"; return true; } rp = &it->second; } // map nodes are stable
    const Rec& r = *rp;
    if (r.cmd != a.cmdHash) { why = "command line changed"; return true; }
    for (auto& i : a.inputs) {
        bool known = false; for (auto& [p, h] : r.inputs) if (p == i) { known = true; break; }
        if (!known) { why = "new input " + i; return true; }
    }
    for (auto& [p, h] : r.inputs) {
        FileSig now; if (!hashFile(p, now)) { why = "input " + p + " is gone"; return true; }
        if (now.hash != h.hash) { why = "input " + p + " changed"; return true; }
    }
    return false;
}

// ---------------------------------------------------------------- running commands
static int waitChild(pid_t pid) {
    int st = 0; while (waitpid(pid, &st, 0) < 0) { if (errno != EINTR) return 128; }
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128;
}
static int runCmd(const vector<string>& argv, string& out, bool stream = false) {
    auto child = [&]() {
        vector<char*> av; for (auto& s : argv) av.push_back(const_cast<char*>(s.c_str())); av.push_back(nullptr);
        execvp(av[0], av.data()); dprintf(2, "okay-build: cannot run '%s': %s\n", av[0] ? av[0] : "(none)", strerror(errno)); _exit(127);
    };
    if (stream) { // console pool: the command owns the terminal, one at a time
        pid_t pid = fork(); if (pid < 0) return 127; if (pid == 0) child(); return waitChild(pid);
    }
    int fd[2]; if (pipe(fd)) return 127;
    pid_t pid = fork();
    if (pid < 0) { close(fd[0]); close(fd[1]); return 127; }
    if (pid == 0) { dup2(fd[1], 1); dup2(fd[1], 2); close(fd[0]); close(fd[1]); child(); }
    close(fd[1]); char b[4096]; ssize_t n; while ((n = read(fd[0], b, sizeof b)) > 0) out.append(b, (size_t)n);
    close(fd[0]); return waitChild(pid);
}

static std::atomic<long long> gNsDirty{0}, gNsSpawn{0}, gNsPost{0}, gNsPrint{0};
static long long nowNs() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
struct Opts { int jobs = 0; bool dry = false, explain = false, verbose = false, profile = false, keep = false; string file = "build.okay"; };

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

static int build(const Manifest& m, const vector<string>& want, const Opts& o) {
    long long tStart = nowNs(); Graph g = plan(m, want); loadState();
    long long tPlan = nowNs() - tStart;
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
            if (o.verbose) for (auto& a : g.acts) if (!a.output.empty()) printf("[up to date] %s\n", a.output.c_str());
            printf("okay-build: ran 0 of %d steps (%d up to date)\n", total, upToDate);
            if (o.profile) printf("okay-build: plan+state load %lld ms\n", tPlan / 1000000);
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
    auto blocked = [&](int id) { int p = poolIdx[id]; return p >= 0 && pools[p].used >= pools[p].depth; };
    auto take = [&]() -> int {
        for (int i = (int)ready.size() - 1; i >= 0; --i) if (!blocked(ready[i])) { int id = ready[i]; ready.erase(ready.begin() + i); return id; }
        return -1;
    };
    auto worker = [&] {
        std::unique_lock<std::mutex> lk(mu);
        for (;;) {
            cv.wait(lk, [&] {
                if (stop || ready.empty()) return running == 0;
                for (int id : ready) if (!blocked(id)) return true;
                return running == 0;
            });
            if (stop || ready.empty()) { if (running == 0) { cv.notify_all(); return; } continue; }
            int id = take();
            if (id < 0) {
                if (running == 0) { anyFail = true; stop = true; firstErr = "okay-build: steps are waiting on a pool but none is free\n"; cv.notify_all(); return; }
                continue;
            }
            ++running; bool stream = false;
            if (poolIdx[id] >= 0) { ++pools[poolIdx[id]].used; stream = pools[poolIdx[id]].name == "console"; }
            const Action& a = g.acts[id]; lk.unlock();
            string why, out; bool d = false, ok = true; vector<std::pair<string, FileSig>> oldProduced;
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
                            fs::create_directories(fs::path(rf).parent_path()); std::ofstream r(rf, std::ios::trunc);
                            for (size_t i = a.rspHead; i < a.argv.size(); ++i) { for (char c : a.argv[i]) { if (c == '\\' || c == '"' || c == ' ') r << '\\'; r << c; } r << '\n'; }
                            argv.resize(a.rspHead); argv.push_back("@" + rf);
                        }
                        long long ts = nowNs(); int rc = runCmd(argv, out, stream); ok = rc == 0; gNsSpawn += nowNs() - ts; ts = nowNs();
                        if (ok && !a.outputs.empty()) {
                            Rec rec; rec.cmd = a.cmdHash; vector<string> ins = a.inputs;
                            if (!a.depfile.empty()) for (auto& h : parseDepfile(a.depfile)) { string np = resolve(a.dir, h); if (std::find(ins.begin(), ins.end(), np) == ins.end()) ins.push_back(np); }
                            for (auto& pth : a.outputs) forgetHash(pth);
                            for (auto& pth : ins) { FileSig h; if (!hashFile(pth, h)) { ok = false; out += "okay-build: cannot hash input " + pth + "\n"; break; } rec.inputs.push_back({pth, h}); }
                            if (ok) noteOutputs(a, rec, oldProduced, out, ok);
                            if (ok) { std::lock_guard<std::mutex> sl(gStateMu); gState[a.output] = rec; }
                            gNsPost += nowNs() - ts;
                        } else if (!ok) out = "okay-build: FAILED: " + joined(a.argv) + "\n" + out;
                    } else wouldRun[id] = 1;
                } else { std::lock_guard<std::mutex> pl(mu); ++upToDate; if (o.verbose) printf("[up to date] %s\n", a.output.c_str()); }
            } catch (const std::exception& e) { ok = false; out += string("okay-build: ") + e.what() + "\n"; }
            lk.lock(); --running; if (poolIdx[id] >= 0) --pools[poolIdx[id]].used; ++finished;
            if (!ok) { anyFail = true; if (!o.keep) stop = true; if (firstErr.empty()) firstErr = out; if (!out.empty()) { fflush(stdout); fputs(out.c_str(), stderr); } }
            else if (!stop) for (int r : a.rdeps) if (--pending[r] == 0) ready.push_back(r);
            cv.notify_all();
        }
    };
    vector<std::thread> th; for (int i = 0; i < jobs; ++i) th.emplace_back(worker);
    for (auto& t : th) t.join();
    if (!o.dry) saveState();
    if (anyFail) { fprintf(stderr, "okay-build: build FAILED (%d step(s) ran, %d of %d finished)\n", ran, finished, total); return 1; }
    printf("okay-build: ran %d of %d steps (%d up to date)%s\n", ran, total, upToDate, o.dry ? " [dry run]" : "");
    if (o.profile) printf("okay-build: plan+state load %lld ms\n", tPlan / 1000000);
    if (o.profile) printf("okay-build: profile (summed over workers, ms): dirty-check %lld, run command %lld, depfile+hash %lld; wall %lld\n", gNsDirty / 1000000, gNsSpawn / 1000000, gNsPost / 1000000, (nowNs() - tStart) / 1000000);
    if (o.verbose) printf("okay-build: hashed %d input file(s), trusted size+mtime for %d\n", gHashed.load(), gTrusted.load());
    return 0;
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
static void writeCompdb(const Manifest& m) {
    Graph g = plan(m, {}); string dir = fs::current_path().string(); std::ofstream f("compile_commands.json"); f << "[\n"; bool first = true;
    for (auto& a : g.acts) if (a.kind == "CC") {
        f << (first ? "" : ",\n") << "  {\"directory\": \"" << jsonEsc(dir) << "\", \"file\": \"" << jsonEsc(a.inputs[0]) << "\", \"arguments\": [";
        for (size_t i = 0; i < a.argv.size(); ++i) { f << (i ? ", " : "") << "\"" << jsonEsc(a.argv[i]) << "\""; }
        f << "]}"; first = false;
    }
    f << "\n]\n"; printf("okay-build: wrote compile_commands.json\n");
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
    for (auto& p : paths) { forgetHash(p); FileSig s; sigs.push_back({p, hashFile(p, s) ? s.hash : string("-")}); }
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

int main(int argc, char** argv) {
    Opts o; string cmd = "build", runDir, importOut = "build.okay"; vector<string> want;
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
            else if (a == "--run-dir" && i + 1 < argc) runDir = argv[++i];
            else if (a == "-o" && i + 1 < argc) importOut = argv[++i];
            else if (a == "build" || a == "clean" || a == "compdb" || a == "import" || a == "graph" || a == "targets") cmd = a;
            else if (a == "-h" || a == "--help") { puts("usage: okay-build [build|clean|compdb|import|graph|targets] [-f build.okay] [-j N] [-k] [--build-dir DIR] [--no-stat] [--dry-run] [--explain] [-v] [target...]"); return 0; }
            else if (!a.empty() && a[0] == '-') throw Fail("unknown option " + a);
            else want.push_back(a);
        }
        if (cmd == "import") { // okay-build import BUILD_DIR [--run-dir DIR] [-o build.okay] [ninja targets...]
            if (want.empty()) throw Fail("usage: okay-build import <cmake build dir> [--run-dir DIR] [-o FILE] [ninja targets...]");
            string bd = want[0]; want.erase(want.begin()); return importCmake(bd, runDir, importOut, want);
        }
        if (gBuildDir != ".") gBuildDir = fs::absolute(gBuildDir).string();
        fs::path mp = fs::absolute(o.file); fs::current_path(mp.parent_path());
        Manifest m = cmd == "build" ? loadWithRegen(mp.string()) : parseManifest(mp.string());
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
