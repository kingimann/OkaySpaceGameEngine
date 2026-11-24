#pragma once
// Private to the engine (included by FbxScene.cpp only): FBX 6.0 / 6.1 files (FBX 2006 to FBX 2009) rewritten as the
// element tree of a 7.x file, so the one scene builder (FbxSceneInternal.hpp) reads both.
//
// Written by us from public prose descriptions of the two layouts and from byte study of files of that time (sources
// in docs/content/model_readers.md, "FBX 6.x and 7.0"); no code from any other FBX reader or writer. What changes:
//
//   6.x                                                   7.x (what the builder reads)
//   ---------------------------------------------------   -----------------------------------------------------------
//   objects named: Model: "Model::Cube", "Mesh"           numbered: Model: 1234, "Cube\0\1Model", "Mesh"
//   Connections: Connect: "OO", "Model::A", "Model::B"    Connections: C: "OO", 1234, 5678 ("Model::Scene" is 0)
//   Properties60 { Property: name, type, flags, v... }    Properties70 { P: name, type, label, flags, v... }
//   the mesh (Vertices, PolygonVertexIndex, layers)       a Geometry object of its own, connected to the Model
//     sits inside its Model
//   a Skin deformer connects to the Model                 ... to the Geometry
//   textures connect to the Model; LayerElementTexture    a texture connects to a material property ("DiffuseColor")
//     (TextureId) says which polygons use which
//   blend shapes: Shape: "name" { Indexes Vertices }      BlendShape deformer -> BlendShapeChannel -> Shape geometry
//     inside the mesh; the weight is a model property
//     with the shape's name
//   GlobalSettings inside Objects                         GlobalSettings at the top level
//   Takes { Take: "name" { LocalTime: a, b                AnimationStack (LocalStart / LocalStop) -> AnimationLayer
//     Model: "Model::X" { Channel: "Transform" {          -> AnimationCurveNode ("Lcl Translation", d|X d|Y d|Z)
//       Channel: "T" { Channel: "X" { Default: d          -> AnimationCurve (KeyTime, KeyValueFloat, KeyAttrFlags,
//         Key: time, value, C|L|U, extra... }}}}}}           KeyAttrDataFloat, KeyAttrRefCount)
//   ASCII arrays are plain value lists ("Vertices:        ASCII arrays are "*N { a: ... }" blocks
//     1,2,3,...")
//
// Key lists: each key is a time (KTime, 46186158000 a second, as in 7.x), a value and an interpolation letter: C
// (constant, then a step letter), L (linear), U (cubic, then a tangent letter and for some of them numbers). The extra
// values are skipped by finding where the next key starts (two numbers and C, L or U). Cubic keys get their tangents
// from the neighbouring keys (the slope between the key before and the key after), not from the file's tangent
// numbers, whose units we could not confirm; the curve passes through every key either way. docs/UNFINISHED.md has
// the row.
#include "okay/Render/FbxReader.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iterator>
#include <new>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace okay {
namespace fbx {
namespace legacy {

enum class Layout { Modern, Legacy, TooOld };

/// The version the file says it is: the binary header or the ASCII "; FBX 6.1.0" comment, else
/// FBXHeaderExtension / FBXVersion; 0 when it says nothing.
inline std::uint32_t VersionOf(const Document& d) {
    if (d.version) return d.version;
    if (const Elem* h = d.root.Find("FBXHeaderExtension"))
        if (const Elem* v = h->Find("FBXVersion"))
            if (!v->props.empty() && v->props[0].IsNumber()) {
                const double x = v->props[0].Num();
                if (x > 0 && x < 100000) return (std::uint32_t)x;
            }
    return 0;
}

/// Which layout the element tree is in. Legacy: the file says 6.x, or (no version, or a 7.x one) its connections are
/// "Connect" records or most of its objects are named rather than numbered.
inline Layout LayoutOf(const Document& d, std::uint32_t& version) {
    version = VersionOf(d);
    if (version && version < 6000) return Layout::TooOld;
    if (version >= 6000 && version < 7000) return Layout::Legacy;
    if (const Elem* c = d.root.Find("Connections")) for (const Elem& e : c->children) if (e.name == "Connect") return Layout::Legacy;
    if (const Elem* o = d.root.Find("Objects")) {
        std::size_t named = 0, numbered = 0;
        for (const Elem& e : o->children) {
            if (e.props.empty()) continue;
            if (e.props[0].type == 'S') ++named; else if (e.props[0].IsNumber()) ++numbered;
        }
        if (named > numbered) return Layout::Legacy;
    }
    return Layout::Modern;
}

namespace detail {

constexpr std::uint32_t kNone = 0xFFFFFFFFu;

inline Prop PS(std::string_view v) { Prop p; p.type = 'S'; new (&p.sv) std::string_view(v); return p; }
inline Prop PL(std::int64_t v) { Prop p; p.type = 'L'; p.i = v; return p; }
inline Prop PD(double v) { Prop p; p.type = 'D'; p.d = v; return p; }

/// "Cube\0\1Model" (how binary files write a name) -> "Model::Cube" (how ASCII files write it), so both spellings
/// of a name find the same object.
inline std::string Key(std::string_view raw) {
    const std::size_t z = raw.find('\0');
    if (z != std::string_view::npos && z + 1 < raw.size() && raw[z + 1] == '\1') {
        std::string k(raw.substr(z + 2)); k += "::"; k.append(raw.data(), z);
        return k;
    }
    return std::string(raw);
}

/// The names of the elements whose values are arrays in 7.x (a 6.x ASCII file writes them as value lists).
inline bool ArrayName(std::string_view n) {
    static const char* const names[] = {"Vertices", "PolygonVertexIndex", "Edges", "Normals", "NormalsIndex", "NormalsW",
        "Binormals", "BinormalsIndex", "Tangents", "TangentsIndex", "UV", "UVIndex", "Colors", "ColorIndex", "Materials",
        "TextureId", "Smoothing", "Visibility", "PolygonGroup", "Indexes", "Weights", "Transform", "TransformLink",
        "TransformAssociateModel", "Matrix", "FullWeights"};
    for (const char* x : names) if (n == x) return true;
    return false;
}

/// The values of an element as doubles: its one array property, or its list of numeric properties. False when it has
/// something else (a string among the values).
inline bool Values(const Elem* e, std::vector<double>& out) {
    out.clear();
    if (!e) return false;
    if (e->props.size() == 1 && e->props[0].type == 'a') {
        const Array& a = e->props[0].arr;
        out.reserve(a.size());
        for (std::size_t k = 0; k < a.size(); ++k) out.push_back(a[k]);
        return true;
    }
    out.reserve(e->props.size());
    for (const Prop& p : e->props) { if (!p.IsNumber()) return false; out.push_back(p.Num()); }
    return true;
}

/// The new element tree, built in the Document's flat arrays with the same index links the parser uses, then turned
/// into pointers once (Finish).
struct Out {
    Document& d;
    std::vector<std::uint32_t> lastChild;   // per element
    std::uint32_t rootLast = kNone;
    explicit Out(Document& doc) : d(doc) {}

    std::uint32_t Add(std::uint32_t parent, std::string_view name, const Prop* props, std::size_t n) {
        const std::uint32_t idx = (std::uint32_t)d.elems.size();
        d.elems.emplace_back();
        Elem& e = d.elems.back();
        e.name = name;
        e.props.begin_ = (std::uint32_t)d.props.size();
        e.props.n = (std::uint32_t)n;
        e.links_ = 0;
        for (std::size_t k = 0; k < n; ++k) d.props.push_back(props[k]);
        lastChild.push_back(kNone);
        const std::uint64_t c1 = (std::uint64_t)idx + 1;
        std::uint32_t& last = parent == kNone ? rootLast : lastChild[parent];
        if (last == kNone) { Elem& p = parent == kNone ? d.root : d.elems[parent]; p.links_ = (p.links_ & 0xFFFFFFFFull) | (c1 << 32); }
        else { Elem& l = d.elems[last]; l.links_ = (l.links_ & ~0xFFFFFFFFull) | c1; }
        last = idx;
        ++d.elementCount;
        return idx;
    }
    std::uint32_t Add(std::uint32_t parent, std::string_view name, std::initializer_list<Prop> props) {
        return Add(parent, name, props.begin(), props.size());
    }
    /// An array property made from `v`: int32 when every value is a 32-bit integer, else double. Kept in the
    /// Document's own number buffers.
    Prop Array(const std::vector<double>& v, bool allowInt = true) {
        bool isInt = allowInt;
        for (double x : v) if (!(x >= -2147483648.0 && x <= 2147483647.0 && x == std::floor(x))) { isInt = false; break; }
        NumberBuffer buf(isInt ? (v.size() * 4 + 7) / 8 : v.size());
        if (!buf.empty()) buf.back() = 0;
        if (isInt) { std::uint8_t* q = (std::uint8_t*)buf.data(); for (std::size_t k = 0; k < v.size(); ++k) { const std::int32_t x = (std::int32_t)v[k]; std::memcpy(q + k * 4, &x, 4); } }
        else for (std::size_t k = 0; k < v.size(); ++k) buf[k] = v[k];
        Prop p; p.type = 'a';
        p.SetArray(isInt ? 'i' : 'd', (std::uint32_t)v.size(), (const std::uint8_t*)buf.data());
        d.numbers.push_back(std::move(buf));
        // (1 + index) of the buffer for an ASCII Document, which keeps its arrays in `numbers`; a binary Document's
        // index means `inflated`, so there the array is simply copied by the builder.
        p.own = d.binary ? 0 : (std::uint32_t)d.numbers.size();
        return p;
    }
    void Finish() { LinkTree(d); }
};

struct Key6 { double t, v; char interp; };

/// The keys of one 6.x channel ("Key" lists). Times as stored (KTime).
inline void ParseKeys(const Elem& ch, std::vector<Key6>& keys) {
    keys.clear();
    std::vector<const Prop*> tok;
    for (const Elem& k : ch.children) if (k.name == "Key") for (const Prop& p : k.props) tok.push_back(&p);
    auto letter = [&](std::size_t i) -> char {
        if (i >= tok.size() || tok[i]->type != 'S' || tok[i]->s().size() != 1) return 0;
        return tok[i]->s()[0];
    };
    auto keyStart = [&](std::size_t i) {
        if (i + 2 >= tok.size() || !tok[i]->IsNumber() || !tok[i + 1]->IsNumber()) return false;
        const char c = letter(i + 2);
        return c == 'C' || c == 'L' || c == 'U';
    };
    std::size_t i = 0;
    while (i < tok.size() && !keyStart(i)) ++i;
    while (i < tok.size()) {
        keys.push_back({tok[i]->Num(), tok[i + 1]->Num(), letter(i + 2)});
        i += 3;
        while (i < tok.size() && !keyStart(i)) ++i;
    }
}

} // namespace detail

/// Rewrites the 6.x element tree `in` as a 7.x one in `out`. With `steal` (== &in, which the caller lets go of
/// afterwards) the decoded arrays of `in` move into `out` instead of being looked up there. The views into the file's
/// bytes stay views: the bytes must outlive `out`, as they must outlive `in`.
inline bool Upgrade(const Document& in, Document* steal, Document& out, std::string& err) {
    using namespace detail;
    out = Document();
    out.version = VersionOf(in);
    out.binary = in.binary;
    if (steal) { out.inflated = std::move(steal->inflated); out.numbers = std::move(steal->numbers); }
    Out o(out);
    out.elems.reserve(in.elems.size() + 64);
    out.props.reserve(in.props.size() + 64);

    const Elem* objects = in.root.Find("Objects");
    if (!objects) { err = "no Objects section (the file holds no scene)"; return false; }

    // Copies `src` (and what is below it) under `parent`; the values of array elements become one array.
    std::vector<double> vals;
    std::vector<Prop> props;
    auto copy = [&](auto&& self, const Elem& src, std::uint32_t parent, std::string_view name) -> void {
        props.clear();
        if (ArrayName(src.name) && !(src.props.size() == 1 && src.props[0].type == 'a') && Values(&src, vals)) props.push_back(o.Array(vals));
        else props.assign(src.props.begin(), src.props.end());
        const std::uint32_t e = o.Add(parent, name, props.data(), props.size());
        for (const Elem& c : src.children) self(self, c, e, c.name);
    };
    // Properties60 -> Properties70 (an empty label inserted after the type).
    auto props70 = [&](const Elem* p60, std::uint32_t parent, const std::vector<Prop>& more) {
        const std::uint32_t p70 = o.Add(parent, "Properties70", {});
        if (p60) for (const Elem& p : p60->children) {
            if (p.name != "Property" || p.props.size() < 3 || p.props[0].type != 'S') continue;
            props.clear();
            props.push_back(p.props[0]); props.push_back(p.props[1]); props.push_back(PS("")); props.push_back(p.props[2]);
            for (std::size_t k = 3; k < p.props.size(); ++k) props.push_back(p.props[k]);
            o.Add(p70, "P", props.data(), props.size());
        }
        for (std::size_t k = 0; k + 5 <= more.size(); k += 5) o.Add(p70, "P", more.data() + k, 5);
        return p70;
    };

    // ---- top level: header, settings, objects, connections ----
    if (const Elem* h = in.root.Find("FBXHeaderExtension")) copy(copy, *h, kNone, h->name);
    const Elem* gs = objects->Find("GlobalSettings");
    if (!gs) gs = in.root.Find("GlobalSettings");
    if (gs) { const std::uint32_t g = o.Add(kNone, "GlobalSettings", {}); props70(gs->Find("Properties60"), g, {}); }
    const std::uint32_t objs = o.Add(kNone, "Objects", {});

    std::unordered_map<std::string, std::int64_t> ids;   // object key ("Model::Cube") -> id
    std::unordered_map<std::int64_t, std::string_view> classOf;
    struct ModelInfo { std::int64_t id = 0, geo = 0, blend = 0; const Elem* e = nullptr; std::unordered_map<std::string, std::int64_t> shapes; };
    std::unordered_map<std::int64_t, ModelInfo> models;
    std::int64_t next = 1;
    struct C { std::string_view type; std::int64_t child, parent; std::string_view prop; };
    std::vector<C> conns;

    for (const Elem& e : objects->children) {
        if (e.name == "GlobalSettings" || e.props.empty() || e.props[0].type != 'S') continue;
        const std::string key = Key(e.props[0].s());
        const std::int64_t id = next++;
        if (!ids.count(key)) ids[key] = id;   // a repeated name: the first object keeps it
        classOf[id] = e.name;
        const std::string_view sub = e.props.size() > 1 && e.props[1].type == 'S' ? e.props[1].s() : std::string_view();
        const std::uint32_t ob = o.Add(objs, e.name, {PL(id), e.props[0], PS(sub)});
        if (e.name != "Model") {
            for (const Elem& c : e.children) {
                if (c.name == "Properties60") props70(&c, ob, {});
                else copy(copy, c, ob, (e.name == "Video" && c.name == "Filename") ? std::string_view("FileName") : c.name);
            }
            continue;
        }
        ModelInfo& mi = models[id];
        mi.id = id; mi.e = &e;
        props70(e.Find("Properties60"), ob, {});
        if (!e.Find("Vertices")) continue;
        // the mesh: a Geometry of its own
        mi.geo = next++;
        classOf[mi.geo] = "Geometry";
        const std::uint32_t g = o.Add(objs, "Geometry", {PL(mi.geo), e.props[0], PS("Mesh")});
        for (const Elem& c : e.children) {
            if (c.name == "Properties60" || c.name == "Shape" || c.name == "Version" || c.name == "MultiLayer" || c.name == "MultiTake" ||
                c.name == "Shading" || c.name == "Culling" || c.name == "TypeFlags" || c.name == "NodeAttributeName") continue;
            copy(copy, c, g, c.name);
        }
        conns.push_back({"OO", mi.geo, id, {}});
        // blend shapes: a BlendShape deformer, a channel per shape (weight: the model property of the shape's name)
        const Elem* p60 = e.Find("Properties60");
        for (const Elem& sh : e.children) {
            if (sh.name != "Shape" || sh.props.empty() || sh.props[0].type != 'S') continue;
            if (!mi.blend) {
                mi.blend = next++;
                classOf[mi.blend] = "Deformer";
                o.Add(objs, "Deformer", {PL(mi.blend), PS("BlendShape"), PS("BlendShape")});
                conns.push_back({"OO", mi.blend, mi.geo, {}});
            }
            const std::string_view sname = sh.props[0].s();
            double weight = 0;
            if (p60) for (const Elem& p : p60->children)
                if (p.name == "Property" && p.props.size() >= 4 && p.props[0].s() == sname && p.props[3].IsNumber()) weight = p.props[3].Num();
            const std::int64_t ch = next++, shape = next++;
            classOf[ch] = "Deformer"; classOf[shape] = "Geometry";
            const std::uint32_t che = o.Add(objs, "Deformer", {PL(ch), PS(sname), PS("BlendShapeChannel")});
            props70(nullptr, che, {PS("DeformPercent"), PS("Number"), PS(""), PS("A"), PD(weight)});
            const std::uint32_t she = o.Add(objs, "Geometry", {PL(shape), PS(sname), PS("Shape")});
            for (const Elem& c : sh.children) copy(copy, c, she, c.name);
            conns.push_back({"OO", ch, mi.blend, {}});
            conns.push_back({"OO", shape, ch, {}});
            mi.shapes[std::string(sname)] = ch;
        }
    }

    // ---- connections (names -> ids) ----
    auto idOf = [&](const Prop& p, std::int64_t& id) -> bool {
        if (p.type != 'S') return false;
        const std::string k = Key(p.s());
        if (k == "Model::Scene") { id = 0; return true; }
        auto it = ids.find(k);
        if (it == ids.end()) return false;
        id = it->second;
        return true;
    };
    std::unordered_map<std::int64_t, std::vector<std::int64_t>> texturesOf, materialsOf;   // model -> in connection order
    if (const Elem* ce = in.root.Find("Connections")) for (const Elem& e : ce->children) {
        if ((e.name != "Connect" && e.name != "C") || e.props.size() < 3 || e.props[0].type != 'S') continue;
        std::int64_t child = 0, parent = 0;
        if (!idOf(e.props[1], child) || !idOf(e.props[2], parent) || child == 0) continue;
        const std::string_view type = e.props[0].s();
        const std::string_view prop = e.props.size() > 3 && e.props[3].type == 'S' ? e.props[3].s() : std::string_view();
        auto pm = models.find(parent);
        const std::string_view cc = classOf[child];
        if (type == "OO" && pm != models.end()) {
            if (cc == "Texture") { texturesOf[parent].push_back(child); continue; }   // placed on a material below
            if (cc == "Material") materialsOf[parent].push_back(child);
            if (cc == "Deformer" && pm->second.geo) parent = pm->second.geo;     // a skin deforms the mesh
        }
        conns.push_back({type, child, parent, prop});
    }
    // Textures: a model's textures go on the materials of the polygons that use them (LayerElementTexture), as the
    // material's DiffuseColor; without that layer texture k goes on material k (or the first texture on all).
    {
        std::unordered_map<std::int64_t, bool> textured;
        for (const C& c : conns) if (c.type == "OP" && classOf[c.child] == "Texture") textured[c.parent] = true;
        std::vector<double> pvi, tid, mid;
        for (auto& [model, texs] : texturesOf) {
            const auto mats = materialsOf.find(model);
            if (mats == materialsOf.end() || mats->second.empty()) continue;
            const Elem* me = models[model].e;
            auto layer = [&](const char* name) -> const Elem* {
                const Elem* best = nullptr; double bi = 1e30;
                for (const Elem& c : me->children) if (c.name == name) { const double k = (!c.props.empty() && c.props[0].IsNumber()) ? c.props[0].Num() : 0; if (k < bi) { best = &c; bi = k; } }
                return best;
            };
            auto mapping = [](const Elem* l) { const Elem* m = l ? l->Find("MappingInformationType") : nullptr; return (m && !m->props.empty()) ? m->props[0].s() : std::string_view(); };
            const Elem* lt = layer("LayerElementTexture");
            const Elem* lm = layer("LayerElementMaterial");
            Values(lt ? lt->Find("TextureId") : nullptr, tid);
            Values(lm ? lm->Find("Materials") : nullptr, mid);
            Values(me->Find("PolygonVertexIndex"), pvi);
            const std::string_view tmap = mapping(lt), mmap = mapping(lm);
            std::size_t polys = 0;
            for (double v : pvi) if (v < 0) ++polys;
            for (std::size_t slot = 0; slot < mats->second.size(); ++slot) {
                std::int64_t pick = -1;
                if (lt && tmap == "AllSame" && !tid.empty()) pick = (std::int64_t)tid[0];
                else if (lt && tmap == "ByPolygon") {
                    std::unordered_map<std::int64_t, std::size_t> votes;
                    for (std::size_t p = 0; p < polys && p < tid.size(); ++p) {
                        const double ms = mmap == "ByPolygon" ? (p < mid.size() ? mid[p] : -1) : (mid.empty() ? 0 : mid[0]);
                        if (ms == (double)slot) ++votes[(std::int64_t)tid[p]];
                    }
                    std::size_t best = 0;
                    for (auto& [t, n] : votes) if (n > best || (n == best && t < pick)) { best = n; pick = t; }
                } else pick = slot < texs.size() ? (std::int64_t)slot : 0;
                if (pick < 0 || pick >= (std::int64_t)texs.size()) continue;
                const std::int64_t mat = mats->second[slot];
                if (textured[mat]) continue;
                textured[mat] = true;
                conns.push_back({"OP", texs[(std::size_t)pick], mat, "DiffuseColor"});
            }
        }
    }

    // ---- takes -> stacks, layers, curve nodes, curves ----
    if (const Elem* takes = in.root.Find("Takes")) {
        std::vector<Key6> keys;
        std::vector<double> kt, kv, kd, kf, kr;
        // One curve from a channel's keys; 0 when it has none.
        auto curve = [&](const Elem& ch) -> std::int64_t {
            ParseKeys(ch, keys);
            if (keys.empty()) return 0;
            const std::size_t n = keys.size();
            kt.assign(n, 0); kv.assign(n, 0); kf.assign(n, 0); kr.assign(n, 1); kd.assign(n * 4, 0);
            for (std::size_t i = 0; i < n; ++i) { kt[i] = keys[i].t; kv[i] = keys[i].v; }
            auto slope = [&](std::size_t i) {
                if (i == 0 || i + 1 >= n) return 0.0;
                const double dt = (kt[i + 1] - kt[i - 1]) / 46186158000.0;
                return dt > 0 ? (kv[i + 1] - kv[i - 1]) / dt : 0.0;
            };
            for (std::size_t i = 0; i < n; ++i) {
                const char c = keys[i].interp;
                kf[i] = c == 'C' ? 0x2 : c == 'L' ? 0x4 : 0x408;   // constant, linear, cubic with the tangents below
                kd[i * 4] = slope(i); kd[i * 4 + 1] = i + 1 < n ? slope(i + 1) : 0.0;
            }
            const std::int64_t id = next++;
            const std::uint32_t ce = o.Add(objs, "AnimationCurve", {PL(id), PS(""), PS("")});
            double def = 0;
            if (const Elem* d = ch.Find("Default")) if (!d->props.empty()) def = d->props[0].Num();
            o.Add(ce, "Default", {PD(def)});
            Prop a = o.Array(kt, false); o.Add(ce, "KeyTime", &a, 1);
            a = o.Array(kv, false); o.Add(ce, "KeyValueFloat", &a, 1);
            a = o.Array(kf); o.Add(ce, "KeyAttrFlags", &a, 1);
            a = o.Array(kd, false); o.Add(ce, "KeyAttrDataFloat", &a, 1);
            a = o.Array(kr); o.Add(ce, "KeyAttrRefCount", &a, 1);
            return id;
        };
        auto defaultOf = [](const Elem* ch, double def) {
            if (ch) if (const Elem* d = ch->Find("Default")) if (!d->props.empty() && d->props[0].IsNumber()) return d->props[0].Num();
            return def;
        };
        for (const Elem& take : takes->children) {
            if (take.name != "Take" || take.props.empty() || take.props[0].type != 'S') continue;
            const std::int64_t stack = next++, layer = next++;
            std::vector<Prop> span;
            if (const Elem* lt = take.Find("LocalTime")) if (lt->props.size() >= 2 && lt->props[0].IsNumber() && lt->props[1].IsNumber()) {
                const Prop sp[] = {PS("LocalStart"), PS("KTime"), PS("Time"), PS(""), PD(lt->props[0].Num()),
                                   PS("LocalStop"), PS("KTime"), PS("Time"), PS(""), PD(lt->props[1].Num())};
                span.assign(std::begin(sp), std::end(sp));
            }
            const std::uint32_t st = o.Add(objs, "AnimationStack", {PL(stack), take.props[0], PS("")});
            props70(nullptr, st, span);
            o.Add(objs, "AnimationLayer", {PL(layer), PS("BaseLayer"), PS("")});
            conns.push_back({"OO", layer, stack, {}});
            for (const Elem& tm : take.children) {
                if (tm.name != "Model" || tm.props.empty()) continue;
                std::int64_t model = 0;
                if (!idOf(tm.props[0], model) || !models.count(model)) continue;
                const ModelInfo& mi = models[model];
                // the T / R / S groups, inside "Transform" or directly under the model
                std::vector<const Elem*> groups;
                for (const Elem& c : tm.children) {
                    if (c.name != "Channel" || c.props.empty()) continue;
                    if (c.props[0].s() == "Transform") { for (const Elem& g : c.children) if (g.name == "Channel") groups.push_back(&g); }
                    else groups.push_back(&c);
                }
                for (const Elem* g : groups) {
                    const std::string_view gn = g->props.empty() ? std::string_view() : g->props[0].s();
                    const char* prop = (gn == "T" || gn == "Lcl Translation") ? "Lcl Translation" : (gn == "R" || gn == "Lcl Rotation") ? "Lcl Rotation"
                                     : (gn == "S" || gn == "Lcl Scaling") ? "Lcl Scaling" : nullptr;
                    if (prop) {
                        const Elem* axis[3] = {};
                        for (const Elem& a : g->children) {
                            if (a.name != "Channel" || a.props.empty()) continue;
                            const std::string_view an = a.props[0].s();
                            if (an == "X") axis[0] = &a; else if (an == "Y") axis[1] = &a; else if (an == "Z") axis[2] = &a;
                        }
                        std::int64_t cv[3] = {0, 0, 0};
                        for (int k = 0; k < 3; ++k) if (axis[k]) cv[k] = curve(*axis[k]);
                        if (!cv[0] && !cv[1] && !cv[2]) continue;
                        const double base = prop[4] == 'S' ? 1.0 : 0.0;   // "Lcl Scaling"
                        const std::int64_t cn = next++;
                        const std::uint32_t cne = o.Add(objs, "AnimationCurveNode", {PL(cn), PS(gn), PS("")});
                        props70(nullptr, cne, {PS("d|X"), PS("Number"), PS(""), PS("A"), PD(defaultOf(axis[0], base)),
                                               PS("d|Y"), PS("Number"), PS(""), PS("A"), PD(defaultOf(axis[1], base)),
                                               PS("d|Z"), PS("Number"), PS(""), PS("A"), PD(defaultOf(axis[2], base))});
                        conns.push_back({"OO", cn, layer, {}});
                        conns.push_back({"OP", cn, model, prop});
                        static const char* const dn[3] = {"d|X", "d|Y", "d|Z"};
                        for (int k = 0; k < 3; ++k) if (cv[k]) conns.push_back({"OP", cv[k], cn, dn[k]});
                        continue;
                    }
                    // a blend shape weight: a channel named as one of the model's shapes (0 to 100)
                    auto sh = mi.shapes.find(std::string(gn));
                    if (sh == mi.shapes.end()) continue;
                    const std::int64_t c = curve(*g);
                    if (!c) continue;
                    const std::int64_t cn = next++;
                    const std::uint32_t cne = o.Add(objs, "AnimationCurveNode", {PL(cn), PS("DeformPercent"), PS("")});
                    props70(nullptr, cne, {PS("d|DeformPercent"), PS("Number"), PS(""), PS("A"), PD(defaultOf(g, 0))});
                    conns.push_back({"OO", cn, layer, {}});
                    conns.push_back({"OP", cn, sh->second, "DeformPercent"});
                    conns.push_back({"OP", c, cn, "d|DeformPercent"});
                }
            }
        }
    }

    const std::uint32_t ce = o.Add(kNone, "Connections", {});
    for (const C& c : conns) {
        if (c.type == "OP") o.Add(ce, "C", {PS("OP"), PL(c.child), PL(c.parent), PS(c.prop)});
        else o.Add(ce, "C", {PS("OO"), PL(c.child), PL(c.parent)});
    }
    o.Finish();
    return true;
}

} // namespace legacy
} // namespace fbx
} // namespace okay
