// Prefab tools, wave 2: nested prefabs, variants, unpack, per-record / per-field apply
// and revert, queries and the asset validator. See docs/engine/prefabs.md.
//
// How it works, in one paragraph: a prefab FILE is read into a RawDoc (the lines as
// written, so a file can be edited and written back without losing anything). Resolving
// it gives an RDoc: the fully expanded object list in depth-first order, with nested
// prefabs and variant bases already folded in and a note per object of where it came
// from (which file object, base object or nested object). An RDoc is rendered back to
// ordinary scene text and loaded by SceneSerializer::InstantiateFromText, so this file
// never builds objects itself and keeps the serializer's all-or-nothing loading.
#include "okay/Scene/PrefabTools.hpp"
#include "PrefabInternal.hpp"
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

using prefab_text::Override;
using prefab_text::kMaxObjects;
using prefab_text::kMaxFileBytes;
using prefab_text::Unset;
using prefab_text::Delta;
using prefab_text::RawObj;
using prefab_text::RawDoc;
using prefab_text::ObjSem;
using prefab_text::Prov;
using prefab_text::RObj;
using prefab_text::RDoc;
using prefab_text::Diag;
using prefab_text::Baseline;
using prefab_text::Ctx;
using prefab_text::Status;
using prefab_text::StackGuard;
using prefab_text::Quote;
using prefab_text::Trim;
using prefab_text::FirstToken;
using prefab_text::SkipSpaces;
using prefab_text::ReadQuoted;
using prefab_text::ReadInt;
using prefab_text::BaseName;
using prefab_text::Canon;
using prefab_text::ResolveRef;
using prefab_text::IsBookkeepingRecord;
using prefab_text::IsStructuralToken;
using prefab_text::RecordKey;
using prefab_text::Capitalized;
using prefab_text::ParseDeltaLine;
using prefab_text::IsWholeTopRecord;
using prefab_text::ParseRaw;
using prefab_text::RawToText;
using prefab_text::Interpret;
using prefab_text::FormatIssue;
using prefab_text::RemoveSubtrees;
using prefab_text::Normalize;
using prefab_text::ApplyDelta;
using prefab_text::Render;
using prefab_text::ReadPrefabText;
using prefab_text::ExpandOwn;
using prefab_text::BuildDoc;
namespace fs = std::filesystem;

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
        // the other colliders' records (SceneSerializer): their fields by name, so an Inspector row (Radius, Size, Offset ...) finds its
        // difference from the prefab (they were "arg0", "arg1" ... before, and no row matched them)
        {"capsulecollider2d", {"size.x", "size.y", "direction", "offset.x", "offset.y", "isTrigger", "layer", "autoFit"}},
        {"edgecollider2d", {"offset.x", "offset.y", "isTrigger", "layer", "oneWay", "oneWayNormal.x", "oneWayNormal.y"}},
        {"polygoncollider2d", {"offset.x", "offset.y", "isTrigger", "layer"}},
        {"boxcollider3d", {"size.x", "size.y", "size.z", "offset.x", "offset.y", "offset.z", "isTrigger", "layer", "autoFit"}},
        {"meshcollider3d", {"size.x", "size.y", "size.z", "offset.x", "offset.y", "offset.z", "isTrigger", "layer", "autoFit"}},
        {"spherecollider3d", {"radius", "offset.x", "offset.y", "offset.z", "isTrigger", "layer", "autoFit"}},
        {"capsulecollider3d", {"radius", "height", "axis", "offset.x", "offset.y", "offset.z", "isTrigger", "layer", "autoFit"}},
        {"cylindercollider3d", {"radius", "height", "axis", "offset.x", "offset.y", "offset.z", "isTrigger", "layer", "autoFit"}},
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
    } else {
        // A top-level instance keeps its place in the scene's list too (the rebuilt copy was appended at the end).
        GameObject* next = nullptr;
        bool seen = false;
        for (const auto& up : scene.Objects()) {
            if (!up || up->IsDestroyed() || !up->transform || up->transform->Parent()) continue;
            if (up.get() == &old) { seen = true; continue; }
            if (seen && up.get() != fresh) { next = up.get(); break; }
        }
        if (next) scene.ReorderSibling(fresh, next, /*after=*/false);
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
        if (tok == "prefab" || tok == "override" || tok == "guid" || tok == "sibling" || tok == "comporder" || tok.empty()) continue;
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
