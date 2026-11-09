// Prefab tools, wave 2: nested prefabs, variants, unpack, per-record / per-field apply
// and revert, queries and the asset validator. See docs/prefab_system.md.
//
// How it works, in one paragraph: a prefab FILE is read into a RawDoc (the lines as
// written, so a file can be edited and written back without losing anything). Resolving
// it gives an RDoc: the fully expanded object list in depth-first order, with nested
// prefabs and variant bases already folded in and a note per object of where it came
// from (which file object, base object or nested object). An RDoc is rendered back to
// ordinary scene text and loaded by SceneSerializer::InstantiateFromText, so this file
// never builds objects itself and keeps the serializer's all-or-nothing loading.
#include "okay/Scene/PrefabTools.hpp"
#include "okay/Core/DataPack.hpp"
#include "okay/Core/SaveData.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace okay {

namespace {

namespace fs = std::filesystem;
using Override = GameObject::PrefabOverride;

constexpr std::size_t kMaxObjects = 200000;                    // objects one prefab may expand to
constexpr std::uintmax_t kMaxFileBytes = 64ull * 1024 * 1024;  // refuse a bigger prefab file

// ---- Text helpers ----------------------------------------------------------------

std::string Quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

std::string Trim(const std::string& s) {
    const std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return std::string();
    const std::size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string FirstToken(const std::string& line) {
    std::size_t a = 0;
    while (a < line.size() && std::isspace((unsigned char)line[a])) ++a;
    std::size_t b = a;
    while (b < line.size() && !std::isspace((unsigned char)line[b])) ++b;
    return line.substr(a, b - a);
}

void SkipSpaces(const std::string& s, std::size_t& p) {
    while (p < s.size() && std::isspace((unsigned char)s[p])) ++p;
}

// A quoted token starting at `p` (after spaces); an unquoted word is accepted too.
bool ReadQuoted(const std::string& s, std::size_t& p, std::string& out) {
    SkipSpaces(s, p);
    out.clear();
    if (p >= s.size()) return false;
    if (s[p] != '"') {
        const std::size_t b = p;
        while (p < s.size() && !std::isspace((unsigned char)s[p])) ++p;
        out = s.substr(b, p - b);
        return !out.empty();
    }
    ++p;
    while (p < s.size()) {
        const char c = s[p++];
        if (c == '\\') { if (p < s.size()) out += s[p++]; }
        else if (c == '"') return true;
        else out += c;
    }
    return false;   // unterminated
}

bool ReadInt(const std::string& s, std::size_t& p, int& v) {
    SkipSpaces(s, p);
    std::size_t q = p;
    if (q < s.size() && (s[q] == '-' || s[q] == '+')) ++q;
    const std::size_t digits = q;
    while (q < s.size() && std::isdigit((unsigned char)s[q])) ++q;
    if (q == digits || q - digits > 9) return false;
    v = std::atoi(s.substr(p, q - p).c_str());
    p = q;
    return true;
}

std::string BaseName(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string Canon(const std::string& path) {
    std::error_code ec;
    fs::path c = fs::weakly_canonical(fs::path(path), ec);
    if (ec) c = fs::path(path).lexically_normal();
    return c.generic_string();
}

// A path named inside a prefab file: as written if that file exists, else next to the
// file that names it.
std::string ResolveRef(const std::string& ref, const std::string& fromFile) {
    std::error_code ec;
    if (ref.empty() || fs::exists(fs::path(ref), ec)) return ref;
    const fs::path from(fromFile);
    if (from.has_parent_path()) {
        const fs::path cand = from.parent_path() / ref;
        if (fs::exists(cand, ec)) return cand.string();
    }
    return ref;
}

// Records that describe an object's link to a prefab or its place in a document, not
// its content: never compared and never overridden (same rule as wave 1).
bool IsBookkeepingRecord(const std::string& tok) {
    return tok == "prefab" || tok == "override" || tok == "guid" || tok == "sibling" || tok == "parent" ||
           tok == "srcscene" || tok == "locked" || tok.empty();
}

// Lines that make up the file structure itself; an override may never be one of these.
bool IsStructuralToken(const std::string& tok) {
    return tok == "end" || tok == "gameobject" || tok == "okayscene" || tok == "parent" || tok == "sibling" ||
           tok == "guid" || tok == "prefab" || tok == "override" || tok == "unset" || tok == "remove" ||
           tok == "under" || tok == "nestedprefab" || tok == "variant" || tok.empty();
}

// A record's identity inside one object: its name plus which occurrence (so two
// colliders stay distinct). `compoff` lines are identified by their whole text.
std::string RecordKey(const std::string& line, std::unordered_map<std::string, int>& seen, int* occ = nullptr) {
    const std::string tok = FirstToken(line);
    const int n = seen[tok]++;
    if (occ) *occ = n;
    if (tok == "compoff") return line;
    return tok + "#" + std::to_string(n);
}

std::string Capitalized(std::string s) {
    if (!s.empty()) s[0] = (char)std::toupper((unsigned char)s[0]);
    return s;
}

// ---- Delta: overrides, unsets and removals ------------------------------------------

struct Unset { int object = 0; std::string record; int occurrence = 0; };

struct Delta {
    std::vector<Override> overrides;
    std::vector<Unset> unsets;
    std::vector<int> removes;
    bool Empty() const { return overrides.empty() && unsets.empty() && removes.empty(); }
};

// One delta record: `override <i> "<line>"`, `unset <i> "<record>" [n]`, `remove <i>`.
// 0 = not a delta line, 1 = parsed, -1 = malformed.
int ParseDeltaLine(const std::string& t, Delta& d) {
    const std::string tok = FirstToken(t);
    if (tok != "override" && tok != "unset" && tok != "remove") return 0;
    std::size_t p = tok.size() + (t.find(tok));
    int obj = 0;
    if (!ReadInt(t, p, obj) || obj < 0) return -1;
    if (tok == "remove") { d.removes.push_back(obj); return 1; }
    std::string text;
    SkipSpaces(t, p);
    if (p >= t.size() || t[p] != '"' || !ReadQuoted(t, p, text) || text.empty()) return -1;   // the line must be quoted
    if (tok == "override") { d.overrides.push_back({obj, Trim(text)}); return 1; }
    int occ = 0;
    if (!ReadInt(t, p, occ) || occ < 0) occ = 0;
    d.unsets.push_back({obj, text, occ});
    return 1;
}

// ---- Raw documents ------------------------------------------------------------------

struct RawObj {
    int idx = 0;                       // the number in its `gameobject` header
    std::string name;
    std::vector<std::string> lines;    // body lines as written, without the closing `end`
};

struct RawDoc {
    int version = 3;
    std::vector<std::string> head;     // top-level lines before the first object (variant, override...)
    std::vector<std::string> tail;     // top-level lines after it
    std::vector<RawObj> objs;
};

// Strict about structure, so a truncated or damaged file is refused here and never
// reaches the object loader half-built.
bool ParseRaw(const std::string& text, RawDoc& doc, std::string& err) {
    doc = RawDoc();
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    bool header = false;
    RawObj* cur = nullptr;
    std::unordered_set<int> seen;
    while (std::getline(in, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = Trim(line);
        if (!header) {
            if (t.empty()) continue;
            if (FirstToken(t) != "okayscene") { err = "missing 'okayscene' header"; return false; }
            std::size_t p = 9;
            int v = 0;
            if (!ReadInt(t, p, v) || v < 1) { err = "bad 'okayscene' version"; return false; }
            doc.version = v;
            header = true;
            continue;
        }
        if (cur) {
            if (t == "end") { cur = nullptr; continue; }
            if (FirstToken(t) == "gameobject") { err = "object " + std::to_string(cur->idx) + " is cut short (no 'end')"; return false; }
            if (!t.empty()) cur->lines.push_back(line);
            continue;
        }
        if (t.empty() || t == "end") continue;
        if (FirstToken(t) == "gameobject") {
            std::size_t p = 10;
            int idx = 0;
            std::string name;
            if (!ReadInt(t, p, idx) || idx < 0 || !ReadQuoted(t, p, name)) {
                err = "malformed 'gameobject' header on line " + std::to_string(lineNo);
                return false;
            }
            if (!seen.insert(idx).second) { err = "duplicate gameobject index " + std::to_string(idx); return false; }
            if (doc.objs.size() >= kMaxObjects) { err = "too many objects"; return false; }
            doc.objs.push_back({idx, name, {}});
            cur = &doc.objs.back();
            continue;
        }
        (doc.objs.empty() ? doc.head : doc.tail).push_back(t);
    }
    if (!header) { err = "missing 'okayscene' header"; return false; }
    if (cur) { err = "object " + std::to_string(cur->idx) + " is cut short (no 'end')"; return false; }
    return true;
}

std::string RawToText(const RawDoc& doc) {
    std::string out = "okayscene " + std::to_string(doc.version) + "\n";
    for (const auto& l : doc.head) out += l + "\n";
    for (const auto& o : doc.objs) {
        out += "gameobject " + std::to_string(o.idx) + " " + Quote(o.name) + "\n";
        for (const auto& l : o.lines) out += l + "\n";
        out += "end\n";
    }
    for (const auto& l : doc.tail) out += l + "\n";
    return out;
}

// What one raw object means.
struct ObjSem {
    int parent = -1;                      // header index of the parent
    bool hasNested = false;
    std::string nested;                   // nestedprefab "<file>"
    int under = -1;                       // attach point (variant / placeholder children)
    Delta delta;                          // a placeholder's override / unset / remove records
    std::vector<std::string> content;     // the object's own records, trimmed
    std::vector<std::string> problems;    // malformed lines
};

ObjSem Interpret(const RawObj& o) {
    ObjSem s;
    for (const auto& raw : o.lines) {
        const std::string t = Trim(raw);
        if (t.empty()) continue;
        const std::string tok = FirstToken(t);
        std::size_t p = t.find(tok) + tok.size();
        if (tok == "parent") { int v = -1; if (ReadInt(t, p, v)) s.parent = v; }
        else if (tok == "nestedprefab") {
            std::string path;
            if (ReadQuoted(t, p, path) && !path.empty()) { s.hasNested = true; s.nested = path; }
            else s.problems.push_back("malformed nestedprefab record");
        }
        else if (tok == "under") { int v = -1; if (ReadInt(t, p, v)) s.under = v; }
        else if (tok == "override" || tok == "unset" || tok == "remove") {
            if (ParseDeltaLine(t, s.delta) < 0) s.problems.push_back("malformed " + tok + " record");
        }
        else if (tok == "sibling" || tok == "guid" || tok == "prefab") { /* link bookkeeping: not content */ }
        else s.content.push_back(t);
    }
    return s;
}

// ---- Resolved documents ----------------------------------------------------------------

// Where an object of a resolved document comes from, in terms of the top file.
struct Prov {
    enum Kind : char { None, Own, Base, Nested } kind = None;
    int a = -1;   // Own: position in the file's objects. Base: object index in the base.
                  // Nested: position of the nestedprefab object in the file's objects.
    int b = -1;   // Nested: object index inside the nested prefab.
};

struct RObj {
    std::string name;
    std::vector<std::string> lines;   // records, trimmed, no parent / link records
    int parent = -1;
    std::string link;                 // root of a nested instance: the file it is an instance of
    Prov prov;
    int attach = -1;                  // scratch: attach point while composing
};

struct RDoc {
    int version = 3;
    std::vector<RObj> objs;           // depth-first, objs[0] is the root
};

struct Diag {
    std::vector<PrefabIssue> issues;
    void Add(PrefabIssue::Severity s, const std::string& file, const std::string& msg) {
        for (const auto& i : issues) if (i.severity == s && i.file == file && i.message == msg) return;
        PrefabIssue is; is.severity = s; is.file = file; is.message = msg;
        issues.push_back(is);
    }
    void Warn(const std::string& file, const std::string& msg) { Add(PrefabIssue::Severity::Warning, file, msg); }
    void Error(const std::string& file, const std::string& msg) { Add(PrefabIssue::Severity::Error, file, msg); }
};

std::string FormatIssue(const PrefabIssue& i) {
    return i.file.empty() ? i.message : BaseName(i.file) + ": " + i.message;
}

// Remove the flagged objects and everything below them. Returns old index -> new index
// (-1 for a removed object). Does not need depth-first order.
std::vector<int> RemoveSubtrees(std::vector<RObj>& objs, std::vector<char> drop) {
    const std::size_t n = objs.size();
    drop.resize(n, 0);
    std::vector<std::vector<int>> kids(n);
    for (std::size_t i = 0; i < n; ++i)
        if (objs[i].parent >= 0 && (std::size_t)objs[i].parent < n) kids[(std::size_t)objs[i].parent].push_back((int)i);
    std::vector<int> stack;
    for (std::size_t i = 0; i < n; ++i) if (drop[i]) stack.push_back((int)i);
    while (!stack.empty()) {
        const int x = stack.back(); stack.pop_back();
        for (int k : kids[(std::size_t)x]) if (!drop[(std::size_t)k]) { drop[(std::size_t)k] = 1; stack.push_back(k); }
    }
    std::vector<int> map(n, -1);
    std::vector<RObj> kept;
    kept.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
        if (!drop[i]) { map[i] = (int)kept.size(); kept.push_back(std::move(objs[i])); }
    for (auto& o : kept) o.parent = (o.parent >= 0 && (std::size_t)o.parent < n) ? map[(std::size_t)o.parent] : -1;
    objs = std::move(kept);
    return map;
}

// Put the objects in depth-first order (children in their current order) and fix the
// parent indices. objs[0] stays the root; any other object without a parent goes under
// it. False if the parent links loop.
bool Normalize(std::vector<RObj>& objs, Diag& diag, const std::string& file) {
    const std::size_t n = objs.size();
    if (n == 0) return false;
    objs[0].parent = -1;
    std::vector<std::vector<int>> kids(n);
    for (std::size_t i = 1; i < n; ++i) {
        int p = objs[i].parent;
        if (p < 0 || (std::size_t)p >= n || (std::size_t)p == i) {
            if (p >= 0) diag.Warn(file, "object '" + objs[i].name + "' names a parent that does not exist; placed under the root");
            else diag.Warn(file, "object '" + objs[i].name + "' has no parent; placed under the root");
            p = 0;
        }
        kids[(std::size_t)p].push_back((int)i);
    }
    std::vector<int> order;
    order.reserve(n);
    std::vector<int> stack{0};
    while (!stack.empty()) {
        const int x = stack.back(); stack.pop_back();
        order.push_back(x);
        const auto& k = kids[(std::size_t)x];
        for (auto it = k.rbegin(); it != k.rend(); ++it) stack.push_back(*it);
    }
    if (order.size() != n) return false;
    std::vector<int> pos(n, -1);
    for (std::size_t i = 0; i < n; ++i) pos[(std::size_t)order[i]] = (int)i;
    std::vector<RObj> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        RObj o = std::move(objs[(std::size_t)order[i]]);
        if (i == 0) o.parent = -1;
        else {
            // the parent the loop above settled on
            int p = o.parent;
            if (p < 0 || (std::size_t)p >= n || (std::size_t)p == (std::size_t)order[i]) p = 0;
            o.parent = pos[(std::size_t)p];
        }
        out.push_back(std::move(o));
    }
    objs = std::move(out);
    return true;
}

// Apply a delta to a resolved object list. All indices in the delta mean the list as it
// is on entry. Returns old index -> new index (-1 for removed objects).
std::vector<int> ApplyDelta(std::vector<RObj>& objs, const Delta& d, Diag& diag, const std::string& file,
                            const std::string& what) {
    const std::size_t n = objs.size();
    std::vector<char> drop(n, 0);
    for (int r : d.removes) {
        if (r == 0) { diag.Warn(file, "remove 0 ignored: the root object cannot be removed"); continue; }
        if (r < 0 || (std::size_t)r >= n) {
            diag.Warn(file, "remove of object " + std::to_string(r) + " dropped: " + what + " no longer has it");
            continue;
        }
        drop[(std::size_t)r] = 1;
    }
    // Which objects the removals take with them (so overrides for them can be reported).
    std::vector<char> gone = drop;
    {
        std::vector<std::vector<int>> kids(n);
        for (std::size_t i = 0; i < n; ++i)
            if (objs[i].parent >= 0 && (std::size_t)objs[i].parent < n) kids[(std::size_t)objs[i].parent].push_back((int)i);
        std::vector<int> st;
        for (std::size_t i = 0; i < n; ++i) if (gone[i]) st.push_back((int)i);
        while (!st.empty()) {
            const int x = st.back(); st.pop_back();
            for (int k : kids[(std::size_t)x]) if (!gone[(std::size_t)k]) { gone[(std::size_t)k] = 1; st.push_back(k); }
        }
    }
    for (const auto& u : d.unsets) {
        if (u.object < 0 || (std::size_t)u.object >= n) {
            diag.Warn(file, "unset of object " + std::to_string(u.object) + " dropped: " + what + " no longer has it");
            continue;
        }
        if (gone[(std::size_t)u.object]) continue;
        int k = 0;
        auto& lines = objs[(std::size_t)u.object].lines;
        for (std::size_t i = 0; i < lines.size(); ++i)
            if (FirstToken(lines[i]) == u.record && k++ == u.occurrence) { lines.erase(lines.begin() + (std::ptrdiff_t)i); break; }
    }
    for (std::size_t oi = 0; oi < d.overrides.size(); ++oi) {
        const Override& ov = d.overrides[oi];
        const std::string line = Trim(ov.line);
        const std::string tok = FirstToken(line);
        if (ov.object < 0 || (std::size_t)ov.object >= n) {
            diag.Warn(file, "prefab override for object " + std::to_string(ov.object) + " dropped: " + what + " no longer has it");
            continue;
        }
        if (line.find('\n') != std::string::npos || line.find('\r') != std::string::npos || IsStructuralToken(tok)) {
            diag.Warn(file, "override for object " + std::to_string(ov.object) + " ignored: '" + tok + "' cannot be overridden");
            continue;
        }
        if (gone[(std::size_t)ov.object]) {
            diag.Warn(file, "override for object " + std::to_string(ov.object) + " ignored: the same file removes it");
            continue;
        }
        // Overrides are written in record order, so the Nth override of a name on an object
        // means the Nth record of that name.
        int occ = 0;
        for (std::size_t pj = 0; pj < oi; ++pj)
            if (d.overrides[pj].object == ov.object && FirstToken(Trim(d.overrides[pj].line)) == tok) ++occ;
        auto& lines = objs[(std::size_t)ov.object].lines;
        bool replaced = false;
        if (tok == "compoff") {
            for (const auto& l : lines) if (l == line) { replaced = true; break; }
        } else {
            int k = 0;
            for (auto& l : lines)
                if (FirstToken(l) == tok && k++ == occ) { l = line; replaced = true; break; }
        }
        if (!replaced) lines.push_back(line);
    }
    return RemoveSubtrees(objs, drop);
}

std::string Render(int version, const std::vector<RObj>& objs) {
    std::string out = "okayscene " + std::to_string(version < 1 ? 3 : version) + "\n";
    for (std::size_t i = 0; i < objs.size(); ++i) {
        out += "gameobject " + std::to_string(i) + " " + Quote(objs[i].name) + "\n";
        for (const auto& l : objs[i].lines) out += "  " + l + "\n";
        out += "  parent " + std::to_string(objs[i].parent) + "\nend\n";
    }
    return out;
}

// ---- The resolver ------------------------------------------------------------------------

struct Baseline;

struct Ctx {
    int maxDepth = PrefabTools::kMaxDepth;
    std::vector<std::string> stackKeys, stackNames;
    std::map<std::string, std::shared_ptr<const RDoc>> okDocs;
    std::map<std::string, std::string> failed;
    std::map<std::string, std::shared_ptr<const Baseline>> baselines;
    std::map<std::string, std::string> textOverride;   // canonical path -> text to use instead of the file
    Diag diag;
    std::string fatal;                                 // cycle / depth / size: stops everything
    int skippedNested = 0;
};

enum class Status { Ok, Failed, Fatal };

bool ReadPrefabText(Ctx& ctx, const std::string& path, const std::string& key, std::string& text, std::string& why) {
    auto ov = ctx.textOverride.find(key);
    if (ov != ctx.textOverride.end()) { text = ov->second; return true; }
    std::error_code ec;
    if (fs::is_directory(path, ec)) { why = "cannot open " + path; return false; }
    const std::uintmax_t size = fs::file_size(path, ec);
    if (!ec && size > kMaxFileBytes) { why = path + " is too large"; return false; }
    std::ifstream f(path, std::ios::binary);
    if (!f) { why = "cannot open " + path; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string raw = ss.str();
    text = DataPack::Unpack(raw);
    if (text.empty() && DataPack::IsPacked(raw)) { why = "tampered or unreadable " + path; return false; }
    if (text.empty()) { why = path + " is empty"; return false; }
    return true;
}

Status ResolveFile(Ctx& ctx, const std::string& path, std::shared_ptr<const RDoc>& out, std::string& why);

// Expand a file's own objects: nested prefabs are resolved and spliced in, with their
// overrides applied. `out` is a forest in file order (sub-trees of nested prefabs sit
// right after their root); roots carry `attach` when the file names one.
Status ExpandOwn(Ctx& ctx, const RawDoc& raw, const std::vector<ObjSem>& sem, const std::string& file,
                 std::vector<RObj>& out, int& version, std::string& why) {
    const std::size_t n = raw.objs.size();
    std::unordered_map<int, int> byIdx;
    for (std::size_t k = 0; k < n; ++k) byIdx[raw.objs[k].idx] = (int)k;
    std::vector<int> parentPos(n, -1);
    for (std::size_t k = 0; k < n; ++k) {
        for (const auto& pr : sem[k].problems) ctx.diag.Warn(file, "object '" + raw.objs[k].name + "': " + pr);
        if (sem[k].parent < 0) continue;
        auto it = byIdx.find(sem[k].parent);
        if (it != byIdx.end() && it->second != (int)k) parentPos[k] = it->second;
        else ctx.diag.Warn(file, "object '" + raw.objs[k].name + "' names a parent that does not exist; treated as a root");
    }

    // 1. Resolve every nested prefab. A cycle or depth error is fatal; a missing or
    //    broken one skips just that instance.
    std::vector<std::shared_ptr<const RDoc>> sub(n);
    std::vector<std::string> subPath(n);
    std::vector<char> skip(n, 0);
    for (std::size_t k = 0; k < n; ++k) {
        if (!sem[k].hasNested) continue;
        const std::string ref = ResolveRef(sem[k].nested, file);
        std::string why2;
        const Status s = ResolveFile(ctx, ref, sub[k], why2);
        if (s == Status::Fatal) return Status::Fatal;
        if (s == Status::Failed) {
            ++ctx.skippedNested;
            ctx.diag.Error(file, "nested prefab '" + sem[k].nested + "' (object '" + raw.objs[k].name + "') skipped: " + why2);
            skip[k] = 1;
        } else subPath[k] = ref;
    }
    // 2. Objects under a skipped one are skipped with it.
    {
        std::vector<std::vector<int>> kids(n);
        for (std::size_t k = 0; k < n; ++k) if (parentPos[k] >= 0) kids[(std::size_t)parentPos[k]].push_back((int)k);
        std::vector<int> st;
        for (std::size_t k = 0; k < n; ++k) if (skip[k]) st.push_back((int)k);
        while (!st.empty()) {
            const int x = st.back(); st.pop_back();
            for (int c : kids[(std::size_t)x]) if (!skip[(std::size_t)c]) { skip[(std::size_t)c] = 1; st.push_back(c); }
        }
    }
    if (n > 0 && skip[0]) { why = "its root object is a nested prefab that cannot be used"; return Status::Failed; }

    // 3. Emit objects in file order, a nested prefab's whole tree right after its root.
    std::vector<int> outIdxOf(n, -1), subStart(n, -1);
    std::vector<std::vector<int>> subMap(n);
    for (std::size_t k = 0; k < n; ++k) {
        if (skip[k]) continue;
        if (!sem[k].hasNested) {
            RObj o;
            o.name = raw.objs[k].name;
            o.lines = sem[k].content;
            o.prov = {Prov::Own, (int)k, -1};
            o.attach = sem[k].under;
            outIdxOf[k] = (int)out.size();
            out.push_back(std::move(o));
            continue;
        }
        std::vector<RObj> e = sub[k]->objs;
        for (std::size_t j = 0; j < e.size(); ++j) e[j].prov = {Prov::Nested, (int)k, (int)j};
        Delta d = sem[k].delta;
        for (const auto& l : sem[k].content) d.overrides.push_back({0, l});   // its own records edit the nested root
        const std::string what = "the nested prefab '" + BaseName(subPath[k]) + "'";
        const std::vector<int> m = ApplyDelta(e, d, ctx.diag, file, what);
        e[0].name = raw.objs[k].name;
        e[0].link = subPath[k];
        e[0].attach = sem[k].under;
        if (out.size() + e.size() > kMaxObjects) {
            ctx.fatal = "prefab expands to more than " + std::to_string(kMaxObjects) + " objects (at '" + BaseName(file) + "')";
            return Status::Fatal;
        }
        version = std::min(version, sub[k]->version);
        const int start = (int)out.size();
        for (std::size_t j = 0; j < e.size(); ++j) {
            RObj o = std::move(e[j]);
            o.parent = j == 0 ? -1 : start + o.parent;
            out.push_back(std::move(o));
        }
        outIdxOf[k] = start; subStart[k] = start; subMap[k] = m;
    }
    // 4. Parents.
    std::vector<char> dropped(out.size(), 0);
    bool anyDropped = false;
    for (std::size_t k = 0; k < n; ++k) {
        if (skip[k] || outIdxOf[k] < 0) continue;
        const int p = parentPos[k];
        RObj& o = out[(std::size_t)outIdxOf[k]];
        if (p < 0) { o.parent = -1; continue; }
        if (sem[(std::size_t)p].hasNested) {
            // A child listed under a nested prefab object goes under one of the nested
            // prefab's objects: the root unless `under` says another.
            const int j = sem[k].under >= 0 ? sem[k].under : 0;
            const int mj = (std::size_t)j < subMap[(std::size_t)p].size() ? subMap[(std::size_t)p][(std::size_t)j] : -1;
            if (mj < 0) {
                ctx.diag.Warn(file, "object '" + raw.objs[k].name + "' dropped: object " + std::to_string(j) + " of the nested prefab '" +
                                    BaseName(subPath[(std::size_t)p]) + "' no longer exists");
                dropped[(std::size_t)outIdxOf[k]] = 1;
                anyDropped = true;
                continue;
            }
            o.parent = subStart[(std::size_t)p] + mj;
            o.attach = -1;
        } else {
            o.parent = outIdxOf[(std::size_t)p];
            o.attach = -1;
        }
    }
    if (anyDropped) RemoveSubtrees(out, dropped);
    return Status::Ok;
}

Status BuildDoc(Ctx& ctx, const RawDoc& raw, const std::string& file, RDoc& doc, std::string& why) {
    // Variant header and top-level delta.
    bool isVariant = false;
    std::string baseRef;
    Delta top;
    for (const auto* part : {&raw.head, &raw.tail})
        for (const auto& l : *part) {
            const std::string tok = FirstToken(l);
            if (tok == "variant") {
                std::size_t p = l.find(tok) + tok.size();
                std::string b;
                if (!isVariant && ReadQuoted(l, p, b) && !b.empty()) { isVariant = true; baseRef = b; }
                else if (isVariant) ctx.diag.Warn(file, "more than one 'variant' record; the first one is used");
                else ctx.diag.Error(file, "malformed 'variant' record");
            } else if (ParseDeltaLine(l, top) < 0) ctx.diag.Warn(file, "malformed '" + tok + "' record ignored");
        }
    std::vector<ObjSem> sem;
    sem.reserve(raw.objs.size());
    for (const auto& o : raw.objs) sem.push_back(Interpret(o));

    int version = raw.version;
    std::vector<RObj> own;
    {
        const Status s = ExpandOwn(ctx, raw, sem, file, own, version, why);
        if (s != Status::Ok) return s;
    }
    if (!isVariant) {
        if (own.empty()) { why = "the prefab has no objects"; return Status::Failed; }
        if (!Normalize(own, ctx.diag, file)) { why = "the parent links of its objects form a loop"; return Status::Failed; }
        for (auto& o : own) o.attach = -1;
        doc.version = version;
        doc.objs = std::move(own);
        return Status::Ok;
    }

    // Variant: resolve the base, apply this file's delta, then add this file's own objects.
    const std::string ref = ResolveRef(baseRef, file);
    std::shared_ptr<const RDoc> base;
    std::string why2;
    const Status bs = ResolveFile(ctx, ref, base, why2);
    if (bs == Status::Fatal) return Status::Fatal;
    if (bs == Status::Failed) { why = "its base prefab '" + baseRef + "' cannot be used: " + why2; return Status::Failed; }
    std::vector<RObj> objs = base->objs;
    for (std::size_t i = 0; i < objs.size(); ++i) objs[i].prov = {Prov::Base, (int)i, -1};
    version = std::min(version, base->version);
    const std::vector<int> map = ApplyDelta(objs, top, ctx.diag, file, "the base prefab '" + BaseName(ref) + "'");
    if (objs.size() + own.size() > kMaxObjects) {
        ctx.fatal = "prefab expands to more than " + std::to_string(kMaxObjects) + " objects (at '" + BaseName(file) + "')";
        return Status::Fatal;
    }
    const int off = (int)objs.size();
    std::vector<char> dropped(objs.size() + own.size(), 0);
    bool anyDropped = false;
    for (std::size_t i = 0; i < own.size(); ++i) {
        RObj o = std::move(own[i]);
        if (o.parent >= 0) o.parent += off;
        else {
            const int u = o.attach >= 0 ? o.attach : 0;
            const int target = (std::size_t)u < map.size() ? map[(std::size_t)u] : -1;
            if (target < 0) {
                ctx.diag.Warn(file, "added object '" + o.name + "' dropped: object " + std::to_string(u) + " of the base prefab no longer exists");
                dropped[(std::size_t)off + i] = 1;
                anyDropped = true;
            }
            o.parent = target;
        }
        o.attach = -1;
        objs.push_back(std::move(o));
    }
    if (anyDropped) RemoveSubtrees(objs, dropped);
    if (!Normalize(objs, ctx.diag, file)) { why = "the parent links of its objects form a loop"; return Status::Failed; }
    doc.version = version;
    doc.objs = std::move(objs);
    return Status::Ok;
}

struct StackGuard {
    Ctx& c;
    explicit StackGuard(Ctx& cx, const std::string& key, const std::string& name) : c(cx) {
        c.stackKeys.push_back(key); c.stackNames.push_back(name);
    }
    ~StackGuard() { c.stackKeys.pop_back(); c.stackNames.pop_back(); }
};

Status ResolveFile(Ctx& ctx, const std::string& path, std::shared_ptr<const RDoc>& out, std::string& why) {
    if (!ctx.fatal.empty()) return Status::Fatal;
    const std::string key = Canon(path);
    // A cycle: this file is already being resolved further up.
    for (std::size_t i = 0; i < ctx.stackKeys.size(); ++i)
        if (ctx.stackKeys[i] == key) {
            std::string chain;
            for (std::size_t j = i; j < ctx.stackNames.size(); ++j) chain += BaseName(ctx.stackNames[j]) + " -> ";
            ctx.fatal = "prefab cycle: " + chain + BaseName(path);
            why = ctx.fatal;
            return Status::Fatal;
        }
    if ((int)ctx.stackKeys.size() >= ctx.maxDepth) {
        std::string chain;
        for (const auto& nme : ctx.stackNames) chain += BaseName(nme) + " -> ";
        ctx.fatal = "prefab nesting is deeper than " + std::to_string(ctx.maxDepth) + " levels: " + chain + BaseName(path);
        why = ctx.fatal;
        return Status::Fatal;
    }
    auto ok = ctx.okDocs.find(key);
    if (ok != ctx.okDocs.end()) { out = ok->second; return Status::Ok; }
    auto bad = ctx.failed.find(key);
    if (bad != ctx.failed.end()) { why = bad->second; return Status::Failed; }

    std::string text;
    if (!ReadPrefabText(ctx, path, key, text, why)) { ctx.failed[key] = why; return Status::Failed; }
    RawDoc raw;
    std::string err;
    if (!ParseRaw(text, raw, err)) {
        why = BaseName(path) + " is not a valid prefab: " + err;
        ctx.failed[key] = why;
        return Status::Failed;
    }
    auto doc = std::make_shared<RDoc>();
    Status s;
    {
        StackGuard guard(ctx, key, path);
        s = BuildDoc(ctx, raw, path, *doc, why);
    }
    if (s == Status::Fatal) { why = ctx.fatal; return s; }
    if (s == Status::Failed) { ctx.failed[key] = why; return s; }
    ctx.okDocs[key] = doc;
    out = doc;
    return Status::Ok;
}

// ---- The live side: an instance's objects as text -------------------------------------

struct LiveObj {
    GameObject* go = nullptr;
    std::string name;
    std::vector<std::string> lines;   // records as the serializer writes them (trimmed, no parent)
    int parent = -1;                  // index into the same list
};

bool ExtractLive(const GameObject& root, std::vector<LiveObj>& out) {
    out.clear();
    std::vector<GameObject*> subtree;
    {
        std::vector<GameObject*> st{const_cast<GameObject*>(&root)};
        while (!st.empty()) {
            GameObject* g = st.back(); st.pop_back();
            subtree.push_back(g);
            const auto& kids = g->transform->Children();
            for (auto it = kids.rbegin(); it != kids.rend(); ++it) st.push_back((*it)->gameObject);
        }
    }
    RawDoc raw;
    std::string err;
    if (!ParseRaw(SceneSerializer::SerializeObject(root), raw, err) || raw.objs.empty()) return false;
    std::unordered_map<int, int> byIdx;
    for (std::size_t i = 0; i < raw.objs.size(); ++i) {
        const RawObj& ro = raw.objs[i];
        LiveObj lo;
        lo.name = ro.name;
        lo.go = (ro.idx >= 0 && (std::size_t)ro.idx < subtree.size()) ? subtree[(std::size_t)ro.idx] : nullptr;
        int parentHdr = -1;
        for (const auto& raw2 : ro.lines) {
            const std::string t = Trim(raw2);
            if (FirstToken(t) == "parent") { std::size_t p = 6; ReadInt(t, p, parentHdr); }
            else if (!t.empty()) lo.lines.push_back(t);
        }
        byIdx[ro.idx] = (int)out.size();
        lo.parent = parentHdr;   // header index for now
        out.push_back(std::move(lo));
    }
    for (auto& lo : out) {
        if (lo.parent < 0) continue;
        auto it = byIdx.find(lo.parent);
        lo.parent = it == byIdx.end() ? -1 : it->second;
    }
    return !out.empty() && out[0].go != nullptr;
}

// What kinds of records an object has, as one string: two objects at the same place with
// different names count as one renamed object only when these agree.
std::string Signature(const std::vector<std::string>& lines) {
    std::set<std::string> toks;
    for (const auto& l : lines) {
        const std::string t = FirstToken(l);
        if (!IsBookkeepingRecord(t)) toks.insert(t);
    }
    std::string s;
    for (const auto& t : toks) s += t + ",";
    return s;
}

// Match tree B to tree A by position and name. Returns, for each object of B, the index
// of its counterpart in A (-1: none). The roots always match. With signatures, an
// object is matched to one of another name (a rename) only if the signatures are equal.
std::vector<int> MatchTrees(const std::vector<std::string>& aName, const std::vector<int>& aParent,
                            const std::vector<std::string>& bName, const std::vector<int>& bParent,
                            const std::vector<std::string>* aSig = nullptr, const std::vector<std::string>* bSig = nullptr) {
    const std::size_t na = aName.size(), nb = bName.size();
    std::vector<int> res(nb, -1);
    if (na == 0 || nb == 0) return res;
    std::vector<std::vector<int>> ka(na), kb(nb);
    for (std::size_t i = 0; i < na; ++i) if (aParent[i] >= 0 && (std::size_t)aParent[i] < na) ka[(std::size_t)aParent[i]].push_back((int)i);
    for (std::size_t i = 0; i < nb; ++i) if (bParent[i] >= 0 && (std::size_t)bParent[i] < nb) kb[(std::size_t)bParent[i]].push_back((int)i);
    res[0] = 0;
    std::deque<std::pair<int, int>> q;   // (a, b)
    q.push_back({0, 0});
    while (!q.empty()) {
        const auto [x, y] = q.front(); q.pop_front();
        const auto& A = ka[(std::size_t)x];
        const auto& B = kb[(std::size_t)y];
        std::vector<char> used(A.size(), 0);
        std::vector<int> mate(B.size(), -1);
        for (std::size_t p = 0; p < B.size(); ++p)   // same place, same name
            if (p < A.size() && !used[p] && aName[(std::size_t)A[p]] == bName[(std::size_t)B[p]]) { mate[p] = (int)p; used[p] = 1; }
        for (std::size_t p = 0; p < B.size(); ++p) {   // same name elsewhere
            if (mate[p] >= 0) continue;
            for (std::size_t r = 0; r < A.size(); ++r)
                if (!used[r] && aName[(std::size_t)A[r]] == bName[(std::size_t)B[p]]) { mate[p] = (int)r; used[r] = 1; break; }
        }
        for (std::size_t p = 0; p < B.size(); ++p)   // same place, renamed
            if (mate[p] < 0 && p < A.size() && !used[p] &&
                (!aSig || !bSig || (*aSig)[(std::size_t)A[p]] == (*bSig)[(std::size_t)B[p]])) { mate[p] = (int)p; used[p] = 1; }
        for (std::size_t p = 0; p < B.size(); ++p)
            if (mate[p] >= 0) {
                res[(std::size_t)B[p]] = A[(std::size_t)mate[p]];
                q.push_back({A[(std::size_t)mate[p]], B[p]});
            }
    }
    return res;
}

// ---- Baselines: a file as the serializer would write it ----------------------------------

struct Baseline {
    std::shared_ptr<const RDoc> doc;
    std::vector<std::vector<std::string>> canon;   // per object: its records in the serializer's own form
};

// The prefab file's objects loaded into a scratch scene and written back, so both sides
// of a comparison come from the same writer (a hand written file may format differently).
Status MakeBaseline(Ctx& ctx, const std::string& path, std::shared_ptr<const Baseline>& out, std::string& why) {
    const std::string key = Canon(path);
    auto hit = ctx.baselines.find(key);
    if (hit != ctx.baselines.end()) { out = hit->second; return Status::Ok; }
    std::shared_ptr<const RDoc> doc;
    const Status s = ResolveFile(ctx, path, doc, why);
    if (s != Status::Ok) return s;
    auto b = std::make_shared<Baseline>();
    b->doc = doc;
    b->canon.resize(doc->objs.size());
    {
        Scene scratch("prefab");
        std::string err;
        std::vector<std::string> swallow;
        GameObject* r = SceneSerializer::InstantiateFromText(scratch, Render(doc->version, doc->objs), &err, &swallow);
        if (!r) { why = BaseName(path) + " does not load: " + err; return Status::Failed; }
        std::vector<LiveObj> live;
        ExtractLive(*r, live);
        std::vector<std::string> an, bn;
        std::vector<int> ap, bp;
        for (const auto& o : doc->objs) { an.push_back(o.name); ap.push_back(o.parent); }
        for (const auto& o : live) { bn.push_back(o.name); bp.push_back(o.parent); }
        const std::vector<int> m = MatchTrees(an, ap, bn, bp);   // live -> doc
        std::vector<char> filled(doc->objs.size(), 0);
        for (std::size_t i = 0; i < live.size(); ++i)
            if (m[i] >= 0) { b->canon[(std::size_t)m[i]] = live[i].lines; filled[(std::size_t)m[i]] = 1; }
        for (std::size_t i = 0; i < filled.size(); ++i) if (!filled[i]) b->canon[i] = doc->objs[i].lines;
    }
    ctx.baselines[key] = b;
    out = b;
    return Status::Ok;
}

// ---- Fields -----------------------------------------------------------------------------

const std::vector<const char*>* Schema(const std::string& rec) {
    static const std::map<std::string, std::vector<const char*>> s = {
        {"transform", {"position.x", "position.y", "position.z", "rotation.x", "rotation.y", "rotation.z", "rotation.w",
                       "scale.x", "scale.y", "scale.z"}},
        {"sprite", {"glyph", "color.r", "color.g", "color.b", "color.a", "size.x", "size.y", "texture", "uvMin.x", "uvMin.y",
                    "uvMax.x", "uvMax.y", "sortOrder", "flipX", "flipY", "sortingLayer", "texFilter"}},
        {"camera", {"projection", "orthographicSize", "fieldOfView", "backgroundColor.r", "backgroundColor.g",
                    "backgroundColor.b", "backgroundColor.a", "main", "clearFlags", "depth", "nearClip", "farClip",
                    "fovAxisHorizontal", "rect.x", "rect.y", "rect.w", "rect.h", "cullingMask", "physicalCamera",
                    "focalLength", "sensorHeight", "zoom", "dutch", "lensShiftX", "lensShiftY", "pixelPerfect",
                    "pixelsPerUnit", "shakeAmount", "shakeAngle", "shakeFrequency", "shakeRecovery"}},
        {"light", {"color.r", "color.g", "color.b", "color.a", "ambient", "intensity", "type", "range", "spotAngle",
                   "spotSoftness", "useTemperature", "temperature", "ambientColor.r", "ambientColor.g", "ambientColor.b",
                   "falloff", "castShadows", "shadowStrength", "shadowBias", "flicker", "flickerSpeed", "enabled"}},
        {"rigidbody2d", {"bodyType", "gravityScale", "mass", "drag", "bounciness", "friction", "angularDrag",
                         "freezeRotation", "allowSleep", "continuousCollision"}},
        {"boxcollider2d", {"size.x", "size.y", "offset.x", "offset.y", "isTrigger", "layer", "autoFit"}},
        {"circlecollider2d", {"radius", "offset.x", "offset.y", "isTrigger", "layer", "autoFit"}},
        {"tag", {"value"}}, {"active", {"value"}}, {"layer", {"value"}}, {"static", {"value"}}, {"uiorder", {"value"}},
    };
    auto it = s.find(rec);
    return it == s.end() ? nullptr : &it->second;
}

struct FieldTok {
    std::string name;
    std::string value;   // the value text (for a key=value token, the part after '=')
    std::string raw;     // the whole token as written
    bool kv = false;
};

// Split a record into tokens: whitespace separated, with "quoted strings" kept whole
// (also after `key=`).
std::vector<FieldTok> Fields(const std::string& rec, const std::string& line) {
    std::vector<FieldTok> out;
    const std::vector<const char*>* schema = Schema(rec);
    std::size_t p = line.find(rec);
    p = p == std::string::npos ? 0 : p + rec.size();
    int positional = 0;
    while (p < line.size()) {
        SkipSpaces(line, p);
        if (p >= line.size()) break;
        const std::size_t b = p;
        bool inQ = false;
        while (p < line.size()) {
            const char c = line[p];
            if (inQ) {
                if (c == '\\') { p += 2; continue; }
                if (c == '"') inQ = false;
            } else {
                if (c == '"') inQ = true;
                else if (std::isspace((unsigned char)c)) break;
            }
            ++p;
        }
        if (p > line.size()) p = line.size();
        FieldTok t;
        t.raw = line.substr(b, p - b);
        // key=value: an identifier, then '=', before any quote
        std::size_t eq = std::string::npos;
        for (std::size_t i = 0; i < t.raw.size(); ++i) {
            const char c = t.raw[i];
            if (c == '=') { eq = i; break; }
            if (!(std::isalnum((unsigned char)c) || c == '_' || c == '.')) break;
        }
        if (eq != std::string::npos && eq > 0) {
            t.kv = true; t.name = t.raw.substr(0, eq); t.value = t.raw.substr(eq + 1);
        } else {
            t.value = t.raw;
            t.name = (schema && (std::size_t)positional < schema->size()) ? std::string((*schema)[(std::size_t)positional])
                                                                          : "arg" + std::to_string(positional);
            ++positional;
        }
        out.push_back(std::move(t));
    }
    return out;
}

std::string JoinTokens(const std::string& rec, const std::vector<FieldTok>& toks) {
    std::string s = rec;
    for (const auto& t : toks) s += " " + t.raw;
    return s;
}

// Compare two records of the same name field by field. False when the lines have a
// different shape (a different number of positional fields), so only whole records can
// be handled.
bool DiffFields(const std::string& rec, const std::string& a, const std::string& b, std::vector<PrefabFieldDiff>& out) {
    const auto fa = Fields(rec, a), fb = Fields(rec, b);
    std::size_t pa = 0, pb = 0;
    for (const auto& t : fa) if (!t.kv) ++pa;
    for (const auto& t : fb) if (!t.kv) ++pb;
    if (pa != pb) return false;
    std::map<std::string, const FieldTok*> kb;
    for (const auto& t : fb) if (t.kv) kb[t.name] = &t;
    std::set<std::string> seen;
    std::size_t ib = 0;
    for (const auto& t : fa) {
        if (t.kv) {
            seen.insert(t.name);
            auto it = kb.find(t.name);
            if (it == kb.end()) { PrefabFieldDiff d; d.name = t.name; d.prefabValue = t.value; d.inInstance = false; out.push_back(d); }
            else if (it->second->value != t.value) { PrefabFieldDiff d; d.name = t.name; d.prefabValue = t.value; d.instanceValue = it->second->value; out.push_back(d); }
        } else {
            while (ib < fb.size() && fb[ib].kv) ++ib;
            if (ib < fb.size()) {
                if (fb[ib].value != t.value) { PrefabFieldDiff d; d.name = t.name; d.prefabValue = t.value; d.instanceValue = fb[ib].value; out.push_back(d); }
                ++ib;
            }
        }
    }
    for (const auto& t : fb)
        if (t.kv && !seen.count(t.name)) { PrefabFieldDiff d; d.name = t.name; d.instanceValue = t.value; d.inPrefab = false; out.push_back(d); }
    return true;
}

// `target` with one field taken from `source`. Fills `err` and returns false when the
// field does not exist or the records have a different shape.
bool ReplaceField(const std::string& rec, const std::string& target, const std::string& source, const std::string& field,
                  std::string& out, std::string& err) {
    auto ft = Fields(rec, target);
    const auto fsrc = Fields(rec, source);
    std::size_t pt = 0, ps = 0;
    for (const auto& t : ft) if (!t.kv) ++pt;
    for (const auto& t : fsrc) if (!t.kv) ++ps;
    if (pt != ps) { err = "record '" + rec + "' has a different shape in the prefab and the instance; use the whole record"; return false; }
    int ti = -1, si = -1;
    for (std::size_t i = 0; i < ft.size(); ++i) if (ft[i].name == field) { ti = (int)i; break; }
    for (std::size_t i = 0; i < fsrc.size(); ++i) if (fsrc[i].name == field) { si = (int)i; break; }
    if (ti < 0 && si < 0) {
        std::string names;
        for (const auto& t : ft) names += (names.empty() ? "" : ", ") + t.name;
        err = "record '" + rec + "' has no field '" + field + "' (fields: " + names + ")";
        return false;
    }
    if (ti >= 0 && si >= 0) {
        if (ft[(std::size_t)ti].kv != fsrc[(std::size_t)si].kv) { err = "field '" + field + "' is written differently on the two sides; use the whole record"; return false; }
        ft[(std::size_t)ti] = fsrc[(std::size_t)si];
    } else if (ti >= 0) {              // only the target has it: a key=value field to drop
        if (!ft[(std::size_t)ti].kv) { err = "field '" + field + "' cannot be dropped; use the whole record"; return false; }
        ft.erase(ft.begin() + ti);
    } else {                           // only the source has it: a key=value field to add
        if (!fsrc[(std::size_t)si].kv) { err = "field '" + field + "' cannot be added; use the whole record"; return false; }
        ft.push_back(fsrc[(std::size_t)si]);
    }
    out = JoinTokens(rec, ft);
    return true;
}

// ---- The difference between an instance and its prefab -----------------------------------

struct DiffData {
    PrefabDiff pub;
    std::vector<Override> overrides;     // Changed and Added records, in object / record order
    std::vector<int> removedObjects;     // topmost lost objects (prefab indices)
    std::vector<Unset> removedRecords;
    std::vector<int> recOverride;        // per pub.records: index into overrides, or -1
    std::vector<int> recUnset;           // per pub.records: index into removedRecords, or -1
    std::vector<LiveObj> live;
    std::vector<int> liveToDoc;          // per live object: prefab index or -1
    std::shared_ptr<const Baseline> base;
};

std::string LivePath(const std::vector<LiveObj>& live, int i) {
    std::vector<std::string> parts;
    for (int g = 0; i >= 0 && g < 4096; i = live[(std::size_t)i].parent, ++g) parts.push_back(live[(std::size_t)i].name);
    std::string s;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) s += (s.empty() ? "" : "/") + *it;
    return s;
}

bool ComputeDiff(Ctx& ctx, const GameObject& root, DiffData& dd, std::string& err) {
    if (root.prefabSource.empty()) { err = "object is not a prefab instance"; return false; }
    {
        std::string why;
        const Status s = MakeBaseline(ctx, root.prefabSource, dd.base, why);
        if (s != Status::Ok) { err = why; return false; }
    }
    if (!ExtractLive(root, dd.live)) { err = "cannot read the instance"; return false; }
    const Baseline& B = *dd.base;
    std::vector<std::string> an, bn;
    std::vector<int> ap, bp;
    for (const auto& o : B.doc->objs) { an.push_back(o.name); ap.push_back(o.parent); }
    for (const auto& o : dd.live) { bn.push_back(o.name); bp.push_back(o.parent); }
    std::vector<std::string> asig, bsig;
    for (const auto& c : B.canon) asig.push_back(Signature(c));
    for (const auto& o : dd.live) bsig.push_back(Signature(o.lines));
    dd.liveToDoc = MatchTrees(an, ap, bn, bp, &asig, &bsig);
    std::vector<int> docToLive(B.doc->objs.size(), -1);
    for (std::size_t i = 0; i < dd.liveToDoc.size(); ++i) if (dd.liveToDoc[i] >= 0) docToLive[(std::size_t)dd.liveToDoc[i]] = (int)i;

    for (std::size_t d = 0; d < B.doc->objs.size(); ++d) {
        const int l = docToLive[d];
        if (l < 0) continue;
        const std::string& oname = B.doc->objs[d].name;
        // key -> (line, occurrence) of the prefab's records
        std::unordered_map<std::string, std::pair<std::string, int>> base;
        std::vector<std::string> baseKeys;
        {
            std::unordered_map<std::string, int> seen;
            for (const auto& ln : B.canon[d]) {
                if (IsBookkeepingRecord(FirstToken(ln))) continue;
                int occ = 0;
                const std::string k = RecordKey(ln, seen, &occ);
                base[k] = {ln, occ};
                baseKeys.push_back(k);
            }
        }
        std::set<std::string> used;
        std::unordered_map<std::string, int> seen;
        for (const auto& ln : dd.live[(std::size_t)l].lines) {
            const std::string tok = FirstToken(ln);
            if (IsBookkeepingRecord(tok)) continue;
            int occ = 0;
            const std::string k = RecordKey(ln, seen, &occ);
            auto b = base.find(k);
            if (b != base.end()) used.insert(k);
            if (b != base.end() && b->second.first == ln) continue;
            PrefabRecordDiff rd;
            rd.object = (int)d; rd.objectName = oname; rd.record = tok; rd.occurrence = occ; rd.instanceLine = ln;
            if (b == base.end()) rd.kind = PrefabRecordDiff::Kind::Added;
            else {
                rd.kind = PrefabRecordDiff::Kind::Changed;
                rd.prefabLine = b->second.first;
                rd.wholeRecordOnly = !DiffFields(tok, rd.prefabLine, ln, rd.fields);
                if (rd.wholeRecordOnly) rd.fields.clear();
            }
            dd.recOverride.push_back((int)dd.overrides.size());
            dd.recUnset.push_back(-1);
            dd.overrides.push_back({(int)d, ln});
            dd.pub.records.push_back(std::move(rd));
        }
        for (const auto& k : baseKeys) {
            if (used.count(k)) continue;
            const auto& pr = base[k];
            PrefabRecordDiff rd;
            rd.kind = PrefabRecordDiff::Kind::Removed;
            rd.object = (int)d; rd.objectName = oname; rd.record = FirstToken(pr.first); rd.occurrence = pr.second;
            rd.prefabLine = pr.first;
            dd.recOverride.push_back(-1);
            dd.recUnset.push_back((int)dd.removedRecords.size());
            dd.removedRecords.push_back({(int)d, rd.record, pr.second});
            dd.pub.records.push_back(std::move(rd));
        }
    }
    // Objects: lost ones (topmost) and added ones (topmost).
    for (std::size_t d = 1; d < B.doc->objs.size(); ++d)
        if (docToLive[d] < 0 && B.doc->objs[d].parent >= 0 && docToLive[(std::size_t)B.doc->objs[d].parent] >= 0) {
            PrefabObjectDiff od;
            od.kind = PrefabObjectDiff::Kind::Removed; od.object = (int)d; od.parentObject = B.doc->objs[d].parent;
            od.name = B.doc->objs[d].name;
            dd.removedObjects.push_back((int)d);
            dd.pub.objects.push_back(od);
        }
    for (std::size_t i = 1; i < dd.live.size(); ++i)
        if (dd.liveToDoc[i] < 0 && dd.live[i].parent >= 0 && dd.liveToDoc[(std::size_t)dd.live[i].parent] >= 0) {
            PrefabObjectDiff od;
            od.kind = PrefabObjectDiff::Kind::Added; od.parentObject = dd.liveToDoc[(std::size_t)dd.live[i].parent];
            od.name = dd.live[i].name; od.path = LivePath(dd.live, (int)i); od.live = dd.live[i].go;
            dd.pub.objects.push_back(od);
        }
    return true;
}

// ---- Instances: building, linking, rebuilding ----------------------------------------------

std::string ErrorFor(const Ctx& ctx, const std::string& why) { return ctx.fatal.empty() ? why : ctx.fatal; }

void MoveWarnings(const Ctx& ctx, std::vector<std::string>* warnings) {
    if (!warnings) return;
    for (const auto& i : ctx.diag.issues) warnings->push_back(FormatIssue(i));
}

// Give every nested instance in a freshly built tree its link and its overrides (the
// difference between it and its own file).
void LinkNested(Ctx& ctx, GameObject& root, const std::vector<RObj>& objs) {
    bool any = false;
    for (std::size_t i = 1; i < objs.size(); ++i) if (!objs[i].link.empty()) { any = true; break; }
    if (!any) return;
    std::vector<LiveObj> live;
    if (!ExtractLive(root, live)) return;
    std::vector<std::string> an, bn;
    std::vector<int> ap, bp;
    for (const auto& o : objs) { an.push_back(o.name); ap.push_back(o.parent); }
    for (const auto& o : live) { bn.push_back(o.name); bp.push_back(o.parent); }
    const std::vector<int> m = MatchTrees(an, ap, bn, bp);   // live -> doc
    std::vector<GameObject*> nested;
    for (std::size_t i = 0; i < live.size(); ++i) {
        const int d = m[i];
        if (d <= 0 || objs[(std::size_t)d].link.empty() || !live[i].go) continue;
        live[i].go->prefabSource = objs[(std::size_t)d].link;
        nested.push_back(live[i].go);
    }
    for (GameObject* g : nested) {
        DiffData dd;
        std::string err;
        if (ComputeDiff(ctx, *g, dd, err)) g->prefabOverrides = dd.overrides;
        else ctx.diag.Warn(g->prefabSource, "could not record the overrides of nested instance '" + g->name + "': " + err);
    }
}

struct RebuildMods {
    std::vector<Override> overrides;
    std::vector<int> removedObjects;
    std::vector<Unset> unsets;
    bool keepAdded = true;
    bool keepNames = false;   // objects keep the names they have now (names are not overrides)
};

GameObject* BuildInstance(Scene& scene, Ctx& ctx, const std::string& path, const RebuildMods& mods, bool allowBroken,
                          std::string* error, std::vector<std::string>* warnings, std::vector<RObj>* outObjs = nullptr) {
    std::shared_ptr<const RDoc> doc;
    std::string why;
    const Status s = ResolveFile(ctx, path, doc, why);
    if (s != Status::Ok) { if (error) *error = ErrorFor(ctx, why); return nullptr; }
    if (!allowBroken && ctx.skippedNested > 0) {
        if (error) {
            *error = "a nested prefab cannot be used";
            for (const auto& i : ctx.diag.issues) if (i.severity == PrefabIssue::Severity::Error) { *error = FormatIssue(i); break; }
        }
        return nullptr;
    }
    std::vector<RObj> objs = doc->objs;
    Delta d;
    d.overrides = mods.overrides;
    d.removes = mods.removedObjects;
    d.unsets = mods.unsets;
    ApplyDelta(objs, d, ctx.diag, path, "the prefab");
    std::string err;
    std::vector<std::string> parseWarn;
    GameObject* root = SceneSerializer::InstantiateFromText(scene, Render(doc->version, objs), &err, &parseWarn);
    if (!root) { if (error) *error = err.empty() ? "cannot load " + path : err; return nullptr; }
    root->prefabSource = path;
    root->prefabOverrides = mods.overrides;
    LinkNested(ctx, *root, objs);
    if (warnings) warnings->insert(warnings->end(), parseWarn.begin(), parseWarn.end());
    MoveWarnings(ctx, warnings);
    if (outObjs) *outObjs = std::move(objs);
    return root;
}

// Replace `old` by a new instance built from its file and `mods`: same name, parent,
// sibling slot and persistent ids; objects added under the old one move across.
GameObject* RebuildInstance(Scene& scene, GameObject& old, const RebuildMods& mods, bool allowBroken,
                            std::string* error, std::vector<std::string>* warnings) {
    if (old.prefabSource.empty()) { if (error) *error = "object is not a prefab instance"; return nullptr; }
    Ctx ctx;
    GameObject* fresh = BuildInstance(scene, ctx, old.prefabSource, mods, allowBroken, error, warnings);
    if (!fresh) return nullptr;   // nothing was added, the instance is as it was
    fresh->name = old.name;
    std::vector<LiveObj> oldL, newL;
    if (ExtractLive(old, oldL) && ExtractLive(*fresh, newL)) {
        std::vector<std::string> an, bn;
        std::vector<int> ap, bp;
        for (const auto& o : oldL) { an.push_back(o.name); ap.push_back(o.parent); }
        for (const auto& o : newL) { bn.push_back(o.name); bp.push_back(o.parent); }
        std::vector<std::string> asig, bsig;
        for (const auto& o : oldL) asig.push_back(Signature(o.lines));
        for (const auto& o : newL) bsig.push_back(Signature(o.lines));
        const std::vector<int> m = MatchTrees(an, ap, bn, bp, &asig, &bsig);   // new -> old
        std::vector<int> oldToNew(oldL.size(), -1);
        for (std::size_t i = 0; i < m.size(); ++i) if (m[i] >= 0) oldToNew[(std::size_t)m[i]] = (int)i;
        for (std::size_t i = 0; i < m.size(); ++i)
            if (m[i] >= 0 && newL[i].go && oldL[(std::size_t)m[i]].go) {
                newL[i].go->SetGuid(oldL[(std::size_t)m[i]].go->Guid());
                if (mods.keepNames) newL[i].go->name = oldL[(std::size_t)m[i]].go->name;
            }
        if (mods.keepAdded) {
            for (std::size_t i = 1; i < oldL.size(); ++i) {
                if (oldToNew[i] >= 0) continue;
                const int p = oldL[i].parent;
                if (p < 0 || oldToNew[(std::size_t)p] < 0 || !oldL[i].go) continue;
                GameObject* np = newL[(std::size_t)oldToNew[(std::size_t)p]].go;
                if (np) oldL[i].go->transform->SetParent(np->transform, /*worldPositionStays=*/false);
            }
        }
    }
    Transform* par = old.transform->Parent();
    if (par) {
        int slot = 0;
        for (std::size_t i = 0; i < par->Children().size(); ++i) if (par->Children()[i] == old.transform) slot = (int)i;
        fresh->transform->SetParent(par, /*worldPositionStays=*/false);
        Transform* before = (std::size_t)slot < par->Children().size() ? par->Children()[(std::size_t)slot] : nullptr;
        if (before && before != fresh->transform) par->ReorderChild(fresh->transform, before, /*after=*/false);
    }
    scene.Destroy(&old);
    return fresh;
}

// Re-capture the stored overrides of an instance, the instances around it and the ones
// inside it (a change to one file moves what its neighbours differ from).
void RefreshLinks(GameObject& x) {
    std::vector<GameObject*> todo;
    for (GameObject* g = &x; g; g = g->transform->Parent() ? g->transform->Parent()->gameObject : nullptr)
        if (!g->prefabSource.empty()) todo.push_back(g);
    std::vector<GameObject*> st{&x};
    while (!st.empty()) {
        GameObject* g = st.back(); st.pop_back();
        for (Transform* c : g->transform->Children()) {
            if (!c->gameObject->prefabSource.empty()) todo.push_back(c->gameObject);
            st.push_back(c->gameObject);
        }
    }
    Ctx ctx;   // fresh: the files have just changed
    for (GameObject* g : todo) {
        DiffData dd;
        std::string err;
        if (ComputeDiff(ctx, *g, dd, err)) g->prefabOverrides = dd.overrides;
    }
}

// ---- Editing a file's text -----------------------------------------------------------------

int FindRecordLine(const std::vector<std::string>& lines, const std::string& rec, int occ, const std::string& wholeLine = "") {
    int k = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::string t = Trim(lines[i]);
        if (rec == "compoff" && !wholeLine.empty()) { if (t == wholeLine) return (int)i; continue; }
        if (FirstToken(t) == rec && k++ == occ) return (int)i;
    }
    return -1;
}

std::string IndentOf(const std::string& l) {
    std::size_t a = 0;
    while (a < l.size() && (l[a] == ' ' || l[a] == '\t')) ++a;
    return l.substr(0, a);
}

// Set the override entry for (object, record, occurrence) in a list of file lines
// (the file's top-level lines for a variant, a nested prefab object's lines otherwise).
// `baseOcc[k]` is the prefab's own k-th record of that name, used to fill earlier
// occurrences so the new entry lands on the right one.
void SetOverrideEntry(std::vector<std::string>& lines, const std::string& indent, int object, const std::string& rec,
                      int occ, const std::string& newLine, const std::vector<std::string>& baseOcc) {
    std::vector<std::size_t> at;   // positions of the entries for this object and record name
    for (std::size_t i = 0; i < lines.size(); ++i) {
        Delta d;
        if (ParseDeltaLine(Trim(lines[i]), d) == 1 && !d.overrides.empty() && d.overrides[0].object == object &&
            FirstToken(d.overrides[0].line) == rec)
            at.push_back(i);
    }
    const std::string entry = indent + "override " + std::to_string(object) + " " + Quote(newLine);
    if (rec == "compoff") {
        for (std::size_t i : at) { Delta d; ParseDeltaLine(Trim(lines[i]), d); if (d.overrides[0].line == newLine) return; }
        lines.push_back(entry);
        return;
    }
    // earlier occurrences without an entry get one that keeps the prefab's own record
    while ((int)at.size() < occ) {
        const std::size_t k = at.size();
        const std::string fill = k < baseOcc.size() ? baseOcc[k] : newLine;
        lines.push_back(indent + "override " + std::to_string(object) + " " + Quote(fill));
        at.push_back(lines.size() - 1);
    }
    if ((int)at.size() > occ) lines[at[(std::size_t)occ]] = entry;
    else lines.push_back(entry);
}

// Write one record difference into a RawDoc following where the object came from.
// Returns false and sets `err` when that is not possible.
bool EditSource(RawDoc& raw, const Baseline& base, const PrefabRecordDiff& rd, const std::string& newLine,
                std::string& err) {
    const Prov pv = base.doc->objs[(std::size_t)rd.object].prov;
    const bool removing = rd.kind == PrefabRecordDiff::Kind::Removed;
    std::vector<std::string> baseOcc;   // the prefab's own records of this name
    for (const auto& l : base.canon[(std::size_t)rd.object]) if (FirstToken(l) == rd.record) baseOcc.push_back(l);
    if (pv.kind == Prov::Own) {
        if (pv.a < 0 || (std::size_t)pv.a >= raw.objs.size()) { err = "internal: object has no place in the file"; return false; }
        auto& lines = raw.objs[(std::size_t)pv.a].lines;
        const int at = FindRecordLine(lines, rd.record, rd.occurrence, removing || rd.record == "compoff" ? rd.prefabLine : "");
        if (removing) {
            if (at < 0) { err = "the record '" + rd.record + "' is not written in the prefab file, so it cannot be removed there"; return false; }
            lines.erase(lines.begin() + at);
        } else if (rd.kind == PrefabRecordDiff::Kind::Added || at < 0) lines.push_back("  " + newLine);
        else lines[(std::size_t)at] = IndentOf(lines[(std::size_t)at]) + newLine;
        return true;
    }
    if (pv.kind == Prov::Base || pv.kind == Prov::Nested) {
        const int target = pv.kind == Prov::Base ? pv.a : pv.b;
        std::vector<std::string>* lines = pv.kind == Prov::Base ? &raw.head : nullptr;
        if (pv.kind == Prov::Nested) {
            if (pv.a < 0 || (std::size_t)pv.a >= raw.objs.size()) { err = "internal: object has no place in the file"; return false; }
            lines = &raw.objs[(std::size_t)pv.a].lines;
        }
        const std::string indent = pv.kind == Prov::Base ? "" : "  ";
        if (removing) {
            lines->push_back(indent + "unset " + std::to_string(target) + " " + Quote(rd.record) + " " + std::to_string(rd.occurrence));
        } else {
            SetOverrideEntry(*lines, indent, target, rd.record,
                             rd.kind == PrefabRecordDiff::Kind::Added ? (int)baseOcc.size() : rd.occurrence, newLine, baseOcc);
        }
        return true;
    }
    err = "internal: object has no source in the file";
    return false;
}

// Read the current text of the prefab file at `path` for editing.
bool ReadEditable(const std::string& path, std::string& text, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { err = "cannot open " + path; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string raw = ss.str();
    if (DataPack::IsPacked(raw)) { err = path + " is packed (read-only); unpack it before applying changes"; return false; }
    text = raw;
    return true;
}

// Check the text would still resolve as a prefab at `path`, then write it atomically.
bool CommitFile(const std::string& path, const std::string& text, std::string& err) {
    Ctx ctx;
    ctx.textOverride[Canon(path)] = text;
    std::shared_ptr<const Baseline> b;
    std::string why;
    const Status s = MakeBaseline(ctx, path, b, why);
    if (s != Status::Ok) {
        err = "not written: the result would not be a valid prefab (" + ErrorFor(ctx, why) + ")";
        return false;
    }
    const bool written = savefmt::AtomicWrite(path, text);
    SceneSerializer::ClearPrefabCache();   // InstantiateFromFile must not hand out the old text
    if (!written) { err = "cannot write " + path; return false; }
    return true;
}

} // namespace

// ---- PrefabReport / PrefabRecordDiff / PrefabDiff --------------------------------------------

bool PrefabReport::Ok() const { return Count(PrefabIssue::Severity::Error) == 0; }

int PrefabReport::Count(PrefabIssue::Severity s) const {
    int n = 0;
    for (const auto& i : issues) if (i.severity == s) ++n;
    return n;
}

std::string PrefabReport::ToString() const {
    std::string out;
    for (const auto& i : issues) {
        const char* sev = i.severity == PrefabIssue::Severity::Error ? "error" : i.severity == PrefabIssue::Severity::Warning ? "warning" : "info";
        out += std::string(sev) + ": " + FormatIssue(i) + "\n";
    }
    return out;
}

std::string PrefabRecordDiff::Describe() const {
    std::string s = Capitalized(record);
    if (kind == Kind::Added) return s + " added to " + objectName;
    if (kind == Kind::Removed) return s + " removed from " + objectName;
    if (fields.empty()) return s + " of " + objectName + " differs";
    for (const auto& f : fields)
        s += " " + f.name + ": " + (f.inPrefab ? f.prefabValue : "(none)") + " -> " + (f.inInstance ? f.instanceValue : "(none)") + ";";
    s.pop_back();
    return s;
}

const PrefabRecordDiff* PrefabDiff::Find(int object, const std::string& record, int occurrence) const {
    for (const auto& r : records) if (r.object == object && r.record == record && r.occurrence == occurrence) return &r;
    return nullptr;
}

// ---- PrefabTools: kinds -------------------------------------------------------------------------

bool PrefabTools::UsesPrefabStructure(const std::string& text) {
    if (text.find("nestedprefab") == std::string::npos && text.find("variant") == std::string::npos) return false;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::string tok = FirstToken(line);
        if (tok == "nestedprefab" || tok == "variant") return true;
    }
    return false;
}

bool PrefabTools::FileUsesPrefabStructure(const std::string& path) {
    Ctx ctx;
    std::string text, why;
    if (!ReadPrefabText(ctx, path, Canon(path), text, why)) return false;
    return UsesPrefabStructure(text);
}

// ---- PrefabTools: instantiate / re-apply / capture -------------------------------------------------

GameObject* PrefabTools::Instantiate(Scene& scene, const std::string& path, const std::vector<Override>& overrides,
                                     std::string* error, std::vector<std::string>* warnings) {
    Ctx ctx;
    RebuildMods mods;
    mods.overrides = overrides;
    return BuildInstance(scene, ctx, path, mods, /*allowBroken=*/true, error, warnings);
}

GameObject* PrefabTools::Reapply(Scene& scene, GameObject& instanceRoot, std::string* error, std::vector<std::string>* warnings) {
    RebuildMods mods;
    mods.overrides = instanceRoot.prefabOverrides;
    return RebuildInstance(scene, instanceRoot, mods, /*allowBroken=*/false, error, warnings);
}

bool PrefabTools::CaptureOverrides(GameObject& instanceRoot, std::string* error) {
    Ctx ctx;
    DiffData dd;
    std::string err;
    if (!ComputeDiff(ctx, instanceRoot, dd, err)) { if (error) *error = ErrorFor(ctx, err); return false; }
    instanceRoot.prefabOverrides = dd.overrides;
    for (GameObject* n : NestedInstances(instanceRoot, true)) {
        DiffData nd;
        std::string e2;
        if (ComputeDiff(ctx, *n, nd, e2)) n->prefabOverrides = nd.overrides;
    }
    return true;
}

// ---- PrefabTools: unpack and queries ----------------------------------------------------------------

bool PrefabTools::IsPrefabInstance(const GameObject& go) { return PrefabRoot(go) != nullptr; }

GameObject* PrefabTools::PrefabRoot(const GameObject& go) {
    GameObject* found = nullptr;
    for (const GameObject* g = &go; g; g = g->transform->Parent() ? g->transform->Parent()->gameObject : nullptr)
        if (!g->prefabSource.empty()) found = const_cast<GameObject*>(g);
    return found;
}

GameObject* PrefabTools::NearestPrefabRoot(const GameObject& go) {
    for (const GameObject* g = &go; g; g = g->transform->Parent() ? g->transform->Parent()->gameObject : nullptr)
        if (!g->prefabSource.empty()) return const_cast<GameObject*>(g);
    return nullptr;
}

std::vector<GameObject*> PrefabTools::NestedInstances(const GameObject& root, bool recursive) {
    std::vector<GameObject*> out;
    std::vector<std::pair<GameObject*, bool>> st;   // (object, below a nested instance)
    const auto& rk = root.transform->Children();
    for (auto it = rk.rbegin(); it != rk.rend(); ++it) st.push_back({(*it)->gameObject, false});
    while (!st.empty()) {
        auto [g, inside] = st.back(); st.pop_back();
        bool nowInside = inside;
        if (!g->prefabSource.empty()) {
            if (recursive || !inside) out.push_back(g);
            nowInside = true;
        }
        const auto& kids = g->transform->Children();
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) st.push_back({(*it)->gameObject, nowInside});
    }
    return out;
}

std::vector<Override> PrefabTools::OverridesOf(const GameObject& root) { return root.prefabOverrides; }

int PrefabTools::Unpack(GameObject& go, bool completely) {
    GameObject* target = PrefabRoot(go);
    if (!target) return 0;
    if (!completely) {
        // What the outer instance changed inside a nested instance moves to that
        // instance's own overrides, so unlinking the outer one loses nothing.
        Ctx ctx;
        for (GameObject* n : NestedInstances(*target, true)) {
            DiffData dd;
            std::string err;
            if (ComputeDiff(ctx, *n, dd, err)) n->prefabOverrides = dd.overrides;
        }
    }
    int count = 0;
    std::vector<GameObject*> todo{target};
    if (completely) { auto n = NestedInstances(*target, true); todo.insert(todo.end(), n.begin(), n.end()); }
    for (GameObject* g : todo) {
        g->prefabSource.clear();
        g->prefabOverrides.clear();
        ++count;
    }
    return count;
}

// ---- PrefabTools: differences, apply, revert ----------------------------------------------------------

bool PrefabTools::Diff(const GameObject& instanceRoot, PrefabDiff& out, std::string* error) {
    Ctx ctx;
    DiffData dd;
    std::string err;
    out = PrefabDiff();
    if (!ComputeDiff(ctx, instanceRoot, dd, err)) { if (error) *error = ErrorFor(ctx, err); return false; }
    out = std::move(dd.pub);
    return true;
}

GameObject* PrefabTools::RevertOverride(Scene& scene, GameObject& instanceRoot, int object, const std::string& record,
                                        const std::string& field, int occurrence, std::string* error,
                                        std::vector<std::string>* warnings) {
    Ctx ctx;
    DiffData dd;
    std::string err;
    if (!ComputeDiff(ctx, instanceRoot, dd, err)) { if (error) *error = ErrorFor(ctx, err); return nullptr; }
    int which = -1;
    for (std::size_t i = 0; i < dd.pub.records.size(); ++i) {
        const auto& r = dd.pub.records[i];
        if (r.object == object && r.record == record && r.occurrence == occurrence) { which = (int)i; break; }
    }
    if (which < 0) {
        if (error) *error = "the instance does not differ from the prefab in record '" + record + "' of object " + std::to_string(object);
        return nullptr;
    }
    const PrefabRecordDiff& rd = dd.pub.records[(std::size_t)which];
    RebuildMods mods;
    mods.overrides = dd.overrides;
    mods.removedObjects = dd.removedObjects;
    mods.unsets = dd.removedRecords;
    mods.keepNames = true;
    const int oi = dd.recOverride[(std::size_t)which], ui = dd.recUnset[(std::size_t)which];
    if (!field.empty() && rd.kind != PrefabRecordDiff::Kind::Changed) {
        if (error) *error = "field '" + field + "' cannot be reverted: the record is not in both the prefab and the instance";
        return nullptr;
    }
    if (rd.kind == PrefabRecordDiff::Kind::Removed) {
        mods.unsets.erase(mods.unsets.begin() + ui);
    } else if (field.empty() || rd.kind == PrefabRecordDiff::Kind::Added) {
        mods.overrides.erase(mods.overrides.begin() + oi);
    } else {
        std::string line;
        if (!ReplaceField(record, rd.instanceLine, rd.prefabLine, field, line, err)) { if (error) *error = err; return nullptr; }
        if (Trim(line) == rd.prefabLine) mods.overrides.erase(mods.overrides.begin() + oi);
        else mods.overrides[(std::size_t)oi].line = line;
    }
    return RebuildInstance(scene, instanceRoot, mods, /*allowBroken=*/false, error, warnings);
}

GameObject* PrefabTools::RevertAll(Scene& scene, GameObject& instanceRoot, std::string* error, std::vector<std::string>* warnings) {
    RebuildMods mods;
    mods.keepAdded = false;
    return RebuildInstance(scene, instanceRoot, mods, /*allowBroken=*/false, error, warnings);
}

namespace {
// Apply the given differences (all of the diff's records when `only` is null) to the
// prefab file of `root`. The file is read once, edited as text, checked and written.
bool ApplyRecords(GameObject& root, const std::string& recordFilter, int object, const std::string& field, int occurrence,
                  bool all, std::string& err) {
    if (root.prefabSource.empty()) { err = "object is not a prefab instance"; return false; }
    const std::string path = root.prefabSource;
    std::string text;
    if (!ReadEditable(path, text, err)) return false;
    Ctx ctx;
    ctx.textOverride[Canon(path)] = text;
    DiffData dd;
    if (!ComputeDiff(ctx, root, dd, err)) { err = ErrorFor(ctx, err); return false; }
    RawDoc raw;
    if (!ParseRaw(text, raw, err)) { err = path + " is not a valid prefab: " + err; return false; }

    std::vector<const PrefabRecordDiff*> todo;
    for (const auto& r : dd.pub.records) {
        if (all || (r.object == object && r.record == recordFilter && r.occurrence == occurrence)) todo.push_back(&r);
    }
    if (todo.empty()) {
        if (all) return true;   // nothing differs
        err = "the instance does not differ from the prefab in record '" + recordFilter + "' of object " + std::to_string(object);
        return false;
    }
    if (!field.empty() && (todo.size() != 1 || todo[0]->kind != PrefabRecordDiff::Kind::Changed)) {
        err = "field '" + field + "' cannot be applied: the record is not in both the prefab and the instance";
        return false;
    }
    // Lost records go last and from the highest occurrence down, so removing one does not
    // move the others still to be edited.
    std::stable_sort(todo.begin(), todo.end(), [](const PrefabRecordDiff* a, const PrefabRecordDiff* b) {
        const bool ra = a->kind == PrefabRecordDiff::Kind::Removed, rb = b->kind == PrefabRecordDiff::Kind::Removed;
        return !ra && rb;
    });
    std::reverse(std::find_if(todo.begin(), todo.end(), [](const PrefabRecordDiff* r) { return r->kind == PrefabRecordDiff::Kind::Removed; }), todo.end());
    for (const PrefabRecordDiff* r : todo) {
        std::string newLine = r->instanceLine;
        if (!field.empty()) {
            if (!ReplaceField(r->record, r->prefabLine, r->instanceLine, field, newLine, err)) return false;
        }
        if (!EditSource(raw, *dd.base, *r, newLine, err)) return false;
    }
    if (!CommitFile(path, RawToText(raw), err)) return false;
    RefreshLinks(root);
    return true;
}
} // namespace

bool PrefabTools::ApplyToSource(GameObject& instanceRoot, int object, const std::string& record, const std::string& field,
                                int occurrence, std::string* error) {
    std::string err;
    const bool ok = ApplyRecords(instanceRoot, record, object, field, occurrence, false, err);
    if (!ok && error) *error = err;
    return ok;
}

bool PrefabTools::ApplyAllToSource(GameObject& instanceRoot, std::string* error) {
    std::string err;
    const bool ok = ApplyRecords(instanceRoot, "", 0, "", 0, true, err);
    if (!ok && error) *error = err;
    return ok;
}

// ---- PrefabTools: authoring -------------------------------------------------------------------------------

namespace {

// One object block of a document being written.
void WriteBlock(std::string& out, int idx, const std::string& name, const std::vector<std::string>& lines, int parent, int under) {
    out += "gameobject " + std::to_string(idx) + " " + Quote(name) + "\n";
    for (const auto& l : lines) out += "  " + l + "\n";
    if (under >= 0) out += "  under " + std::to_string(under) + "\n";
    out += "  parent " + std::to_string(parent) + "\nend\n";
}

std::vector<std::string> ContentLines(const LiveObj& o) {
    std::vector<std::string> out;
    for (const auto& l : o.lines) {
        const std::string tok = FirstToken(l);
        if (tok == "prefab" || tok == "override" || tok == "guid" || tok == "sibling" || tok.empty()) continue;
        out.push_back(l);
    }
    return out;
}

// Delta records for a nested instance or variant, from a diff.
void WriteDelta(std::vector<std::string>& lines, const std::string& indent, const DiffData& dd) {
    for (const auto& o : dd.overrides) lines.push_back(indent + "override " + std::to_string(o.object) + " " + Quote(o.line));
    for (const auto& u : dd.removedRecords)
        lines.push_back(indent + "unset " + std::to_string(u.object) + " " + Quote(u.record) + " " + std::to_string(u.occurrence));
    for (int r : dd.removedObjects) lines.push_back(indent + "remove " + std::to_string(r));
}

} // namespace

bool PrefabTools::SavePrefab(const GameObject& root, const std::string& path, std::string* error) {
    auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
    std::vector<LiveObj> live;
    if (!ExtractLive(root, live)) return fail("nothing to save: the object is not saved by the scene serializer");
    const std::size_t n = live.size();
    std::vector<std::vector<int>> kids(n);
    for (std::size_t i = 0; i < n; ++i) if (live[i].parent >= 0) kids[(std::size_t)live[i].parent].push_back((int)i);
    std::unordered_map<GameObject*, int> liveIndex;
    for (std::size_t i = 0; i < n; ++i) liveIndex[live[i].go] = (int)i;

    // Nested instances (not the root, not inside another one, file still readable) become
    // `nestedprefab` objects. Their own objects are not copied; objects added inside them
    // are written as children of the placeholder.
    Ctx ctx;
    std::vector<char> isHolder(n, 0), nestedPart(n, 0);
    std::vector<int> holderOf(n, -1), docIdxOf(n, -1);
    std::vector<DiffData> holderDiff(n);
    {
        std::vector<int> st{0};
        while (!st.empty()) {
            const int x = st.back(); st.pop_back();
            for (int c : kids[(std::size_t)x]) {
                GameObject* g = live[(std::size_t)c].go;
                std::string err;
                if (g && !g->prefabSource.empty() && ComputeDiff(ctx, *g, holderDiff[(std::size_t)c], err)) {
                    isHolder[(std::size_t)c] = 1;
                    const DiffData& dd = holderDiff[(std::size_t)c];
                    for (std::size_t j = 0; j < dd.live.size(); ++j) {
                        auto it = liveIndex.find(dd.live[j].go);
                        if (it == liveIndex.end()) continue;
                        holderOf[(std::size_t)it->second] = c;
                        docIdxOf[(std::size_t)it->second] = dd.liveToDoc[j];
                        if (j == 0 || dd.liveToDoc[j] >= 0) nestedPart[(std::size_t)it->second] = 1;
                    }
                } else st.push_back(c);
            }
        }
    }
    std::vector<int> order;   // depth-first
    {
        std::vector<int> st{0};
        while (!st.empty()) {
            const int x = st.back(); st.pop_back();
            order.push_back(x);
            for (auto it = kids[(std::size_t)x].rbegin(); it != kids[(std::size_t)x].rend(); ++it) st.push_back(*it);
        }
    }
    std::vector<int> outIdx(n, -1);
    int next = 0;
    for (int i : order) if (!nestedPart[(std::size_t)i] || isHolder[(std::size_t)i]) outIdx[(std::size_t)i] = next++;
    std::string text = "okayscene " + std::to_string(SceneSerializer::kSceneFormatVersion) + "\n";
    for (int i : order) {
        if (outIdx[(std::size_t)i] < 0) continue;
        const LiveObj& o = live[(std::size_t)i];
        int parent = -1, under = -1;
        if (i != 0 && o.parent >= 0) {
            const int p = o.parent;
            if (nestedPart[(std::size_t)p] && !isHolder[(std::size_t)p]) {
                // added inside a nested prefab: child of the placeholder, attached at the
                // nested object it was under
                parent = outIdx[(std::size_t)holderOf[(std::size_t)p]];
                under = docIdxOf[(std::size_t)p] > 0 ? docIdxOf[(std::size_t)p] : -1;
            } else parent = outIdx[(std::size_t)p];
        }
        std::vector<std::string> lines;
        if (isHolder[(std::size_t)i]) {
            lines.push_back("nestedprefab " + Quote(o.go->prefabSource));
            WriteDelta(lines, "", holderDiff[(std::size_t)i]);
        } else lines = ContentLines(o);
        WriteBlock(text, outIdx[(std::size_t)i], o.name, lines, parent, under);
    }
    // Never write a prefab that cannot be used (a cycle through the file being replaced,
    // for instance).
    std::string err;
    if (!CommitFile(path, text, err)) return fail(err);
    return true;
}

bool PrefabTools::SaveVariant(const GameObject& instanceRoot, const std::string& variantPath, std::string* error) {
    auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
    if (instanceRoot.prefabSource.empty()) return fail("object is not a prefab instance");
    if (Canon(variantPath) == Canon(instanceRoot.prefabSource)) return fail("a variant cannot be saved over its own base prefab");
    Ctx ctx;
    DiffData dd;
    std::string err;
    if (!ComputeDiff(ctx, instanceRoot, dd, err)) return fail(ErrorFor(ctx, err));
    std::string text = "okayscene " + std::to_string(SceneSerializer::kSceneFormatVersion) + "\n";
    text += "variant " + Quote(instanceRoot.prefabSource) + "\n";
    std::vector<std::string> head;
    WriteDelta(head, "", dd);
    for (const auto& l : head) text += l + "\n";
    // Added objects: each topmost one with its subtree.
    const std::size_t n = dd.live.size();
    std::vector<std::vector<int>> kids(n);
    for (std::size_t i = 0; i < n; ++i) if (dd.live[i].parent >= 0) kids[(std::size_t)dd.live[i].parent].push_back((int)i);
    int next = 0;
    for (std::size_t i = 1; i < n; ++i) {
        if (dd.liveToDoc[i] >= 0) continue;
        const int p = dd.live[i].parent;
        if (p < 0 || dd.liveToDoc[(std::size_t)p] < 0) continue;   // not a topmost added object
        std::vector<int> sub;
        std::vector<int> stk{(int)i};
        while (!stk.empty()) {
            const int x = stk.back(); stk.pop_back();
            sub.push_back(x);
            for (auto it = kids[(std::size_t)x].rbegin(); it != kids[(std::size_t)x].rend(); ++it) stk.push_back(*it);
        }
        std::unordered_map<int, int> idxOf;
        for (int x : sub) idxOf[x] = next++;
        for (int x : sub) {
            const bool top = x == (int)i;
            WriteBlock(text, idxOf[x], dd.live[(std::size_t)x].name, ContentLines(dd.live[(std::size_t)x]),
                       top ? -1 : idxOf[dd.live[(std::size_t)x].parent], top ? dd.liveToDoc[(std::size_t)p] : -1);
        }
    }
    if (!CommitFile(variantPath, text, err)) return fail(err);
    return true;
}

// ---- PrefabTools: validation ------------------------------------------------------------------------------

PrefabReport PrefabTools::ValidateAsset(const std::string& path) {
    PrefabReport rep;
    rep.path = path;
    Ctx ctx;
    std::deque<std::string> queue{path};
    std::set<std::string> visited;
    while (!queue.empty()) {
        const std::string f = queue.front(); queue.pop_front();
        const std::string key = Canon(f);
        if (!visited.insert(key).second) continue;
        rep.files.push_back(f);
        std::string text, why;
        if (!ReadPrefabText(ctx, f, key, text, why)) {
            ctx.diag.Error(f, why);
            continue;
        }
        RawDoc raw;
        std::string err;
        if (!ParseRaw(text, raw, err)) { ctx.diag.Error(f, "not a valid prefab: " + err); continue; }
        // What it points at: the variant base and the nested prefabs.
        std::vector<std::string> refs;
        for (const auto* part : {&raw.head, &raw.tail})
            for (const auto& l : *part)
                if (FirstToken(l) == "variant") { std::size_t p = l.find("variant") + 7; std::string b; if (ReadQuoted(l, p, b) && !b.empty()) refs.push_back(b); }
        for (const auto& o : raw.objs) {
            const ObjSem s = Interpret(o);
            if (s.hasNested) refs.push_back(s.nested);
        }
        // Resolve it (this finds stale overrides, cycles, depth, missing files) and load it once.
        {
            std::shared_ptr<const Baseline> b;
            std::string w2;
            ctx.fatal.clear();
            const Status s = MakeBaseline(ctx, f, b, w2);
            if (s == Status::Fatal) ctx.diag.Error(f, ctx.fatal);
            else if (s == Status::Failed) ctx.diag.Error(f, w2);
            ctx.fatal.clear();
            ctx.stackKeys.clear(); ctx.stackNames.clear();
        }
        std::error_code ec;
        for (const auto& r : refs) {
            const std::string rr = ResolveRef(r, f);
            if (fs::exists(fs::path(rr), ec)) queue.push_back(rr);   // a missing one is already reported above
        }
    }
    rep.issues = ctx.diag.issues;
    return rep;
}

PrefabReport PrefabTools::ValidateInstance(const GameObject& instanceRoot) {
    PrefabReport rep;
    rep.path = instanceRoot.prefabSource;
    Ctx ctx;
    std::vector<const GameObject*> todo{&instanceRoot};
    for (GameObject* n : NestedInstances(instanceRoot, true)) todo.push_back(n);
    for (const GameObject* g : todo) {
        if (g->prefabSource.empty()) continue;
        std::shared_ptr<const RDoc> doc;
        std::string why;
        const Status s = ResolveFile(ctx, g->prefabSource, doc, why);
        if (s != Status::Ok) { ctx.diag.Error(g->prefabSource, ErrorFor(ctx, why)); ctx.fatal.clear(); continue; }
        rep.files.push_back(g->prefabSource);
        for (const auto& ov : g->prefabOverrides)
            if (ov.object < 0 || (std::size_t)ov.object >= doc->objs.size())
                ctx.diag.Warn(g->prefabSource, "override for object " + std::to_string(ov.object) + " of '" + g->name +
                                               "' names an object the prefab no longer has (it has " +
                                               std::to_string(doc->objs.size()) + ")");
    }
    rep.issues = ctx.diag.issues;
    return rep;
}

} // namespace okay
