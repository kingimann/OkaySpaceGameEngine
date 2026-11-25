// okay-build: a reader for the part of the CMake language that this project's CMakeLists.txt files use.
// Written from the public description of the language (commands, arguments, quoting, ${variables}, if() conditions, lists,
// functions and macros, target and test commands). No code or tests from CMake or any other build tool were read or used
// (CLAUDE.md rule 19a). It does not run CMake and it does not need CMake: it follows the files itself, on this machine,
// and writes a build.okay manifest. Anything it does not understand is counted and reported (`cmake --audit`), never guessed.
// Included by okay-build.cpp (one translation unit). Not Windows-specific code, but only run on Linux so far.
#pragma once
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>
#ifndef _WIN32
#include <unistd.h>
#include <sys/wait.h>
#define CMR_POPEN popen
#define CMR_PCLOSE pclose
#else
#define CMR_POPEN _popen
#define CMR_PCLOSE _pclose
#endif

namespace cmr {
using std::string; using std::vector;
namespace fs = std::filesystem;

struct Arg { string text; bool quoted = false, bracket = false; };
struct Cmd { string name; vector<Arg> args; int line = 0; string file; };

struct ReadError { string msg; };

static string lower(string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
static string upper(string s) { for (auto& c : s) c = (char)toupper((unsigned char)c); return s; }

// ---- 1. Text to commands ----------------------------------------------------------------------------------------------
// A command is NAME ( arguments ). Arguments are separated by white space; "quoted" ones keep their spaces and know that they were
// quoted; [[bracket]] ones are taken literally; unquoted ones end at white space or a parenthesis. # starts a comment (#[[ ]] a long one).
static vector<Cmd> parseFile(const string& text, const string& file) {
    vector<Cmd> out; size_t i = 0, n = text.size(); int line = 1;
    auto bracketOpen = [&](size_t at, size_t& eqs) { // at points to '[' ; returns true when [=*[ starts here
        size_t j = at + 1; eqs = 0; while (j < n && text[j] == '=') { ++eqs; ++j; } return j < n && text[j] == '[';
    };
    auto readBracket = [&](size_t& at, size_t eqs) { // at on the first '[' ; returns the inside, leaves at after the closer
        size_t start = at + eqs + 2; string close = "]" + string(eqs, '=') + "]";
        size_t e = text.find(close, start); if (e == string::npos) throw ReadError{file + ":" + std::to_string(line) + ": unterminated bracket argument"};
        string body = text.substr(start, e - start); for (char c : body) if (c == '\n') ++line;
        at = e + close.size(); return body;
    };
    auto skipSpace = [&]() {
        for (;;) {
            while (i < n && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) { if (text[i] == '\n') ++line; ++i; }
            if (i < n && text[i] == '#') {
                size_t eqs; if (i + 1 < n && text[i + 1] == '[' && bracketOpen(i + 1, eqs)) { size_t at = i + 1; readBracket(at, eqs); i = at; }
                else while (i < n && text[i] != '\n') ++i;
                continue;
            }
            break;
        }
    };
    for (;;) {
        skipSpace(); if (i >= n) break;
        size_t b = i; while (i < n && (isalnum((unsigned char)text[i]) || text[i] == '_')) ++i;
        if (i == b) throw ReadError{file + ":" + std::to_string(line) + ": expected a command name"};
        Cmd c; c.name = text.substr(b, i - b); c.line = line; c.file = file;
        while (i < n && (text[i] == ' ' || text[i] == '\t')) ++i;
        if (i >= n || text[i] != '(') throw ReadError{file + ":" + std::to_string(line) + ": expected '(' after " + c.name};
        ++i; int depth = 1;
        for (;;) {
            skipSpace(); if (i >= n) throw ReadError{file + ":" + std::to_string(c.line) + ": missing ')' for " + c.name};
            char ch = text[i];
            if (ch == ')') { if (--depth == 0) { ++i; break; } c.args.push_back({")", false}); ++i; continue; }
            if (ch == '(') { ++depth; c.args.push_back({"(", false}); ++i; continue; }
            size_t eqs;
            if (ch == '[' && bracketOpen(i, eqs)) { size_t at = i; string body = readBracket(at, eqs); i = at; c.args.push_back({body, true, true}); continue; }
            if (ch == '"') {
                string v; ++i;
                for (;;) {
                    if (i >= n) throw ReadError{file + ":" + std::to_string(line) + ": unterminated string"};
                    char d = text[i];
                    if (d == '\\' && i + 1 < n) {
                        char e = text[i + 1];
                        if (e == 'n') { v += '\n'; i += 2; } else if (e == 't') { v += '\t'; i += 2; } else if (e == 'r') { v += '\r'; i += 2; }
                        else if (e == '\n') { ++line; i += 2; } // line continuation
                        else if (e == ';') { v += "\\;"; i += 2; } // keeps the escaped semicolon out of list splitting
                        else { v += e; i += 2; }
                        continue;
                    }
                    if (d == '"') { ++i; break; }
                    if (d == '\n') ++line;
                    v += d; ++i;
                }
                c.args.push_back({v, true}); continue;
            }
            string v;
            while (i < n && !isspace((unsigned char)text[i]) && text[i] != '(' && text[i] != ')' && text[i] != '#') {
                if (text[i] == '\\' && i + 1 < n) { v += (text[i + 1] == ';' ? string("\\;") : string(1, text[i + 1])); i += 2; continue; }
                if (text[i] == '"') { // a quote inside an unquoted word (NAME="x y"): a "legacy" argument, the quotes stay in the text
                    v += '"'; ++i; while (i < n && text[i] != '"') { if (text[i] == '\\' && i + 1 < n) { v += text[i + 1]; i += 2; } else v += text[i++]; }
                    if (i < n) { v += '"'; ++i; } continue;
                }
                if (text[i] == '$' && i + 1 < n && (text[i + 1] == '{' || text[i + 1] == '<')) { // keep ${...} and $<...> in one word even with spaces inside
                    char open = text[i + 1], close = open == '{' ? '}' : '>'; int d = 0; size_t j = i + 1;
                    for (; j < n; ++j) { if (text[j] == open) ++d; else if (text[j] == close && --d == 0) { ++j; break; } else if (text[j] == '\n') break; }
                    v += text.substr(i, j - i); i = j; continue;
                }
                v += text[i++];
            }
            c.args.push_back({v, false});
        }
        out.push_back(std::move(c));
    }
    return out;
}

// string(MD5): RFC 1321, written from the RFC's description (not a security function here: CMake projects use it to make names).
// The 64 constants are floor(|sin(i + 1)| * 2^32), worked out the way the RFC defines them.
static string md5Hex(const string& msg) {
    static uint32_t K[64]; static bool ready = false;
    if (!ready) { for (int i = 0; i < 64; ++i) K[i] = (uint32_t)(long long)(std::fabs(std::sin((double)(i + 1))) * 4294967296.0); ready = true; }
    static const int R[4][4] = {{7, 12, 17, 22}, {5, 9, 14, 20}, {4, 11, 16, 23}, {6, 10, 15, 21}};
    uint32_t h[4] = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
    string m = msg; uint64_t bits = (uint64_t)msg.size() * 8; m += (char)0x80;
    while (m.size() % 64 != 56) m += (char)0;
    for (int i = 0; i < 8; ++i) m += (char)((bits >> (8 * i)) & 0xff); // length, low byte first
    for (size_t off = 0; off < m.size(); off += 64) {
        uint32_t w[16];
        for (int i = 0; i < 16; ++i) { w[i] = 0; for (int b = 0; b < 4; ++b) w[i] |= (uint32_t)(unsigned char)m[off + 4 * i + b] << (8 * b); }
        uint32_t A = h[0], B = h[1], C = h[2], D = h[3];
        for (int i = 0; i < 64; ++i) {
            uint32_t f; int g, round = i / 16;
            if (round == 0) { f = (B & C) | (~B & D); g = i; } else if (round == 1) { f = (D & B) | (~D & C); g = (5 * i + 1) % 16; }
            else if (round == 2) { f = B ^ C ^ D; g = (3 * i + 5) % 16; } else { f = C ^ (B | ~D); g = (7 * i) % 16; }
            uint32_t x = A + f + K[i] + w[g]; int r = R[round][i % 4];
            A = D; D = C; C = B; B = B + ((x << r) | (x >> (32 - r)));
        }
        h[0] += A; h[1] += B; h[2] += C; h[3] += D;
    }
    string out; char b[3];
    for (int i = 0; i < 4; ++i) for (int k = 0; k < 4; ++k) { snprintf(b, sizeof b, "%02x", (h[i] >> (8 * k)) & 0xff); out += b; }
    return out;
}

// ---- Paths in the build folder ------------------------------------------------------------------------------------------
// The build folder is the word {bin}. binNormal() tidies "{bin}/a/../b" to "{bin}/b"; a path that climbs above {bin} keeps its
// leading "..", and "{bin}x" is left as it is, so inBin() can refuse both. Used by the reader and by okay-build's manifest parser.
static string binNormal(const string& p) {
    if (p.compare(0, 5, "{bin}") != 0 || p.size() == 5 || p[5] != '/') return p;
    string n = fs::path(p.substr(6)).lexically_normal().generic_string();
    while (!n.empty() && n.back() == '/') n.pop_back();
    return n.empty() || n == "." ? string("{bin}") : "{bin}/" + n;
}
static bool inBin(const string& p) { // a file strictly inside {bin}, already tidied by binNormal()
    if (p.compare(0, 6, "{bin}/") != 0 || p.size() == 6) return false;
    string rest = p.substr(6);
    return !(rest == ".." || rest.compare(0, 3, "../") == 0) && rest[0] != '/';
}

// ---- 2. What the evaluator builds ------------------------------------------------------------------------------------------
struct Target {
    string name, kind; // exe | lib | iface | imported | alias
    string aliasOf, dir, libKind; // libKind: "", "shared" or "object"
    vector<string> sources, incs, pubIncs, ifIncs, defs, pubDefs, ifDefs, opts, pubOpts, ifOpts, links, pubLinks, ifLinks, linkOpts;
    // directory-wide settings captured when the target was made
    vector<string> dirIncs, dirDefs, dirOpts, dirLinkOpts;
    vector<std::pair<string, string>> fileFlags, fileDefines, fileIncludes;
    vector<string> linkProp, ifLinkProp; // LINK_LIBRARIES and INTERFACE_LINK_LIBRARIES as get_target_property reads them, in call order
    std::map<string, string> props;
    bool cxx17 = false, excluded = false;
    string skipReason; // set when the target cannot be written (missing source, needs a download, ...)
};
struct TestDef { string name; vector<string> cmd; string cwd; std::map<string, string> props; string dir; };
struct Func { vector<string> params; vector<Cmd> body; bool macro = false; };

struct Options {
    string root; // absolute source folder
    string binDir = "{bin}"; // what CMAKE_BINARY_DIR reads as
    std::map<string, string> defs; // -D NAME=VALUE
    std::map<string, string> fetched; // FetchContent name -> folder with the sources already on disk
    string buildType = "Release";
    bool quiet = true;
};

struct Tally { std::map<string, int> run, unknown, genex; vector<string> messages; int options = 0, setOptions = 0, widened = 0; };

// The variable table. A function call opens a frame; the first write of each name in the frame keeps the old value, and closing the
// frame puts those back. This gives a function its own scope without copying the whole table on every call.
struct VarMap {
    std::map<string, string> m;
    struct Old { bool had; string value; };
    vector<std::map<string, Old>> frames;
    void note(const string& k) {
        if (frames.empty()) return; auto& f = frames.back(); if (f.count(k)) return;
        auto it = m.find(k); f.emplace(k, it == m.end() ? Old{false, string()} : Old{true, it->second});
    }
    string& operator[](const string& k) { note(k); return m[k]; }
    size_t erase(const string& k) { note(k); return m.erase(k); }
    std::map<string, string>::const_iterator find(const string& k) const { return m.find(k); }
    std::map<string, string>::const_iterator end() const { return m.end(); }
    size_t count(const string& k) const { return m.count(k); }
    void push() { frames.emplace_back(); }
    void pop() { auto f = std::move(frames.back()); frames.pop_back(); for (auto& o : f) { note(o.first); if (o.second.had) m[o.first] = o.second.value; else m.erase(o.first); } }
};

struct Interp {
    Options opt; Tally tally;
    VarMap vars; std::map<string, string> cache; std::set<string> cacheForced;
    std::map<string, Target> targets; vector<string> order; // creation order
    vector<TestDef> tests;
    std::map<string, Func> funcs;
    struct FetchInfo { string git, tag, url, sha256; };
    std::map<string, FetchInfo> fetchDeclared; vector<string> fetchUsed; // FetchContent: what was declared, and what a target actually needs downloaded
    vector<string> dirDefs, dirOpts, dirIncs, dirLinkOpts;
    string curSrc, curBin; // CMAKE_CURRENT_SOURCE_DIR / BINARY_DIR
    vector<std::pair<string, string>> parentWrites; // PARENT_SCOPE writes seen in the running function/directory
    int depth = 0; bool returning = false, breaking = false, continuing = false; // continuing: continue() skips the rest of this loop pass
    std::map<string, string> srcProps; // "file|PROP" -> value (per-file properties collected as set_source_files_properties runs)
    bool testingOn = false;
    // Files the CMake code writes while it is read (file(WRITE/APPEND), cmake -E copy): path -> text. Only paths in the build folder
    // ({bin}) go into the manifest; the source tree is never written.
    std::map<string, string> written; std::set<string> folders; // folders: file(MAKE_DIRECTORY) in {bin}
    std::set<string> inputs; // every file read (CMake files, file(READ/STRINGS), CMAKE_CONFIGURE_DEPENDS): a change means read again
    bool readText(const string& path, string& out) {
        auto w = written.find(path); if (w != written.end()) { out = w->second; return true; }
        std::ifstream in(path, std::ios::binary); if (!in) return false;
        out.assign((std::istreambuf_iterator<char>(in)), {}); inputs.insert(path); return true;
    }
    int cmakeTool(const vector<string>& w); // `cmake -E ...` done here: CMake itself is never run
    std::map<string, string> globalProps; // set_property(GLOBAL ...)
    std::map<string, std::regex> rxCache; // each pattern is compiled once (the module grouping matches thousands of times)
    const std::regex& rx(const string& pattern) { auto it = rxCache.find(pattern); if (it == rxCache.end()) it = rxCache.emplace(pattern, std::regex(pattern)).first; return it->second; }
    std::map<string, vector<string>> dirDefsFinal; // add_compile_definitions as it stands at the end of each folder (CMake reads it when it generates)
    vector<std::pair<string, vector<string>>> deferred; // cmake_language(DEFER CALL ...) for the folder being read
    void runDeferred() { auto d = std::move(deferred); deferred.clear(); for (auto& c : d) { auto f = funcs.find(lower(c.first)); if (f != funcs.end()) callFunc(f->second, c.first, c.second); else tally.unknown["cmake_language(DEFER) of " + c.first]++; } }
    void doProperty(const string& n, const vector<string>& a);
    void parseArguments(const vector<string>& a);

    // ---- variables ----
    bool has(const string& k) const { return vars.count(k) || cache.count(k); }
    string get(const string& k) const {
        auto it = vars.find(k); if (it != vars.end()) return it->second;
        auto c = cache.find(k); if (c != cache.end()) return c->second;
        return "";
    }
    static vector<string> splitList(const string& s) {
        vector<string> r; string cur;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == ';') { cur += ';'; ++i; }
            else if (s[i] == ';') { r.push_back(cur); cur.clear(); }
            else cur += s[i];
        }
        r.push_back(cur);
        if (r.size() == 1 && r[0].empty()) r.clear();
        return r;
    }
    static string joinList(const vector<string>& v) { string r; for (size_t i = 0; i < v.size(); ++i) { if (i) r += ';'; r += v[i]; } return r; }
    static bool isTrue(const string& s) {
        string u = upper(s);
        if (u == "ON" || u == "YES" || u == "TRUE" || u == "Y") return true;
        if (u.empty() || u == "OFF" || u == "NO" || u == "FALSE" || u == "N" || u == "IGNORE" || u == "NOTFOUND" || u == "0") return false;
        if (u.size() >= 9 && u.compare(u.size() - 9, 9, "-NOTFOUND") == 0) return false;
        char* e = nullptr; double d = strtod(s.c_str(), &e); if (e && *e == 0 && e != s.c_str()) return d != 0;
        return true; // any other string counts as true for a constant; a variable name is looked up before this is asked
    }

    // ---- ${expansion} ----
    string expand(const string& s, int level = 0) {
        if (level > 50) throw ReadError{"variable expansion nests too deeply"};
        string out;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '$' && i + 1 < s.size() && (s[i + 1] == '{' || (s.compare(i, 5, "$ENV{") == 0))) {
                bool env = s[i + 1] == 'E'; size_t open = i + (env ? 4 : 1); int d = 0; size_t j = open;
                for (; j < s.size(); ++j) { if (s[j] == '{') ++d; else if (s[j] == '}' && --d == 0) break; }
                if (j >= s.size()) { out += s[i]; continue; }
                string name = expand(s.substr(open + 1, j - open - 1), level + 1);
                if (env) { const char* e = getenv(name.c_str()); out += e ? e : ""; }
                else out += get(name);
                i = j; continue;
            }
            if (s[i] == '$' && i + 1 < s.size() && s[i + 1] == '<') { // a generator expression: evaluate the few we know
                int d = 0; size_t j = i + 1;
                for (; j < s.size(); ++j) { if (s[j] == '<') ++d; else if (s[j] == '>' && --d == 0) break; }
                if (j >= s.size()) { out += s[i]; continue; }
                out += genex(s.substr(i + 2, j - i - 2)); i = j; continue;
            }
            out += s[i];
        }
        return out;
    }
    string genex(const string& body) {
        auto colon = body.find(':'); string head = body.substr(0, colon), rest = colon == string::npos ? "" : body.substr(colon + 1);
        if (head == "BUILD_INTERFACE") { tally.genex["BUILD_INTERFACE"]++; return expand(rest); }
        if (head == "INSTALL_INTERFACE") { tally.genex["INSTALL_INTERFACE"]++; return ""; }
        if (head == "TARGET_OBJECTS") { tally.genex["TARGET_OBJECTS"]++; return "{TARGET_OBJECTS:" + expand(rest) + "}"; } // the objects of an OBJECT library: linked in whole
        if (head == "TARGET_FILE") { tally.genex["TARGET_FILE"]++; return "{target:" + expand(rest) + "}"; }
        if (head == "TARGET_PROPERTY") {
            tally.genex["TARGET_PROPERTY"]++; auto comma = rest.find(','); string tn = expand(rest.substr(0, comma)), pn = comma == string::npos ? "" : rest.substr(comma + 1);
            Target* t = tgt(tn); if (!t) return ""; vector<string> v;
            if (pn == "INTERFACE_INCLUDE_DIRECTORIES") { v = t->pubIncs; v.insert(v.end(), t->ifIncs.begin(), t->ifIncs.end()); }
            else if (pn == "INTERFACE_COMPILE_DEFINITIONS") { v = t->pubDefs; v.insert(v.end(), t->ifDefs.begin(), t->ifDefs.end()); }
            else if (pn == "INTERFACE_COMPILE_OPTIONS") { v = t->pubOpts; v.insert(v.end(), t->ifOpts.begin(), t->ifOpts.end()); }
            return joinList(v);
        }
        if (head == "CXX_COMPILER_ID") return "";
        // $<LINK_ONLY:x> stays as text: it is a value CMake keeps in link properties (and a regex may look for it); a link item
        // spelled this way is linked as x (resolveTarget)
        if (head == "LINK_ONLY") { tally.genex["LINK_ONLY"]++; return "$<LINK_ONLY:" + expand(rest) + ">"; }
        tally.genex["$<" + head + ">"]++; return "";
    }
    // arguments as the command sees them: expanded, unquoted ones split at ';' and dropped when empty
    vector<string> args(const vector<Arg>& in, size_t from = 0) {
        vector<string> r;
        for (size_t i = from; i < in.size(); ++i) {
            if (in[i].bracket) { r.push_back(in[i].text); continue; } // [[ ]] text is taken exactly as written
            string e = expand(in[i].text);
            if (in[i].quoted) { string t; for (size_t k = 0; k < e.size(); ++k) { if (e[k] == '\\' && k + 1 < e.size() && e[k + 1] == ';') { t += ';'; ++k; } else t += e[k]; } r.push_back(t); }
            else for (auto& p : splitList(e)) r.push_back(p);
        }
        return r;
    }

    // ---- if() ----
    struct CTok { string s; bool quoted; };
    string deref(const CTok& t) { if (!t.quoted && has(t.s)) return get(t.s); return t.s; }
    bool truthy(const CTok& t) {
        if (t.quoted) return isTrue(t.s) && !(t.s.empty());
        if (has(t.s)) return isTrue(get(t.s));
        string u = upper(t.s);
        if (u == "ON" || u == "YES" || u == "TRUE" || u == "Y") return true;
        char* e = nullptr; double d = strtod(t.s.c_str(), &e); if (e && *e == 0 && e != t.s.c_str()) return d != 0;
        return false;
    }
    static int cmpVersion(const string& a, const string& b) {
        auto part = [](const string& s) { vector<long> v; std::stringstream ss(s); string x; while (std::getline(ss, x, '.')) v.push_back(atol(x.c_str())); return v; };
        auto x = part(a), y = part(b); size_t m = std::max(x.size(), y.size()); x.resize(m); y.resize(m);
        return x < y ? -1 : x > y ? 1 : 0;
    }
    bool cond(const vector<CTok>& t) {
        size_t p = 0;
        std::function<bool()> orE, andE, notE, binE;
        auto isOp = [&](size_t at, const char* w) { return at < t.size() && !t[at].quoted && t[at].s == w; };
        auto unary = [&]() -> bool {
            if (p < t.size() && !t[p].quoted && t[p].s == "(") {
                ++p; bool v = orE(); if (p < t.size() && t[p].s == ")") ++p; return v;
            }
            if (p + 1 < t.size() && !t[p].quoted) {
                const string& w = t[p].s;
                if (w == "DEFINED") { string nm = t[p + 1].s; p += 2; if (nm.compare(0, 4, "ENV{") == 0 && nm.back() == '}') return getenv(nm.substr(4, nm.size() - 5).c_str()) != nullptr; return has(nm); }
                if (w == "TARGET") { string nm = t[p + 1].s; p += 2; return targets.count(nm) > 0; }
                if (w == "COMMAND") { string nm = t[p + 1].s; p += 2; return funcs.count(lower(nm)) > 0 || knownCommand(lower(nm)); }
                if (w == "EXISTS") { string nm = deref(t[p + 1]); p += 2; std::error_code ec; return fs::exists(nm, ec); }
                if (w == "IS_DIRECTORY") { string nm = deref(t[p + 1]); p += 2; std::error_code ec; return fs::is_directory(nm, ec); }
                if (w == "IS_ABSOLUTE") { string nm = deref(t[p + 1]); p += 2; return !nm.empty() && nm[0] == '/'; }
                if (w == "POLICY") { p += 2; return true; }
            }
            if (p >= t.size()) return false;
            return truthy(t[p++]);
        };
        binE = [&]() -> bool {
            size_t start = p; bool left = unary();
            if (p < t.size() && !t[p].quoted) {
                const string w = t[p].s;
                static const std::set<string> bin = {"STREQUAL", "STRLESS", "STRGREATER", "EQUAL", "LESS", "GREATER", "LESS_EQUAL", "GREATER_EQUAL", "MATCHES", "IN_LIST",
                    "VERSION_LESS", "VERSION_GREATER", "VERSION_EQUAL", "VERSION_LESS_EQUAL", "VERSION_GREATER_EQUAL", "STRGREATER_EQUAL", "STRLESS_EQUAL", "PATH_EQUAL"};
                if (bin.count(w) && p + 1 < t.size() && start + 1 == p) { // the left side was a single word
                    string a = deref(t[start]), b = deref(t[p + 1]); p += 2;
                    if (w == "STREQUAL") return a == b; if (w == "STRLESS") return a < b; if (w == "STRGREATER") return a > b;
                    if (w == "STRLESS_EQUAL") return a <= b; if (w == "STRGREATER_EQUAL") return a >= b;
                    if (w == "EQUAL") return atof(a.c_str()) == atof(b.c_str()); if (w == "LESS") return atof(a.c_str()) < atof(b.c_str());
                    if (w == "GREATER") return atof(a.c_str()) > atof(b.c_str()); if (w == "LESS_EQUAL") return atof(a.c_str()) <= atof(b.c_str());
                    if (w == "GREATER_EQUAL") return atof(a.c_str()) >= atof(b.c_str());
                    if (w == "MATCHES") { try { std::smatch m; bool ok = std::regex_search(a, m, rx(b)); setMatch(ok, m); return ok; } catch (...) { return false; } }
                    if (w == "IN_LIST") { auto l = splitList(get(t[p - 1].s)); return std::find(l.begin(), l.end(), a) != l.end(); }
                    if (w == "VERSION_LESS") return cmpVersion(a, b) < 0; if (w == "VERSION_GREATER") return cmpVersion(a, b) > 0;
                    if (w == "VERSION_EQUAL") return cmpVersion(a, b) == 0; if (w == "VERSION_LESS_EQUAL") return cmpVersion(a, b) <= 0;
                    if (w == "VERSION_GREATER_EQUAL") return cmpVersion(a, b) >= 0;
                    return a == b;
                }
            }
            return left;
        };
        notE = [&]() -> bool { if (isOp(p, "NOT")) { ++p; return !notE(); } return binE(); };
        andE = [&]() -> bool { bool v = notE(); while (isOp(p, "AND")) { ++p; bool r = notE(); v = v && r; } return v; };
        orE = [&]() -> bool { bool v = andE(); while (isOp(p, "OR")) { ++p; bool r = andE(); v = v || r; } return v; };
        if (t.empty()) return false;
        return orE();
    }
    void setMatch(bool ok, const std::smatch& m) {
        if (!ok) return;
        vars["CMAKE_MATCH_0"] = m.str(0);
        for (size_t i = 1; i < 10; ++i) vars["CMAKE_MATCH_" + std::to_string(i)] = i < m.size() ? m.str(i) : "";
    }
    bool condArgs(const Cmd& c) {
        vector<CTok> t;
        for (auto& a : c.args) {
            if (a.bracket) { t.push_back({a.text, true}); continue; }
            string e = expand(a.text);
            if (a.quoted) t.push_back({e, true});
            else for (auto& p : splitList(e)) t.push_back({p, false});
        }
        return cond(t);
    }

    // ---- paths ----
    string abs(const string& p, const string& base) {
        if (p.empty()) return base;
        if (p.compare(0, 5, "{bin}") == 0) return binNormal(p);
        if (p[0] != '/' && base.compare(0, 5, "{bin}") == 0) return binNormal(base + "/" + p);
        string r = (p[0] == '/' ? fs::path(p) : fs::path(base) / p).lexically_normal().generic_string();
        while (r.size() > 1 && r.back() == '/') r.pop_back();
        return r;
    }

    // ---- known commands (for if(COMMAND ...) and the audit) ----
    static bool knownCommand(const string& n) {
        static const std::set<string> k = {"set", "unset", "option", "message", "if", "foreach", "function", "macro", "include", "add_subdirectory", "project",
            "list", "string", "file", "add_executable", "add_library", "target_link_libraries", "target_include_directories", "target_compile_definitions",
            "target_compile_options", "target_compile_features", "target_sources", "target_link_options", "set_target_properties", "add_test", "set_tests_properties",
            "find_package", "find_program", "find_path", "pkg_check_modules", "check_include_file_cxx", "add_compile_options", "add_compile_definitions",
            "add_link_options", "include_directories", "enable_testing", "cmake_minimum_required", "enable_language", "get_filename_component",
            "set_source_files_properties", "set_property", "get_property", "get_target_property", "cmake_parse_arguments", "separate_arguments", "cmake_language", "while", "find_library", "execute_process", "FetchContent_Declare", "FetchContent_MakeAvailable", "return", "break", "continue", "math", "cmake_policy"};
        return k.count(n) > 0;
    }

    // ---- running commands ----
    void run(const vector<Cmd>& cs, size_t b, size_t e);
    void exec(const vector<Cmd>& cs, size_t& i, size_t e);
    void callFunc(const Func& f, const string& name, const vector<string>& av);
    void sub(const string& dir);
    void includeFile(const string& file);
    void builtin(const Cmd& c);
    void doSet(const Cmd& c);
    void doList(const vector<string>& a);
    void doString(const vector<string>& a);
    void doFile(const vector<string>& a);
    void doTarget(const Cmd& c, const vector<string>& a);
    void doFind(const string& name, const vector<string>& a);
    void addTarget(const Cmd& c, const vector<string>& a, bool lib);
    Target* tgt(const string& n) { auto it = targets.find(n); if (it == targets.end()) return nullptr; if (!it->second.aliasOf.empty()) return tgt(it->second.aliasOf); return &it->second; }
    void setVar(const string& k, const string& v, bool parent) { if (parent) parentWrites.push_back({k, v}); else vars[k] = v; }
    void init();
    void runTop();
};

static size_t matchEnd(const vector<Cmd>& cs, size_t i, size_t e, const char* open, const char* close) {
    int d = 0;
    for (size_t k = i; k < e; ++k) {
        string n = lower(cs[k].name);
        if (n == open) ++d; else if (n == close && --d == 0) return k;
    }
    throw ReadError{cs[i].file + ":" + std::to_string(cs[i].line) + ": " + open + "() has no " + close + "()"};
}

inline void Interp::run(const vector<Cmd>& cs, size_t b, size_t e) {
    for (size_t i = b; i < e && !returning && !breaking && !continuing;) exec(cs, i, e);
}

inline void Interp::exec(const vector<Cmd>& cs, size_t& i, size_t e) {
    const Cmd& c = cs[i]; string n = lower(c.name);
    tally.run[n]++;
    if (n == "if") {
        size_t end = matchEnd(cs, i, e, "if", "endif");
        // split into branches at depth 1
        vector<size_t> marks{i}; int d = 0;
        for (size_t k = i; k <= end; ++k) {
            string kn = lower(cs[k].name);
            if (kn == "if") ++d; else if (kn == "endif") --d;
            else if (d == 1 && (kn == "elseif" || kn == "else")) marks.push_back(k);
        }
        marks.push_back(end);
        for (size_t m = 0; m + 1 < marks.size(); ++m) {
            string kn = lower(cs[marks[m]].name); bool take = kn == "else" ? true : condArgs(cs[marks[m]]);
            if (take) { run(cs, marks[m] + 1, marks[m + 1]); break; }
        }
        i = end + 1; return;
    }
    if (n == "foreach") {
        size_t end = matchEnd(cs, i, e, "foreach", "endforeach");
        auto a = args(c.args); vector<string> items; string var = a.empty() ? "" : a[0];
        if (a.size() >= 2 && a[1] == "IN") {
            size_t k = 2; string mode = k < a.size() ? a[k] : "";
            for (++k; k < a.size(); ++k) {
                if (a[k] == "LISTS" || a[k] == "ITEMS") { mode = a[k]; continue; }
                if (mode == "LISTS") for (auto& x : splitList(get(a[k]))) items.push_back(x); else items.push_back(a[k]);
            }
        } else if (a.size() >= 2 && a[1] == "RANGE") {
            long lo = 0, hi = 0; if (a.size() == 3) hi = atol(a[2].c_str()); else if (a.size() >= 4) { lo = atol(a[2].c_str()); hi = atol(a[3].c_str()); }
            for (long k = lo; k <= hi; ++k) items.push_back(std::to_string(k));
        } else for (size_t k = 1; k < a.size(); ++k) items.push_back(a[k]);
        bool had = has(var); string old = get(var);
        for (auto& it : items) { vars[var] = it; run(cs, i + 1, end); continuing = false; if (breaking) { breaking = false; break; } if (returning) break; }
        if (!had) vars.erase(var); else vars[var] = old;
        i = end + 1; return;
    }
    if (n == "while") { // the condition is checked before each pass; break() leaves the loop
        size_t end = matchEnd(cs, i, e, "while", "endwhile"); long passes = 0;
        while (!returning && condArgs(c)) {
            if (++passes > 100000) throw ReadError{c.file + ":" + std::to_string(c.line) + ": while() ran 100000 times; stopping"};
            run(cs, i + 1, end); continuing = false; if (breaking) { breaking = false; break; }
        }
        i = end + 1; return;
    }
    if (n == "function" || n == "macro") {
        string close = n == "function" ? "endfunction" : "endmacro"; size_t end = matchEnd(cs, i, e, n.c_str(), close.c_str());
        auto a = args(c.args); if (a.empty()) throw ReadError{c.file + ":" + std::to_string(c.line) + ": " + n + "() needs a name"};
        Func f; f.macro = n == "macro"; for (size_t k = 1; k < a.size(); ++k) f.params.push_back(a[k]);
        f.body.assign(cs.begin() + i + 1, cs.begin() + end); funcs[lower(a[0])] = f; i = end + 1; return;
    }
    if (n == "return") { returning = true; ++i; return; }
    if (n == "break") { breaking = true; ++i; return; }
    if (n == "continue") { continuing = true; ++i; return; }
    ++i;
    auto fit = funcs.find(n);
    if (fit != funcs.end()) { callFunc(fit->second, n, args(c.args)); return; }
    builtin(c);
}

inline void Interp::callFunc(const Func& f, const string& name, const vector<string>& av) {
    if (++depth > 40) throw ReadError{"function calls nest too deeply at " + name};
    if (!f.macro) vars.push(); auto savedWrites = parentWrites; parentWrites.clear();
    for (size_t k = 0; k < f.params.size(); ++k) vars[f.params[k]] = k < av.size() ? av[k] : "";
    vector<string> extra; for (size_t k = f.params.size(); k < av.size(); ++k) extra.push_back(av[k]);
    vars["ARGN"] = joinList(extra); vars["ARGC"] = std::to_string(av.size()); vars["ARGV"] = joinList(av);
    for (size_t k = 0; k < av.size() && k < 10; ++k) vars["ARGV" + std::to_string(k)] = av[k];
    run(f.body, 0, f.body.size()); returning = false;
    auto writes = parentWrites;
    if (!f.macro) vars.pop(); // a macro changes the caller's variables directly
    parentWrites = savedWrites;
    for (auto& w : writes) vars[w.first] = w.second;
    --depth;
}

inline void Interp::includeFile(const string& file) {
    std::ifstream in(file); if (!in) return;
    string text((std::istreambuf_iterator<char>(in)), {}); inputs.insert(file);
    auto cs = parseFile(text, fs::path(file).lexically_relative(opt.root).generic_string());
    string was = get("CMAKE_CURRENT_LIST_FILE"); vars["CMAKE_CURRENT_LIST_FILE"] = file;
    vars["CMAKE_CURRENT_LIST_DIR"] = fs::path(file).parent_path().generic_string();
    run(cs, 0, cs.size()); returning = false; vars["CMAKE_CURRENT_LIST_FILE"] = was;
}

inline void Interp::sub(const string& dir) {
    string src = abs(dir, curSrc); string file = src + "/CMakeLists.txt";
    std::ifstream in(file); if (!in) throw ReadError{"add_subdirectory: no CMakeLists.txt in " + src};
    string text((std::istreambuf_iterator<char>(in)), {}); inputs.insert(file);
    auto cs = parseFile(text, fs::path(file).lexically_relative(opt.root).generic_string());
    auto savedVars = vars; auto savedWrites = parentWrites; parentWrites.clear();
    auto sd = dirDefs, so = dirOpts, si = dirIncs, sl = dirLinkOpts; string ss = curSrc, sb = curBin;
    string rel = fs::path(src).lexically_relative(opt.root).generic_string(); if (rel == ".") rel = "";
    curSrc = src; curBin = opt.binDir + (rel.empty() ? "" : "/" + rel);
    vars["CMAKE_CURRENT_SOURCE_DIR"] = curSrc; vars["CMAKE_CURRENT_BINARY_DIR"] = curBin; vars["CMAKE_CURRENT_LIST_FILE"] = file; vars["CMAKE_CURRENT_LIST_DIR"] = curSrc;
    auto savedDefer = std::move(deferred); deferred.clear();
    run(cs, 0, cs.size()); returning = false; runDeferred(); deferred = std::move(savedDefer);
    dirDefsFinal[curSrc] = dirDefs;
    auto writes = parentWrites;
    vars = savedVars; parentWrites = savedWrites;
    for (auto& w : writes) vars[w.first] = w.second;
    dirDefs = sd; dirOpts = so; dirIncs = si; dirLinkOpts = sl; curSrc = ss; curBin = sb;
}

inline void Interp::doSet(const Cmd& c) {
    auto a = args(c.args); if (a.empty()) return;
    string name = a[0]; bool parent = false, isCache = false, force = false; vector<string> vals;
    for (size_t k = 1; k < a.size(); ++k) {
        if (a[k] == "PARENT_SCOPE") parent = true;
        else if (a[k] == "CACHE") { isCache = true; k += 2; } // CACHE <type> <docstring>
        else if (a[k] == "FORCE") force = true;
        else if (a[k] == "INTERNAL" || a[k] == "BOOL" || a[k] == "STRING") continue;
        else vals.push_back(a[k]);
    }
    // quoted arguments are not split, so "a;b" stays one element, but a list value is just ';'-joined text either way
    string v = joinList(vals);
    if (isCache) {
        auto d = opt.defs.find(name);
        if (d != opt.defs.end() && !force) { cache[name] = d->second; tally.setOptions++; return; }
        if (!cache.count(name) || force) cache[name] = v;
        return;
    }
    if (c.args.size() == 1 || (a.size() == 1)) { if (parent) parentWrites.push_back({name, ""}); else vars.erase(name); return; }
    setVar(name, v, parent);
}

inline void Interp::doList(const vector<string>& a) {
    if (a.size() < 2) return; const string& op = a[0]; const string& var = a[1];
    vector<string> cur = splitList(get(var));
    if (op == "APPEND") { for (size_t k = 2; k < a.size(); ++k) cur.push_back(a[k]); vars[var] = joinList(cur); }
    else if (op == "PREPEND") { cur.insert(cur.begin(), a.begin() + 2, a.end()); vars[var] = joinList(cur); }
    else if (op == "LENGTH" && a.size() > 2) vars[a[2]] = std::to_string(cur.size());
    else if (op == "GET" && a.size() > 3) {
        vector<string> r; for (size_t k = 2; k + 1 < a.size(); ++k) { long idx = atol(a[k].c_str()); if (idx < 0) idx += (long)cur.size(); r.push_back(idx >= 0 && idx < (long)cur.size() ? cur[(size_t)idx] : ""); }
        vars[a.back()] = joinList(r);
    }
    else if (op == "POP_FRONT" || op == "POP_BACK") { // list(POP_FRONT <list> [<out>...]): each out takes one item; with none, one item is dropped
        bool front = op == "POP_FRONT"; vector<string> outs(a.begin() + 2, a.end());
        if (outs.empty()) { if (!cur.empty()) { if (front) cur.erase(cur.begin()); else cur.pop_back(); } }
        for (auto& o : outs) { if (cur.empty()) break; if (front) { vars[o] = cur.front(); cur.erase(cur.begin()); } else { vars[o] = cur.back(); cur.pop_back(); } }
        if (cur.empty()) vars.erase(var); else vars[var] = joinList(cur);
    }
    else if (op == "REMOVE_ITEM") { vector<string> r; for (auto& x : cur) if (std::find(a.begin() + 2, a.end(), x) == a.end()) r.push_back(x); vars[var] = joinList(r); }
    else if (op == "REMOVE_DUPLICATES") { vector<string> r; for (auto& x : cur) if (std::find(r.begin(), r.end(), x) == r.end()) r.push_back(x); vars[var] = joinList(r); }
    else if (op == "FIND" && a.size() > 3) { auto it = std::find(cur.begin(), cur.end(), a[2]); vars[a[3]] = it == cur.end() ? "-1" : std::to_string(it - cur.begin()); }
    else if (op == "JOIN" && a.size() > 3) { string r; for (size_t k = 0; k < cur.size(); ++k) { if (k) r += a[2]; r += cur[k]; } vars[a[3]] = r; }
    else if (op == "SORT") { std::sort(cur.begin(), cur.end()); vars[var] = joinList(cur); }
    else if (op == "INSERT" && a.size() > 3) { size_t at = (size_t)std::min<long>(std::max<long>(0, atol(a[2].c_str())), (long)cur.size()); cur.insert(cur.begin() + at, a.begin() + 3, a.end()); vars[var] = joinList(cur); }
    else if (op == "TRANSFORM" && a.size() >= 3) { // list(TRANSFORM <list> <ACTION> [args] [REGEX r] [OUTPUT_VARIABLE v])
        const string& act = a[2]; size_t k = 3; string arg1, arg2, outVar = var, sel;
        if (act == "APPEND" || act == "PREPEND") { if (k < a.size()) arg1 = a[k++]; }
        else if (act == "REPLACE") { if (k + 1 < a.size()) { arg1 = a[k]; arg2 = a[k + 1]; k += 2; } }
        else if (act != "TOLOWER" && act != "TOUPPER" && act != "STRIP" && act != "GENEX_STRIP") { tally.unknown["list(TRANSFORM " + act + ")"]++; return; }
        for (; k < a.size(); ++k) {
            if (a[k] == "OUTPUT_VARIABLE" && k + 1 < a.size()) outVar = a[++k];
            else if (a[k] == "REGEX" && k + 1 < a.size()) sel = a[++k];
            else { tally.unknown["list(TRANSFORM) selector " + a[k]]++; return; }
        }
        try {
            std::regex selRx(sel.empty() ? string(".*") : sel);
            for (auto& x : cur) {
                if (!sel.empty() && !std::regex_search(x, selRx)) continue;
                if (act == "APPEND") x += arg1; else if (act == "PREPEND") x = arg1 + x;
                else if (act == "TOLOWER") for (auto& ch : x) ch = (char)tolower((unsigned char)ch);
                else if (act == "TOUPPER") for (auto& ch : x) ch = (char)toupper((unsigned char)ch);
                else if (act == "STRIP") { size_t b = x.find_first_not_of(" \t\r\n"), e2 = x.find_last_not_of(" \t\r\n"); x = b == string::npos ? "" : x.substr(b, e2 - b + 1); }
                else if (act == "GENEX_STRIP") { if (x.find("$<") != string::npos) x.clear(); }
                else if (act == "REPLACE") { string rep2; for (size_t q = 0; q < arg2.size(); ++q) { if (arg2[q] == '\\' && q + 1 < arg2.size() && isdigit((unsigned char)arg2[q + 1])) { rep2 += '$'; rep2 += arg2[++q]; } else if (arg2[q] == '$') rep2 += "$$"; else rep2 += arg2[q]; } x = std::regex_replace(x, rx(arg1), rep2); }
            }
        } catch (const std::regex_error&) { tally.unknown["list(TRANSFORM) regex"]++; return; }
        if (act == "GENEX_STRIP") { vector<string> r; for (auto& x : cur) if (!x.empty()) r.push_back(x); cur = r; }
        vars[outVar] = joinList(cur);
    }
    else tally.unknown["list(" + op + ")"]++;
}

// get_property / set_property for the scopes the project uses: GLOBAL, TARGET, SOURCE (and DIRECTORY CMAKE_CONFIGURE_DEPENDS).
inline void Interp::doProperty(const string& n, const vector<string>& a) {
    if (n == "set_property") {
        if (a.empty()) return; const string scope = a[0]; size_t k = 1; vector<string> names; bool append = false, appendStr = false;
        for (; k < a.size() && a[k] != "PROPERTY"; ++k) { if (a[k] == "APPEND") append = true; else if (a[k] == "APPEND_STRING") appendStr = true; else names.push_back(a[k]); }
        if (k + 1 >= a.size()) return; string prop = a[k + 1]; vector<string> vals(a.begin() + (long)k + 2, a.end());
        auto put = [&](string& slot) { if (appendStr) { for (auto& v : vals) slot += v; } else if (append) { auto l = splitList(slot); for (auto& v : vals) l.push_back(v); slot = joinList(l); } else slot = joinList(vals); };
        if (scope == "GLOBAL") { put(globalProps[prop]); return; }
        if (scope == "DIRECTORY") { if (prop == "CMAKE_CONFIGURE_DEPENDS") for (auto& v : vals) inputs.insert(abs(v, curSrc)); return; }
        if (scope == "TARGET") { for (auto& t : names) if (Target* tt = tgt(t)) put(tt->props[prop]); return; }
        if (scope == "SOURCE") {
            for (auto& f : names) { string key = abs(f, curSrc) + "|" + prop; string slot = srcProps.count(key) ? srcProps[key] : ""; put(slot); srcProps[key] = slot; }
            return;
        }
        if (scope == "TEST") { // the tests made so far with these names (labels, environment, ...); the last test of a name is the one meant
            for (auto& nm : names) for (auto it = tests.rbegin(); it != tests.rend(); ++it) if (it->name == nm) { put(it->props[prop]); break; }
            return;
        }
        if (scope == "CACHE") return; // STRINGS, HELPSTRING, ADVANCED: hints for CMake's editors; nothing to build
        tally.unknown["set_property(" + scope + ")"]++; return;
    }
    // get_property(<var> <scope> [name] PROPERTY <prop> [SET|DEFINED])
    if (a.size() < 4) return; const string& var = a[0]; const string& scope = a[1]; size_t k = 2; string name;
    if (scope != "GLOBAL" && k < a.size() && a[k] != "PROPERTY") name = a[k++];
    if (k >= a.size() || a[k] != "PROPERTY" || k + 1 >= a.size()) return; string prop = a[k + 1];
    bool askSet = k + 2 < a.size() && (a[k + 2] == "SET" || a[k + 2] == "DEFINED");
    bool found = false; string v;
    if (scope == "GLOBAL") { auto it = globalProps.find(prop); if (it != globalProps.end()) { found = true; v = it->second; } }
    else if (scope == "TARGET") { if (Target* t = tgt(name)) { auto it = t->props.find(prop); if (it != t->props.end()) { found = true; v = it->second; } } }
    else if (scope == "SOURCE") { auto it = srcProps.find(abs(name, curSrc) + "|" + prop); if (it != srcProps.end()) { found = true; v = it->second; } }
    else if (scope == "DIRECTORY" && prop == "TESTS") { // the tests added so far in that folder, in order
        string dir = name.empty() ? curSrc : abs(name, curSrc); vector<string> names;
        for (auto& t : tests) if (t.dir == dir) names.push_back(t.name);
        found = true; v = joinList(names);
    }
    else { tally.unknown["get_property(" + scope + (scope == "DIRECTORY" ? " " + prop : string()) + ")"]++; }
    vars[var] = askSet ? (found ? "1" : "0") : v;
}

// cmake_parse_arguments(<prefix> <options> <one_value_keywords> <multi_value_keywords> <args>...) and the PARSE_ARGV <N> form.
inline void Interp::parseArguments(const vector<string>& a0) {
    vector<string> a = a0; vector<string> items; size_t first = 4;
    if (!a.empty() && a[0] == "PARSE_ARGV") {
        if (a.size() < 5) return; long from = atol(a[1].c_str()); a.erase(a.begin(), a.begin() + 2);
        long argc = atol(get("ARGC").c_str()); for (long q = from; q < argc; ++q) items.push_back(get("ARGV" + std::to_string(q)));
        first = a.size();
    }
    if (a.size() < 4) return;
    const string pre = a[0] + "_"; auto opts = splitList(a[1]), ones = splitList(a[2]), multis = splitList(a[3]);
    for (size_t q = first; q < a.size(); ++q) items.push_back(a[q]);
    auto in = [](const vector<string>& l, const string& w) { return std::find(l.begin(), l.end(), w) != l.end(); };
    std::map<string, vector<string>> got; std::set<string> seen; vector<string> unparsed, missing; string cur; bool curMulti = false, curHas = false;
    auto close = [&]() { if (!cur.empty() && !curHas) missing.push_back(cur); cur.clear(); };
    for (auto& w : items) {
        if (in(opts, w)) { close(); seen.insert(w); continue; }
        if (in(ones, w) || in(multis, w)) { close(); cur = w; curMulti = in(multis, w); curHas = false; seen.insert(w); got[w].clear(); continue; }
        if (!cur.empty()) { got[cur].push_back(w); curHas = true; if (!curMulti) cur.clear(); continue; }
        unparsed.push_back(w);
    }
    close();
    for (auto& o : opts) vars[pre + o] = seen.count(o) ? "TRUE" : "FALSE";
    for (auto* l : {&ones, &multis}) for (auto& o : *l) { auto it = got.find(o); if (it == got.end() || it->second.empty()) vars.erase(pre + o); else vars[pre + o] = joinList(it->second); }
    if (unparsed.empty()) vars.erase(pre + "UNPARSED_ARGUMENTS"); else vars[pre + "UNPARSED_ARGUMENTS"] = joinList(unparsed);
    if (missing.empty()) vars.erase(pre + "KEYWORDS_MISSING_VALUES"); else vars[pre + "KEYWORDS_MISSING_VALUES"] = joinList(missing);
}

static string cmakeRegexToStd(const string& r) { return r; }

inline void Interp::doString(const vector<string>& a) {
    if (a.empty()) return; const string& op = a[0];
    try {
        if (op == "REPLACE" && a.size() >= 5) { string in; for (size_t k = 4; k < a.size(); ++k) in += a[k]; string r = in; size_t p = 0; while (!a[1].empty() && (p = r.find(a[1], p)) != string::npos) { r.replace(p, a[1].size(), a[2]); p += a[2].size(); } vars[a[3]] = r; return; }
        if (op == "FIND" && a.size() >= 4) { size_t p = a[1].find(a[2]); if (a.size() > 4 && a[4] == "REVERSE") p = a[1].rfind(a[2]); vars[a[3]] = p == string::npos ? "-1" : std::to_string(p); return; }
        if (op == "SUBSTRING" && a.size() >= 5) { long b = atol(a[2].c_str()), l = atol(a[3].c_str()); string s = a[1]; if (b > (long)s.size()) b = (long)s.size(); vars[a[4]] = l < 0 ? s.substr((size_t)b) : s.substr((size_t)b, (size_t)l); return; }
        if (op == "LENGTH" && a.size() >= 3) { vars[a[2]] = std::to_string(a[1].size()); return; }
        if (op == "TOUPPER" && a.size() >= 3) { vars[a[2]] = upper(a[1]); return; }
        if (op == "TOLOWER" && a.size() >= 3) { vars[a[2]] = lower(a[1]); return; }
        if (op == "APPEND" && a.size() >= 2) { string s = get(a[1]); for (size_t k = 2; k < a.size(); ++k) s += a[k]; vars[a[1]] = s; return; }
        if (op == "CONCAT" && a.size() >= 2) { string s; for (size_t k = 2; k < a.size(); ++k) s += a[k]; vars[a[1]] = s; return; }
        if (op == "JOIN" && a.size() >= 3) { string s; for (size_t k = 3; k < a.size(); ++k) { if (k > 3) s += a[1]; s += a[k]; } vars[a[2]] = s; return; }
        if (op == "STRIP" && a.size() >= 3) { string s = a[1]; size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n"); vars[a[2]] = b == string::npos ? "" : s.substr(b, e - b + 1); return; }
        if (op == "REGEX" && a.size() >= 2) {
            const string& m = a[1];
            if (m == "REPLACE" && a.size() >= 5) {
                string rep = a[3]; string r2; // \1 -> $1 for the standard library
                for (size_t k = 0; k < rep.size(); ++k) { if (rep[k] == '\\' && k + 1 < rep.size() && isdigit((unsigned char)rep[k + 1])) { r2 += '$'; r2 += rep[++k]; } else if (rep[k] == '$') r2 += "$$"; else r2 += rep[k]; }
                string in; for (size_t k = 5; k < a.size(); ++k) in += a[k];
                vars[a[4]] = std::regex_replace(in, rx(cmakeRegexToStd(a[2])), r2); return;
            }
            if ((m == "MATCH" || m == "MATCHALL") && a.size() >= 5) {
                string in; for (size_t k = 4; k < a.size(); ++k) in += a[k]; const std::regex& re = rx(cmakeRegexToStd(a[2])); vector<string> found;
                for (auto it = std::sregex_iterator(in.begin(), in.end(), re); it != std::sregex_iterator(); ++it) { found.push_back(it->str(0)); if (m == "MATCH") { setMatch(true, *it); break; } }
                vars[a[3]] = joinList(found); return;
            }
        }
        if (op == "MD5" && a.size() >= 2) { string in; for (size_t k = 2; k < a.size(); ++k) in += a[k]; vars[a[1]] = md5Hex(in); return; }
        if (op == "SHA1" || op == "SHA256") { if (a.size() >= 2) vars[a[1]] = "0"; tally.unknown["string(" + op + ")"]++; return; }
    } catch (const std::regex_error&) { tally.unknown["string(regex)"]++; return; }
    tally.unknown["string(" + op + ")"]++;
}

// math(EXPR): 64-bit integers with ( ) unary - + ~, then * / %, + -, << >>, &, ^, | (lowest), as in C. Decimal and 0x numbers.
// + - * and unary - wrap around (two's complement, done on unsigned numbers so it is defined); a shift outside 0..63, a division
// by zero and the smallest number divided by -1 are errors.
static bool mathExpr(const string& src, long long& out) {
    size_t p = 0; bool ok = true;
    auto ws = [&]() { while (p < src.size() && isspace((unsigned char)src[p])) ++p; };
    std::function<long long(int)> bin;
    std::function<long long()> unary = [&]() -> long long {
        ws(); if (p >= src.size()) { ok = false; return 0; }
        char ch = src[p];
        if (ch == '-') { ++p; return (long long)(0ull - (unsigned long long)unary()); } if (ch == '+') { ++p; return unary(); } if (ch == '~') { ++p; return ~unary(); }
        if (ch == '(') { ++p; long long v = bin(0); ws(); if (p < src.size() && src[p] == ')') ++p; else ok = false; return v; }
        if (!isdigit((unsigned char)ch)) { ok = false; return 0; }
        size_t used = 0; long long v = 0;
        try { v = std::stoll(src.substr(p), &used, src.compare(p, 2, "0x") == 0 || src.compare(p, 2, "0X") == 0 ? 16 : 10); } catch (...) { ok = false; return 0; }
        p += used; return v;
    };
    // levels: 0 |, 1 ^, 2 &, 3 << >>, 4 + -, 5 * / %
    bin = [&](int level) -> long long {
        if (level > 5) return unary();
        long long v = bin(level + 1);
        for (;;) {
            ws(); if (p >= src.size()) return v;
            string op;
            if (level == 0 && src[p] == '|') op = "|"; else if (level == 1 && src[p] == '^') op = "^"; else if (level == 2 && src[p] == '&') op = "&";
            else if (level == 3 && (src.compare(p, 2, "<<") == 0 || src.compare(p, 2, ">>") == 0)) op = src.substr(p, 2);
            else if (level == 4 && (src[p] == '+' || src[p] == '-')) op = src.substr(p, 1);
            else if (level == 5 && (src[p] == '*' || src[p] == '/' || src[p] == '%')) op = src.substr(p, 1);
            if (op.empty()) return v;
            p += op.size(); long long r = bin(level + 1);
            if ((op == "/" || op == "%") && (r == 0 || (r == -1 && v == std::numeric_limits<long long>::min()))) { ok = false; return 0; }
            if ((op == "<<" || op == ">>") && (r < 0 || r > 63)) { ok = false; return 0; }
            typedef unsigned long long U;
            if (op == "|") v |= r; else if (op == "^") v ^= r; else if (op == "&") v &= r;
            else if (op == "<<") v = (long long)((U)v << r); else if (op == ">>") v >>= r; // >> of a negative number keeps the sign (C++20 rule; g++ and clang do this)
            else if (op == "+") v = (long long)((U)v + (U)r); else if (op == "-") v = (long long)((U)v - (U)r); else if (op == "*") v = (long long)((U)v * (U)r);
            else if (op == "/") v /= r; else v %= r;
        }
    };
    out = bin(0); ws(); return ok && p == src.size();
}

// The `cmake -E` commands the project runs while it is configured, done in memory. Written from what each command is documented to do.
inline int Interp::cmakeTool(const vector<string>& w) {
    if (w.size() < 3 || w[1] != "-E") { tally.unknown["execute_process(cmake)"]++; return 1; }
    const string& op = w[2];
    if ((op == "copy_if_different" || op == "copy") && w.size() >= 5) { // copy <file>... <destination>; a folder destination gets each file by its name
        string dest = abs(w.back(), curSrc); bool many = w.size() > 5, isDir = many || (!w.back().empty() && w.back().back() == '/');
        for (auto& kv : written) if (kv.first.compare(0, dest.size() + 1, dest + "/") == 0) { isDir = true; break; }
        for (size_t k = 3; k + 1 < w.size(); ++k) {
            string from = abs(w[k], curSrc), text; if (!readText(from, text)) return 1;
            string to = isDir ? binNormal(dest + "/" + fs::path(from).filename().generic_string()) : dest;
            if (!inBin(to)) { tally.unknown["write outside the build folder"]++; return 1; }
            written[to] = text;
        }
        return 0;
    }
    if (op == "make_directory" || op == "remove" || op == "rm" || op == "touch" || op == "echo") return 0; // nothing to keep: folders are made when files are written
    tally.unknown["cmake -E " + op]++; return 1;
}

inline void Interp::doFile(const vector<string>& a) {
    if (a.empty()) return; const string& op = a[0];
    if (op == "READ" && a.size() >= 3) { string t; readText(abs(a[1], curSrc), t); vars[a[2]] = t; return; }
    if (op == "STRINGS" && a.size() >= 3) {
        string whole; readText(abs(a[1], curSrc), whole); std::istringstream in(whole); string line; vector<string> found; string re;
        for (size_t k = 3; k + 1 < a.size(); ++k) if (a[k] == "REGEX") re = a[k + 1];
        std::regex rx; bool use = !re.empty(); try { if (use) rx = std::regex(re); } catch (...) { use = false; }
        while (std::getline(in, line)) { if (!line.empty() && line.back() == '\r') line.pop_back(); if (!use || std::regex_search(line, rx)) { string esc; for (char ch : line) { if (ch == ';') esc += "\\;"; else esc += ch; } found.push_back(esc); } }
        vars[a[2]] = joinList(found); return;
    }
    if (op == "GLOB" || op == "GLOB_RECURSE") {
        vector<string> found; string var = a.size() > 1 ? a[1] : "", relTo;
        auto wild = [](const string& x) { return x.find_first_of("*?[") != string::npos; };
        auto rxOf = [](const string& fnpat) { string rx; for (char ch : fnpat) { if (ch == '*') rx += "[^/]*"; else if (ch == '?') rx += "[^/]"; else if (strchr(".+()[]{}^$|\\", ch)) { rx += '\\'; rx += ch; } else rx += ch; } return std::regex(rx); };
        for (size_t k = 2; k < a.size(); ++k) {
            if (a[k] == "CONFIGURE_DEPENDS" || a[k] == "FOLLOW_SYMLINKS") continue;
            if (a[k] == "RELATIVE" && k + 1 < a.size()) { relTo = abs(a[++k], curSrc); continue; }
            if (a[k] == "LIST_DIRECTORIES" && k + 1 < a.size()) { ++k; continue; }
            fs::path pat = abs(a[k], curSrc); std::error_code ec;
            // folders: walk each part; a part with * or ? matches the folder names in the folders found so far
            vector<string> parts; for (auto& q : pat.parent_path()) parts.push_back(q.generic_string());
            vector<fs::path> dirs{fs::path()};
            for (auto& q : parts) {
                vector<fs::path> next;
                for (auto& d : dirs) {
                    if (!wild(q)) { next.push_back(d / q); continue; }
                    std::regex re = rxOf(q);
                    for (auto& e : fs::directory_iterator(d, ec)) if (e.is_directory(ec) && std::regex_match(e.path().filename().string(), re)) next.push_back(e.path());
                }
                dirs.swap(next);
            }
            std::regex re = rxOf(pat.filename().string());
            for (auto& dir : dirs) {
                if (!fs::is_directory(dir, ec)) continue;
                auto take = [&](const fs::path& p) { if (std::regex_match(p.filename().string(), re)) found.push_back(p.lexically_normal().generic_string()); };
                if (op == "GLOB") for (auto& e : fs::directory_iterator(dir, ec)) take(e.path());
                else for (auto& e : fs::recursive_directory_iterator(dir, ec)) if (e.is_regular_file()) take(e.path());
            }
        }
        if (!relTo.empty()) for (auto& f : found) f = fs::path(f).lexically_relative(relTo).generic_string();
        std::sort(found.begin(), found.end()); vars[var] = joinList(found); return;
    }
    if (op == "RELATIVE_PATH" && a.size() >= 4) { vars[a[1]] = fs::path(abs(a[3], curSrc)).lexically_relative(abs(a[2], curSrc)).generic_string(); return; }
    if ((op == "WRITE" || op == "APPEND") && a.size() >= 2) { // kept in memory; a file in the build folder goes into the manifest
        string p = abs(a[1], curSrc), text; for (size_t k = 2; k < a.size(); ++k) text += a[k];
        if (!inBin(p)) { tally.unknown["write outside the build folder"]++; return; }
        if (op == "WRITE") written[p] = text; else { string old; readText(p, old); written[p] = old + text; }
        return;
    }
    if (op == "MAKE_DIRECTORY") { // a folder in the build folder is made by the build (a linker map, a log folder ...); others are not ours to make
        for (size_t k = 1; k < a.size(); ++k) { string p = abs(a[k], curSrc); if (inBin(p)) folders.insert(p); else if (p.compare(0, 5, "{bin}") == 0) tally.unknown["write outside the build folder"]++; }
        return;
    }
    if (op == "COPY" || op == "REMOVE" || op == "REMOVE_RECURSE" || op == "TOUCH") return; // the build writes nothing here
    tally.unknown["file(" + op + ")"]++;
}

inline void Interp::doFind(const string& name, const vector<string>& a) {
    if (name == "find_package") {
        if (a.empty()) return; const string& p = a[0]; bool required = std::find(a.begin(), a.end(), "REQUIRED") != a.end();
        bool found = false;
        if (p == "Threads") { found = true; Target t; t.name = "Threads::Threads"; t.kind = "imported"; t.ifLinks = {"pthread"}; targets[t.name] = t; vars["CMAKE_THREAD_LIBS_INIT"] = "-pthread"; vars["Threads_FOUND"] = "TRUE"; vars["THREADS_FOUND"] = "TRUE"; }
        else if (p == "SDL2") {
            std::error_code ec; bool h = fs::exists("/usr/include/SDL2/SDL.h", ec) || fs::exists("/usr/include/x86_64-linux-gnu/SDL2/SDL.h", ec) || fs::exists("/usr/local/include/SDL2/SDL.h", ec);
            if (h) { found = true; string inc = fs::exists("/usr/include/SDL2/SDL.h", ec) ? "/usr/include/SDL2" : fs::exists("/usr/local/include/SDL2/SDL.h", ec) ? "/usr/local/include/SDL2" : "/usr/include/x86_64-linux-gnu/SDL2";
                Target t; t.name = "SDL2::SDL2"; t.kind = "imported"; t.ifLinks = {"SDL2"}; t.ifIncs = {inc}; targets[t.name] = t;
                vars["SDL2_INCLUDE_DIRS"] = inc; vars["SDL2_LIBRARIES"] = "SDL2"; vars["SDL2_FOUND"] = "TRUE"; }
        }
        else if (p == "OpenGL") { std::error_code ec; found = fs::exists("/usr/include/GL/gl.h", ec); if (found) { Target t; t.name = "OpenGL::GL"; t.kind = "imported"; t.ifLinks = {"GL"}; targets[t.name] = t; vars["OPENGL_gl_LIBRARY"] = "GL"; vars["OPENGL_FOUND"] = "TRUE"; } }
        else if (p == "PkgConfig") { found = system("pkg-config --version >/dev/null 2>&1") == 0; if (found) vars["PkgConfig_FOUND"] = "TRUE"; }
        if (!found && required) throw ReadError{"find_package(" + p + " REQUIRED): not found on this machine"};
        if (!found) { vars[p + "_FOUND"] = "FALSE"; }
        return;
    }
    if (name == "find_program") {
        if (a.empty()) return; const string& var = a[0]; vector<string> names; bool inNames = false;
        for (size_t k = 1; k < a.size(); ++k) { if (a[k] == "NAMES") { inNames = true; continue; } if (a[k] == "PATHS" || a[k] == "HINTS" || a[k] == "REQUIRED" || a[k] == "NO_DEFAULT_PATH") { inNames = false; continue; } if (inNames || names.empty()) names.push_back(a[k]); }
        const char* pe = getenv("PATH"); vector<string> dirs; { std::stringstream ss(pe ? pe : ""); string d; while (std::getline(ss, d, ':')) dirs.push_back(d); }
        for (auto& n : names) {
            if (!n.empty() && n[0] == '/') { std::error_code ec; if (fs::exists(n, ec)) { cache[var] = n; return; } continue; }
            for (auto& d : dirs) { std::error_code ec; string cand = d + "/" + n; if (fs::is_regular_file(cand, ec) && (fs::status(cand, ec).permissions() & (fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec)) != fs::perms::none) { cache[var] = cand; return; } }
        }
        cache[var] = var + "-NOTFOUND"; return;
    }
    if (name == "find_library") { // the library folders of this host; a name is tried as lib<name>.so, lib<name>.a and as given
        if (a.empty()) return; const string& var = a[0];
        { string was = get(var); if (!was.empty() && was.find("-NOTFOUND") == string::npos) return; } // already found (a cache entry): kept
        vector<string> names, dirs; int mode = 0; // 0 names, 1 paths
        for (size_t k = 1; k < a.size(); ++k) {
            if (a[k] == "NAMES") { mode = 0; continue; } if (a[k] == "PATHS" || a[k] == "HINTS") { mode = 1; continue; }
            if (a[k] == "REQUIRED" || a[k] == "NO_DEFAULT_PATH" || a[k] == "NAMES_PER_DIR" || a[k] == "PATH_SUFFIXES" || a[k] == "DOC") { mode = 2; continue; }
            if (mode == 0) names.push_back(a[k]); else if (mode == 1) dirs.push_back(abs(a[k], curSrc));
        }
        for (const char* d : {"/usr/local/lib", "/usr/lib/x86_64-linux-gnu", "/usr/lib", "/lib/x86_64-linux-gnu", "/lib"}) dirs.push_back(d);
        for (auto& d : dirs) for (auto& nm : names) for (const string& cand : {d + "/lib" + nm + ".so", d + "/lib" + nm + ".a", d + "/" + nm}) {
            std::error_code ec; if (fs::is_regular_file(cand, ec)) { cache[var] = cand; return; }
        }
        cache[var] = var + "-NOTFOUND"; return;
    }
    if (name == "find_path") {
        if (a.size() < 2) return; for (const char* d : {"/usr/include", "/usr/local/include", "/usr/include/x86_64-linux-gnu"}) { std::error_code ec; if (fs::exists(string(d) + "/" + a[1], ec)) { cache[a[0]] = d; return; } }
        cache[a[0]] = a[0] + "-NOTFOUND"; return;
    }
    if (name == "check_include_file_cxx" || name == "check_include_file") {
        if (a.size() < 2) return; bool ok = false;
        for (const char* d : {"/usr/include", "/usr/local/include", "/usr/include/x86_64-linux-gnu"}) { std::error_code ec; if (fs::exists(string(d) + "/" + a[0], ec)) ok = true; }
        cache[a[1]] = ok ? "1" : ""; return;
    }
    if (name == "pkg_check_modules") {
        if (a.size() < 2) return; const string& pre = a[0]; vector<string> mods; bool imported = false;
        for (size_t k = 1; k < a.size(); ++k) { if (a[k] == "IMPORTED_TARGET") imported = true; else if (a[k] != "QUIET" && a[k] != "REQUIRED" && a[k] != "REQUIRED") mods.push_back(a[k]); }
        string ml; for (auto& m : mods) ml += " '" + m + "'";
        auto capture = [&](const string& cmd) { string r; FILE* f = CMR_POPEN((cmd + " 2>/dev/null").c_str(), "r"); if (!f) return r; char b[512]; size_t n; while ((n = fread(b, 1, sizeof b, f)) > 0) r.append(b, n); CMR_PCLOSE(f); while (!r.empty() && (r.back() == '\n' || r.back() == ' ')) r.pop_back(); return r; };
        if (system(("pkg-config --exists" + ml + " >/dev/null 2>&1").c_str()) != 0) { vars[pre + "_FOUND"] = ""; return; }
        vars[pre + "_FOUND"] = "1";
        auto words = [&](const string& s, const char* pfx) { vector<string> r; std::stringstream ss(s); string w; size_t L = strlen(pfx); while (ss >> w) if (w.compare(0, L, pfx) == 0) r.push_back(w.substr(L)); return r; };
        vars[pre + "_LIBRARIES"] = joinList(words(capture("pkg-config --libs-only-l" + ml), "-l"));
        vars[pre + "_INCLUDE_DIRS"] = joinList(words(capture("pkg-config --cflags-only-I" + ml), "-I"));
        if (imported) { Target t; t.name = "PkgConfig::" + pre; t.kind = "imported"; t.ifLinks = splitList(get(pre + "_LIBRARIES")); t.ifIncs = splitList(get(pre + "_INCLUDE_DIRS")); targets[t.name] = t; }
        return;
    }
}

inline void Interp::addTarget(const Cmd& c, const vector<string>& a, bool lib) {
    if (a.empty()) return; Target t; t.name = a[0]; t.dir = curSrc;
    size_t k = 1; string type;
    if (lib) {
        if (k < a.size() && (a[k] == "STATIC" || a[k] == "SHARED" || a[k] == "MODULE" || a[k] == "OBJECT" || a[k] == "INTERFACE")) type = a[k++];
        else if (k < a.size() && a[k] == "ALIAS") { t.kind = "alias"; t.aliasOf = k + 1 < a.size() ? a[k + 1] : ""; targets[t.name] = t; order.push_back(t.name); return; }
        if (k < a.size() && a[k] == "IMPORTED") { t.kind = "imported"; targets[t.name] = t; return; }
    }
    t.kind = lib ? (type == "INTERFACE" ? "iface" : "lib") : "exe";
    if (type == "SHARED" || type == "MODULE") t.libKind = "shared"; else if (type == "OBJECT") t.libKind = "object";
    for (; k < a.size(); ++k) {
        if (a[k] == "WIN32" || a[k] == "MACOSX_BUNDLE") continue;
        if (a[k] == "EXCLUDE_FROM_ALL") { t.excluded = true; continue; }
        t.sources.push_back(abs(a[k], curSrc));
    }
    t.dirDefs = dirDefs; t.dirOpts = dirOpts; t.dirIncs = dirIncs; t.dirLinkOpts = dirLinkOpts;
    (void)c;
    if (targets.count(t.name)) throw ReadError{c.file + ":" + std::to_string(c.line) + ": target '" + t.name + "' is defined twice"};
    targets[t.name] = t; order.push_back(t.name);
}

inline void Interp::doTarget(const Cmd& c, const vector<string>& a) {
    string n = lower(c.name);
    if (a.empty()) return; Target* t = tgt(a[0]);
    if (!t) { if (n == "target_link_libraries" || n == "target_include_directories" || n == "target_compile_definitions" || n == "target_compile_options") tally.unknown[n + " on an unknown target"]++; return; }
    string scope = "PRIVATE";
    auto into = [&](vector<string>& priv, vector<string>& pub, vector<string>& iface, const string& v) {
        if (scope == "PRIVATE") priv.push_back(v); else if (scope == "PUBLIC") pub.push_back(v); else iface.push_back(v);
        if (t->kind == "iface" && scope != "INTERFACE") { /* an interface library's own settings are all interface */ }
    };
    if (t->kind == "iface") scope = "INTERFACE";
    for (size_t k = 1; k < a.size(); ++k) {
        const string& w = a[k];
        if (w == "PRIVATE" || w == "PUBLIC" || w == "INTERFACE") { scope = w; continue; }
        if (w == "SYSTEM" || w == "BEFORE" || w == "AFTER") continue;
        if (n == "target_sources") t->sources.push_back(abs(w, curSrc));
        else if (n == "target_include_directories") into(t->incs, t->pubIncs, t->ifIncs, abs(w, curSrc));
        else if (n == "target_compile_definitions") into(t->defs, t->pubDefs, t->ifDefs, w);
        else if (n == "target_compile_options") into(t->opts, t->pubOpts, t->ifOpts, w);
        else if (n == "target_link_options") t->linkOpts.push_back(w);
        else if (n == "target_link_libraries") {
            if (w == "debug" || w == "optimized" || w == "general") continue;
            into(t->links, t->pubLinks, t->ifLinks, w);
            // the properties: PRIVATE and PUBLIC are the target's own links; PUBLIC and INTERFACE go to its users, and a static
            // library hands its PRIVATE links on as $<LINK_ONLY:x> (its users must link them too)
            if (t->kind != "iface" && scope != "INTERFACE") t->linkProp.push_back(w);
            if (scope != "PRIVATE" || t->kind == "iface") t->ifLinkProp.push_back(w);
            else if (t->kind == "lib" && t->libKind != "shared" && t->libKind != "object") t->ifLinkProp.push_back("$<LINK_ONLY:" + w + ">");
        }
        else if (n == "target_compile_features") { if (w == "cxx_std_17" || w == "cxx_std_20" || w == "cxx_std_14" || w == "cxx_std_11") t->cxx17 = true; }
    }
}

inline void Interp::builtin(const Cmd& c) {
    string n = lower(c.name);
    if (n == "set") { doSet(c); return; }
    if (n == "unset") { auto a = args(c.args); if (!a.empty()) { bool parent = std::find(a.begin(), a.end(), "PARENT_SCOPE") != a.end(); if (parent) parentWrites.push_back({a[0], ""}); else { vars.erase(a[0]); if (std::find(a.begin(), a.end(), "CACHE") != a.end()) cache.erase(a[0]); } } return; }
    auto a = args(c.args);
    if (n == "option") {
        if (a.empty()) return; tally.options++;
        auto d = opt.defs.find(a[0]);
        if (d != opt.defs.end()) { cache[a[0]] = d->second; tally.setOptions++; return; }
        if (!cache.count(a[0]) && !vars.count(a[0])) cache[a[0]] = a.size() > 2 ? a[2] : "OFF";
        return;
    }
    if (n == "message") {
        if (a.empty()) return;
        if (a[0] == "FATAL_ERROR" || a[0] == "SEND_ERROR") { string m; for (size_t k = 1; k < a.size(); ++k) m += a[k]; throw ReadError{c.file + ":" + std::to_string(c.line) + ": the CMake file stops here: " + m}; }
        if (a[0] == "WARNING" || a[0] == "AUTHOR_WARNING" || a[0] == "DEPRECATION") { string m; for (size_t k = 1; k < a.size(); ++k) m += a[k]; tally.messages.push_back(m); }
        return;
    }
    if (n == "project") {
        if (a.empty()) return; vars["PROJECT_NAME"] = a[0]; vars["CMAKE_PROJECT_NAME"] = a[0];
        for (size_t k = 1; k + 1 < a.size(); ++k) if (a[k] == "VERSION") { vars["PROJECT_VERSION"] = a[k + 1]; vars[a[0] + "_VERSION"] = a[k + 1]; }
        vars["PROJECT_SOURCE_DIR"] = curSrc; vars["PROJECT_BINARY_DIR"] = curBin; return;
    }
    if (n == "cmake_minimum_required" || n == "cmake_policy" || n == "enable_language" || n == "mark_as_advanced" || n == "configure_file" || n == "add_custom_target" || n == "add_custom_command" || n == "install" || n == "add_dependencies" || n == "source_group" || n == "include_guard") {
        if (n == "configure_file" || n == "add_custom_target" || n == "add_custom_command" || n == "install" || n == "add_dependencies") tally.unknown[n]++;
        if (n == "add_custom_target" && !a.empty()) { Target t; t.name = a[0]; t.kind = "custom"; t.skipReason = "add_custom_target is not expressed"; targets[t.name] = t; }
        return;
    }
    if (n == "enable_testing") { testingOn = true; return; }
    if (n == "include") {
        if (a.empty()) return; string f = a[0];
        if (f.find('/') == string::npos && f.find(".cmake") == string::npos) return; // a module name (FetchContent, CheckIncludeFileCXX ...): handled by the commands themselves
        includeFile(abs(f, curSrc)); return;
    }
    if (n == "add_subdirectory") { if (!a.empty()) sub(a[0]); return; }
    if (n == "list") { doList(a); return; }
    if (n == "string") { doString(a); return; }
    if (n == "file") { doFile(a); return; }
    if (n == "get_filename_component") {
        if (a.size() < 3) return; string in = a[1], r; fs::path p(in); const string& m = a[2];
        if (m == "ABSOLUTE" || m == "REALPATH") r = abs(in, curSrc); else if (m == "DIRECTORY" || m == "PATH") r = p.parent_path().generic_string();
        else if (m == "NAME") r = p.filename().generic_string(); else if (m == "EXT") { auto e = p.filename().string(); auto d = e.find('.'); r = d == string::npos ? "" : e.substr(d); }
        else if (m == "NAME_WE") r = p.stem().generic_string(); else if (m == "NAME_WLE") r = p.stem().generic_string(); else r = in;
        vars[a[0]] = r; return;
    }
    if (n == "find_package" || n == "find_program" || n == "find_library" || n == "find_path" || n == "pkg_check_modules" || n == "check_include_file_cxx" || n == "check_include_file") { doFind(n, a); return; }
    if (n == "execute_process") {
        string cmdline, outVar, resVar; bool inCmd = false; vector<string> w;
        for (size_t k = 0; k < a.size(); ++k) {
            if (a[k] == "COMMAND") { inCmd = true; continue; }
            if (a[k] == "OUTPUT_VARIABLE" && k + 1 < a.size()) { outVar = a[++k]; inCmd = false; continue; }
            if (a[k] == "RESULT_VARIABLE" && k + 1 < a.size()) { resVar = a[++k]; inCmd = false; continue; }
            if (a[k].rfind("OUTPUT_", 0) == 0 || a[k].rfind("ERROR_", 0) == 0 || a[k] == "TIMEOUT" || a[k] == "WORKING_DIRECTORY") { inCmd = false; if (a[k] == "TIMEOUT" || a[k] == "WORKING_DIRECTORY") ++k; continue; }
            if (inCmd) w.push_back(a[k]);
        }
        if (!w.empty() && w[0] == get("CMAKE_COMMAND")) { int rc = cmakeTool(w); if (!outVar.empty()) vars[outVar] = ""; if (!resVar.empty()) vars[resVar] = std::to_string(rc); return; }
        for (auto& x : w) { string q = "'"; for (char ch : x) { if (ch == '\'') q += "'\\''"; else q += ch; } cmdline += q + "' "; }
        string out; int rc = 1;
        if (!w.empty()) { FILE* f = CMR_POPEN((cmdline + " 2>/dev/null").c_str(), "r"); if (f) { char b[512]; size_t k; while ((k = fread(b, 1, sizeof b, f)) > 0) out.append(b, k); int st = CMR_PCLOSE(f);
#ifdef _WIN32
 rc = st;
#else
 rc = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
#endif
 } }
        if (!outVar.empty()) vars[outVar] = out; if (!resVar.empty()) vars[resVar] = std::to_string(rc); return;
    }
    if (n == "add_executable") { addTarget(c, a, false); return; }
    if (n == "add_library") { addTarget(c, a, true); return; }
    if (n.rfind("target_", 0) == 0) { doTarget(c, a); return; }
    if (n == "add_compile_definitions") { for (auto& w : a) dirDefs.push_back(w); return; }
    if (n == "add_compile_options") { for (auto& w : a) dirOpts.push_back(w); return; }
    if (n == "add_link_options") { for (auto& w : a) dirLinkOpts.push_back(w); return; }
    if (n == "include_directories") { for (auto& w : a) if (w != "SYSTEM" && w != "BEFORE" && w != "AFTER") dirIncs.push_back(abs(w, curSrc)); return; }
    if (n == "set_target_properties") {
        size_t p = 0; while (p < a.size() && a[p] != "PROPERTIES") ++p;
        for (size_t k = 0; k < p; ++k) { Target* t = tgt(a[k]); if (!t) continue; for (size_t q = p + 1; q + 1 < a.size(); q += 2) t->props[a[q]] = a[q + 1]; }
        return;
    }
    if (n == "set_source_files_properties") {
        size_t p = 0; while (p < a.size() && a[p] != "PROPERTIES") ++p;
        for (size_t q = p + 1; q + 1 < a.size(); q += 2) for (size_t k = 0; k < p; ++k) srcProps[abs(a[k], curSrc) + "|" + a[q]] += (srcProps.count(abs(a[k], curSrc) + "|" + a[q]) ? ";" : "") + a[q + 1];
        return;
    }
    if (n == "set_property" || n == "get_property") { doProperty(n, a); return; }
    if (n == "cmake_parse_arguments") { parseArguments(a); return; }
    if (n == "math") {
        if (a.size() < 3 || a[0] != "EXPR") { tally.unknown["math(" + (a.empty() ? string() : a[0]) + ")"]++; return; }
        long long v = 0; if (!mathExpr(a[2], v)) throw ReadError{c.file + ":" + std::to_string(c.line) + ": math(EXPR) cannot read '" + a[2] + "'"};
        bool hex = a.size() >= 5 && a[3] == "OUTPUT_FORMAT" && a[4] == "HEXADECIMAL";
        if (hex) { char b[32]; snprintf(b, sizeof b, "0x%llx", v); vars[a[1]] = b; } else vars[a[1]] = std::to_string(v);
        return;
    }
    if (n == "get_target_property" && a.size() >= 3) { // SOURCES and SOURCE_DIR come from what the reader recorded; other properties from set_target_properties
        Target* t = tgt(a[1]); string v; bool ok = t != nullptr;
        auto raw = targets.find(a[1]);
        if (a[2] == "ALIASED_TARGET") { bool al = raw != targets.end() && !raw->second.aliasOf.empty(); vars[a[0]] = al ? raw->second.aliasOf : a[0] + "-NOTFOUND"; return; }
        if (t && (a[2] == "LINK_LIBRARIES" || a[2] == "INTERFACE_LINK_LIBRARIES")) {
            const vector<string>& l = a[2] == "LINK_LIBRARIES" ? t->linkProp : t->ifLinkProp; // recorded by target_link_libraries
            vars[a[0]] = l.empty() ? a[0] + "-NOTFOUND" : joinList(l); return;
        }
        if (t && a[2] == "SOURCES") v = joinList(t->sources); else if (t && a[2] == "SOURCE_DIR") v = t->dir; else if (t && a[2] == "NAME") v = t->name;
        else if (t) { auto it = t->props.find(a[2]); ok = it != t->props.end(); if (ok) v = it->second; }
        vars[a[0]] = ok ? v : a[0] + "-NOTFOUND"; return;
    }
    if (n == "separate_arguments" && !a.empty()) { // UNIX_COMMAND: split on blanks outside quotes, \ escapes the next character
        if (a.size() == 1) { string v = get(a[0]); for (auto& ch : v) if (ch == ' ') ch = ';'; vars[a[0]] = v; return; }
        if (a.size() < 3 || (a[1] != "UNIX_COMMAND" && a[1] != "NATIVE_COMMAND")) { tally.unknown["separate_arguments(" + (a.size() > 1 ? a[1] : string()) + ")"]++; return; }
        vector<string> words; string w; bool have = false; char q = 0; const string& t = a[2];
        for (size_t k = 0; k < t.size(); ++k) {
            char ch = t[k];
            if (q) { if (ch == q) q = 0; else if (ch == '\\' && q == '"' && k + 1 < t.size()) w += t[++k]; else w += ch; continue; }
            if (ch == '"' || ch == '\'') { q = ch; have = true; continue; }
            if (ch == '\\' && k + 1 < t.size()) { w += t[++k]; have = true; continue; }
            if (isspace((unsigned char)ch)) { if (have) { words.push_back(w); w.clear(); have = false; } continue; }
            w += ch; have = true;
        }
        if (have) words.push_back(w);
        vars[a[0]] = joinList(words); return;
    }
    if (n == "cmake_language" && a.size() >= 2) {
        if (a[0] == "CALL") { auto f = funcs.find(lower(a[1])); if (f != funcs.end()) callFunc(f->second, a[1], vector<string>(a.begin() + 2, a.end())); else tally.unknown["cmake_language(CALL " + a[1] + ")"]++; return; }
        if (a[0] == "DEFER") { size_t k = 1; while (k < a.size() && a[k] != "CALL") ++k; if (k + 1 < a.size()) deferred.push_back({a[k + 1], vector<string>(a.begin() + (long)k + 2, a.end())}); return; }
        tally.unknown["cmake_language(" + a[0] + ")"]++; return;
    }
    if (n == "add_test") {
        TestDef t; t.dir = curSrc; size_t k = 0;
        if (!a.empty() && a[0] == "NAME") {
            if (a.size() < 2) return; t.name = a[1]; k = 2; bool inCmd = false;
            for (; k < a.size(); ++k) {
                if (a[k] == "COMMAND") { inCmd = true; continue; }
                if (a[k] == "WORKING_DIRECTORY" && k + 1 < a.size()) { t.cwd = a[++k]; inCmd = false; continue; }
                if (a[k] == "CONFIGURATIONS" || a[k] == "COMMAND_EXPAND_LISTS") { inCmd = false; continue; }
                if (inCmd) t.cmd.push_back(a[k]);
            }
        } else if (!a.empty()) { t.name = a[0]; for (k = 1; k < a.size(); ++k) t.cmd.push_back(a[k]); }
        if (!t.name.empty()) tests.push_back(t);
        return;
    }
    if (n == "set_tests_properties") {
        size_t p = 0; while (p < a.size() && a[p] != "PROPERTIES") ++p;
        for (size_t k = 0; k < p; ++k) for (auto& t : tests) if (t.name == a[k]) for (size_t q = p + 1; q + 1 < a.size(); q += 2) t.props[a[q]] = a[q + 1];
        return;
    }
    if (n == "fetchcontent_declare") {
        if (a.empty()) return; FetchInfo f;
        for (size_t k = 1; k + 1 < a.size(); ++k) {
            if (a[k] == "GIT_REPOSITORY") f.git = a[k + 1]; else if (a[k] == "GIT_TAG") f.tag = a[k + 1]; else if (a[k] == "URL") f.url = a[k + 1];
            else if (a[k] == "URL_HASH" && a[k + 1].compare(0, 7, "SHA256=") == 0) f.sha256 = a[k + 1].substr(7);
        }
        fetchDeclared[lower(a[0])] = f; return;
    }
    if (n == "fetchcontent_makeavailable") {
        for (auto& nm : a) {
            string l = lower(nm); auto f = opt.fetched.find(l);
            string dir = f != opt.fetched.end() ? f->second : "/okay-not-fetched/" + l;
            if (f == opt.fetched.end() && fetchDeclared.count(l)) { // not given on disk: the manifest gets a `fetch` block and the sources are expected in _deps/<name>-src
                dir = opt.root + "/_deps/" + l + "-src"; if (std::find(fetchUsed.begin(), fetchUsed.end(), l) == fetchUsed.end()) fetchUsed.push_back(l);
            }
            vars[l + "_SOURCE_DIR"] = dir; vars[l + "_BINARY_DIR"] = opt.binDir + "/_deps/" + l + "-build"; { std::error_code fe; vars[l + "_POPULATED"] = (f != opt.fetched.end() || fs::exists(dir, fe)) ? "TRUE" : ""; }
        }
        return;
    }
    tally.unknown[n]++;
}

inline void Interp::init() {
    for (auto& d : opt.defs) cache[d.first] = d.second;
    vars["CMAKE_SOURCE_DIR"] = opt.root; vars["CMAKE_BINARY_DIR"] = opt.binDir; vars["PROJECT_SOURCE_DIR"] = opt.root; vars["PROJECT_BINARY_DIR"] = opt.binDir;
    curSrc = opt.root; curBin = opt.binDir;
    vars["CMAKE_CURRENT_SOURCE_DIR"] = curSrc; vars["CMAKE_CURRENT_BINARY_DIR"] = curBin;
    vars["CMAKE_SYSTEM_NAME"] = "Linux"; vars["UNIX"] = "1"; vars["CMAKE_DL_LIBS"] = "dl"; vars["CMAKE_CXX_COMPILER_ID"] = "GNU"; vars["CMAKE_C_COMPILER_ID"] = "GNU";
    vars["CMAKE_VERSION"] = "3.28.0"; vars["CMAKE_MAJOR_VERSION"] = "3"; vars["CMAKE_MINOR_VERSION"] = "28"; // the language level this reader follows
    vars["CMAKE_COMMAND"] = "cmake"; vars["CMAKE_CROSSCOMPILING"] = ""; vars["CMAKE_HOST_UNIX"] = "1";
    vars["CMAKE_BUILD_TYPE"] = opt.defs.count("CMAKE_BUILD_TYPE") ? opt.defs["CMAKE_BUILD_TYPE"] : opt.buildType;
    cache["CMAKE_BUILD_TYPE"] = vars["CMAKE_BUILD_TYPE"];
}

inline void Interp::runTop() {
    string file = opt.root + "/CMakeLists.txt"; std::ifstream in(file); if (!in) throw ReadError{"no CMakeLists.txt in " + opt.root};
    string text((std::istreambuf_iterator<char>(in)), {}); inputs.insert(file);
    auto cs = parseFile(text, "CMakeLists.txt");
    vars["CMAKE_CURRENT_LIST_FILE"] = file; vars["CMAKE_CURRENT_LIST_DIR"] = opt.root;
    run(cs, 0, cs.size()); runDeferred(); dirDefsFinal[curSrc] = dirDefs;
}


// ---- 3. Interpreter result to a build.okay manifest ----------------------------------------------------------------------
struct Report {
    int targetsDeclared = 0, exes = 0, libs = 0, ifaces = 0, emitted = 0, skipped = 0, sources = 0, defines = 0, flags = 0, perFile = 0;
    int generated = 0; vector<string> inputs; // generate blocks written; the files read (for the buildfile block)
    int fetches = 0, testsDuplicate = 0, testsNoCommand = 0, testsDeclared = 0, testsExe = 0, testsScript = 0, testsEmitted = 0, testsSkipped = 0, testsNeedCmake = 0;
    vector<std::pair<string, string>> skippedTargets; // name, reason
    vector<std::pair<string, string>> skippedTests;
    vector<string> unresolved; // link items that were neither a target nor a plain library name
    bool accountDefines = false;
};

// A generated file's text in one manifest line: \\ \" \n \r \t and \xHH for other control bytes. okay-build's parser reads it back.
static string escapeText(const string& s) {
    string r; char b[5];
    for (unsigned char c : s) {
        if (c == '\\') r += "\\\\"; else if (c == '"') r += "\\\""; else if (c == '\n') r += "\\n"; else if (c == '\r') r += "\\r"; else if (c == '\t') r += "\\t";
        else if (c < 32 || c == 127) { snprintf(b, sizeof b, "\\x%02x", c); r += b; } else r += (char)c;
    }
    return r;
}
static bool implicitInclude(const string& p) { return p == "/usr/include" || p == "/usr/local/include" || p == "/usr/include/x86_64-linux-gnu"; } // the compiler already searches these, in its own order
static string rootRel(const string& p, const string& root) {
    if (p.compare(0, root.size() + 1, root + "/") == 0) return p.substr(root.size() + 1);
    return p;
}
static string srcWord(const string& p, const string& root) { // for tests: root -> {src}
    string r = p; size_t at = 0;
    while ((at = r.find(root, at)) != string::npos) { r.replace(at, root.size(), "{src}"); at += 5; }
    return r;
}
static string qw(const string& s) {
    if (!s.empty() && s.find_first_of(" \t#") == string::npos && s.find('"') == string::npos) return s;
    string e; for (char c : s) { if (c == '"') e += "\\\""; else e += c; }
    return "\"" + e + "\"";
}
static void addUnique(vector<string>& v, const string& x) { if (!x.empty() && std::find(v.begin(), v.end(), x) == v.end()) v.push_back(x); }
static vector<string> splitSemi(const string& s) { return Interp::splitList(s); }

struct Resolved { vector<string> deps, libs, linkFlags, incs, defs, opts; };

static Resolved resolveTarget(Interp& in, Target& t, Report& rep) {
    Resolved r;
    // what a dependent gets from `d`: its PUBLIC and INTERFACE includes, definitions and options, and what d links PUBLIC
    std::set<string> seen;
    std::function<void(const string&, bool)> item = [&](const string& w, bool direct) {
        (void)direct;
        if (w.empty()) return;
        if (w.compare(0, 12, "$<LINK_ONLY:") == 0 && w.back() == '>') { item(w.substr(12, w.size() - 13), direct); return; }
        if (w[0] == '-') { if (w.compare(0, 2, "-l") == 0) addUnique(r.libs, w.substr(2)); else addUnique(r.linkFlags, w); return; }
        Target* d = in.tgt(w);
        if (!d) {
            if (w.find("::") != string::npos) { rep.unresolved.push_back(t.name + " -> " + w); return; }
            if (w.find('/') != string::npos) { addUnique(r.linkFlags, w); return; }
            if (w.compare(0, 2, "$<") == 0) return;
            addUnique(r.libs, w); return;
        }
        if (d->kind == "lib") {
            addUnique(r.deps, d->name);
            if (seen.insert(d->name).second) {
                for (auto& x : d->pubIncs) addUnique(r.incs, x); for (auto& x : d->ifIncs) addUnique(r.incs, x);
                for (auto& x : d->pubDefs) addUnique(r.defs, x); for (auto& x : d->ifDefs) addUnique(r.defs, x);
                for (auto& x : d->pubOpts) addUnique(r.opts, x); for (auto& x : d->ifOpts) addUnique(r.opts, x);
                for (auto& x : d->pubLinks) { Target* e = in.tgt(x); if (e && (e->kind == "lib")) { addUnique(r.deps, e->name); item(x, false); } else if (e && (e->kind == "iface" || e->kind == "imported")) item(x, false); }
                for (auto& x : d->ifLinks) item(x, false);
            }
        } else if (d->kind == "iface" || d->kind == "imported") {
            if (!seen.insert(d->name).second) return;
            for (auto& x : d->pubIncs) addUnique(r.incs, x); for (auto& x : d->ifIncs) addUnique(r.incs, x);
            for (auto& x : d->pubDefs) addUnique(r.defs, x); for (auto& x : d->ifDefs) addUnique(r.defs, x);
            for (auto& x : d->pubOpts) addUnique(r.opts, x); for (auto& x : d->ifOpts) addUnique(r.opts, x);
            for (auto& x : d->pubLinks) item(x, false); for (auto& x : d->ifLinks) item(x, false);
            for (auto& x : d->links) item(x, false); // an interface library lists its own links as INTERFACE
        }
        // an exe or a custom target as a link item is not meaningful: ignore
    };
    for (auto& w : t.links) item(w, true);
    for (auto& w : t.pubLinks) item(w, true);
    for (auto& w : t.ifLinks) item(w, true);
    return r;
}

// Writes the manifest text. Returns it; `rep` says what could and could not be expressed.
static string emitManifest(Interp& in, Report& rep) {
    const string& root = in.opt.root;
    string bt = in.get("CMAKE_BUILD_TYPE"); string btFlags, btDef;
    if (bt == "Release") { btFlags = "-O3"; btDef = "NDEBUG"; } else if (bt == "Debug") btFlags = "-g";
    else if (bt == "RelWithDebInfo") { btFlags = "-O2 -g"; btDef = "NDEBUG"; } else if (bt == "MinSizeRel") { btFlags = "-Os"; btDef = "NDEBUG"; }
    string stdv = in.get("CMAKE_CXX_STANDARD").empty() ? "17" : in.get("CMAKE_CXX_STANDARD");
    string stdFlag = "-std=" + string(Interp::isTrue(in.get("CMAKE_CXX_EXTENSIONS")) || !in.has("CMAKE_CXX_EXTENSIONS") ? "gnu++" : "c++") + stdv;
    for (auto& n : in.order) {
        Target& t = in.targets[n]; ++rep.targetsDeclared;
        if (t.kind == "exe") ++rep.exes; else if (t.kind == "lib") ++rep.libs; else if (t.kind == "iface") ++rep.ifaces;
    }
    // add_compile_definitions applies to every target of its folder, also to targets made before it: use the folder's final list
    for (auto& n : in.order) { Target& t = in.targets[n]; auto it = in.dirDefsFinal.find(t.dir); if (it != in.dirDefsFinal.end()) t.dirDefs = it->second; }
    // $<TARGET_OBJECTS:lib> among the sources: the program links that OBJECT library whole (every object in, as CMake adds them)
    for (auto& n : in.order) {
        Target& t = in.targets[n]; vector<string> keep;
        for (auto& s : t.sources) {
            auto at = s.find("{TARGET_OBJECTS:");
            if (at == string::npos) { keep.push_back(s); continue; }
            string lib = s.substr(at + 16); lib = lib.substr(0, lib.find('}'));
            if (std::find(t.links.begin(), t.links.end(), lib) == t.links.end()) t.links.push_back(lib);
        }
        t.sources = keep;
    }
    // 1. skip what cannot be written: missing sources, downloads, unsupported kinds. Dependents of a skipped target are skipped too.
    for (auto& n : in.order) {
        Target& t = in.targets[n]; if (t.kind != "exe" && t.kind != "lib") continue;
        if (t.skipReason.empty()) {
            if (t.sources.empty()) t.skipReason = "no sources";
            for (auto& s : t.sources) {
                if (s.compare(0, 5, "{bin}") == 0) { if (in.written.count(s)) continue; t.skipReason = "generated source " + s; break; }
                std::error_code ec; bool willFetch = false; for (auto& fu : in.fetchUsed) if (s.rfind(root + "/_deps/" + fu + "-src/", 0) == 0) willFetch = true;
                if (!willFetch && !fs::exists(s, ec)) { t.skipReason = s.compare(0, 18, "/okay-not-fetched/") == 0 ? "needs a download: " + s.substr(18, s.find('/', 18) - 18) + " (give --fetched NAME=DIR)" : "missing source " + rootRel(s, root); break; }
            }
        }
    }
    std::map<string, Resolved> res;
    for (auto& n : in.order) { Target& t = in.targets[n]; if (t.kind == "exe" || t.kind == "lib") res[n] = resolveTarget(in, t, rep); }
    for (bool changed = true; changed;) {
        changed = false;
        for (auto& n : in.order) {
            Target& t = in.targets[n]; if ((t.kind != "exe" && t.kind != "lib") || !t.skipReason.empty()) continue;
            for (auto& d : res[n].deps) { Target* dt = in.tgt(d); if (dt && !dt->skipReason.empty()) { t.skipReason = "needs " + d + ", which is skipped (" + dt->skipReason + ")"; changed = true; break; } }
        }
    }
    std::ostringstream out;
    out << "# generated by okay-build cmake from the CMakeLists.txt files (read by okay-build itself; CMake is not run)\n";
    out << "# build type " << bt << ", paths are relative to the repository root\n";
    bool account = false;
    std::set<string> accountNames = {"OKAY_DEFAULT_ACCOUNT_URL", "OKAY_DEFAULT_ACCOUNT_KEY"};
    auto isAccount = [&](const string& d) { auto e = d.find('='); return accountNames.count(d.substr(0, e)) > 0; };
    for (auto& n : in.order) { Target& t = in.targets[n]; if (t.kind == "exe" || t.kind == "lib") for (auto& d : t.dirDefs) if (isAccount(d)) account = true; }
    rep.accountDefines = account;
    if (account) out << "account_defines = on\n";
    out << "toolchain\n  path_map = on\nend\n";
    for (auto& fu : in.fetchUsed) {
        auto& f = in.fetchDeclared[fu]; out << "fetch " << qw(fu) << "\n";
        if (!f.git.empty()) out << "  git = " << qw(f.git) << "\n  commit = " << qw(f.tag) << "\n"; else out << "  url = " << qw(f.url) << "\n  sha256 = " << qw(f.sha256) << "\n";
        out << "  dir = _deps/" << fu << "-src\nend\n"; ++rep.fetches;
    }
    for (auto& f : in.folders) out << "folder " << qw(f) << "\n";
    for (auto& g : in.written) { // files the CMake code wrote while it was read: the build writes them (only when the text changed)
        out << "generate " << qw(g.first) << "\n";
        const size_t chunk = 4000; // several text lines keep each line short; they are joined with nothing in between
        for (size_t at = 0; at < g.second.size() || at == 0; at += chunk) out << "  text = \"" << escapeText(g.second.substr(at, chunk)) << "\"\n";
        out << "end\n"; ++rep.generated;
    }
    for (auto& i : in.inputs) rep.inputs.push_back(i);
    std::set<string> emitted;
    for (auto& n : in.order) {
        Target& t = in.targets[n]; if (t.kind != "exe" && t.kind != "lib") continue;
        if (!t.skipReason.empty()) { ++rep.skipped; rep.skippedTargets.push_back({n, t.skipReason}); continue; }
        Resolved& r = res[n]; ++rep.emitted; emitted.insert(n);
        vector<string> incs, defs, opts, linkFlags = r.linkFlags, libs = r.libs;
        for (auto& x : t.dirIncs) if (!implicitInclude(x)) addUnique(incs, rootRel(x, root)); for (auto& x : t.incs) if (!implicitInclude(x)) addUnique(incs, rootRel(x, root)); for (auto& x : t.pubIncs) if (!implicitInclude(x)) addUnique(incs, rootRel(x, root));
        for (auto& x : r.incs) if (!implicitInclude(x)) addUnique(incs, rootRel(x, root));
        for (auto& x : t.dirDefs) if (!isAccount(x)) addUnique(defs, x); for (auto& x : t.defs) addUnique(defs, x); for (auto& x : t.pubDefs) addUnique(defs, x);
        for (auto& x : r.defs) addUnique(defs, x);
        if (!btDef.empty()) addUnique(defs, btDef);
        bool cxx = t.cxx17; for (auto& s : t.sources) { string e = fs::path(s).extension().string(); if (e == ".cpp" || e == ".cc" || e == ".cxx") cxx = true; }
        if (cxx) addUnique(opts, stdFlag);
        for (auto& w : splitSemi(btFlags.empty() ? "" : btFlags)) { (void)w; }
        { std::stringstream ss(btFlags); string w; while (ss >> w) addUnique(opts, w); }
        for (auto& x : t.dirOpts) addUnique(opts, x); for (auto& x : t.opts) addUnique(opts, x); for (auto& x : t.pubOpts) addUnique(opts, x);
        for (auto& x : r.opts) addUnique(opts, x);
        // LINKER:a,b is the compiler driver's -Wl,a,b; SHELL:"x y" is several words (CMake's link-option prefixes)
        auto linkWord = [&](const string& x) {
            if (x.compare(0, 7, "LINKER:") == 0) { addUnique(linkFlags, "-Wl," + x.substr(7)); return; }
            if (x.compare(0, 6, "SHELL:") == 0) { std::istringstream ws(x.substr(6)); string w; while (ws >> w) linkFlags.push_back(w); return; }
            addUnique(linkFlags, x);
        };
        for (auto& x : t.dirLinkOpts) linkWord(x); for (auto& x : t.linkOpts) linkWord(x);
        out << "target " << qw(n) << " " << (t.kind == "lib" ? (t.libKind.empty() ? "lib" : t.libKind.c_str()) : "exe") << "\n";
        out << "  sources =";
        for (auto& s : t.sources) { out << " " << qw(rootRel(s, root)); ++rep.sources; }
        out << "\n";
        auto line = [&](const char* k, const vector<string>& v) { if (v.empty()) return; out << "  " << k << " ="; for (auto& w : v) out << " " << qw(w); out << "\n"; };
        { auto on = t.props.find("OUTPUT_NAME"); if (on != t.props.end() && !on->second.empty()) out << "  output_name = " << qw(on->second) << "\n"; }
        line("include", incs); line("define", defs); line("flags", opts); line("link_flags", linkFlags); line("libs", libs); line("deps", r.deps);
        rep.defines += (int)defs.size(); rep.flags += (int)opts.size();
        for (auto& s : t.sources) {
            for (const char* prop : {"COMPILE_OPTIONS", "COMPILE_FLAGS", "COMPILE_DEFINITIONS", "INCLUDE_DIRECTORIES"}) {
                auto it = in.srcProps.find(s + "|" + prop); if (it == in.srcProps.end()) continue;
                for (auto& w : splitSemi(it->second)) {
                    if (w.empty()) continue; ++rep.perFile; string rs = qw(rootRel(s, root));
                    if (string(prop) == "COMPILE_DEFINITIONS") out << "  file_define = " << rs << " " << qw(w) << "\n";
                    else if (string(prop) == "INCLUDE_DIRECTORIES") out << "  file_include = " << rs << " " << qw(rootRel(w, root)) << "\n";
                    else out << "  file_flag = " << rs << " " << qw(w) << "\n";
                }
            }
        }
        out << "end\n";
    }
    // tests
    std::set<string> seenTests;
    for (auto& tdef : in.tests) {
        ++rep.testsDeclared; if (!seenTests.insert(tdef.name).second) { ++rep.testsDuplicate; continue; }
        if (tdef.cmd.empty()) { ++rep.testsNoCommand; continue; }
        vector<string> words, needs; bool skip = false; string why;
        bool exe = false;
        for (size_t k = 0; k < tdef.cmd.size(); ++k) {
            string w = tdef.cmd[k]; Target* tg = k == 0 || w.compare(0, 8, "{target:") == 0 ? in.tgt(w.compare(0, 8, "{target:") == 0 ? w.substr(8, w.size() - 9) : w) : nullptr;
            if (tg && tg->kind == "exe") {
                if (!emitted.count(tg->name)) { skip = true; why = "needs " + tg->name + ", which is not written (" + (tg->skipReason.empty() ? string("not an emitted target") : tg->skipReason) + ")"; }
                needs.push_back(tg->name); words.push_back("{target:" + tg->name + "}"); if (k == 0) exe = true; continue;
            }
            if (w == "cmake") ++rep.testsNeedCmake;
            words.push_back(srcWord(w, root));
        }
        if (exe) ++rep.testsExe; else ++rep.testsScript;
        if (skip) { ++rep.testsSkipped; rep.skippedTests.push_back({tdef.name, why}); continue; }
        ++rep.testsEmitted;
        string cwd = tdef.cwd.empty() ? rootRel(tdef.dir, root) : tdef.cwd;
        if (tdef.cwd.empty()) cwd = "{bin}" + (cwd.empty() || cwd == tdef.dir ? string() : "/" + cwd);
        else cwd = srcWord(cwd, root);
        out << "ctest " << qw(tdef.name) << "\n  cmd =";
        for (auto& w : words) out << " " << qw(w);
        out << "\n  cwd = " << qw(cwd) << "\n";
        auto pr = [&](const char* k) { auto it = tdef.props.find(k); return it == tdef.props.end() ? string() : it->second; };
        if (!pr("TIMEOUT").empty()) out << "  timeout = " << (long long)atof(pr("TIMEOUT").c_str()) << "\n";
        if (Interp::isTrue(pr("WILL_FAIL")) && !pr("WILL_FAIL").empty()) out << "  will_fail = on\n";
        if (!pr("SKIP_RETURN_CODE").empty()) out << "  skip_code = " << atoi(pr("SKIP_RETURN_CODE").c_str()) << "\n";
        for (const char* key : {"ENVIRONMENT", "LABELS", "DEPENDS"}) {
            auto v = splitSemi(pr(key)); if (v.empty()) continue;
            out << "  " << (string(key) == "ENVIRONMENT" ? "env" : string(key) == "LABELS" ? "labels" : "depends") << " =";
            for (auto& w : v) out << " " << qw(string(key) == "ENVIRONMENT" ? srcWord(w, root) : w);
            out << "\n";
        }
        if (Interp::isTrue(pr("RUN_SERIAL")) && !pr("RUN_SERIAL").empty()) out << "  serial = on\n";
        out << "end\n";
    }
    return out.str();
}

// Reads the project and returns the manifest text. Throws ReadError.
static string readProject(const Options& o, Report& rep, Tally& tally, std::map<string, string>* varsOut = nullptr) {
    Interp in; in.opt = o; in.init();
    in.runTop();
    string m = emitManifest(in, rep);
    tally = in.tally; if (varsOut) *varsOut = in.vars.m;
    return m;
}

} // namespace cmr
