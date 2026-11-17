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
    vector<string> flags, linkFlags;
};
struct Target {
    string name; bool isLib = false; int line = 0;
    vector<string> sources, includes, exportIncludes, defines, flags, linkFlags, libs, deps;
};
struct Manifest { Toolchain tc; string outdir = "build"; vector<Target> targets; };

static vector<string> tokens(const string& line) {
    vector<string> t; std::istringstream in(line); string w;
    while (in >> w) t.push_back(w);
    return t;
}

static Manifest parseManifest(const string& file) {
    std::ifstream in(file);
    if (!in) throw Fail("cannot open manifest " + file);
    Manifest m; string line; int ln = 0;
    enum { TOP, TC, TARGET } mode = TOP;
    auto err = [&](const string& msg) { return Fail(file + ":" + std::to_string(ln) + ": " + msg); };
    while (std::getline(in, line)) {
        ++ln;
        auto h = line.find('#'); if (h != string::npos) line.resize(h);
        auto t = tokens(line); if (t.empty()) continue;
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
            else throw err("unknown toolchain key '" + k + "'");
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
    return m;
}

// ---------------------------------------------------------------- hashing
static string hex64(uint64_t h) { char b[17]; snprintf(b, sizeof b, "%016llx", (unsigned long long)h); return b; }
static uint64_t fnv(const char* p, size_t n, uint64_t h = 1469598103934665603ull) {
    for (size_t i = 0; i < n; ++i) { h ^= (unsigned char)p[i]; h *= 1099511628211ull; }
    return h;
}
static std::mutex gHashMu; static std::map<string, string> gHashCache;
static bool hashFile(const string& p, string& out) {
    { std::lock_guard<std::mutex> l(gHashMu); auto it = gHashCache.find(p); if (it != gHashCache.end()) { out = it->second; return true; } }
    std::ifstream f(p, std::ios::binary); if (!f) return false;
    uint64_t h = 1469598103934665603ull; char buf[65536];
    while (f.read(buf, sizeof buf) || f.gcount()) { h = fnv(buf, (size_t)f.gcount(), h); if (f.eof()) break; }
    out = hex64(h);
    std::lock_guard<std::mutex> l(gHashMu); gHashCache[p] = out; return true;
}
static void forgetHash(const string& p) { std::lock_guard<std::mutex> l(gHashMu); gHashCache.erase(p); }
static string hashArgv(const vector<string>& a) { uint64_t h = 1469598103934665603ull; for (auto& s : a) { h = fnv(s.data(), s.size(), h); h = fnv("\0", 1, h); } return hex64(h); }

// ---------------------------------------------------------------- graph
struct Action {
    int id = 0; string kind, desc, output, depfile; vector<string> argv, inputs; vector<int> deps, rdeps;
    string target;
};
struct Graph { vector<Action> acts; vector<std::pair<string, int>> targetFinal; };

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

    // chosen set = wanted targets + transitive deps
    std::set<string> chosen;
    std::function<void(const string&)> pick = [&](const string& n) { if (!chosen.insert(n).second) return; for (auto& d : byName[n]->deps) pick(d); };
    if (want.empty()) for (auto& t : m.targets) pick(t.name);
    for (auto& w : want) { if (!byName.count(w)) throw Fail("unknown target '" + w + "'"); pick(w); }

    Graph g; std::map<string, int> finalAct; std::map<string, string> finalPath;
    auto outPath = [&](const Target& t) { return m.outdir + "/" + (t.isLib ? m.tc.libp + t.name + m.tc.libs : t.name + m.tc.exe); };
    // post-order: deps are planned before dependents
    std::set<string> done; vector<string> order;
    std::function<void(const string&)> post = [&](const string& n) { if (!done.insert(n).second) return; for (auto& d : byName[n]->deps) post(d); order.push_back(n); };
    for (auto& t : m.targets) if (chosen.count(t.name)) post(t.name);

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
            a.output = ".okaybuild/obj/" + t.name + "/" + rel + m.tc.obj; a.depfile = a.output + ".d"; a.inputs = {s};
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
        Action a; a.target = t.name; a.output = outPath(t); a.deps = objActs; a.inputs = objs; a.desc = a.output;
        if (t.isLib) { a.kind = "AR"; a.argv = {m.tc.ar, "rcs", a.output}; for (auto& o : objs) a.argv.push_back(o); }
        else {
            a.kind = "LD"; a.argv = {m.tc.cxx}; for (auto& o : objs) a.argv.push_back(o);
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
struct Rec { string cmd; vector<std::pair<string, string>> inputs; };
static std::map<string, Rec> gState; static std::mutex gStateMu;
static const char* kStatePath = ".okaybuild/state";
static void loadState() {
    std::ifstream f(kStatePath); string l; Rec* cur = nullptr;
    if (!std::getline(f, l) || l != "okaybuild-state 1") return; // unknown/missing: rebuild everything
    while (std::getline(f, l)) {
        std::vector<string> p; std::stringstream ss(l); string x; while (std::getline(ss, x, '\t')) p.push_back(x);
        if (p.size() == 3 && p[0] == "O") cur = &(gState[p[1]] = Rec{p[2], {}});
        else if (p.size() == 3 && p[0] == "I" && cur) cur->inputs.push_back({p[1], p[2]});
    }
}
static void saveState() {
    fs::create_directories(".okaybuild");
    { std::ofstream f(string(kStatePath) + ".tmp", std::ios::trunc); f << "okaybuild-state 1\n";
      for (auto& [o, r] : gState) { f << "O\t" << o << "\t" << r.cmd << "\n"; for (auto& [p, h] : r.inputs) f << "I\t" << p << "\t" << h << "\n"; } }
    fs::rename(string(kStatePath) + ".tmp", kStatePath);
}

static bool dirty(const Action& a, string& why) {
    if (!fs::exists(a.output)) { why = "output " + a.output + " is missing"; return true; }
    Rec r; { std::lock_guard<std::mutex> l(gStateMu); auto it = gState.find(a.output); if (it == gState.end()) { why = "no record in state file (never built by okay-build)"; return true; } r = it->second; }
    if (r.cmd != hashArgv(a.argv)) { why = "command line changed"; return true; }
    std::set<string> have; for (auto& [p, h] : r.inputs) have.insert(p);
    for (auto& i : a.inputs) if (!have.count(i)) { why = "new input " + i; return true; }
    for (auto& [p, h] : r.inputs) {
        string now; if (!hashFile(p, now)) { why = "input " + p + " is gone"; return true; }
        if (now != h) { why = "input " + p + " changed"; return true; }
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

struct Opts { int jobs = 0; bool dry = false, explain = false, verbose = false; string file = "build.okay"; };

static int build(const Manifest& m, const vector<string>& want, const Opts& o) {
    Graph g = plan(m, want); loadState();
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
            string why; bool d = dirty(a, why);
            if (!d && o.dry) for (int dep : a.deps) if (wouldRun[dep]) { d = true; why = "input " + g.acts[dep].output + " would be rebuilt"; }
            bool ok = true; string out;
            if (d) {
                { std::lock_guard<std::mutex> pl(mu); ++ran; printf("[%d/%d] %s %s\n", ran, total, a.kind.c_str(), a.desc.c_str());
                  if (o.explain) { printf("        because: %s\n", why.c_str()); }
                  if (o.verbose || o.dry) { printf("        %s\n", joined(a.argv).c_str()); }
                  fflush(stdout); }
                if (!o.dry) {
                    fs::create_directories(fs::path(a.output).parent_path()); if (a.kind == "AR") fs::remove(a.output);
                    int rc = runCmd(a.argv, out); ok = rc == 0;
                    if (ok) {
                        Rec r; r.cmd = hashArgv(a.argv); vector<string> ins = a.inputs;
                        if (a.kind == "CC") for (auto& h : parseDepfile(a.depfile)) { string n = fs::path(h).lexically_normal().generic_string(); if (std::find(ins.begin(), ins.end(), n) == ins.end()) ins.push_back(n); }
                        forgetHash(a.output);
                        for (auto& p : ins) { string h; if (!hashFile(p, h)) { ok = false; out += "okay-build: cannot hash input " + p + "\n"; break; } r.inputs.push_back({p, h}); }
                        if (ok) { std::lock_guard<std::mutex> sl(gStateMu); gState[a.output] = r; }
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
    Opts o; string cmd = "build"; vector<string> want;
    try {
        for (int i = 1; i < argc; ++i) {
            string a = argv[i];
            if (a == "-f" && i + 1 < argc) o.file = argv[++i];
            else if (a == "-j" && i + 1 < argc) o.jobs = atoi(argv[++i]);
            else if (a.rfind("-j", 0) == 0 && a.size() > 2) o.jobs = atoi(a.c_str() + 2);
            else if (a == "--dry-run" || a == "-n") o.dry = true;
            else if (a == "--explain") o.explain = true;
            else if (a == "-v") o.verbose = true;
            else if (a == "build" || a == "clean" || a == "compdb") cmd = a;
            else if (a == "-h" || a == "--help") { puts("usage: okay-build [build|clean|compdb] [-f build.okay] [-j N] [--dry-run] [--explain] [-v] [target...]"); return 0; }
            else if (!a.empty() && a[0] == '-') throw Fail("unknown option " + a);
            else want.push_back(a);
        }
        fs::path mp = fs::absolute(o.file); Manifest m = parseManifest(mp.string()); fs::current_path(mp.parent_path());
        if (cmd == "clean") {
            Graph g = plan(m, {}); for (auto& a : g.acts) if (a.kind != "CC") fs::remove(a.output);
            fs::remove_all(".okaybuild"); fs::remove("compile_commands.json"); printf("okay-build: cleaned\n"); return 0;
        }
        if (cmd == "compdb") { writeCompdb(m); return 0; }
        return build(m, want, o);
    } catch (const Fail& e) { fprintf(stderr, "okay-build: error: %s\n", e.what()); return 2; }
    catch (const std::exception& e) { fprintf(stderr, "okay-build: internal error: %s\n", e.what()); return 3; }
}
