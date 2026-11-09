#include "okay/Scene/SceneMerge.hpp"
#include "SceneTextDetail.hpp"

#include <algorithm>
#include <list>
#include <unordered_map>
#include <unordered_set>

namespace okay {

using namespace scenetext;

namespace {

using Conflict = SceneMerge::Conflict;
using Kind = Conflict::Kind;
using Policy = SceneMerge::Policy;

enum Side { kNone = 0, kOurs = 1, kTheirs = 2 };

struct Ctx {
    Policy policy = Policy::Mark;
    std::vector<Conflict>* conflicts = nullptr;
    int Pref() const { return policy == Policy::PreferTheirs ? kTheirs : kOurs; }
    const char* Name(int side) const { return side == kTheirs ? "theirs" : "ours"; }
};

struct Who {
    std::string object, name, path;
};

void AddConflict(Ctx& cx, const Who& w, Kind kind, const std::string& record, const std::string& field,
                 const std::string& b, const std::string& o, const std::string& t, const std::string& resolution) {
    Conflict c;
    c.kind = kind;
    c.object = w.object;
    c.name = w.name;
    c.path = w.path;
    c.record = record;
    c.field = field;
    c.base = b;
    c.ours = o;
    c.theirs = t;
    c.resolution = resolution;
    cx.conflicts->push_back(std::move(c));
}

// The three-way rule for one value. `from` says which side's change was taken (0: nobody
// changed it, or both changed it the same way).
template <class V>
V Scalar3(const V& b, const V& o, const V& t, bool hasBase, int pref, bool& conflict, int& from) {
    conflict = false;
    from = kNone;
    if (o == t) return o;
    if (hasBase && o == b) { from = kTheirs; return t; }
    if (hasBase && t == b) { from = kOurs; return o; }
    conflict = true;
    from = pref;
    return pref == kOurs ? o : t;
}

// ---- merging ordered lists ----------------------------------------------------------

// Three-way merge of three orderings of (mostly) the same items. `survivors` are the
// items that exist in the result; the order is the base's, with every item either side
// moved or added put right after its predecessor on that side. Items both sides placed
// differently go to the preferred side's place and are reported in `conflicts`. Where both
// sides insert after the same item, the one with the smaller tie key goes first, so the
// result does not depend on which side is "ours".
std::vector<std::string> MergeOrder(const std::vector<std::string>& B, const std::vector<std::string>& O,
                                    const std::vector<std::string>& T, const std::vector<std::string>& survivors,
                                    const std::unordered_map<std::string, std::string>* tie, int pref,
                                    std::vector<std::string>* conflicts) {
    std::unordered_set<std::string> S(survivors.begin(), survivors.end());
    auto filt = [&](const std::vector<std::string>& v) {
        std::vector<std::string> r;
        r.reserve(v.size());
        for (const auto& x : v) if (S.count(x)) r.push_back(x);
        return r;
    };
    const std::vector<std::string> Bs = filt(B), Os = filt(O), Ts = filt(T);
    std::list<std::string> R(Bs.begin(), Bs.end());
    std::unordered_map<std::string, std::list<std::string>::iterator> where;
    for (auto it = R.begin(); it != R.end(); ++it) where[*it] = it;
    std::unordered_set<std::string> inBase(Bs.begin(), Bs.end());
    std::unordered_map<std::string, int> insertedBy;
    std::unordered_map<std::string, std::string> anchor[3];
    std::unordered_set<std::string> moved[3];
    auto tieOf = [&](const std::string& id) -> const std::string& {
        if (tie) { auto f = tie->find(id); if (f != tie->end()) return f->second; }
        return id;
    };
    auto place = [&](const std::string& e, const std::string& pred, int side) {
        auto w = where.find(e);
        if (w != where.end()) { R.erase(w->second); where.erase(w); }
        auto it = pred.empty() ? R.begin() : std::next(where[pred]);
        const int other = side == kOurs ? kTheirs : kOurs;
        auto ins = [&](const std::list<std::string>::iterator& i) {
            if (i == R.end()) return false;
            auto f = insertedBy.find(*i);
            return f != insertedBy.end() && f->second == other;
        };
        auto predIns = insertedBy.find(pred);
        const bool predOwn = !pred.empty() && predIns != insertedBy.end() && predIns->second == side;
        if (!predOwn && ins(it) && tieOf(*it) < tieOf(e))
            while (ins(it)) ++it;
        auto ni = R.insert(it, e);
        where[e] = ni;
        insertedBy[e] = side;
    };
    for (int side = kOurs; side <= kTheirs; ++side) {
        const std::vector<std::string>& X = side == kOurs ? Os : Ts;
        const std::vector<char> stable = StableElements(Bs, X);
        std::string lastBase, pred;
        for (std::size_t k = 0; k < X.size(); ++k) {
            const std::string& e = X[k];
            if (!stable[k]) {
                anchor[side][e] = lastBase;
                moved[side].insert(e);
                bool skip = false;
                if (side == kTheirs && moved[kOurs].count(e)) {
                    if (anchor[kOurs][e] != anchor[kTheirs][e]) {
                        if (conflicts) conflicts->push_back(e);
                        skip = pref == kOurs;
                    } else skip = true;
                }
                if (!skip) place(e, pred, side);
            }
            if (inBase.count(e)) lastBase = e;
            pred = e;
        }
    }
    for (const auto& s : survivors) if (!where.count(s)) { R.push_back(s); where[s] = std::prev(R.end()); }
    return std::vector<std::string>(R.begin(), R.end());
}

// ---- merging records ----------------------------------------------------------------

bool SameRecords(const std::vector<Record>& a, const std::vector<Record>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (a[i].text != b[i].text) return false;
    return true;
}

// Records that an object may legitimately have several of.
bool AllowsSeveral(const std::string& key) {
    static const std::unordered_set<std::string> k = {
        "script", "visualscript", "actions", "tweenanim", "syncvar", "netvalidator", "animsm", "compoff", "override"};
    return k.count(key) != 0;
}

std::string Join(const std::vector<std::string>& toks) {
    std::string s;
    for (std::size_t i = 0; i < toks.size(); ++i) { if (i) s += ' '; s += toks[i]; }
    return s;
}

// Both sides changed one record: merge it field by field where the shape allows it.
Record MergeOne(const Record& b, const Record& o, const Record& t, Ctx& cx, const Who& who, const std::string& label) {
    if (o.text == t.text) return o;
    if (o.text == b.text) return t;
    if (t.text == b.text) return o;
    const auto tb = SceneText::Tokenize(b.text), to = SceneText::Tokenize(o.text), tt = SceneText::Tokenize(t.text);
    Record out;
    out.key = o.key;
    if (IsKeyValueRecord(tb) && IsKeyValueRecord(to) && IsKeyValueRecord(tt)) {
        using Val = std::pair<bool, std::string>;
        auto load = [](const std::vector<SceneText::Token>& toks, std::vector<std::string>& order) {
            std::unordered_map<std::string, std::string> m;
            for (std::size_t i = 1; i < toks.size(); ++i) {
                const std::size_t eq = toks[i].text.find('=');
                order.push_back(toks[i].text.substr(0, eq));
                m[order.back()] = toks[i].text.substr(eq + 1);
            }
            return m;
        };
        std::vector<std::string> ob, oo, ot;
        const auto mb = load(tb, ob), mo = load(to, oo), mt = load(tt, ot);
        std::vector<std::string> keys = ob;
        std::vector<std::string> fresh;
        for (const auto& k : oo) if (!mb.count(k)) fresh.push_back(k);
        for (const auto& k : ot) if (!mb.count(k) && !mo.count(k)) fresh.push_back(k);
        std::sort(fresh.begin(), fresh.end());
        keys.insert(keys.end(), fresh.begin(), fresh.end());
        std::vector<std::string> toks;
        toks.push_back(o.key);
        auto get = [](const std::unordered_map<std::string, std::string>& m, const std::string& k) {
            auto f = m.find(k);
            return f == m.end() ? Val(false, std::string()) : Val(true, f->second);
        };
        for (const auto& k : keys) {
            const Val vb = get(mb, k), vo = get(mo, k), vt = get(mt, k);
            bool conflict = false;
            int from = 0;
            const Val v = Scalar3<Val>(vb, vo, vt, true, cx.Pref(), conflict, from);
            if (conflict)
                AddConflict(cx, who, Kind::FieldChanged, label, k, vb.first ? vb.second : "(none)", vo.first ? vo.second : "(none)",
                            vt.first ? vt.second : "(none)", cx.Name(cx.Pref()));
            if (v.first) toks.push_back(k + "=" + v.second);
        }
        out.text = Join(toks);
        return out;
    }
    if (tb.size() == to.size() && to.size() == tt.size() && !tb.empty()) {
        std::vector<std::string> toks;
        toks.push_back(tb[0].text);
        const bool single = tb.size() == 2;
        for (std::size_t i = 1; i < tb.size(); ++i) {
            bool conflict = false;
            int from = 0;
            const std::string v = Scalar3<std::string>(tb[i].text, to[i].text, tt[i].text, true, cx.Pref(), conflict, from);
            if (conflict)
                AddConflict(cx, who, Kind::FieldChanged, label, single ? std::string() : FieldName(b.key, i - 1), tb[i].text, to[i].text,
                            tt[i].text, cx.Name(cx.Pref()));
            toks.push_back(v);
        }
        out.text = Join(toks);
        return out;
    }
    AddConflict(cx, who, Kind::RecordChanged, label, "", b.text, o.text, t.text, cx.Name(cx.Pref()));
    return cx.Pref() == kOurs ? o : t;
}

// Three-way merge of two edited record lists (an object's, or the file-level ones).
std::vector<Record> MergeRecordLists(const std::vector<Record>& rb, const std::vector<Record>& ro, const std::vector<Record>& rt,
                                     bool hasBase, Ctx& cx, const Who& who) {
    if (SameRecords(ro, rt)) return ro;
    if (hasBase && SameRecords(ro, rb)) return rt;
    if (hasBase && SameRecords(rt, rb)) return ro;
    static const std::vector<Record> kNone0;
    const std::vector<Record>& B = hasBase ? rb : kNone0;
    const auto po = AlignRecords(B, ro), pt = AlignRecords(B, rt);
    const std::size_t nb = B.size();
    std::vector<int> oIdx(nb, -1), tIdx(nb, -1);
    std::vector<int> oAdded, tAdded;
    for (const auto& p : po) { if (p.a >= 0 && p.b >= 0) oIdx[(std::size_t)p.a] = p.b; else if (p.a < 0) oAdded.push_back(p.b); }
    for (const auto& p : pt) { if (p.a >= 0 && p.b >= 0) tIdx[(std::size_t)p.a] = p.b; else if (p.a < 0) tAdded.push_back(p.b); }

    std::unordered_map<std::string, Record> rec;
    std::vector<std::string> survivors;
    auto keep = [&](const std::string& id, const Record& r) { rec[id] = r; survivors.push_back(id); };
    std::vector<std::string> seqB, idO(ro.size()), idT(rt.size());
    for (std::size_t k = 0; k < nb; ++k) {
        const std::string id = "b" + std::to_string(k);
        seqB.push_back(id);
        if (oIdx[k] >= 0) idO[(std::size_t)oIdx[k]] = id;
        if (tIdx[k] >= 0) idT[(std::size_t)tIdx[k]] = id;
    }
    for (int r : oAdded) idO[(std::size_t)r] = "o" + std::to_string(r);
    for (int r : tAdded) idT[(std::size_t)r] = "t" + std::to_string(r);

    for (std::size_t k = 0; k < nb; ++k) {
        const std::string id = "b" + std::to_string(k);
        const int o = oIdx[k], t = tIdx[k];
        if (o < 0 && t < 0) continue;
        const std::string label = LabelAt(B, k);
        if (o < 0) {
            if (rt[(std::size_t)t].text == B[k].text) continue;     // theirs left it alone: deleted
            const bool takeTheirs = cx.Pref() == kTheirs;
            AddConflict(cx, who, Kind::RecordDeletedAndEdited, label, "", B[k].text, "(deleted)", rt[(std::size_t)t].text,
                        takeTheirs ? "theirs" : "ours");
            if (takeTheirs) keep(id, rt[(std::size_t)t]);
            continue;
        }
        if (t < 0) {
            if (ro[(std::size_t)o].text == B[k].text) continue;
            const bool takeOurs = cx.Pref() == kOurs;
            AddConflict(cx, who, Kind::RecordDeletedAndEdited, label, "", B[k].text, ro[(std::size_t)o].text, "(deleted)",
                        takeOurs ? "ours" : "theirs");
            if (takeOurs) keep(id, ro[(std::size_t)o]);
            continue;
        }
        keep(id, MergeOne(B[k], ro[(std::size_t)o], rt[(std::size_t)t], cx, who, label));
    }

    // Records added on a side. Both sides adding the same record is one record; two
    // different ones of a kind that may repeat are both kept; otherwise it is a conflict.
    std::vector<char> usedT(tAdded.size(), 0);
    for (int r : oAdded) {
        const std::string id = "o" + std::to_string(r);
        Record chosen = ro[(std::size_t)r];
        const std::string ident = RecordIdentity(chosen);
        for (std::size_t q = 0; q < tAdded.size(); ++q) {
            if (usedT[q] || RecordIdentity(rt[(std::size_t)tAdded[q]]) != ident) continue;
            const Record& other = rt[(std::size_t)tAdded[q]];
            if (other.text == chosen.text) { usedT[q] = 1; idT[(std::size_t)tAdded[q]] = id; break; }
            if (AllowsSeveral(chosen.key)) continue;           // both stay
            usedT[q] = 1;
            idT[(std::size_t)tAdded[q]] = id;
            AddConflict(cx, who, Kind::RecordAddedDifferently, LabelAt(ro, (std::size_t)r), "", "", chosen.text, other.text, cx.Name(cx.Pref()));
            if (cx.Pref() == kTheirs) chosen = other;
            break;
        }
        keep(id, chosen);
    }
    for (std::size_t q = 0; q < tAdded.size(); ++q)
        if (!usedT[q] && idT[(std::size_t)tAdded[q]] == "t" + std::to_string(tAdded[q]))
            keep(idT[(std::size_t)tAdded[q]], rt[(std::size_t)tAdded[q]]);

    std::unordered_map<std::string, std::string> tie;
    for (const auto& kv : rec) tie[kv.first] = kv.second.text;
    const std::vector<std::string> order = MergeOrder(seqB, idO, idT, survivors, &tie, cx.Pref(), nullptr);
    std::vector<Record> out;
    out.reserve(order.size());
    for (const auto& id : order) out.push_back(rec[id]);
    return out;
}

// ---- the merge of one scene ---------------------------------------------------------

struct Entity {
    int b = -1, o = -1, t = -1;       // index in each model, -1 when it is not there
    std::string key;
    bool survives = false;
    // The merged object.
    std::string name, guid, headerTail;
    int parent = -1;                  // entity index
    int parentFrom = kNone;
    std::vector<Record> records;
    int keptSide = kNone;             // the side whose version was taken whole (deleted on the other)
};

std::string SnapshotOf(const SceneText::Model& m, int idx) {
    const auto& o = m.objects[(std::size_t)idx];
    std::string s = "name " + SceneText::Quote(o.name);
    for (const auto& r : o.records) s += "\n" + r.text;
    return s;
}

} // namespace

std::string SceneMerge::Conflict::KindName() const {
    switch (kind) {
        case Kind::FieldChanged: return "field-changed";
        case Kind::RecordChanged: return "record-changed";
        case Kind::RecordDeletedAndEdited: return "record-deleted-and-edited";
        case Kind::RecordAddedDifferently: return "record-added-differently";
        case Kind::ObjectDeletedAndEdited: return "object-deleted-and-edited";
        case Kind::RenamedDifferently: return "renamed-differently";
        case Kind::ParentChanged: return "parent-changed";
        case Kind::ParentDeleted: return "parent-deleted";
        case Kind::ParentCycle: return "parent-cycle";
        case Kind::OrderChanged: return "order-changed";
        case Kind::Invalid: return "invalid";
    }
    return "?";
}

SceneMerge::Result SceneMerge::Merge(const std::string& baseText, const std::string& oursText, const std::string& theirsText) {
    return Merge(baseText, oursText, theirsText, Options());
}

SceneMerge::Result SceneMerge::Merge(const std::string& baseText, const std::string& oursText, const std::string& theirsText,
                                     const Options& opts) {
    Result res;
    res.policy = opts.policy;
    res.text = oursText;
    Ctx cx;
    cx.policy = opts.policy;
    cx.conflicts = &res.conflicts;

    // Canonical text first, so layout noise is never a change.
    const std::string co = SceneText::Canonicalize(oursText), ct = SceneText::Canonicalize(theirsText);
    SceneText::Model O, T, B;
    std::string err;
    if (!SceneText::BuildModel(co, O, &err)) { res.error = "ours: " + err; return res; }
    if (!SceneText::BuildModel(ct, T, &err)) { res.error = "theirs: " + err; return res; }
    if (O.version > SceneText::CurrentVersion() || T.version > SceneText::CurrentVersion()) {
        res.error = "okayscene " + std::to_string(std::max(O.version, T.version)) + " is newer than this build reads (" +
                    std::to_string(SceneText::CurrentVersion()) + "); not merged";
        return res;
    }
    if (O.version != T.version) {
        res.error = "ours is okayscene " + std::to_string(O.version) + " and theirs " + std::to_string(T.version) +
                    ": bring both to one version first (SceneMigration)";
        return res;
    }
    // The cheap cases are exact: identical sides, or one side unchanged. A side the loader
    // would warn about (a parent that does not exist, a repeated id) is not passed through:
    // the full merge below writes it back repaired.
    std::string cb;
    const bool emptyBase = baseText.find_first_not_of(" \t\r\n") == std::string::npos;
    const bool shortcuts = !opts.skipShortcuts;
    const bool oursRegular = O.warnings.empty(), theirsRegular = T.warnings.empty();
    if (shortcuts && co == ct && oursRegular) { res.ok = true; res.text = co; return res; }
    if (!emptyBase) {
        cb = SceneText::Canonicalize(baseText);
        if (shortcuts && cb == co && theirsRegular) { res.ok = true; res.text = ct; return res; }
        if (shortcuts && cb == ct && oursRegular) { res.ok = true; res.text = co; return res; }
        if (!SceneText::BuildModel(cb, B, &err)) { res.error = "base: " + err; return res; }
    } else {
        B.version = O.version;
        B.children.clear();
    }
    const bool hasBase = !emptyBase;
    res.ok = true;
    for (const auto& w : O.warnings) res.warnings.push_back("ours: " + w);
    for (const auto& w : T.warnings) res.warnings.push_back("theirs: " + w);

    const std::size_t nb = B.objects.size(), no = O.objects.size(), nt = T.objects.size();

    // ---- identity: one entity per object across the three versions ----
    const Matching mo = MatchObjects(B, O, true), mt = MatchObjects(B, T, true);
    std::vector<Entity> ents;
    std::unordered_set<std::string> usedKeys;
    std::vector<int> entB(nb, -1), entO(no, -1), entT(nt, -1);
    for (std::size_t i = 0; i < nb; ++i) {
        Entity e;
        e.b = (int)i;
        e.key = ObjectKey(B.objects[i]);
        usedKeys.insert(e.key);
        entB[i] = (int)ents.size();
        ents.push_back(std::move(e));
    }
    for (std::size_t j = 0; j < no; ++j)
        if (mo.bToA[j] >= 0) { entO[j] = entB[(std::size_t)mo.bToA[j]]; ents[(std::size_t)entO[j]].o = (int)j; }
    for (std::size_t j = 0; j < nt; ++j)
        if (mt.bToA[j] >= 0) { entT[j] = entB[(std::size_t)mt.bToA[j]]; ents[(std::size_t)entT[j]].t = (int)j; }
    std::unordered_map<std::string, int> addedByGuid, addedByPath;
    for (std::size_t j = 0; j < no; ++j) {
        if (entO[j] >= 0) continue;
        Entity e;
        e.o = (int)j;
        e.key = ObjectKey(O.objects[j]);
        while (usedKeys.count(e.key)) e.key += "+";
        usedKeys.insert(e.key);
        entO[j] = (int)ents.size();
        if (!O.objects[j].guid.empty()) addedByGuid[O.objects[j].guid] = entO[j];
        addedByPath[O.objects[j].path] = entO[j];
        ents.push_back(std::move(e));
    }
    for (std::size_t j = 0; j < nt; ++j) {
        if (entT[j] >= 0) continue;
        int hit = -1;
        const auto& obj = T.objects[j];
        if (!obj.guid.empty()) { auto f = addedByGuid.find(obj.guid); if (f != addedByGuid.end() && ents[(std::size_t)f->second].t < 0) hit = f->second; }
        if (hit < 0) {
            auto f = addedByPath.find(obj.path);
            if (f != addedByPath.end() && ents[(std::size_t)f->second].t < 0) {
                const auto& oo = O.objects[(std::size_t)ents[(std::size_t)f->second].o];
                if (oo.guid.empty() || obj.guid.empty()) hit = f->second;
            }
        }
        if (hit >= 0) { ents[(std::size_t)hit].t = (int)j; entT[j] = hit; continue; }
        Entity e;
        e.t = (int)j;
        e.key = ObjectKey(obj);
        while (usedKeys.count(e.key)) e.key += "+";
        usedKeys.insert(e.key);
        entT[j] = (int)ents.size();
        ents.push_back(std::move(e));
    }
    const std::size_t ne = ents.size();
    auto parentEnt = [&](const SceneText::Model& m, const std::vector<int>& entOf, int idx) {
        const int p = idx < 0 ? -1 : m.objects[(std::size_t)idx].parent;
        return p < 0 ? -1 : entOf[(std::size_t)p];
    };
    auto pathOf = [&](const Entity& e) -> std::string {
        if (e.o >= 0) return O.objects[(std::size_t)e.o].path;
        if (e.t >= 0) return T.objects[(std::size_t)e.t].path;
        return e.b >= 0 ? B.objects[(std::size_t)e.b].path : std::string();
    };
    auto pathOfEnt = [&](int e) { return e < 0 ? std::string("(scene root)") : pathOf(ents[(std::size_t)e]); };
    // The path of an entity as one version has it (0 base, 1 ours, 2 theirs), else as any has it.
    auto pathIn = [&](int e, int side) -> std::string {
        if (e < 0) return "(scene root)";
        const Entity& en = ents[(std::size_t)e];
        if (side == 0 && en.b >= 0) return B.objects[(std::size_t)en.b].path;
        if (side == kOurs && en.o >= 0) return O.objects[(std::size_t)en.o].path;
        if (side == kTheirs && en.t >= 0) return T.objects[(std::size_t)en.t].path;
        return pathOf(en);
    };
    auto nameOf = [&](const Entity& e) -> std::string {
        if (e.o >= 0) return O.objects[(std::size_t)e.o].name;
        if (e.t >= 0) return T.objects[(std::size_t)e.t].name;
        return e.b >= 0 ? B.objects[(std::size_t)e.b].name : std::string();
    };
    auto whoOf = [&](const Entity& e) { return Who{e.key, nameOf(e), pathOf(e)}; };

    // ---- file-level settings ----
    std::vector<Record> globals;
    {
        Who w;
        globals = MergeRecordLists(B.globals, O.globals, T.globals, hasBase, cx, w);
    }

    // ---- each object ----
    auto sideEdited = [&](const Entity& e, int side) {
        const SceneText::Model& m = side == kOurs ? O : T;
        const std::vector<int>& entOf = side == kOurs ? entO : entT;
        const int idx = side == kOurs ? e.o : e.t;
        const auto& ob = B.objects[(std::size_t)e.b];
        const auto& os = m.objects[(std::size_t)idx];
        if (ob.name != os.name || ob.guid != os.guid || ob.headerTail != os.headerTail) return true;
        if (parentEnt(B, entB, e.b) != parentEnt(m, entOf, idx)) return true;
        return !SameRecords(ob.records, os.records);
    };
    auto takeSide = [&](Entity& e, int side) {
        const SceneText::Model& m = side == kOurs ? O : T;
        const std::vector<int>& entOf = side == kOurs ? entO : entT;
        const int idx = side == kOurs ? e.o : e.t;
        const auto& os = m.objects[(std::size_t)idx];
        e.name = os.name;
        e.guid = os.guid;
        e.headerTail = os.headerTail;
        e.records = os.records;
        e.parent = parentEnt(m, entOf, idx);
        e.parentFrom = side;
        e.keptSide = side;
    };
    for (std::size_t k = 0; k < ne; ++k) {
        Entity& e = ents[k];
        const Who who = whoOf(e);
        if (e.b >= 0 && e.o < 0 && e.t < 0) continue;
        if (e.b >= 0 && (e.o < 0 || e.t < 0)) {
            const int gone = e.o < 0 ? kOurs : kTheirs, other = e.o < 0 ? kTheirs : kOurs;
            if (!sideEdited(e, other)) continue;       // the other side did not touch it: the deletion stands
            const bool keepIt = cx.Pref() == other;
            AddConflict(cx, who, Kind::ObjectDeletedAndEdited, "", "", SnapshotOf(B, e.b),
                        gone == kOurs ? "(deleted)" : SnapshotOf(O, e.o), gone == kOurs ? SnapshotOf(T, e.t) : "(deleted)",
                        keepIt ? cx.Name(other) : cx.Name(gone));
            if (keepIt) { takeSide(e, other); e.survives = true; }
            continue;
        }
        e.survives = true;
        if (e.o >= 0 && e.t < 0) { takeSide(e, kOurs); e.keptSide = kNone; continue; }
        if (e.t >= 0 && e.o < 0) { takeSide(e, kTheirs); e.keptSide = kNone; continue; }
        // Both sides have it.
        const bool inBase = e.b >= 0;
        const auto& os = O.objects[(std::size_t)e.o];
        const auto& ts = T.objects[(std::size_t)e.t];
        bool conflict = false;
        int from = 0;
        e.name = Scalar3<std::string>(inBase ? B.objects[(std::size_t)e.b].name : std::string(), os.name, ts.name, inBase, cx.Pref(), conflict, from);
        if (conflict)
            AddConflict(cx, who, Kind::RenamedDifferently, "", "", inBase ? B.objects[(std::size_t)e.b].name : "(none)", os.name, ts.name,
                        cx.Name(cx.Pref()));
        e.guid = Scalar3<std::string>(inBase ? B.objects[(std::size_t)e.b].guid : std::string(), os.guid, ts.guid, inBase, cx.Pref(), conflict, from);
        if (conflict) res.warnings.push_back("object " + e.key + " has different ids on the two sides; kept the preferred one");
        e.headerTail = Scalar3<std::string>(inBase ? B.objects[(std::size_t)e.b].headerTail : std::string(), os.headerTail, ts.headerTail, inBase,
                                            cx.Pref(), conflict, from);
        const int pb = inBase ? parentEnt(B, entB, e.b) : -1;
        e.parent = Scalar3<int>(pb, parentEnt(O, entO, e.o), parentEnt(T, entT, e.t), inBase, cx.Pref(), conflict, from);
        e.parentFrom = from;
        if (conflict)
            AddConflict(cx, who, Kind::ParentChanged, "", "", inBase ? pathIn(pb, 0) : "(none)", pathIn(parentEnt(O, entO, e.o), kOurs),
                        pathIn(parentEnt(T, entT, e.t), kTheirs), cx.Name(cx.Pref()));
        static const std::vector<Record> kNoRecords;
        e.records = MergeRecordLists(inBase ? B.objects[(std::size_t)e.b].records : kNoRecords, os.records, ts.records, inBase, cx, who);
    }

    // ---- parents that must exist, and no loops ----
    auto sideParentOf = [&](int ent, int side) -> int {   // the parent of `ent` as `side` had it
        const Entity& e = ents[(std::size_t)ent];
        if (side == kOurs && e.o >= 0) return parentEnt(O, entO, e.o);
        if (side == kTheirs && e.t >= 0) return parentEnt(T, entT, e.t);
        return e.b >= 0 ? parentEnt(B, entB, e.b) : -1;
    };
    for (int guard = 0; guard < 1000; ++guard) {
        bool changed = false;
        for (std::size_t k = 0; k < ne; ++k) {
            Entity& e = ents[k];
            if (!e.survives || e.parent < 0 || ents[(std::size_t)e.parent].survives) continue;
            // Its parent was deleted by the other side.
            const int p = e.parent;
            int keepSide = e.parentFrom != kNone ? e.parentFrom : e.keptSide;
            const bool keep = keepSide == cx.Pref() || keepSide == kNone;
            Who who = whoOf(e);
            std::string ancestorPath;
            if (keep) {
                int anc = sideParentOf(p, keepSide == kNone ? cx.Pref() : keepSide);
                int hops = 0;
                while (anc >= 0 && !ents[(std::size_t)anc].survives && ++hops < 100000) anc = sideParentOf(anc, keepSide == kNone ? cx.Pref() : keepSide);
                if (anc >= 0 && !ents[(std::size_t)anc].survives) anc = -1;
                e.parent = anc;
                e.parentFrom = kNone;
                ancestorPath = pathOfEnt(anc);
            } else e.survives = false;
            const std::string snap = keepSide == kTheirs && e.t >= 0 ? SnapshotOf(T, e.t) : (e.o >= 0 ? SnapshotOf(O, e.o) : std::string());
            AddConflict(cx, who, Kind::ParentDeleted, "", "parent", "parent " + pathOfEnt(p),
                        keepSide == kTheirs ? "(parent deleted)" : snap, keepSide == kTheirs ? snap : "(parent deleted)",
                        keep ? "kept under " + ancestorPath : std::string("dropped"));
            changed = true;
        }
        // Loops: follow every parent chain.
        std::vector<int> state(ne, 0);   // 0 new, 1 on the current walk, 2 done
        for (std::size_t s = 0; s < ne; ++s) {
            if (!ents[s].survives || state[s]) continue;
            std::vector<int> walk;
            int cur = (int)s;
            while (cur >= 0 && ents[(std::size_t)cur].survives && state[(std::size_t)cur] == 0) {
                state[(std::size_t)cur] = 1;
                walk.push_back(cur);
                cur = ents[(std::size_t)cur].parent;
            }
            if (cur >= 0 && ents[(std::size_t)cur].survives && state[(std::size_t)cur] == 1) {
                // `cur` is on the walk: the loop is cur .. end of walk.
                std::vector<int> loop;
                bool in = false;
                for (int w : walk) { if (w == cur) in = true; if (in) loop.push_back(w); }
                std::sort(loop.begin(), loop.end(), [&](int a, int b) { return ents[(std::size_t)a].key < ents[(std::size_t)b].key; });
                // Break it where the non-preferred side made the move.
                const int pref = cx.Pref(), other = pref == kOurs ? kTheirs : kOurs;
                int victim = loop.front();
                for (int m : loop) if (ents[(std::size_t)m].parentFrom == other) { victim = m; break; }
                Entity& v = ents[(std::size_t)victim];
                const int old = v.parent;
                int np = v.parentFrom == other ? sideParentOf(victim, pref) : -1;
                if (np >= 0 && (!ents[(std::size_t)np].survives || np == victim)) np = -1;
                v.parent = np;
                v.parentFrom = kNone;
                AddConflict(cx, whoOf(v), Kind::ParentCycle, "", "", "", pathOfEnt(old), pathOfEnt(np),
                            "parent set to " + pathOfEnt(np));
                changed = true;
            }
            for (int w : walk) state[(std::size_t)w] = 2;
        }
        if (!changed) break;
    }

    // Whatever the loops above could not settle: an object whose parent is gone becomes a root
    // (the check below would otherwise send the merge back to ours).
    for (auto& e : ents)
        if (e.survives && e.parent >= 0 && !ents[(std::size_t)e.parent].survives) {
            res.warnings.push_back("object " + e.key + " lost its parent and became a root");
            e.parent = -1;
        }

    // ---- order: the file order, and each parent's children ----
    std::vector<std::string> survivorKeys;
    for (const auto& e : ents) if (e.survives) survivorKeys.push_back(e.key);
    std::vector<std::string> seqB, seqO, seqT;
    for (std::size_t i = 0; i < nb; ++i) seqB.push_back(ents[(std::size_t)entB[i]].key);
    for (std::size_t j = 0; j < no; ++j) seqO.push_back(ents[(std::size_t)entO[j]].key);
    for (std::size_t j = 0; j < nt; ++j) seqT.push_back(ents[(std::size_t)entT[j]].key);
    std::unordered_map<std::string, int> entOfKey;
    for (std::size_t k = 0; k < ne; ++k) entOfKey[ents[k].key] = (int)k;
    std::vector<std::string> orderConflicts;
    const std::vector<std::string> fileOrder = MergeOrder(seqB, seqO, seqT, survivorKeys, nullptr, cx.Pref(), &orderConflicts);
    for (const auto& key : orderConflicts) {
        const Entity& e = ents[(std::size_t)entOfKey[key]];
        AddConflict(cx, whoOf(e), Kind::OrderChanged, "", "", "", "moved", "moved elsewhere", cx.Name(cx.Pref()));
    }
    std::unordered_map<std::string, int> posOfKey;
    for (std::size_t i = 0; i < fileOrder.size(); ++i) posOfKey[fileOrder[i]] = (int)i;

    SceneText::Model N;
    N.version = O.version;
    N.globals = globals;
    N.objects.resize(fileOrder.size());
    std::unordered_set<std::string> seenGuids;
    for (std::size_t i = 0; i < fileOrder.size(); ++i) {
        const Entity& e = ents[(std::size_t)entOfKey[fileOrder[i]]];
        SceneText::ModelObject& nobj = N.objects[i];
        nobj.name = e.name;
        nobj.headerTail = e.headerTail;
        nobj.records = e.records;
        nobj.guid = e.guid;
        if (!nobj.guid.empty() && !seenGuids.insert(nobj.guid).second) {
            res.warnings.push_back("object " + e.key + " had a duplicate id; it was dropped");
            nobj.guid.clear();
        }
        nobj.parent = e.parent < 0 ? -1 : posOfKey[ents[(std::size_t)e.parent].key];
    }
    SceneText::SetFileChildOrder(N);
    {
        // Children per parent: the base order, our moves and their moves, merged.
        auto childKeys = [&](const SceneText::Model& m, const std::vector<int>& entOf, int idx) {
            std::vector<std::string> v;
            if (idx < 0) return v;
            for (int c : m.children[(std::size_t)idx]) v.push_back(ents[(std::size_t)entOf[(std::size_t)c]].key);
            return v;
        };
        for (std::size_t p = 0; p < N.objects.size(); ++p) {
            if (N.children[p].size() < 2) continue;
            const Entity& pe = ents[(std::size_t)entOfKey[fileOrder[p]]];
            std::vector<std::string> surv;
            for (int c : N.children[p]) surv.push_back(fileOrder[(std::size_t)c]);
            std::vector<std::string> cb2 = childKeys(B, entB, pe.b), co2 = childKeys(O, entO, pe.o), ct2 = childKeys(T, entT, pe.t);
            std::vector<std::string> oc;
            const std::vector<std::string> merged = MergeOrder(cb2, co2, ct2, surv, nullptr, cx.Pref(), &oc);
            for (const auto& key : oc) {
                const Entity& e = ents[(std::size_t)entOfKey[key]];
                AddConflict(cx, whoOf(e), Kind::OrderChanged, "", "", "", "moved among siblings", "moved elsewhere", cx.Name(cx.Pref()));
            }
            std::vector<int> list;
            for (const auto& key : merged) list.push_back(posOfKey[key]);
            N.children[p] = list;
        }
    }
    const std::string merged = SceneText::Emit(N);

    // ---- check the result, or fall back to ours ----
    std::string why;
    const SceneText::Validation v = SceneText::Validate(merged);
    bool good = v.ok;
    if (!good) why = v.problems.empty() ? "invalid" : v.problems[0];
    if (good && opts.validator) {
        std::string e2;
        if (!opts.validator(merged, &e2)) { good = false; why = e2.empty() ? "the validator refused the merged text" : e2; }
    }
    if (!good) {
        res.fellBack = true;
        res.text = co;
        AddConflict(cx, Who(), Kind::Invalid, "", "", "", "", "", "ours: " + why);
        return res;
    }
    res.text = merged;
    return res;
}

// ---- reports ------------------------------------------------------------------------

namespace {
std::string Q(const std::string& s) { return ShortValue(s, 80); }
const char* PolicyName(SceneMerge::Policy p) {
    return p == SceneMerge::Policy::PreferOurs ? "prefer-ours" : p == SceneMerge::Policy::PreferTheirs ? "prefer-theirs" : "mark";
}
} // namespace

std::string SceneMerge::Result::ReportText() const {
    std::string out;
    if (!ok) return "merge failed: " + error + "\n";
    if (conflicts.empty()) return fellBack ? "merge fell back to ours\n" : "merged cleanly\n";
    out += "merged with " + std::to_string(conflicts.size()) + " conflict" + (conflicts.size() == 1 ? "" : "s") +
           " (policy " + PolicyName(policy) + ")\n";
    int n = 0;
    for (const auto& c : conflicts) {
        out += std::to_string(++n) + ". " + c.KindName();
        if (!c.path.empty()) out += " at " + c.path;
        else if (!c.name.empty()) out += " at " + c.name;
        if (!c.record.empty()) out += ", " + c.record;
        if (!c.field.empty()) out += "." + c.field;
        out += ": base " + Q(c.base) + ", ours " + Q(c.ours) + ", theirs " + Q(c.theirs) + " -> " + c.resolution + "\n";
    }
    for (const auto& w : warnings) out += "warning: " + w + "\n";
    return out;
}

std::string SceneMerge::Result::ReportJson() const {
    std::string o = "{\"ok\":";
    o += ok ? "true" : "false";
    o += ",\"fellBack\":";
    o += fellBack ? "true" : "false";
    o += ",\"policy\":" + JsonString(PolicyName(policy));
    if (!ok) o += ",\"error\":" + JsonString(error);
    o += ",\"conflicts\":[";
    for (std::size_t i = 0; i < conflicts.size(); ++i) {
        const auto& c = conflicts[i];
        if (i) o += ",";
        o += "{\"kind\":" + JsonString(c.KindName()) + ",\"object\":" + JsonString(c.object) + ",\"name\":" + JsonString(c.name) +
             ",\"path\":" + JsonString(c.path) + ",\"record\":" + JsonString(c.record) + ",\"field\":" + JsonString(c.field) +
             ",\"base\":" + JsonString(c.base) + ",\"ours\":" + JsonString(c.ours) + ",\"theirs\":" + JsonString(c.theirs) +
             ",\"resolution\":" + JsonString(c.resolution) + "}";
    }
    o += "],\"warnings\":[";
    for (std::size_t i = 0; i < warnings.size(); ++i) { if (i) o += ","; o += JsonString(warnings[i]); }
    o += "]}\n";
    return o;
}

} // namespace okay
