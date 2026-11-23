// The prefab file reader and resolver: prefab text read into raw documents, objects expanded with their nested prefabs
// and variant bases folded in. Moved verbatim out of PrefabTools.cpp (docs/engine/modularization_plan.md, batch P1).
#include "PrefabInternal.hpp"
#include "okay/Core/DataPack.hpp"
#include "okay/Core/SaveData.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <set>
#include <sstream>
#include <unordered_set>

namespace okay {
namespace prefab_text {


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
// its content: never compared and never overridden (same rule as wave 1). `comporder` is the order the
// components were added in (a plain AddComponent is recorded too): an instance that gained a component would
// otherwise list a second difference for it.
bool IsBookkeepingRecord(const std::string& tok) {
    return tok == "prefab" || tok == "override" || tok == "guid" || tok == "sibling" || tok == "parent" ||
           tok == "srcscene" || tok == "locked" || tok == "comporder" || tok.empty();
}

// Lines that make up the file structure itself; an override may never be one of these.
bool IsStructuralToken(const std::string& tok) {
    return tok == "end" || tok == "gameobject" || tok == "okayscene" || tok == "parent" || tok == "sibling" ||
           tok == "guid" || tok == "prefab" || tok == "override" || tok == "unset" || tok == "remove" ||
           tok == "under" || tok == "nestedprefab" || tok == "variant" || tok.empty();
}

// A record's identity inside one object: its name plus which occurrence (so two
// colliders stay distinct). `compoff` lines are identified by their whole text.
std::string RecordKey(const std::string& line, std::unordered_map<std::string, int>& seen, int* occ) {
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

// A top-level line that is a complete record this loader knows: a `variant` with its
// quoted base path, or a well-formed override / unset / remove record.
bool IsWholeTopRecord(const std::string& t) {
    const std::string tok = FirstToken(t);
    if (tok == "variant") {
        std::size_t p = t.find(tok) + tok.size();
        SkipSpaces(t, p);
        std::string b;
        return p < t.size() && t[p] == '"' && ReadQuoted(t, p, b) && !b.empty();
    }
    Delta d;
    return ParseDeltaLine(t, d) == 1;
}

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
        // The writer ends every line with a newline. A last top-level line without one
        // that is not a whole record means the file was cut in the middle of that line:
        // refuse it instead of dropping the half record and loading a different prefab.
        if (in.eof() && !IsWholeTopRecord(t)) {
            err = "the file is cut short (its last line '" + t + "' is incomplete)";
            return false;
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

} // namespace prefab_text
} // namespace okay
