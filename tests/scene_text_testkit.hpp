#pragma once
// Shared helpers for the scene text tests (canonical form, diff, merge, migration):
// a seeded random scene generator built on the live engine (so every record is one
// the real writer produces), a messy-formatting perturbation, and edit helpers that
// work on SceneText::Model.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/SceneText.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace tk {

using okay::SceneText;
using okay::Scene;
using okay::SceneSerializer;
using okay::GameObject;

/// Number of seeds a property test runs: `dflt`, or OKAY_TEST_SEEDS when set (for long soak runs).
inline int Seeds(int dflt) {
    const char* e = std::getenv("OKAY_TEST_SEEDS");
    return e && *e ? std::max(1, std::atoi(e)) : dflt;
}

// ---- seeded random -----------------------------------------------------------

struct TkRng {
    std::uint64_t s;
    explicit TkRng(std::uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) { next(); next(); }
    std::uint64_t next() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 0x2545F4914F6CDD1Dull;
    }
    int range(int n) { return n <= 1 ? 0 : (int)(next() % (std::uint64_t)n); }
    bool chance(double p) { return (double)(next() >> 11) / 9007199254740992.0 < p; }
    float real(float lo, float hi) { return lo + (hi - lo) * (float)((double)(next() >> 11) / 9007199254740992.0); }
};

// A float as the writer would print it (default stream precision): "3.25", "-0.5", "12".
inline std::string FloatText(float v) {
    std::ostringstream o;
    o << v;
    return o.str();
}

inline std::string RandFloatText(TkRng& r, float lo = -50.0f, float hi = 50.0f) {
    // Quantise so the text is short and survives a float round trip unchanged.
    const float v = (float)((int)(r.real(lo, hi) * 4.0f)) / 4.0f;
    return FloatText(v);
}

// ---- live scenes -------------------------------------------------------------

inline std::string GuidFor(std::uint64_t n) {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%016llx%016llx", (unsigned long long)(n * 0x9E3779B97F4A7C15ull), (unsigned long long)(n + 0xABCDEFull));
    return buf;
}

/// A random scene saved by the real writer: `n` objects, a random hierarchy (some
/// duplicate names), assorted components, multi-line script text and a few
/// non-default scene settings. Every object has a guid unless `guids` is false.
inline std::string LiveSceneText(TkRng& r, int n, bool guids = true) {
    static const char* names[] = {"Player", "Enemy", "Wall", "Coin", "Light", "Spawner", "Camera", "Door", "Crate", "Tree"};
    static const char* tags[] = {"player", "enemy", "pickup", "solid"};
    Scene s("Gen " + std::to_string(r.range(1000)));
    s.physics().gravity = {0.0f, -(float)(5 + r.range(10))};
    std::vector<GameObject*> objs;
    for (int i = 0; i < n; ++i) {
        GameObject* g = s.CreateGameObject(r.chance(0.5) ? std::string(names[r.range(10)]) : std::string("Obj") + std::to_string(i));
        if (guids) g->SetGuid(GuidFor((std::uint64_t)r.next() | 1ull));
        g->transform->localPosition = {r.real(-20, 20), r.real(-20, 20), r.real(-5, 5)};
        if (r.chance(0.2)) g->transform->localScale = {r.real(0.5f, 3.0f), r.real(0.5f, 3.0f), 1.0f};
        if (r.chance(0.3)) g->tag = tags[r.range(4)];
        if (r.chance(0.1)) g->isStatic = true;
        if (r.chance(0.1)) g->layer = 1 + r.range(5);
        if (r.chance(0.5)) {
            auto* sr = g->AddComponent<okay::SpriteRenderer>();
            sr->glyph = (char)('A' + r.range(26));
            sr->color = okay::Color(r.real(0, 1), r.real(0, 1), r.real(0, 1), 1.0f);
            sr->size = {r.real(0.5f, 4.0f), r.real(0.5f, 4.0f)};
        }
        if (r.chance(0.3)) {
            auto* l = g->AddComponent<okay::Light>();
            l->intensity = r.real(0.1f, 3.0f);
            l->range = r.real(2.0f, 20.0f);
        }
        if (r.chance(0.3)) {
            auto* b = g->AddComponent<okay::BoxCollider2D>();
            b->size = {r.real(0.5f, 3.0f), r.real(0.5f, 3.0f)};
        }
        if (r.chance(0.2)) {
            auto* rb = g->AddComponent<okay::Rigidbody2D>();
            rb->mass = r.real(0.5f, 5.0f);
        }
        if (r.chance(0.1)) {
            auto* t = g->AddComponent<okay::TextRenderer>();
            t->text = "Score: " + std::to_string(r.range(100)) + " \"best\"";
        }
        if (r.chance(0.08)) {
            auto* sc = g->AddComponent<okay::ScriptComponent>("okayscript");
            sc->SetSource("// script " + std::to_string(r.range(1000)) + "\nprint(\"hi\")\n  // end   \n");
        }
        if (i > 0 && r.chance(0.6)) g->transform->SetParent(objs[(std::size_t)r.range(i)]->transform, false);
        objs.push_back(g);
    }
    // Reorder some families so `sibling` records appear.
    for (GameObject* g : objs) {
        auto kids = g->transform->Children();
        if (kids.size() >= 2 && r.chance(0.3))
            g->transform->ReorderChild(kids.back(), kids.front(), false);
    }
    return SceneSerializer::Serialize(s);
}

inline std::string StripGuids(const std::string& text) {
    std::istringstream in(text);
    std::string line, out;
    while (std::getline(in, line)) {
        std::size_t b = line.find_first_not_of(' ');
        if (b != std::string::npos && line.compare(b, 5, "guid ") == 0) continue;
        out += line + "\n";
    }
    return out;
}

/// Load `text` with the real loader and write the scene back out. False if it does not load.
inline bool Reserialize(const std::string& text, std::string* out, std::string* err = nullptr) {
    Scene s("probe");
    std::vector<std::string> warn;
    std::string e;
    if (!SceneSerializer::Deserialize(s, text, &e, &warn)) { if (err) *err = e; return false; }
    if (out) *out = SceneSerializer::Serialize(s);
    return true;
}

// ---- messy formatting --------------------------------------------------------

/// The same scene in a messier layout: CRLF, odd indentation and spacing, blank
/// lines, trailing blanks, padded decimals, shuffled bookkeeping records, object
/// numbers with gaps. Quoted strings are never touched.
inline std::string Mess(const std::string& text, TkRng& r) {
    SceneText::Document d;
    if (!SceneText::Parse(text, d)) return text;
    const bool crlf = r.chance(0.5);
    const char* eol = crlf ? "\r\n" : "\n";
    auto ws = [&]() -> std::string {
        switch (r.range(4)) { case 0: return " "; case 1: return "  "; case 2: return "\t"; default: return " "; }
    };
    auto pad = [&](const SceneText::Record& rec) {
        std::string out;
        const auto toks = SceneText::Tokenize(rec.text);
        for (std::size_t i = 0; i < toks.size(); ++i) {
            std::string t = toks[i].text;
            // Pad only decimals of the records that are all numbers.
            if (i > 0 && !toks[i].quoted && t.find('.') != std::string::npos && t.find_first_of("eE") == std::string::npos &&
                SceneText::NormalizeNumber(t) == t &&
                (rec.key == "transform" || rec.key == "light" || rec.key == "boxcollider2d" || rec.key == "rigidbody2d" ||
                 rec.key == "sprite" || rec.key == "gravity") && r.chance(0.5))
                t += r.chance(0.5) ? "0" : "000";
            if (i) out += ws();
            out += t;
        }
        return out;
    };
    std::string out;
    auto blank = [&]() { if (r.chance(0.15)) out += eol; };
    out += "okayscene" + ws() + std::to_string(d.version) + (r.chance(0.2) ? "  " : "") + eol;
    for (const auto& g : d.globals) { blank(); out += pad(g) + (r.chance(0.2) ? " " : "") + eol; }
    // Objects with numbering gaps.
    std::unordered_map<int, int> remap;
    for (std::size_t i = 0; i < d.objects.size(); ++i) remap[d.objects[i].index] = (int)(i * 7 + 3);
    for (auto& o : d.objects) {
        blank();
        out += "gameobject" + ws() + std::to_string(remap[o.index]) + ws() + SceneText::Quote(o.name) + eol;
        std::vector<SceneText::Record> recs = o.records;
        for (auto& rec : recs) {
            if (rec.key != "parent") continue;
            const auto toks = SceneText::Tokenize(rec.text);
            const int p = std::atoi(toks[1].text.c_str());
            rec.text = "parent " + std::to_string(p >= 0 && remap.count(p) ? remap[p] : -1);
        }
        // Shuffle the bookkeeping records (all known, so the loader does not care).
        std::vector<std::size_t> head;
        for (std::size_t i = 0; i < recs.size(); ++i)
            if (recs[i].key == "guid" || recs[i].key == "active" || recs[i].key == "tag" || recs[i].key == "static" ||
                recs[i].key == "layer" || recs[i].key == "parent" || recs[i].key == "sibling" || recs[i].key == "locked")
                head.push_back(i);
        std::vector<SceneText::Record> picked;
        for (std::size_t i : head) picked.push_back(recs[i]);
        for (std::size_t i = picked.size(); i > 1; --i) std::swap(picked[i - 1], picked[(std::size_t)r.range((int)i)]);
        for (std::size_t k = 0; k < head.size(); ++k) recs[head[k]] = picked[k];
        for (auto& rec : recs) {
            blank();
            std::string indent;
            switch (r.range(4)) { case 0: indent = "  "; break; case 1: indent = "    "; break; case 2: indent = "\t"; break; default: indent = " "; }
            out += indent + pad(rec) + (r.chance(0.2) ? "  " : "") + eol;
        }
        out += "end" + std::string(r.chance(0.2) ? " " : "") + eol;
    }
    if (r.chance(0.5)) out += eol;
    return out;
}

// ---- model edits -------------------------------------------------------------

inline int FindByGuid(const SceneText::Model& m, const std::string& guid) {
    for (std::size_t i = 0; i < m.objects.size(); ++i) if (m.objects[i].guid == guid) return (int)i;
    return -1;
}

/// Rebuild `children` from `parent` links, keeping the order the old lists had.
inline void FixChildren(SceneText::Model& m) {
    std::vector<std::vector<int>> fresh(m.objects.size());
    std::vector<char> placed(m.objects.size(), 0);
    for (std::size_t p = 0; p < m.children.size() && p < m.objects.size(); ++p)
        for (int c : m.children[p])
            if (c >= 0 && (std::size_t)c < m.objects.size() && m.objects[(std::size_t)c].parent == (int)p) {
                fresh[p].push_back(c);
                placed[(std::size_t)c] = 1;
            }
    m.roots.clear();
    for (std::size_t i = 0; i < m.objects.size(); ++i) {
        if (m.objects[i].parent < 0) m.roots.push_back((int)i);
        else if (!placed[i]) fresh[(std::size_t)m.objects[i].parent].push_back((int)i);
    }
    m.children = std::move(fresh);
}

/// Remove the objects flagged in `drop` (and nothing else), remapping parents.
inline void DropObjects(SceneText::Model& m, const std::vector<char>& drop) {
    std::vector<int> remap(m.objects.size(), -1);
    std::vector<SceneText::ModelObject> keep;
    for (std::size_t i = 0; i < m.objects.size(); ++i)
        if (!drop[i]) { remap[i] = (int)keep.size(); keep.push_back(m.objects[i]); }
    for (auto& o : keep) o.parent = o.parent >= 0 ? remap[(std::size_t)o.parent] : -1;
    std::vector<std::vector<int>> kids(keep.size());
    for (std::size_t p = 0; p < m.children.size(); ++p) {
        if (remap[p] < 0) continue;
        for (int c : m.children[p]) if (remap[(std::size_t)c] >= 0) kids[(std::size_t)remap[p]].push_back(remap[(std::size_t)c]);
    }
    m.objects = std::move(keep);
    m.children = std::move(kids);
    FixChildren(m);
}

inline bool InSubtree(const SceneText::Model& m, int node, int root) {
    for (int p = node; p >= 0; p = m.objects[(std::size_t)p].parent) if (p == root) return true;
    return false;
}

inline std::vector<int> Subtree(const SceneText::Model& m, int root) {
    std::vector<int> out;
    for (std::size_t i = 0; i < m.objects.size(); ++i) if (InSubtree(m, (int)i, root)) out.push_back((int)i);
    return out;
}

/// Replace one float token of a numeric record (a value the real loader reads as a float).
inline bool EditNumber(SceneText::ModelObject& o, TkRng& r) {
    static const struct { const char* key; int first, last; } fields[] = {
        {"transform", 0, 9}, {"light", 0, 5}, {"boxcollider2d", 0, 3}, {"rigidbody2d", 1, 5}, {"sprite", 5, 6}};
    std::vector<std::size_t> cand;
    for (std::size_t i = 0; i < o.records.size(); ++i)
        for (const auto& f : fields) if (o.records[i].key == f.key) cand.push_back(i);
    if (cand.empty()) return false;
    SceneText::Record& rec = o.records[cand[(std::size_t)r.range((int)cand.size())]];
    auto toks = SceneText::Tokenize(rec.text);
    for (const auto& f : fields) {
        if (rec.key != f.key) continue;
        const int idx = 1 + f.first + r.range(f.last - f.first + 1);
        if ((std::size_t)idx >= toks.size() || toks[(std::size_t)idx].quoted) return false;
        toks[(std::size_t)idx].text = RandFloatText(r, 0.25f, 40.0f);
        std::string t;
        for (std::size_t i = 0; i < toks.size(); ++i) { if (i) t += ' '; t += toks[i].text; }
        rec.text = t;
        return true;
    }
    return false;
}


inline void SwapObjects(SceneText::Model& m, int i, int j) {
    auto f = [&](int x) { return x == i ? j : x == j ? i : x; };
    std::swap(m.objects[(std::size_t)i], m.objects[(std::size_t)j]);
    for (auto& o : m.objects) if (o.parent >= 0) o.parent = f(o.parent);
    std::swap(m.children[(std::size_t)i], m.children[(std::size_t)j]);
    for (auto& l : m.children) for (int& c : l) c = f(c);
    FixChildren(m);
}

inline std::string NewGuid(TkRng& r) { return GuidFor((std::uint64_t)r.next() | 1ull); }

inline int AddObject(SceneText::Model& m, TkRng& r, int parent, const std::string& name = std::string()) {
    SceneText::ModelObject o;
    o.name = name.empty() ? "New" + std::to_string(r.range(1000)) : name;
    o.guid = NewGuid(r);
    o.parent = parent;
    o.records.push_back({"active", "active 1"});
    o.records.push_back({"transform", "transform " + RandFloatText(r) + " " + RandFloatText(r) + " 0 0 0 0 1 1 1 1"});
    m.objects.push_back(o);
    FixChildren(m);
    return (int)m.objects.size() - 1;
}

/// One random edit of any kind. Returns false when it found nothing to do.
inline bool RandomEdit(SceneText::Model& m, TkRng& r) {
    const int n = (int)m.objects.size();
    switch (r.range(12)) {
        case 0: case 1: case 2: return n && EditNumber(m.objects[(std::size_t)r.range(n)], r);
        case 3: if (!n) return false; m.objects[(std::size_t)r.range(n)].name = "Renamed" + std::to_string(r.range(100)); return true;
        case 4: {   // tag: set or change or remove
            if (!n) return false;
            auto& o = m.objects[(std::size_t)r.range(n)];
            for (std::size_t i = 0; i < o.records.size(); ++i) if (o.records[i].key == "tag") {
                if (r.chance(0.3)) o.records.erase(o.records.begin() + (std::ptrdiff_t)i);
                else o.records[i].text = "tag \"t" + std::to_string(r.range(50)) + "\"";
                return true;
            }
            o.records.insert(o.records.begin() + (o.records.empty() ? 0 : 1), {"tag", "tag \"t" + std::to_string(r.range(50)) + "\""});
            return true;
        }
        case 5: {   // add or remove a collider record
            if (!n) return false;
            auto& o = m.objects[(std::size_t)r.range(n)];
            for (std::size_t i = 0; i < o.records.size(); ++i) if (o.records[i].key == "boxcollider2d") { o.records.erase(o.records.begin() + (std::ptrdiff_t)i); return true; }
            o.records.push_back({"boxcollider2d", "boxcollider2d " + RandFloatText(r, 0.5f, 5.0f) + " 1 0 0 0 0 0"});
            return true;
        }
        case 6: AddObject(m, r, n && r.chance(0.7) ? r.range(n) : -1); return true;
        case 7: {   // delete a subtree
            if (n < 3) return false;
            const int root = r.range(n);
            std::vector<char> drop(m.objects.size(), 0);
            for (int x : Subtree(m, root)) drop[(std::size_t)x] = 1;
            DropObjects(m, drop);
            return true;
        }
        case 8: {   // reparent
            if (n < 3) return false;
            const int o = r.range(n);
            const int np = r.chance(0.2) ? -1 : r.range(n);
            if (np >= 0 && InSubtree(m, np, o)) return false;
            if (m.objects[(std::size_t)o].parent == np) return false;
            m.objects[(std::size_t)o].parent = np;
            FixChildren(m);
            return true;
        }
        case 9: {   // reorder children of some parent
            std::vector<int> cands;
            for (std::size_t p = 0; p < m.children.size(); ++p) if (m.children[p].size() >= 2) cands.push_back((int)p);
            if (cands.empty()) return false;
            auto& kids = m.children[(std::size_t)cands[(std::size_t)r.range((int)cands.size())]];
            const int from = r.range((int)kids.size()), to = r.range((int)kids.size());
            if (from == to) return false;
            const int c = kids[(std::size_t)from];
            kids.erase(kids.begin() + from);
            kids.insert(kids.begin() + to, c);
            return true;
        }
        case 10: {   // a file-level setting
            for (auto& g : m.globals) if (g.key == "gravity") { g.text = "gravity 0 -" + std::to_string(1 + r.range(30)); return true; }
            return false;
        }
        default: {   // a script's source
            for (auto& o : m.objects) for (auto& rec : o.records) if (rec.key == "script" && r.chance(0.5)) {
                rec.text = "script \"okayscript\" \"// edited " + std::to_string(r.range(1000)) + "\nprint(1)\n\" \"\" 0";
                return true;
            }
            return false;
        }
    }
}

// ---- a tiny scene builder for hand-written cases -------------------------------

struct Doc {
    struct Obj { std::string guid, name; int parent = -1; std::vector<std::string> recs; int sibling = -1; };
    int version = 3;
    std::vector<std::string> globals;
    std::vector<Obj> objs;
    Doc& add(const std::string& guid, const std::string& name, int parent, std::vector<std::string> recs = {}, int sibling = -1) {
        objs.push_back({guid, name, parent, std::move(recs), sibling});
        return *this;
    }
    std::string text() const {
        std::string t = "okayscene " + std::to_string(version) + "\n";
        for (const auto& g : globals) t += g + "\n";
        for (std::size_t i = 0; i < objs.size(); ++i) {
            const Obj& o = objs[i];
            t += "gameobject " + std::to_string(i) + " " + SceneText::Quote(o.name) + "\n";
            if (!o.guid.empty()) t += "  guid " + SceneText::Quote(o.guid) + "\n";
            t += "  active 1\n";
            t += "  parent " + std::to_string(o.parent) + "\n";
            if (o.sibling >= 0) t += "  sibling " + std::to_string(o.sibling) + "\n";
            for (const auto& r : o.recs) t += "  " + r + "\n";
            t += "end\n";
        }
        return t;
    }
};

inline bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

/// A strict JSON syntax check (RFC 8259 subset: what a hand written emitter can get wrong).
inline bool ValidJson(const std::string& s) {
    std::size_t i = 0;
    struct P {
        const std::string& s; std::size_t& i;
        void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\t' || s[i] == '\r')) ++i; }
        bool lit(const char* w) { std::size_t n = std::char_traits<char>::length(w); if (s.compare(i, n, w) == 0) { i += n; return true; } return false; }
        bool str() {
            if (i >= s.size() || s[i] != '"') return false;
            ++i;
            while (i < s.size()) {
                unsigned char c = (unsigned char)s[i];
                if (c == '"') { ++i; return true; }
                if (c < 0x20) return false;
                if (c == '\\') {
                    ++i;
                    if (i >= s.size()) return false;
                    char e = s[i];
                    if (e == 'u') { for (int k = 1; k <= 4; ++k) { if (i + (std::size_t)k >= s.size() || !std::isxdigit((unsigned char)s[i + (std::size_t)k])) return false; } i += 4; }
                    else if (!std::strchr("\"\\/bfnrt", e)) return false;
                }
                ++i;
            }
            return false;
        }
        bool num() {
            std::size_t b = i;
            if (i < s.size() && s[i] == '-') ++i;
            while (i < s.size() && std::isdigit((unsigned char)s[i])) ++i;
            return i > b;
        }
        bool val(int depth) {
            if (depth > 200) return false;
            ws();
            if (i >= s.size()) return false;
            if (s[i] == '{') {
                ++i; ws();
                if (i < s.size() && s[i] == '}') { ++i; return true; }
                for (;;) {
                    ws();
                    if (!str()) return false;
                    ws();
                    if (i >= s.size() || s[i] != ':') return false;
                    ++i;
                    if (!val(depth + 1)) return false;
                    ws();
                    if (i < s.size() && s[i] == ',') { ++i; continue; }
                    if (i < s.size() && s[i] == '}') { ++i; return true; }
                    return false;
                }
            }
            if (s[i] == '[') {
                ++i; ws();
                if (i < s.size() && s[i] == ']') { ++i; return true; }
                for (;;) {
                    if (!val(depth + 1)) return false;
                    ws();
                    if (i < s.size() && s[i] == ',') { ++i; continue; }
                    if (i < s.size() && s[i] == ']') { ++i; return true; }
                    return false;
                }
            }
            if (s[i] == '"') return str();
            if (lit("true") || lit("false") || lit("null")) return true;
            return num();
        }
    } p{s, i};
    if (!p.val(0)) return false;
    p.ws();
    return i == s.size();
}

inline std::string Dump(const std::string& s) { return "<<<\n" + s + ">>>"; }

} // namespace tk
