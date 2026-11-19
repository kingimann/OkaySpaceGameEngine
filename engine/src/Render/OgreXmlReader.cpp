// Our own OGRE XML mesh (.mesh.xml) reader. Written from the public description of the OGRE mesh XML format (the file the
// OgreXMLConverter tool and the exporters write: <mesh> with <sharedgeometry>, <submeshes>, <vertexbuffer> blocks of
// <vertex> elements, <faces>) and of the OGRE material script language (the .material files that name the colours and the
// texture of a submesh's material); no code from any other importer. One part per submesh. Uses the strict XML reader of the
// batch-B readers. Bounds-checked, capped, no recursion. See okay/Render/ModelData.hpp and docs/own_ogre_reader.md.
#include "BinReader.hpp"
#include "ReaderCommon.hpp"
#include "TextReaderUtil.hpp"
#include "SceneRefPath.hpp"
#include "XmlLite.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace okay {

namespace {
namespace ogre {

constexpr std::size_t kMaxFileBytes = 256u * 1024u * 1024u;
constexpr long kMaxVerts = 8000000, kMaxFaces = 20000000;
constexpr std::size_t kMaxOutVerts = 40000000, kMaxSubmeshes = 100000;

bool Fail(std::string* err, const std::string& m) { if (err) *err = "OGRE: " + m; return false; }

struct Geom {
    std::vector<Vec3> pos, nrm;
    std::vector<Vec2> uv;
    std::vector<char> hasPos, hasNrm, hasUv;
    std::int64_t count = 0;
};

// attribute `key` of element i as a number (false when it is missing or not a number)
bool Num(const detail::xml::FlatDoc& d, int i, std::string_view key, double& v) {
    std::string_view s;
    return d.Attr(i, key, s) && detail::rc::ParseNumber(s.data(), s.data() + s.size(), v);
}

// One <geometry> / <sharedgeometry> element: the vertexcount attribute and the <vertexbuffer> children (position, normal and
// the first texcoord set may be spread over several buffers; vertex k of every buffer is vertex k of the geometry).
bool ReadGeometry(const detail::xml::FlatDoc& d, int g, Geom& out, std::string* err, int& extraUv, int& colours, int& tangentsSeen) {
    double vc;
    if (!Num(d, g, "vertexcount", vc) || vc < 0 || vc > (double)kMaxVerts || vc != std::floor(vc))
        return Fail(err, "bad vertexcount '" + d.AttrOr(g, "vertexcount", "") + "'");
    out.count = (std::int64_t)vc;
    const std::size_t n = (std::size_t)vc;
    {   // the claimed count must be backed by <vertex> elements before anything is allocated from it
        std::size_t most = 0;
        for (int b = d.Kid(g); b >= 0; b = d.Next(b)) {
            if (d.Name(b) != "vertexbuffer") continue;
            std::size_t k = 0;
            for (int vi = d.Kid(b); vi >= 0; vi = d.Next(vi)) if (d.Name(vi) == "vertex") ++k;
            most = std::max(most, k);
        }
        if (most != n) return Fail(err, "the vertexcount " + std::to_string(n) + " does not match the " + std::to_string(most) + " <vertex> elements");
    }
    out.pos.assign(n, Vec3{0, 0, 0}); out.nrm.assign(n, Vec3{0, 0, 0}); out.uv.assign(n, Vec2{0, 0});
    out.hasPos.assign(n, 0); out.hasNrm.assign(n, 0); out.hasUv.assign(n, 0);
    for (int b = d.Kid(g); b >= 0; b = d.Next(b)) {
        if (d.Name(b) != "vertexbuffer") continue;
        std::size_t k = 0;
        for (int vi = d.Kid(b); vi >= 0; vi = d.Next(vi)) {
            if (d.Name(vi) != "vertex") continue;
            if (k >= n) return Fail(err, "a vertexbuffer has more <vertex> elements than the vertexcount " + std::to_string(n));
            bool uvDone = false;
            for (int e = d.Kid(vi); e >= 0; e = d.Next(e)) {
                const std::string_view nm = d.Name(e);
                double a, bb, c;
                if (nm == "position") {
                    if (!Num(d, e, "x", a) || !Num(d, e, "y", bb) || !Num(d, e, "z", c)) return Fail(err, "bad <position> in vertex " + std::to_string(k));
                    out.pos[k] = Vec3{(float)a, (float)bb, (float)c}; out.hasPos[k] = 1;
                } else if (nm == "normal") {
                    if (!Num(d, e, "x", a) || !Num(d, e, "y", bb) || !Num(d, e, "z", c)) return Fail(err, "bad <normal> in vertex " + std::to_string(k));
                    out.nrm[k] = Vec3{(float)a, (float)bb, (float)c}; out.hasNrm[k] = 1;
                } else if (nm == "texcoord") {
                    if (uvDone) { ++extraUv; continue; }
                    if (!Num(d, e, "u", a) || !Num(d, e, "v", bb)) return Fail(err, "bad <texcoord> in vertex " + std::to_string(k));
                    out.uv[k] = Vec2{(float)a, (float)bb}; out.hasUv[k] = 1; uvDone = true;
                } else if (nm == "colour_diffuse" || nm == "colour_specular") ++colours;
                else if (nm == "tangent" || nm == "binormal") ++tangentsSeen;
            }
            ++k;
        }
    }
    return true;
}

// ---- material scripts ----
struct Mat { std::string name; bool hasDiffuse = false; Color diffuse{1, 1, 1, 1}; bool hasEmissive = false; Color emissive{0, 0, 0, 1}; float shininess = -1; std::string texture; };

// Words and braces of a material script: "//" comments dropped, quoted strings kept whole.
void Words(const std::string& s, std::vector<std::string>& w) {
    std::size_t i = 0;
    w.clear();
    while (i < s.size() && w.size() < 2000000) {
        const char c = s[i];
        if (detail::txt::IsWs(c)) { ++i; continue; }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') { while (i < s.size() && s[i] != '\n' && s[i] != '\r') ++i; continue; }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') { const std::size_t e = s.find("*/", i + 2); i = e == std::string::npos ? s.size() : e + 2; continue; }
        if (c == '{' || c == '}' || c == ':') { w.push_back(std::string(1, c)); ++i; continue; }
        if (c == '"') { std::size_t e = s.find('"', i + 1); if (e == std::string::npos) e = s.size(); w.push_back(s.substr(i + 1, e - i - 1)); i = e + 1; continue; }
        std::size_t e = i;
        while (e < s.size() && !detail::txt::IsWs(s[e]) && s[e] != '{' && s[e] != '}') ++e;
        w.push_back(s.substr(i, e - i));
        i = e;
    }
}

void ParseScript(const std::string& script, std::map<std::string, Mat>& mats) {
    std::vector<std::string> w;
    Words(script, w);
    std::size_t i = 0;
    while (i < w.size()) {
        if (w[i] != "material") { ++i; continue; }
        ++i;
        if (i >= w.size()) break;
        Mat m;
        m.name = w[i++];
        while (i < w.size() && w[i] != "{") ++i;   // skips ": parent"
        if (i >= w.size()) break;
        ++i;
        int depth = 1;
        std::vector<std::string> kind;   // keyword that opened each open block
        std::string last;
        bool inFirstPass = true, seenPass = false, haveTex = false;
        kind.push_back("material");
        while (i < w.size() && depth > 0) {
            const std::string& t = w[i];
            if (t == "{") { ++depth; kind.push_back(last); last.clear(); ++i; continue; }
            if (t == "}") {
                --depth;
                if (!kind.empty()) { if (kind.back() == "pass") { if (seenPass) inFirstPass = false; } kind.pop_back(); }
                ++i; continue;
            }
            if (t == "pass") seenPass = true;
            if (t == "pass" || t == "technique" || t == "texture_unit") last = t;   // the block keyword; a name may follow it before the brace
            const std::string top = kind.empty() ? "" : kind.back();
            auto num = [&](std::size_t at, double& v) { return at < w.size() && detail::rc::ParseNumber(w[at], v); };
            if (top == "pass" && inFirstPass) {
                double a, b, c, d;
                if (t == "diffuse" && num(i + 1, a) && num(i + 2, b) && num(i + 3, c)) { m.hasDiffuse = true; m.diffuse = Color{(float)a, (float)b, (float)c, num(i + 4, d) ? (float)d : 1.0f}; }
                else if (t == "emissive" && num(i + 1, a) && num(i + 2, b) && num(i + 3, c)) { if (a + b + c > 0) { m.hasEmissive = true; m.emissive = Color{(float)a, (float)b, (float)c, 1}; } }
                else if (t == "specular" && num(i + 1, a) && num(i + 2, b) && num(i + 3, c) && num(i + 4, d)) { if (num(i + 5, a)) m.shininess = (float)std::max(0.0, a); }
            } else if (top == "texture_unit" && inFirstPass && !haveTex && t == "texture" && i + 1 < w.size()) { m.texture = w[i + 1]; haveTex = true; }
            ++i;
        }
        mats[m.name] = m;
    }
}

// ---- the skeleton (.skeleton.xml) and the bone assignments of a mesh ----
// Written from the public description of the OGRE skeleton XML (<skeleton> with <bones>, <bonehierarchy> and <animations>; a
// bone has a position, a rotation as angle and axis and a scale; a keyframe holds a translate, a rotate and a scale that are
// applied on top of the bone's bind pose: the translation is added to the bind position in the parent's space, the rotation is
// multiplied on the right of the bind rotation, the scales multiply). No code from OGRE or any importer.
constexpr std::size_t kMaxBones = 10000, kMaxAnims = 2000, kMaxKeys = 4000000;

struct Infl { int idx[4] = {-1, -1, -1, -1}; float w[4] = {0, 0, 0, 0}; };
using InflTab = std::unordered_map<std::uint32_t, Infl>;   // vertex index -> its four strongest influences (OGRE bone ids)

struct Skin {
    bool on = false;
    std::vector<ImportedBone> bones;
    std::unordered_map<int, int> boneOfId;   // OGRE bone id -> index into bones
    bool needRoot = false;                   // some vertex has no usable weight: it is tied to a root bone (identity)
    int boneCount = 0, clipCount = 0;
    int unweighted = 0, badAssign = 0, droppedInfl = 0, rigidSubmeshes = 0;
    int badParents = 0, loops = 0, dupNames = 0, singular = 0, unknownTracks = 0, badKeys = 0, emptyClips = 0;
};

Quat AxisAngleQ(double angle, double x, double y, double z) {
    const double l = std::sqrt(x * x + y * y + z * z);
    if (!(l > 1e-12) || !std::isfinite(l) || !std::isfinite(angle)) return Quat{0, 0, 0, 1};
    const double h = angle * 0.5, sn = std::sin(h) / l;
    return Quat{(float)(x * sn), (float)(y * sn), (float)(z * sn), (float)std::cos(h)};
}

// attribute as a number, `dflt` when it is missing; false when it is there and is not a number
bool NumOr(const detail::xml::FlatDoc& d, int i, std::string_view key, double dflt, double& v) {
    std::string_view s;
    if (!d.Attr(i, key, s)) { v = dflt; return true; }
    return detail::rc::ParseNumber(s.data(), s.data() + s.size(), v);
}
bool XYZ(const detail::xml::FlatDoc& d, int e, double dflt, double v[3]) {
    return NumOr(d, e, "x", dflt, v[0]) && NumOr(d, e, "y", dflt, v[1]) && NumOr(d, e, "z", dflt, v[2]);
}
// <rotation angle="..."><axis x y z/></rotation> (also <rotate>): false only on a bad number
bool ReadRot(const detail::xml::FlatDoc& d, int e, Quat& q) {
    double a = 0, ax[3] = {0, 0, 1};
    if (!NumOr(d, e, "angle", 0, a)) return false;
    const int axn = d.First(e, "axis");
    if (axn >= 0 && !XYZ(d, axn, 0, ax)) return false;
    q = AxisAngleQ(a, ax[0], ax[1], ax[2]);
    return true;
}
bool ReadScale(const detail::xml::FlatDoc& d, int e, Vec3& s) {
    double v[3], f = 1;
    if (!XYZ(d, e, 1, v) || !NumOr(d, e, "factor", 1, f)) return false;
    s = Vec3{(float)(v[0] * f), (float)(v[1] * f), (float)(v[2] * f)};
    return true;
}

struct SkelBone { int id = 0; std::string name; Vec3 t{0, 0, 0}; Quat r{0, 0, 0, 1}; Vec3 s{1, 1, 1}; };

// Reads a skeleton into `out` (nodes after the root, clips) and `skin` (bones with inverse bind matrices). On false nothing was
// added to `out` and `why` says what is wrong; the caller then imports the mesh rigid.
bool LoadSkeleton(std::string text, ImportedModel& out, Skin& skin, std::string& why) {
    bool converted = false;
    if (!detail::txt::NormalizeEncoding(text, converted, why, kMaxFileBytes)) return false;
    detail::xml::FlatDoc d;
    std::string xerr;
    if (!detail::xml::ParseFlat(text, d, xerr)) { why = xerr; return false; }
    if (d.Name(0) != "skeleton") { why = "not an OGRE skeleton (the root element is <" + std::string(d.Name(0)) + ">, not <skeleton>)"; return false; }
    const int bn = d.First(0, "bones");
    if (bn < 0) { why = "the skeleton has no <bones>"; return false; }
    std::vector<SkelBone> bones;
    std::unordered_map<int, int> byId;
    std::unordered_map<std::string, int> byName;
    for (int e = d.Kid(bn); e >= 0; e = d.Next(e)) {
        if (d.Name(e) != "bone") continue;
        if (bones.size() >= kMaxBones) { why = "too many bones"; return false; }
        SkelBone b;
        double id = (double)bones.size();
        if (!NumOr(d, e, "id", id, id) || id < 0 || id > 1e9 || id != std::floor(id)) { why = "bad bone id '" + d.AttrOr(e, "id", "") + "'"; return false; }
        b.id = (int)id;
        b.name = d.AttrOr(e, "name", "");
        if (byId.count(b.id)) { why = "two bones have the id " + std::to_string(b.id); return false; }
        double v[3];
        const int pe = d.First(e, "position");
        if (pe >= 0) { if (!XYZ(d, pe, 0, v)) { why = "bad bone position"; return false; } b.t = Vec3{(float)v[0], (float)v[1], (float)v[2]}; }
        const int re = d.First(e, "rotation");
        if (re >= 0 && !ReadRot(d, re, b.r)) { why = "bad bone rotation"; return false; }
        const int se = d.First(e, "scale");
        if (se >= 0 && !ReadScale(d, se, b.s)) { why = "bad bone scale"; return false; }
        byId[b.id] = (int)bones.size();
        if (!b.name.empty() && !byName.count(b.name)) byName[b.name] = (int)bones.size();
        bones.push_back(std::move(b));
    }
    if (bones.empty()) { why = "the skeleton has no bones"; return false; }
    const std::size_t nb = bones.size();
    std::vector<int> par(nb, -1);
    int badParents = 0;
    const int bh = d.First(0, "bonehierarchy");
    if (bh >= 0) for (int e = d.Kid(bh); e >= 0; e = d.Next(e)) {
        if (d.Name(e) != "boneparent") continue;
        auto c = byName.find(d.AttrOr(e, "bone", "")), p = byName.find(d.AttrOr(e, "parent", ""));
        if (c == byName.end() || p == byName.end() || c->second == p->second) { ++badParents; continue; }
        par[(std::size_t)c->second] = p->second;
    }
    std::vector<int> order;
    const int loops = detail::txt::OrderParentsFirst(par, order);

    // nodes (parent first) and global bind matrices
    std::vector<ImportedNode> nodes;
    std::vector<std::string> nodeName(nb);
    std::vector<int> nodeOf(nb, 0);
    std::vector<Mat4> abs(nb);
    std::unordered_map<std::string, int> used;
    used[out.nodes.empty() ? "OGRE" : out.nodes[0].name] = 1;
    int dupNames = 0;
    const int base = (int)out.nodes.size();
    for (int bi : order) {
        const SkelBone& b = bones[(std::size_t)bi];
        std::string nm = b.name.empty() ? "bone" + std::to_string(b.id) : b.name;
        int& u = used[nm];
        if (u++) { ++dupNames; std::string cand; do { cand = nm + "_" + std::to_string(u++); } while (used.count(cand)); nm = cand; used[nm] = 1; }
        nodeName[(std::size_t)bi] = nm;
        ImportedNode nd;
        nd.name = nm;
        nd.parent = par[(std::size_t)bi] < 0 ? 0 : nodeOf[(std::size_t)par[(std::size_t)bi]];
        nd.position = b.t; nd.rotation = b.r; nd.scale = b.s;
        const Mat4 l = Mat4::TRS(b.t, b.r, b.s);
        abs[(std::size_t)bi] = par[(std::size_t)bi] < 0 ? l : abs[(std::size_t)par[(std::size_t)bi]] * l;
        nodeOf[(std::size_t)bi] = base + (int)nodes.size();
        nodes.push_back(std::move(nd));
    }
    for (const Mat4& m : abs) for (int k = 0; k < 16; ++k) if (!std::isfinite(m.m[k]) || std::fabs(m.m[k]) > 1e30f) { why = "the bone transforms multiply out to values that are too large"; return false; }

    // animations: keyframes are deltas from the bind pose
    std::vector<ImportedClip> clips;
    std::size_t keyTotal = 0;
    int unknownTracks = 0, badKeys = 0, emptyClips = 0;
    const int an = d.First(0, "animations");
    if (an >= 0) for (int a = d.Kid(an); a >= 0; a = d.Next(a)) {
        if (d.Name(a) != "animation") continue;
        if (clips.size() + (std::size_t)emptyClips >= kMaxAnims) { why = "too many animations"; return false; }
        ImportedClip clip;
        clip.name = d.AttrOr(a, "name", "");
        if (clip.name.empty()) clip.name = "animation" + std::to_string(clips.size());
        const int tr = d.First(a, "tracks");
        if (tr >= 0) for (int t = d.Kid(tr); t >= 0; t = d.Next(t)) {
            if (d.Name(t) != "track") continue;
            auto bi = byName.find(d.AttrOr(t, "bone", ""));
            if (bi == byName.end()) { ++unknownTracks; continue; }
            const SkelBone& b = bones[(std::size_t)bi->second];
            const int kf = d.First(t, "keyframes");
            if (kf < 0) continue;
            struct K { float time; Vec3 p; Quat q; Vec3 s; };
            std::vector<K> ks;
            bool anyT = false, anyR = false, anyS = false;
            for (int k = d.Kid(kf); k >= 0; k = d.Next(k)) {
                if (d.Name(k) != "keyframe") continue;
                if (++keyTotal > kMaxKeys) { why = "too many keyframes"; return false; }
                double tm;
                if (!NumOr(d, k, "time", 0, tm) || tm < 0) { ++badKeys; continue; }
                K key{(float)tm, b.t, b.r, b.s};
                const int te = d.First(k, "translate"), ro = d.First(k, "rotate"), sc = d.First(k, "scale");
                double v[3];
                if (te >= 0) { if (!XYZ(d, te, 0, v)) { ++badKeys; continue; } key.p = Vec3{(float)(b.t.x + v[0]), (float)(b.t.y + v[1]), (float)(b.t.z + v[2])}; anyT = true; }
                if (ro >= 0) { Quat q; if (!ReadRot(d, ro, q)) { ++badKeys; continue; } key.q = b.r * q; anyR = true; }
                if (sc >= 0) { Vec3 sv; if (!ReadScale(d, sc, sv)) { ++badKeys; continue; } key.s = Vec3{b.s.x * sv.x, b.s.y * sv.y, b.s.z * sv.z}; anyS = true; }
                ks.push_back(key);
            }
            if (ks.empty()) continue;
            std::stable_sort(ks.begin(), ks.end(), [](const K& x, const K& y) { return x.time < y.time; });
            ImportedChannel ch;
            ch.node = nodeName[(std::size_t)bi->second];
            for (const K& key : ks) {
                if (anyT) ch.position.push_back({key.time, key.p});
                if (anyS) ch.scale.push_back({key.time, key.s});
                if (anyR) {
                    Quat q = key.q.Normalized();
                    if (!ch.rotation.empty() && Quat::Dot(q, ch.rotation.back().value) < 0.0f) q = Quat{-q.x, -q.y, -q.z, -q.w};   // stay on one side so interpolation takes the short way
                    ch.rotation.push_back({key.time, q});
                }
            }
            if (!ch.position.empty() || !ch.rotation.empty() || !ch.scale.empty()) clip.channels.push_back(std::move(ch));
        }
        if (clip.channels.empty()) ++emptyClips; else clips.push_back(std::move(clip));
    }

    // commit
    skin.on = true;
    skin.boneCount = (int)nb;
    skin.clipCount = (int)clips.size();
    skin.bones.resize(nb);
    for (std::size_t i = 0; i < nb; ++i) {
        ImportedBone& ib = skin.bones[i];
        ib.name = nodeName[i];
        if (detail::rc::Singular3(abs[i])) { ib.inverseBind = Mat4::Identity(); ++skin.singular; } else ib.inverseBind = abs[i].Inverse();
        skin.boneOfId[bones[i].id] = (int)i;
    }
    for (ImportedNode& nd : nodes) out.nodes.push_back(std::move(nd));
    for (ImportedClip& c : clips) out.clips.push_back(std::move(c));
    skin.badParents = badParents; skin.loops = loops; skin.dupNames = dupNames; skin.unknownTracks = unknownTracks; skin.badKeys = badKeys; skin.emptyClips = emptyClips;
    return true;
}

// <boneassignments>: vertexboneassignment vertexindex / boneindex / weight. Keeps the four strongest per vertex (a bone named twice
// adds up). A bad entry is counted and skipped.
void ReadAssign(const detail::xml::FlatDoc& d, int ba, std::int64_t vcount, InflTab& tab, Skin& skin) {
    for (int e = d.Kid(ba); e >= 0; e = d.Next(e)) {
        if (d.Name(e) != "vertexboneassignment") continue;
        double vi, bi, w;
        if (!Num(d, e, "vertexindex", vi) || !Num(d, e, "boneindex", bi) || !Num(d, e, "weight", w) ||
            vi < 0 || vi >= (double)vcount || vi != std::floor(vi) || bi < 0 || bi > 1e9 || bi != std::floor(bi)) { ++skin.badAssign; continue; }
        if (!(w > 0)) continue;
        Infl& in = tab[(std::uint32_t)vi];
        int same = -1, empty = -1, weakest = -1;
        for (int k = 0; k < 4; ++k) {
            if (in.idx[k] == (int)bi) { same = k; break; }
            if (in.idx[k] < 0) { if (empty < 0) empty = k; }
            else if (weakest < 0 || in.w[k] < in.w[weakest]) weakest = k;
        }
        if (same >= 0) in.w[same] += (float)w;
        else if (empty >= 0) { in.idx[empty] = (int)bi; in.w[empty] = (float)w; }
        else if ((float)w > in.w[weakest]) { in.idx[weakest] = (int)bi; in.w[weakest] = (float)w; ++skin.droppedInfl; }
        else ++skin.droppedInfl;
    }
}

// ---- shared by the XML and the binary mesh: one submesh into one part ----
struct Build {
    explicit Build(ImportedModel& o) : out(o) {}
    ImportedModel& out;
    std::string baseDir;
    std::map<std::string, Mat> mats;
    std::map<std::string, int> matOut;
    std::map<int, std::string> names;   // submesh index -> name (<submeshnames> / the binary name table)
    Skin skin;
    std::string skelWhy;   // why a linked skeleton was not read (empty: no skeleton was supplied)
    std::size_t outVerts = 0;
    int extraUv = 0, colours = 0, tangents = 0, boneAssign = 0, lines = 0, noGeom = 0, degenerate = 0, badIdx = 0;
};

// A strip alternates its winding: triangle i is (s[i], s[i+1], s[i+2]) for even i and (s[i+1], s[i], s[i+2]) for odd i;
// a fan shares its first vertex: (s[0], s[i+1], s[i+2]). Repeated indices make degenerate triangles, which BuildMesh drops.
void ExpandSequence(const std::vector<std::uint32_t>& seq, bool strip, std::vector<std::uint32_t>& faceIdx) {
    for (std::size_t i = 0; i + 2 < seq.size(); ++i) {
        if (strip) { faceIdx.push_back(seq[i + (i & 1)]); faceIdx.push_back(seq[i + 1 - (i & 1)]); faceIdx.push_back(seq[i + 2]); }
        else { faceIdx.push_back(seq[0]); faceIdx.push_back(seq[i + 1]); faceIdx.push_back(seq[i + 2]); }
    }
}

// The triangles of submesh `myIdx` (three indices into `g` each) as one part with its material. False only on a hard error
// (too many output vertices, a vertex out of range); a submesh with nothing to draw is counted and skipped.
bool AddSubmesh(Build& b, const Geom& g, const std::vector<std::uint32_t>& faceIdx, int myIdx, const std::string& matName, bool hasBoneAssign, std::string* err, const InflTab* infl = nullptr) {
    detail::rc::Soup soup;
    std::vector<std::uint32_t> orig;   // soup position index -> vertex index of the geometry
    const std::size_t nv = (std::size_t)g.count;
    // positions are the geometry's; only the vertices the faces use are copied (a submesh may use a few of a big shared buffer)
    std::unordered_map<std::uint32_t, std::uint32_t> remap;
    bool anyUv = false, anyNrm = false;
    for (std::size_t t = 0; t + 2 < faceIdx.size(); t += 3) {
        if (faceIdx[t] >= nv || faceIdx[t + 1] >= nv || faceIdx[t + 2] >= nv) { ++b.badIdx; continue; }
        for (int c = 0; c < 3; ++c) {
            const std::uint32_t v = faceIdx[t + (std::size_t)c];
            auto r = remap.emplace(v, (std::uint32_t)soup.pos.size());
            if (r.second) { soup.pos.push_back(g.pos[v]); orig.push_back(v); }
            soup.ci.push_back(r.first->second);
            soup.cuv.push_back(g.uv[v]);
            soup.cnrm.push_back(g.nrm[v]);
            if (g.hasUv[v]) anyUv = true;
            if (g.hasNrm[v]) anyNrm = true;
        }
    }
    if (soup.ci.empty()) { ++b.noGeom; return true; }
    if (!anyUv) soup.cuv.clear();
    const bool allNrm = anyNrm && [&] { for (std::size_t c = 0; c < soup.ci.size(); ++c) { const Vec3& n = soup.cnrm[c]; if (n.x == 0 && n.y == 0 && n.z == 0) return false; } return true; }();
    if (!allNrm) soup.cnrm.clear();
    ImportedPart part;
    detail::rc::BuildStats st;
    std::vector<std::uint32_t> srcPos;
    if (!detail::rc::BuildMesh(soup, 180.0, part.mesh, &st, kMaxOutVerts - std::min(b.outVerts, kMaxOutVerts), infl ? &srcPos : nullptr)) return Fail(err, "too many output vertices");
    b.degenerate += st.degenerate;
    b.outVerts += part.mesh.vertices.size();
    if (part.mesh.triangles.empty()) return true;
    if (!detail::rc::PartInRange(part)) return Fail(err, "a vertex is out of range");
    if (infl && b.skin.on) {   // skin weights follow the vertices through the welding
        const int rootIdx = (int)b.skin.bones.size();
        part.bones = b.skin.bones;
        part.jointIdx.reserve(part.mesh.vertices.size());
        part.jointWt.reserve(part.mesh.vertices.size());
        for (std::size_t o = 0; o < part.mesh.vertices.size(); ++o) {
            std::array<int, 4> ji{0, 0, 0, 0};
            std::array<float, 4> jw{0, 0, 0, 0};
            int cnt = 0;
            float used = 0;
            auto it = infl->find(orig[srcPos[o]]);
            if (it != infl->end()) for (int k = 0; k < 4; ++k) {
                if (it->second.idx[k] < 0) continue;
                auto bi = b.skin.boneOfId.find(it->second.idx[k]);
                if (bi == b.skin.boneOfId.end()) { ++b.skin.badAssign; continue; }
                ji[(std::size_t)cnt] = bi->second; jw[(std::size_t)cnt] = it->second.w[k]; used += it->second.w[k]; ++cnt;
            }
            if (cnt == 0 || !(used > 0.0f) || !std::isfinite(used)) { b.skin.needRoot = true; ++b.skin.unweighted; ji = {rootIdx, 0, 0, 0}; jw = {1, 0, 0, 0}; }
            else for (float& w : jw) w /= used;
            part.jointIdx.push_back(ji);
            part.jointWt.push_back(jw);
        }
    } else if (b.skin.on) ++b.skin.rigidSubmeshes;
    std::string pname = matName.empty() ? "submesh" + std::to_string(myIdx) : matName;
    { auto nm = b.names.find(myIdx); if (nm != b.names.end()) pname = nm->second; }
    part.mesh.name = pname;
    part.node = 0;
    if (!matName.empty()) {
        auto it = b.matOut.find(matName);
        if (it == b.matOut.end()) {
            ImportedMaterial im;
            im.name = matName;
            auto mi = b.mats.find(matName);
            if (mi != b.mats.end()) {
                const Mat& m = mi->second;
                im.hasDiffuse = m.hasDiffuse; im.diffuse = m.diffuse; im.hasEmissive = m.hasEmissive; im.emissive = m.emissive; im.shininess = m.shininess;
                if (!m.texture.empty()) {
                    std::string t = m.texture;
                    for (char& c : t) if (c == '\\') c = '/';
                    if (!b.baseDir.empty() && t[0] != '/' && t.find(':') == std::string::npos) t = b.baseDir + t;
                    im.texture = t;
                }
            }
            b.out.materials.push_back(im);
            it = b.matOut.emplace(matName, (int)b.out.materials.size() - 1).first;
        }
        part.material = it->second;
    }
    if (hasBoneAssign) ++b.boneAssign;
    b.out.parts.push_back(std::move(part));
    return true;
}

// What the file holds that was not read. `skeleton` is the linked skeleton's name (null: no link).
void AddNotes(Build& b, const std::string* skeleton, bool meshBoneAssign, bool posesOrAnims, bool lod, bool noScripts) {
    auto note = [&](const std::string& s) { b.out.notes.push_back("OGRE: " + s); };
    if (skeleton && !b.skin.on) note("skeleton '" + *skeleton + "' (and the bone assignments of " + std::to_string(b.boneAssign + (meshBoneAssign ? 1 : 0)) + " mesh part(s)) not read" + (b.skelWhy.empty() ? "" : " (" + b.skelWhy + ")") + ": the mesh is imported at its bind pose, rigid");
    else if (!skeleton && meshBoneAssign) note("bone assignments without a skeleton link not read");
    if (b.skin.on) {
        const Skin& k = b.skin;
        note("skeleton read: " + std::to_string(k.boneCount) + " bone(s), " + std::to_string(k.clipCount) + " animation(s); keys play with linear interpolation (the interpolation modes of the file are not read)");
        if (k.rigidSubmeshes) note(std::to_string(k.rigidSubmeshes) + " submesh(es) have no bone assignments and stay rigid at node 0");
        if (k.unweighted) note(std::to_string(k.unweighted) + " vertex(es) without a usable bone weight are tied to a root bone (they do not move)");
        if (k.badAssign) note(std::to_string(k.badAssign) + " bone assignment(s) with a bad number, a vertex out of range or an unknown bone skipped");
        if (k.droppedInfl) note(std::to_string(k.droppedInfl) + " bone influence(s) beyond the four strongest per vertex dropped");
        if (k.badParents) note(std::to_string(k.badParents) + " bone parent link(s) naming an unknown bone skipped");
        if (k.loops) note(std::to_string(k.loops) + " bone parent loop(s) cut");
        if (k.dupNames) note(std::to_string(k.dupNames) + " bone name(s) repeated; renamed");
        if (k.singular) note(std::to_string(k.singular) + " bone(s) with a flat bind transform got an identity inverse bind");
        if (k.unknownTracks) note(std::to_string(k.unknownTracks) + " animation track(s) naming an unknown bone skipped");
        if (k.badKeys) note(std::to_string(k.badKeys) + " keyframe(s) with a bad number or a negative time skipped");
        if (k.emptyClips) note(std::to_string(k.emptyClips) + " animation(s) without keys skipped");
    }
    if (posesOrAnims) note("morph / pose animations of the mesh are not read");
    if (lod) note("levels of detail are not read");
    if (b.lines) note(std::to_string(b.lines) + " submesh(es) that are not triangle lists, strips or fans (lines, points) skipped");
    if (b.noGeom) note(std::to_string(b.noGeom) + " submesh(es) without geometry or faces skipped");
    if (b.extraUv) note(std::to_string(b.extraUv) + " texture coordinate set(s) after the first are not imported");
    if (b.colours) note(std::to_string(b.colours) + " vertex colour(s) not imported");
    if (b.tangents) note("stored tangents / binormals are not imported");
    if (b.badIdx) note(std::to_string(b.badIdx) + " triangle(s) with an index past the vertex list skipped");
    if (b.degenerate) note(std::to_string(b.degenerate) + " degenerate triangle(s) skipped");
    if (noScripts) note("no .material script was supplied: materials carry their names only");
    note("coordinates, winding and texture coordinates as written; the material scripts' first pass colours and first texture unit texture are read, nothing else of them");
}

// The .material scripts next to a mesh file (a few, bounded): the material names of the submeshes are looked up in them.
// The script named like the mesh comes first, then the others in name order; at most 32 are read. `suffixLen` is the length
// of the mesh file's double extension (".mesh.xml" = 9, ".mesh" = 5).
std::vector<std::string> ScriptsNextTo(const std::string& path, std::size_t suffixLen, std::string& dir) {
    namespace fs = std::filesystem;
    std::vector<std::string> scripts;
    const std::size_t sl = path.find_last_of("/\\");
    dir.clear();
    if (sl != std::string::npos) dir = path.substr(0, sl + 1);
    std::error_code ec;
    std::vector<fs::path> found;
    std::string stem = path.substr(sl == std::string::npos ? 0 : sl + 1);
    if (stem.size() >= suffixLen) stem.resize(stem.size() - suffixLen);
    stem = detail::txt::Lower(stem);
    for (fs::directory_iterator it(fs::u8path(dir.empty() ? std::string(".") : dir), ec), end; !ec && it != end && found.size() < 4000; it.increment(ec)) {
        std::error_code e2;
        if (!it->is_regular_file(e2)) continue;
        if (detail::txt::Lower(it->path().extension().string()) == ".material") found.push_back(it->path());
    }
    std::sort(found.begin(), found.end(), [&](const fs::path& a, const fs::path& b) {
        const bool sa = detail::txt::Lower(a.stem().string()) == stem, sb = detail::txt::Lower(b.stem().string()) == stem;
        if (sa != sb) return sa;
        return a < b;
    });
    for (const fs::path& p : found) {
        if (scripts.size() >= 32) break;
        std::string s, e3;
        if (detail::txt::ReadFileCapped(p.u8string(), 4u * 1024u * 1024u, s, &e3)) scripts.push_back(std::move(s));
    }
    return scripts;
}

// ---- the binary mesh (.mesh) ----
// Written from the public description of the OGRE binary mesh file (the chunk list of the OGRE manual's "Mesh" chapter and
// its serializer notes) and checked byte by byte against the three binary meshes of Assimp's test folder, whose XML twins
// the XML reader above reads; no code from OGRE or any importer. The file is a 16-bit header id (0x1000) and a version
// line, then chunks: a 16-bit id and a 32-bit length. The lengths cannot be trusted to skip a chunk that holds text (the
// serializer leaves strings out of some of them), so the known chunks are read field by field in the order the format
// writes them, and a chunk id that is not a child of the chunk being read ends that chunk.
namespace bm {

enum : std::uint16_t {
    kHeader = 0x1000, kMesh = 0x3000, kSubmesh = 0x4000, kSubOp = 0x4010, kSubBone = 0x4100, kSubAlias = 0x4200,
    kGeometry = 0x5000, kDecl = 0x5100, kElement = 0x5110, kVBuffer = 0x5200, kVBufferData = 0x5210,
    kSkeletonLink = 0x6000, kMeshBone = 0x7000, kLod = 0x8000, kBounds = 0x9000, kNameTable = 0xA000, kNameEntry = 0xA100,
    kEdgeLists = 0xB000, kPoses = 0xC000, kAnimations = 0xD000, kExtremes = 0xE000,
};
// vertex element semantics and types (the numbers of the format)
enum { kSemPosition = 1, kSemBlendWeights = 2, kSemBlendIndices = 3, kSemNormal = 4, kSemDiffuse = 5, kSemSpecular = 6, kSemTexCoord = 7, kSemBinormal = 8, kSemTangent = 9 };
enum { kFloat1 = 0, kFloat2 = 1, kFloat3 = 2, kFloat4 = 3 };
constexpr std::size_t kMaxStr = 4096;
constexpr std::uint32_t kMaxIndices = 3u * 20000000u;

// counts per vertex (as the XML reader does), saturating
void AddCount(int& c, std::size_t n) { c = (int)std::min<std::size_t>((std::size_t)c + n, 1000000000u); }

int TypeSize(std::uint16_t t) {
    switch (t) {
        case 0: return 4; case 1: return 8; case 2: return 12; case 3: return 16;    // float1..4
        case 4: return 4;                                                           // packed colour
        case 5: return 2; case 6: return 4; case 7: return 6; case 8: return 8;     // short1..4
        case 9: return 4; case 10: return 4; case 11: return 4;                     // ubyte4, ARGB, ABGR colour
        default: return -1;
    }
}

struct Elem { std::uint16_t source, type, semantic, offset, index; };

struct Reader {
    detail::bin::Br r;
    std::string err;
    explicit Reader(const std::string& s) : r(s) {}
    bool Bad(const std::string& m) { if (err.empty()) err = m; return false; }
    // a text field: bytes up to a line feed (consumed)
    bool Line(std::string& s) {
        std::size_t e = 0;
        while (r.pos + e < r.n && e < kMaxStr && r.d[r.pos + e] != '\n') ++e;
        if (r.pos + e >= r.n || e >= kMaxStr) return Bad("a text field has no end");
        s.assign((const char*)r.d + r.pos, e);
        if (!s.empty() && s.back() == '\r') s.pop_back();
        r.pos += e + 1;
        return true;
    }
    // the next chunk header without moving (false at the end of the file)
    bool Peek(std::uint16_t& id, std::uint32_t& len) {
        if (r.Left() < 6) return false;
        const std::size_t at = r.pos;
        id = r.U16(); len = r.U32();
        r.pos = at;
        return true;
    }
    void Header() { r.Skip(6); }
    // skips a chunk this reader does not read, by its length (which counts the 6-byte header)
    bool SkipChunk(std::uint32_t len) {
        if (len < 6 || len > r.Left()) return Bad("a chunk's length runs past the end of the file");
        r.Skip(len);
        return !r.bad;
    }
};

// <geometry>: the vertex count, the declaration and the buffers it names
bool ReadGeometry(Reader& R, Geom& g, Build& b) {
    const std::uint32_t vc = R.r.U32();
    if (R.r.bad) return R.Bad("the geometry is cut off");
    if (vc > (std::uint32_t)kMaxVerts) return R.Bad("too many vertices (" + std::to_string(vc) + ")");
    std::vector<Elem> elems;
    struct Buf { std::uint16_t bind, size; std::size_t at; };
    std::vector<Buf> bufs;
    std::uint16_t id; std::uint32_t len;
    while (R.Peek(id, len)) {
        if (id == kDecl) {
            R.Header();
            while (R.Peek(id, len) && id == kElement) {
                R.Header();
                Elem e{R.r.U16(), R.r.U16(), R.r.U16(), R.r.U16(), R.r.U16()};
                if (R.r.bad) return R.Bad("a vertex element is cut off");
                if (elems.size() >= 64) return R.Bad("too many vertex elements");
                elems.push_back(e);
            }
        } else if (id == kVBuffer) {
            R.Header();
            Buf bf{R.r.U16(), R.r.U16(), 0};
            std::uint16_t did; std::uint32_t dlen;
            if (R.r.bad || !R.Peek(did, dlen) || did != kVBufferData) return R.Bad("a vertex buffer has no data");
            R.Header();
            if (!R.r.Fits(vc, bf.size)) return R.Bad("a vertex buffer runs past the end of the file");
            bf.at = R.r.pos;
            R.r.Skip((std::uint64_t)vc * bf.size);
            if (bufs.size() >= 64) return R.Bad("too many vertex buffers");
            bufs.push_back(bf);
        } else break;
    }
    // the positions must be in a buffer before anything is allocated from the count (the buffers were checked to fit the file,
    // and a position takes 12 bytes of its vertex, so the arrays below are bounded by the file's size)
    if (vc > 0) {
        bool backed = false;
        for (const Elem& e : elems) {
            if (e.semantic != kSemPosition || e.type < 2 || e.type > kFloat4) continue;
            for (const Buf& x : bufs) if (x.bind == e.source && (std::size_t)e.offset + (std::size_t)TypeSize(e.type) <= x.size) backed = true;
        }
        if (!backed) return R.Bad("the geometry has no 32-bit float positions inside a vertex buffer");
    }
    g.count = vc;
    const std::size_t n = vc;
    g.pos.assign(n, Vec3{0, 0, 0}); g.nrm.assign(n, Vec3{0, 0, 0}); g.uv.assign(n, Vec2{0, 0});
    g.hasPos.assign(n, 0); g.hasNrm.assign(n, 0); g.hasUv.assign(n, 0);
    bool havePos = false;
    for (const Elem& e : elems) {
        const Buf* bf = nullptr;
        for (const Buf& x : bufs) if (x.bind == e.source) bf = &x;
        const int ts = TypeSize(e.type);
        if (!bf) { if (e.semantic == kSemPosition) return R.Bad("the positions name a vertex buffer the geometry does not have"); continue; }
        if (ts < 0 || (std::size_t)e.offset + (std::size_t)ts > bf->size) return R.Bad("a vertex element lies outside its vertex (type " + std::to_string(e.type) + ")");
        if (e.semantic == kSemDiffuse || e.semantic == kSemSpecular) { AddCount(b.colours, n); continue; }
        if (e.semantic == kSemTangent || e.semantic == kSemBinormal) { AddCount(b.tangents, n); continue; }
        if (e.semantic == kSemTexCoord && e.index > 0) { AddCount(b.extraUv, n); continue; }
        if (e.semantic != kSemPosition && e.semantic != kSemNormal && e.semantic != kSemTexCoord) continue;   // blend weights and indices: the skin is not read
        const int comps = e.type <= kFloat4 ? e.type + 1 : 0;
        if (comps == 0) {
            if (e.semantic == kSemPosition) return R.Bad("positions of type " + std::to_string(e.type) + " are not read (only 32-bit floats)");
            continue;
        }
        if (e.semantic != kSemTexCoord && comps < 3) return R.Bad("a position or normal with fewer than 3 components");
        const unsigned char* base = R.r.d + bf->at + e.offset;
        for (std::size_t k = 0; k < n; ++k) {
            float f[3] = {0, 0, 0};
            std::memcpy(f, base + k * bf->size, (std::size_t)std::min(comps, 3) * 4);
            if (!std::isfinite(f[0]) || !std::isfinite(f[1]) || !std::isfinite(f[2])) return R.Bad("vertex " + std::to_string(k) + " holds a value that is not a finite number");
            if (e.semantic == kSemPosition) { g.pos[k] = Vec3{f[0], f[1], f[2]}; g.hasPos[k] = 1; }
            else if (e.semantic == kSemNormal) { g.nrm[k] = Vec3{f[0], f[1], f[2]}; g.hasNrm[k] = 1; }
            else { g.uv[k] = Vec2{f[0], comps >= 2 ? f[1] : 0.0f}; g.hasUv[k] = 1; }
        }
        if (e.semantic == kSemPosition) havePos = true;
    }
    if (n > 0 && !havePos) return R.Bad("the geometry has no positions");
    return true;
}

struct Sub {
    std::string material;
    bool shared = false, ownGeom = false, boneAssign = false;
    std::uint16_t op = 4;   // triangle list unless an operation chunk says otherwise
    std::vector<std::uint32_t> idx;
    Geom geom;
};

bool ReadSubmesh(Reader& R, Sub& s, Build& b) {
    if (!R.Line(s.material)) return false;
    s.shared = R.r.U8() != 0;
    const std::uint32_t ic = R.r.U32();
    const bool i32 = R.r.U8() != 0;
    if (R.r.bad) return R.Bad("a submesh is cut off");
    if (ic > kMaxIndices) return R.Bad("too many indices (" + std::to_string(ic) + ")");
    if (!R.r.Fits(ic, i32 ? 4 : 2)) return R.Bad("a submesh's index list runs past the end of the file");
    s.idx.resize(ic);
    for (std::uint32_t i = 0; i < ic; ++i) s.idx[i] = i32 ? R.r.U32() : R.r.U16();
    std::uint16_t id; std::uint32_t len;
    if (!s.shared) {
        if (!R.Peek(id, len) || id != kGeometry) return R.Bad("a submesh without shared vertices has no geometry");
        R.Header();
        if (!ReadGeometry(R, s.geom, b)) return false;
        s.ownGeom = true;
    }
    while (R.Peek(id, len)) {
        if (id == kSubOp) { R.Header(); s.op = R.r.U16(); }
        else if (id == kSubBone) { R.Header(); R.r.Skip(10); s.boneAssign = true; }   // vertex (32), bone (16), weight (float)
        else if (id == kSubAlias) { R.Header(); std::string a, t; if (!R.Line(a) || !R.Line(t)) return false; }
        else break;
        if (R.r.bad) return R.Bad("a submesh chunk is cut off");
    }
    return true;
}

} // namespace bm

} // namespace ogre
} // namespace

using SkeletonLoader = std::function<bool(const std::string& linkName, std::string& text, std::string& why)>;

static bool ReadOgreImpl(std::string text, const std::string& baseDir, ImportedModel& out, std::string* err, const std::vector<std::string>* scripts, const SkeletonLoader* loadSkeleton = nullptr) {
    using namespace ogre;
    bool converted = false;
    std::string why;
    if (!detail::txt::NormalizeEncoding(text, converted, why, kMaxFileBytes)) return Fail(err, why);
    detail::xml::FlatDoc doc;   // offsets into `text`, which lives to the end of this function
    std::string xerr;
    if (!detail::xml::ParseFlat(text, doc, xerr)) return Fail(err, xerr);
    if (doc.Name(0) != "mesh") return Fail(err, "not an OGRE mesh (the root element is <" + std::string(doc.Name(0)) + ">, not <mesh>)");

    Build b(out);
    b.baseDir = baseDir;
    if (scripts) for (const std::string& s : *scripts) ParseScript(s, b.mats);

    Geom shared;
    bool haveShared = false;
    const int sg = doc.First(0, "sharedgeometry");
    if (sg >= 0) {
        if (!ReadGeometry(doc, sg, shared, err, b.extraUv, b.colours, b.tangents)) return false;
        haveShared = true;
    }
    const int sm = doc.First(0, "submeshes");
    if (sm < 0) return Fail(err, "the file has no <submeshes>");

    ImportedNode root;
    root.name = "OGRE";
    out.nodes.push_back(root);

    const int skel = doc.First(0, "skeletonlink");
    const std::string skelName = skel >= 0 ? doc.AttrOr(skel, "name", "") : std::string();
    if (skel >= 0 && loadSkeleton && *loadSkeleton) {
        std::string stext, why;
        if (!(*loadSkeleton)(skelName, stext, why) || !LoadSkeleton(std::move(stext), out, b.skin, why)) b.skelWhy = why.empty() ? "it could not be read" : why;
    }
    InflTab meshAssign;   // <boneassignments> of the mesh: for the shared vertices
    if (b.skin.on) { const int mba = doc.First(0, "boneassignments"); if (mba >= 0) ReadAssign(doc, mba, shared.count, meshAssign, b.skin); }

    {   // <submeshnames>: index -> name
        const int sn = doc.First(0, "submeshnames");
        if (sn >= 0) for (int e = doc.Kid(sn); e >= 0; e = doc.Next(e)) {
            double ix;
            if (doc.Name(e) == "submeshname" && Num(doc, e, "index", ix) && ix >= 0 && ix < 1e9 && b.names.size() < kMaxSubmeshes) b.names[(int)ix] = doc.AttrOr(e, "name", "");
        }
    }
    int idx = 0;
    for (int s = doc.Kid(sm); s >= 0; s = doc.Next(s)) {
        if (doc.Name(s) != "submesh") continue;
        if ((std::size_t)idx >= kMaxSubmeshes) return Fail(err, "too many submeshes");
        const int myIdx = idx++;
        const std::string op = detail::txt::Lower(doc.AttrOr(s, "operationtype", "triangle_list"));
        if (op != "triangle_list" && op != "triangle_strip" && op != "triangle_fan") { ++b.lines; continue; }
        const std::string us = detail::txt::Lower(doc.AttrOr(s, "usesharedvertices", "false"));
        const bool useShared = us == "true" || us == "1";
        Geom own;
        const Geom* g = &own;
        if (useShared) {
            if (!haveShared) return Fail(err, "submesh " + std::to_string(myIdx) + " uses shared vertices but the file has no <sharedgeometry>");
            g = &shared;
        } else {
            const int gi = doc.First(s, "geometry");
            if (gi < 0) { ++b.noGeom; continue; }
            if (!ReadGeometry(doc, gi, own, err, b.extraUv, b.colours, b.tangents)) return false;
        }
        const int fi = doc.First(s, "faces");
        if (fi < 0) { ++b.noGeom; continue; }
        double fc;
        if (!Num(doc, fi, "count", fc) || fc < 0 || fc > (double)kMaxFaces || fc != std::floor(fc)) return Fail(err, "bad faces count '" + doc.AttrOr(fi, "count", "") + "'");
        std::vector<std::uint32_t> faceIdx;   // 3 per triangle
        std::size_t seen = 0;
        const bool strip = op == "triangle_strip", fan = op == "triangle_fan";
        std::vector<std::uint32_t> seq;   // strip and fan: the index sequence, the first face's indices then each later face's newest one
        auto toIdx = [](double v) { return (std::uint32_t)std::min(v, 4294967295.0); };
        for (int f = doc.Kid(fi); f >= 0; f = doc.Next(f)) {
            if (doc.Name(f) != "face") continue;
            if (++seen > (std::size_t)fc) return Fail(err, "more <face> elements than the faces count " + std::to_string((std::int64_t)fc));
            double v[3] = {0, 0, 0};
            bool has[3] = {false, false, false};
            static const char* const names[3] = {"v1", "v2", "v3"};
            for (int k = 0; k < 3; ++k) {
                std::string_view at;
                if (!doc.Attr(f, names[k], at)) continue;
                if (!detail::rc::ParseNumber(at.data(), at.data() + at.size(), v[k]) || v[k] < 0 || v[k] != std::floor(v[k])) return Fail(err, "bad <face> in submesh " + std::to_string(myIdx));
                has[k] = true;
            }
            if (!strip && !fan) {
                if (!has[0] || !has[1] || !has[2]) return Fail(err, "bad <face> in submesh " + std::to_string(myIdx));
                faceIdx.push_back(toIdx(v[0])); faceIdx.push_back(toIdx(v[1])); faceIdx.push_back(toIdx(v[2]));
                continue;
            }
            // A strip or fan face element may carry all three indices of its triangle (v3 is the new one) or only the new index
            // (the files that write the sequence once). The first face starts the sequence; every later one adds its last index.
            if (!has[0] && !has[1] && !has[2]) return Fail(err, "bad <face> in submesh " + std::to_string(myIdx));
            if (seq.empty()) { for (int k = 0; k < 3; ++k) if (has[k]) seq.push_back(toIdx(v[k])); }
            else { const int last = has[2] ? 2 : has[1] ? 1 : 0; seq.push_back(toIdx(v[last])); }
        }
        if (strip || fan) ExpandSequence(seq, strip, faceIdx);
        InflTab subAssign;
        const InflTab* infl = nullptr;
        const int sba = doc.First(s, "boneassignments");
        if (b.skin.on) {
            if (sba >= 0) { ReadAssign(doc, sba, g->count, subAssign, b.skin); infl = &subAssign; }
            else if (useShared && !meshAssign.empty()) infl = &meshAssign;
        }
        if (!AddSubmesh(b, *g, faceIdx, myIdx, doc.AttrOr(s, "material", ""), sba >= 0, err, infl)) return false;
    }
    if (out.parts.empty()) return Fail(err, "no submesh has both vertices and triangles");
    out.backend = "own";

    if (b.skin.on) {
        if (b.skin.needRoot) { ImportedBone rb; rb.name = out.nodes[0].name; rb.inverseBind = Mat4::Identity(); for (ImportedPart& p : out.parts) if (!p.bones.empty()) p.bones.push_back(rb); }
    }
    AddNotes(b, skel >= 0 ? &skelName : nullptr, doc.First(0, "boneassignments") >= 0, doc.First(0, "poses") >= 0 || doc.First(0, "animations") >= 0,
             doc.First(0, "levelofdetail") >= 0, !scripts || scripts->empty());
    return true;
}

static bool ReadOgreBinImpl(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err, const std::vector<std::string>* scripts) {
    using namespace ogre;
    if (bytes.size() > kMaxFileBytes) return Fail(err, "the file is larger than " + std::to_string(kMaxFileBytes >> 20) + " MB");
    if (bytes.size() >= 2 && (unsigned char)bytes[0] == 0x10 && (unsigned char)bytes[1] == 0x00) return Fail(err, "a big-endian binary mesh (not read)");
    bm::Reader R(bytes);
    std::string version;
    if (R.r.U16() != bm::kHeader || !R.Line(version)) return Fail(err, "not an OGRE binary mesh (no header)");
    if (version.rfind("[MeshSerializer_v", 0) != 0) return Fail(err, "not an OGRE binary mesh (version '" + version.substr(0, 40) + "')");
    // v1.8 and later write the layout read here; the older ones (1.4 and before) differ in the parts that hold levels of
    // detail and animation, which are not read anyway, and are accepted with a note.
    std::uint16_t id; std::uint32_t len;
    if (!R.Peek(id, len) || id != bm::kMesh) return Fail(err, "the file has no mesh chunk");
    R.Header();
    R.r.U8();   // "skeletally animated": the skeleton link says the same

    Build b(out);
    b.baseDir = baseDir;
    if (scripts) for (const std::string& s : *scripts) ParseScript(s, b.mats);
    Geom shared;
    bool haveShared = false, meshBone = false, poses = false, lod = false, cutShort = false;
    std::string skeleton;
    bool haveSkeleton = false;
    std::vector<bm::Sub> subs;
    while (R.Peek(id, len)) {
        if (id == bm::kSubmesh) {
            R.Header();
            if (subs.size() >= kMaxSubmeshes) return Fail(err, "too many submeshes");
            subs.emplace_back();
            if (!bm::ReadSubmesh(R, subs.back(), b)) return Fail(err, R.err);
        } else if (id == bm::kGeometry) {
            R.Header();
            if (haveShared) return Fail(err, "two shared geometries");
            if (!bm::ReadGeometry(R, shared, b)) return Fail(err, R.err);
            haveShared = true;
        } else if (id == bm::kSkeletonLink) {
            R.Header();
            if (!R.Line(skeleton)) return Fail(err, R.err);
            haveSkeleton = true;
        } else if (id == bm::kMeshBone) {
            R.Header(); R.r.Skip(10); meshBone = true;
        } else if (id == bm::kBounds) {
            R.Header(); R.r.Skip(28);   // box and radius: worked out from the vertices instead
        } else if (id == bm::kNameTable) {
            R.Header();
            while (R.Peek(id, len) && id == bm::kNameEntry) {
                R.Header();
                const std::uint16_t ix = R.r.U16();
                std::string nm;
                if (R.r.bad || !R.Line(nm)) return Fail(err, R.err.empty() ? "a submesh name is cut off" : R.err);
                if (b.names.size() < kMaxSubmeshes) b.names[ix] = nm;
            }
        } else {
            // levels of detail, edge lists, poses, animations, extremes and anything newer: skipped by their length; a length
            // that does not fit ends the reading there (the submeshes before it are kept, with a note)
            if (id == bm::kLod) lod = true;
            if (id == bm::kPoses || id == bm::kAnimations) poses = true;
            if (!R.SkipChunk(len)) { cutShort = true; break; }
        }
        if (R.r.bad) return Fail(err, "a chunk is cut off");
    }
    if (subs.empty()) return Fail(err, "the file has no submeshes");

    ImportedNode root;
    root.name = "OGRE";
    out.nodes.push_back(root);
    for (std::size_t i = 0; i < subs.size(); ++i) {
        bm::Sub& s = subs[i];
        const int myIdx = (int)i;
        if (s.op != 4 && s.op != 5 && s.op != 6) { ++b.lines; continue; }   // 1 points, 2 line list, 3 line strip
        const Geom* g = &s.geom;
        if (s.shared) {
            if (!haveShared) return Fail(err, "submesh " + std::to_string(myIdx) + " uses shared vertices but the file has no shared geometry");
            g = &shared;
        }
        std::vector<std::uint32_t> faceIdx;
        if (s.op == 4) { faceIdx = std::move(s.idx); faceIdx.resize(faceIdx.size() / 3 * 3); }
        else ExpandSequence(s.idx, s.op == 5, faceIdx);
        if (!AddSubmesh(b, *g, faceIdx, myIdx, s.material, s.boneAssign, err)) return false;
        s.geom = Geom{};   // freed as soon as its part is built
    }
    if (out.parts.empty()) return Fail(err, "no submesh has both vertices and triangles");
    out.backend = "own";
    AddNotes(b, haveSkeleton ? &skeleton : nullptr, meshBone, poses, lod, !scripts || scripts->empty());
    if (cutShort) out.notes.push_back("OGRE: a chunk after the submeshes has a length past the end of the file: the rest of the file was not read");
    if (version != "[MeshSerializer_v1.8]" && version != "[MeshSerializer_v1.10]" && version != "[MeshSerializer_v1.100]")
        out.notes.push_back("OGRE: binary mesh version '" + version.substr(0, 40) + "' was not checked against a real file (1.8 was)");
    return true;
}

bool ReadOgreXmlBuffer(const std::string& text, const std::string& baseDir, ImportedModel& out, std::string* err, const std::vector<std::string>* materialScripts, const std::string* skeletonXml) {
    out = ImportedModel{};
    SkeletonLoader loader;
    if (skeletonXml) loader = [skeletonXml](const std::string&, std::string& t, std::string&) { t = *skeletonXml; return true; };
    const bool ok = ReadOgreImpl(text, baseDir, out, err, materialScripts, skeletonXml ? &loader : nullptr);
    if (!ok) out = ImportedModel{};
    return ok;
}

bool ReadOgreXmlFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, ogre::kMaxFileBytes, bytes, err)) return false;
    std::string dir;
    std::vector<std::string> scripts = ogre::ScriptsNextTo(path, 9, dir);   // ".mesh.xml"
    // the bytes are handed over, not copied (ReadOgreXmlBuffer copies the caller's text)
    out = ImportedModel{};
    // the linked skeleton, "<name>.xml" next to the mesh (name is "x.skeleton": the file is x.skeleton.xml), only inside the mesh's folder
    SkeletonLoader loader = [&dir](const std::string& link, std::string& t, std::string& why) {
        std::string joined, e1, e2;
        if (link.empty()) { why = "the skeleton link has no name"; return false; }
        if (!detail::ResolveInsideDir(dir, link + ".xml", joined, e1)) { why = e1; return false; }
        if (!detail::txt::ReadFileCapped(joined, 64u * 1024u * 1024u, t, &e2)) { why = "no readable " + link + ".xml next to the mesh"; return false; }
        return true;
    };
    const bool ok = ReadOgreImpl(std::move(bytes), dir, out, err, &scripts, &loader);
    if (!ok) out = ImportedModel{};
    return ok;
}

bool ReadOgreBinaryBuffer(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err, const std::vector<std::string>* materialScripts) {
    out = ImportedModel{};
    const bool ok = ReadOgreBinImpl(bytes, baseDir, out, err, materialScripts);
    if (!ok) out = ImportedModel{};
    return ok;
}

bool ReadOgreBinaryFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, ogre::kMaxFileBytes, bytes, err)) return false;
    std::string dir;
    std::vector<std::string> scripts = ogre::ScriptsNextTo(path, 5, dir);   // ".mesh"
    const bool ok = ReadOgreBinImpl(bytes, dir, out, err, &scripts);
    if (!ok) out = ImportedModel{};
    return ok;
}

} // namespace okay
