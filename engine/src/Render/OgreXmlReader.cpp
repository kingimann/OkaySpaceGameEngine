// Our own OGRE XML mesh (.mesh.xml) reader. Written from the public description of the OGRE mesh XML format (the file the
// OgreXMLConverter tool and the exporters write: <mesh> with <sharedgeometry>, <submeshes>, <vertexbuffer> blocks of
// <vertex> elements, <faces>) and of the OGRE material script language (the .material files that name the colours and the
// texture of a submesh's material); no code from any other importer. One part per submesh. Uses the strict XML reader of the
// batch-B readers. Bounds-checked, capped, no recursion. See okay/Render/ModelData.hpp and docs/content/model_readers.md.
#include "BinReader.hpp"
#include "ReaderCommon.hpp"
#include "TextReaderUtil.hpp"
#include "SceneRefPath.hpp"
#include "XmlLite.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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

// ---- skeletons (.skeleton.xml) ----
// Written from the public description of the OGRE skeleton XML: <bones> of <bone id name> with <position>, <rotation angle>
// (radians) holding an <axis>, and <scale>; <bonehierarchy> of <boneparent bone parent> (names); <animations> of <animation
// name length> holding <tracks> of <track bone> holding <keyframes> of <keyframe time> with <translate>, <rotate> and <scale>.
// A keyframe is relative to the bone's bind pose (OGRE resets the node to its bind pose, then translates, rotates and
// scales it by the key), so position = bind + translate, rotation = bind * rotate, scale = bind * scale. Not checked against
// a real OGRE file.
constexpr std::size_t kMaxBones = 20000, kMaxClips = 20000, kMaxKeys = 4000000, kMaxAssign = 40000000;

struct SkBone { std::string name; long id = 0; Vec3 pos{0, 0, 0}; Quat rot{0, 0, 0, 1}; Vec3 scl{1, 1, 1}; int parent = -1; Mat4 world; };
struct Skel {
    std::vector<SkBone> bones;                 // parent first
    std::map<long, int> byId;
    std::vector<ImportedClip> clips;
    std::vector<std::string> notes;
};

// attribute as a number, `dflt` when missing; false when present but not a number
bool NumOr(const detail::xml::FlatDoc& d, int i, const char* key, double dflt, double& v) {
    std::string_view s;
    if (!d.Attr(i, key, s)) { v = dflt; return true; }
    return detail::rc::ParseNumber(s.data(), s.data() + s.size(), v);
}
bool ReadVec(const detail::xml::FlatDoc& d, int e, double dflt, Vec3& out) {
    double x, y, z;
    if (!NumOr(d, e, "x", dflt, x) || !NumOr(d, e, "y", dflt, y) || !NumOr(d, e, "z", dflt, z)) return false;
    out = Vec3{(float)x, (float)y, (float)z};
    return true;
}
// a <rotate> / <rotation> element: angle in radians and an <axis> child (no axis or a zero axis: no rotation)
bool ReadRot(const detail::xml::FlatDoc& d, int e, Quat& q) {
    double a;
    if (!NumOr(d, e, "angle", 0, a)) return false;
    q = Quat{0, 0, 0, 1};
    const int ax = d.First(e, "axis");
    if (ax < 0) return true;
    Vec3 v;
    if (!ReadVec(d, ax, 0, v)) return false;
    const double l = std::sqrt((double)v.x * v.x + (double)v.y * v.y + (double)v.z * v.z);
    if (!(l > 1e-12) || a == 0) return true;
    const double sn = std::sin(a * 0.5) / l;
    q = Quat{(float)(v.x * sn), (float)(v.y * sn), (float)(v.z * sn), (float)std::cos(a * 0.5)}.Normalized();
    return true;
}
// <scale x y z/> or <scale factor="f"/> (x, y, z default to the factor)
bool ReadScale(const detail::xml::FlatDoc& d, int e, Vec3& v) {
    double f;
    if (!NumOr(d, e, "factor", 1, f)) return false;
    return ReadVec(d, e, f, v);
}

// A skeleton as either form gives it, before ordering: the bones in file order, each one's parent (a file index, -1 for none),
// and the clips with their keys still relative to the bind pose. FinishSkeleton turns it into a Skel.
struct RawKey { double time; Vec3 t; Quat r; Vec3 s; };
struct RawTrack { int bone; std::vector<RawKey> keys; };   // bone: index into RawSkel::file
struct RawClip { std::string name; std::vector<RawTrack> tracks; };
struct RawSkel {
    std::vector<SkBone> file;
    std::vector<int> par;
    std::vector<RawClip> clips;
    std::vector<std::string> notes;
};

// parent first (depth-first from the roots, children in file order, no recursion), world bind poses, and the keys made
// absolute: position = bind + translate, rotation = bind * rotate, scale = bind * scale
bool FinishSkeleton(RawSkel& raw, Skel& sk, std::string* err) {
    const std::vector<SkBone>& file = raw.file;
    const std::vector<int>& par = raw.par;
    if (file.empty()) return Fail(err, "the skeleton has no bones");
    std::vector<std::vector<int>> kids(file.size());
    std::vector<int> roots;
    for (std::size_t i = 0; i < file.size(); ++i) { if (par[i] < 0) roots.push_back((int)i); else kids[(std::size_t)par[i]].push_back((int)i); }
    std::vector<int> order, stack, newIdx(file.size(), -1);
    for (std::size_t r = 0; r < roots.size(); ++r) {
        stack.push_back(roots[r]);
        while (!stack.empty()) {
            const int i = stack.back(); stack.pop_back();
            newIdx[(std::size_t)i] = (int)order.size();
            order.push_back(i);
            for (std::size_t k = kids[(std::size_t)i].size(); k-- > 0;) stack.push_back(kids[(std::size_t)i][k]);
        }
    }
    if (order.size() != file.size()) return Fail(err, "the bone hierarchy has a loop");
    for (const int i : order) {
        SkBone b = file[(std::size_t)i];
        b.parent = par[(std::size_t)i] < 0 ? -1 : newIdx[(std::size_t)par[(std::size_t)i]];
        const Mat4 local = Mat4::TRS(b.pos, b.rot, b.scl);
        b.world = b.parent < 0 ? local : sk.bones[(std::size_t)b.parent].world * local;
        sk.byId[b.id] = (int)sk.bones.size();
        sk.bones.push_back(std::move(b));
    }
    for (const SkBone& b : sk.bones) for (int k = 0; k < 16; ++k) if (!std::isfinite(b.world.m[k]) || std::fabs(b.world.m[k]) > 1e30f) return Fail(err, "the bone transforms multiply out to values that are too large");

    for (RawClip& rc : raw.clips) {
        ImportedClip clip;
        clip.name = rc.name;
        for (RawTrack& t : rc.tracks) {
            if (t.keys.empty()) continue;
            const SkBone& bind = file[(std::size_t)t.bone];
            ImportedChannel ch;
            ch.node = bind.name;
            std::stable_sort(t.keys.begin(), t.keys.end(), [](const RawKey& x, const RawKey& y) { return x.time < y.time; });
            for (const RawKey& k : t.keys) {
                ch.position.push_back({(float)k.time, Vec3{bind.pos.x + k.t.x, bind.pos.y + k.t.y, bind.pos.z + k.t.z}});
                ch.rotation.push_back({(float)k.time, (bind.rot * k.r).Normalized()});
                ch.scale.push_back({(float)k.time, Vec3{bind.scl.x * k.s.x, bind.scl.y * k.s.y, bind.scl.z * k.s.z}});
            }
            clip.channels.push_back(std::move(ch));
        }
        sk.clips.push_back(std::move(clip));
    }
    raw.clips.clear();
    for (std::string& n : raw.notes) sk.notes.push_back(std::move(n));
    return true;
}

bool ParseSkeletonXml(std::string text, Skel& sk, std::string* err) {
    bool converted = false;
    std::string why;
    if (!detail::txt::NormalizeEncoding(text, converted, why, kMaxFileBytes)) return Fail(err, why);
    detail::xml::FlatDoc doc;
    std::string xerr;
    if (!detail::xml::ParseFlat(text, doc, xerr)) return Fail(err, xerr);
    if (doc.Name(0) != "skeleton") return Fail(err, "not an OGRE skeleton (the root element is <" + std::string(doc.Name(0)) + ">, not <skeleton>)");
    const int bs = doc.First(0, "bones");
    if (bs < 0) return Fail(err, "the skeleton has no <bones>");

    RawSkel raw;
    std::vector<SkBone>& file = raw.file;
    std::map<std::string, int> byName;
    std::map<long, int> idSeen;
    for (int b = doc.Kid(bs); b >= 0; b = doc.Next(b)) {
        if (doc.Name(b) != "bone") continue;
        if (file.size() >= kMaxBones) return Fail(err, "too many bones");
        SkBone k;
        double id;
        if (!NumOr(doc, b, "id", (double)file.size(), id) || id < 0 || id > 1e9 || id != std::floor(id)) return Fail(err, "bad bone id '" + doc.AttrOr(b, "id", "") + "'");
        k.id = (long)id;
        k.name = doc.AttrOr(b, "name", "");
        if (k.name.empty()) return Fail(err, "a bone has no name");
        if (!byName.emplace(k.name, (int)file.size()).second) return Fail(err, "two bones are named '" + k.name + "'");
        if (!idSeen.emplace(k.id, (int)file.size()).second) return Fail(err, "two bones have the id " + std::to_string(k.id));
        const int pe = doc.First(b, "position"), re = doc.First(b, "rotation"), se = doc.First(b, "scale");
        if (pe >= 0 && !ReadVec(doc, pe, 0, k.pos)) return Fail(err, "bad <position> of bone '" + k.name + "'");
        if (re >= 0 && !ReadRot(doc, re, k.rot)) return Fail(err, "bad <rotation> of bone '" + k.name + "'");
        if (se >= 0 && !ReadScale(doc, se, k.scl)) return Fail(err, "bad <scale> of bone '" + k.name + "'");
        file.push_back(std::move(k));
    }
    if (file.empty()) return Fail(err, "the skeleton has no bones");
    raw.par.assign(file.size(), -1);
    const int bh = doc.First(0, "bonehierarchy");
    if (bh >= 0) for (int e = doc.Kid(bh); e >= 0; e = doc.Next(e)) {
        if (doc.Name(e) != "boneparent") continue;
        const std::string cn = doc.AttrOr(e, "bone", ""), pn = doc.AttrOr(e, "parent", "");
        const auto c = byName.find(cn), p = byName.find(pn);
        if (c == byName.end() || p == byName.end()) return Fail(err, "<boneparent> names a bone the skeleton does not have ('" + cn + "' under '" + pn + "')");
        if (raw.par[(std::size_t)c->second] >= 0) return Fail(err, "bone '" + cn + "' has two parents");
        raw.par[(std::size_t)c->second] = p->second;
    }

    // animations
    std::size_t keys = 0;
    int shown = 0;
    const int an = doc.First(0, "animations");
    if (an >= 0) for (int a = doc.Kid(an); a >= 0; a = doc.Next(a)) {
        if (doc.Name(a) != "animation") continue;
        if (raw.clips.size() >= kMaxClips) return Fail(err, "too many animations");
        RawClip clip;
        clip.name = doc.AttrOr(a, "name", "");
        const int tr = doc.First(a, "tracks");
        if (tr >= 0) for (int t = doc.Kid(tr); t >= 0; t = doc.Next(t)) {
            if (doc.Name(t) != "track") continue;
            const std::string bn = doc.AttrOr(t, "bone", "");
            const auto bi = byName.find(bn);
            if (bi == byName.end()) {
                if (shown++ < 8) raw.notes.push_back("OGRE: animation '" + clip.name + "' has a track for bone '" + bn + "', which the skeleton does not have (skipped)");
                continue;
            }
            RawTrack track;
            track.bone = bi->second;
            const int kf = doc.First(t, "keyframes");
            if (kf >= 0) for (int e = doc.Kid(kf); e >= 0; e = doc.Next(e)) {
                if (doc.Name(e) != "keyframe") continue;
                if (++keys > kMaxKeys) return Fail(err, "too many keyframes");
                double tm;
                if (!NumOr(doc, e, "time", 0, tm) || tm < 0) return Fail(err, "bad keyframe time '" + doc.AttrOr(e, "time", "") + "' in animation '" + clip.name + "'");
                RawKey k{tm, Vec3{0, 0, 0}, Quat{0, 0, 0, 1}, Vec3{1, 1, 1}};
                const int te = doc.First(e, "translate"), re = doc.First(e, "rotate"), se = doc.First(e, "scale");
                if (te >= 0 && !ReadVec(doc, te, 0, k.t)) return Fail(err, "bad <translate> in animation '" + clip.name + "'");
                if (re >= 0 && !ReadRot(doc, re, k.r)) return Fail(err, "bad <rotate> in animation '" + clip.name + "'");
                if (se >= 0 && !ReadScale(doc, se, k.s)) return Fail(err, "bad <scale> in animation '" + clip.name + "'");
                track.keys.push_back(k);
            }
            clip.tracks.push_back(std::move(track));
        }
        raw.clips.push_back(std::move(clip));
    }
    return FinishSkeleton(raw, sk, err);
}

// the bones as nodes under the root (node 0, already in `out`), then the clips and notes
void AddSkeletonToModel(const Skel& sk, ImportedModel& out) {
    const int base = (int)out.nodes.size();
    for (const SkBone& b : sk.bones) {
        ImportedNode n;
        n.name = b.name; n.position = b.pos; n.rotation = b.rot; n.scale = b.scl;
        n.parent = b.parent < 0 ? 0 : base + b.parent;
        out.nodes.push_back(n);
    }
    for (const ImportedClip& c : sk.clips) out.clips.push_back(c);
    for (const std::string& n : sk.notes) out.notes.push_back(n);
}

// Vertex bone assignments of one geometry, sorted by vertex (stable, so file order breaks ties)
struct Asg { std::uint32_t v; int bone; float w; };
struct Skin {
    const Skel* sk = nullptr;
    std::vector<Asg> shared;   // the mesh-level <boneassignments> (the shared geometry's)
    int bad = 0, dropped = 0, singular = 0;
};
bool ReadAssignments(const detail::xml::FlatDoc& d, int ba, std::int64_t count, Skin& skin, std::vector<Asg>& out, std::string* err) {
    if (ba < 0) return true;
    for (int e = d.Kid(ba); e >= 0; e = d.Next(e)) {
        if (d.Name(e) != "vertexboneassignment") continue;
        if (out.size() >= kMaxAssign) return Fail(err, "too many bone assignments");
        double v, bi, w;
        if (!Num(d, e, "vertexindex", v) || !Num(d, e, "boneindex", bi) || !NumOr(d, e, "weight", 1, w) || v < 0 || v != std::floor(v) || bi < 0 || bi != std::floor(bi)) { ++skin.bad; continue; }
        const auto it = skin.sk->byId.find((long)std::min(bi, 2e9));
        if (v >= (double)count || it == skin.sk->byId.end() || !(w > 0)) { ++skin.bad; continue; }
        out.push_back(Asg{(std::uint32_t)v, it->second, (float)w});
    }
    std::stable_sort(out.begin(), out.end(), [](const Asg& a, const Asg& b) { return a.v < b.v; });
    return true;
}

// The binary form's assignments (vertex u32, bone u16, weight f32 per record) as read, checked against the skeleton here
struct RawAsg { std::uint32_t v; std::uint16_t bone; float w; };
bool FilterAssignments(const std::vector<RawAsg>& raw, std::int64_t count, Skin& skin, std::vector<Asg>& out) {
    for (const RawAsg& r : raw) {
        const auto it = skin.sk->byId.find((long)r.bone);
        if ((std::int64_t)r.v >= count || it == skin.sk->byId.end() || !(r.w > 0)) { ++skin.bad; continue; }
        out.push_back(Asg{r.v, it->second, r.w});
    }
    std::stable_sort(out.begin(), out.end(), [](const Asg& a, const Asg& b) { return a.v < b.v; });
    return true;
}

// Skin `part` (made from soup positions `src`, soup position -> geometry vertex `orig`) from the assignments `asg`.
void SkinPart(Skin& skin, ImportedPart& part, const std::vector<std::uint32_t>& src, const std::vector<std::uint32_t>& orig, const std::vector<Asg>& asg) {
    const Skel& sk = *skin.sk;
    for (const SkBone& b : sk.bones) {
        ImportedBone ib;
        ib.name = b.name;
        if (detail::rc::Singular3(b.world)) { ib.inverseBind = Mat4::Identity(); ++skin.singular; } else ib.inverseBind = b.world.Inverse();
        part.bones.push_back(ib);
    }
    int rootBone = -1;
    part.jointIdx.assign(part.mesh.vertices.size(), std::array<int, 4>{0, 0, 0, 0});
    part.jointWt.assign(part.mesh.vertices.size(), std::array<float, 4>{0, 0, 0, 0});
    for (std::size_t i = 0; i < part.mesh.vertices.size(); ++i) {
        const std::uint32_t gv = orig[src[i]];
        auto lo = std::lower_bound(asg.begin(), asg.end(), gv, [](const Asg& a, std::uint32_t v) { return a.v < v; });
        std::vector<std::pair<int, float>> w;   // the same bone twice adds up
        for (; lo != asg.end() && lo->v == gv; ++lo) {
            bool merged = false;
            for (auto& p : w) if (p.first == lo->bone) { p.second += lo->w; merged = true; break; }
            if (!merged) w.emplace_back(lo->bone, lo->w);
        }
        std::stable_sort(w.begin(), w.end(), [](const std::pair<int, float>& a, const std::pair<int, float>& b) { return a.second > b.second; });
        if (w.size() > 4) { ++skin.dropped; w.resize(4); }
        double sum = 0;
        for (const auto& p : w) sum += p.second;
        if (w.empty() || !(sum > 0) || !std::isfinite(sum)) {   // no usable weight: follow the root node
            if (rootBone < 0) { ImportedBone rb; rb.name = "OGRE"; rb.inverseBind = Mat4::Identity(); part.bones.push_back(rb); rootBone = (int)part.bones.size() - 1; }
            part.jointIdx[i][0] = rootBone; part.jointWt[i][0] = 1.0f;
            continue;
        }
        for (std::size_t k = 0; k < w.size(); ++k) { part.jointIdx[i][k] = w[k].first; part.jointWt[i][k] = (float)(w[k].second / sum); }
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
bool AddSubmesh(Build& b, const Geom& g, const std::vector<std::uint32_t>& faceIdx, int myIdx, const std::string& matName, bool hasBoneAssign, std::string* err,
                Skin* skin = nullptr, const std::vector<Asg>* asg = nullptr) {
    detail::rc::Soup soup;
    const std::size_t nv = (std::size_t)g.count;
    // positions are the geometry's; only the vertices the faces use are copied (a submesh may use a few of a big shared buffer)
    std::unordered_map<std::uint32_t, std::uint32_t> remap;
    std::vector<std::uint32_t> orig, srcPos;   // soup position -> geometry vertex; output vertex -> soup position
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
    if (!detail::rc::BuildMesh(soup, 180.0, part.mesh, &st, kMaxOutVerts - std::min(b.outVerts, kMaxOutVerts), skin ? &srcPos : nullptr)) return Fail(err, "too many output vertices");
    b.degenerate += st.degenerate;
    b.outVerts += part.mesh.vertices.size();
    if (part.mesh.triangles.empty()) return true;
    if (!detail::rc::PartInRange(part)) return Fail(err, "a vertex is out of range");
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
    if (skin && asg) SkinPart(*skin, part, srcPos, orig, *asg);
    if (hasBoneAssign) ++b.boneAssign;
    b.out.parts.push_back(std::move(part));
    return true;
}

// What the file holds that was not read. `skeleton` is the linked skeleton's name (null: no link).
void AddNotes(Build& b, const std::string* skeleton, bool meshBoneAssign, bool posesOrAnims, bool lod, bool noScripts, bool skinned = false) {
    auto note = [&](const std::string& s) { b.out.notes.push_back("OGRE: " + s); };
    if (skinned) {}
    else if (skeleton) note("skeleton '" + *skeleton + "' (and the bone assignments of " + std::to_string(b.boneAssign + (meshBoneAssign ? 1 : 0)) + " mesh part(s)) not read: the mesh is imported at its bind pose, rigid");
    else if (meshBoneAssign) note("bone assignments without a skeleton link not read");
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
    std::vector<RawAsg> asg;   // 0x4100 bone assignments
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
        else if (id == kSubBone) {   // vertex (32), bone (16), weight (float)
            R.Header();
            if (s.asg.size() >= kMaxAssign) return R.Bad("too many bone assignments");
            RawAsg a; a.v = R.r.U32(); a.bone = R.r.U16(); a.w = R.r.F32();
            if (R.r.bad) return R.Bad("a bone assignment is cut off");
            s.asg.push_back(a); s.boneAssign = true;
        }
        else if (id == kSubAlias) { R.Header(); std::string a, t; if (!R.Line(a) || !R.Line(t)) return false; }
        else break;
        if (R.r.bad) return R.Bad("a submesh chunk is cut off");
    }
    return true;
}

} // namespace bm

// ---- the binary skeleton (.skeleton) ----
// Written from the public description of the OGRE binary skeleton file (the chunk list of the skeleton serializer's file-format
// notes): no code from OGRE or any importer. The same framing as the binary mesh: a 16-bit header id (0x1000) and a version line
// ("[Serializer_v1.10]", or "[Serializer_v1.80]" which adds the blend mode chunk), then chunks of a 16-bit id and a 32-bit length
// that counts the 6-byte header. Strings end at a line feed. Chunks:
//   0x1010 blend mode: u16 (0 average, 1 cumulative; how a game mixes clips, not part of the import)
//   0x2000 bone: name, u16 handle, position (3 floats), orientation (x, y, z, w), and a scale (3 floats) only when the chunk's
//          length leaves room for it
//   0x3000 bone parent: u16 child handle, u16 parent handle
//   0x4000 animation: name, f32 length; followed by an optional 0x4010 base info (name, f32 time: the base pose of an additive
//          clip) and the 0x4100 tracks: u16 bone handle, followed by its 0x4110 keyframes: f32 time, rotation (x, y, z, w),
//          translation (3 floats), and a scale when the chunk's length leaves room for it
//   0x5000 animation link: name, f32 scale (another skeleton whose clips this one may share)
// The handles are the ids the mesh's bone assignments use, and the keyframes are relative to the bind pose, as in the XML form.
namespace sb {
enum : std::uint16_t { kHeader = 0x1000, kBlendMode = 0x1010, kBone = 0x2000, kParent = 0x3000, kAnim = 0x4000, kAnimBase = 0x4010,
                       kTrack = 0x4100, kKey = 0x4110, kLink = 0x5000 };
constexpr std::uint32_t kBoneNoScale = 6 + 2 + 12 + 16;   // header, handle, position, orientation (plus the name)
constexpr std::uint32_t kKeyNoScale = 6 + 4 + 16 + 12;    // header, time, rotation, translation

bool Vec(bm::Reader& R, Vec3& v) { v.x = R.r.F32(); v.y = R.r.F32(); v.z = R.r.F32(); return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool Rot(bm::Reader& R, Quat& q) {
    const float x = R.r.F32(), y = R.r.F32(), z = R.r.F32(), w = R.r.F32();
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(w)) return false;
    q = Quat{x, y, z, w}.Normalized();   // a zero quaternion becomes no turn
    return true;
}
} // namespace sb

bool ParseSkeletonBin(const std::string& bytes, Skel& sk, std::string* err) {
    if (bytes.size() > kMaxFileBytes) return Fail(err, "the file is larger than " + std::to_string(kMaxFileBytes >> 20) + " MB");
    if (bytes.size() >= 2 && (unsigned char)bytes[0] == 0x10 && (unsigned char)bytes[1] == 0x00) return Fail(err, "a big-endian binary skeleton (not read)");
    bm::Reader R(bytes);
    std::string version;
    if (R.r.U16() != sb::kHeader || !R.Line(version)) return Fail(err, "not an OGRE binary skeleton (no header)");
    if (version.rfind("[Serializer_v", 0) != 0) return Fail(err, "not an OGRE binary skeleton (version '" + version.substr(0, 40) + "')");

    RawSkel raw;
    std::map<long, int> byHandle;
    std::map<std::string, int> byName;
    struct Link { std::uint16_t child, parent; };
    std::vector<Link> links;
    struct PendingTrack { std::size_t clip; std::uint16_t handle; std::vector<RawKey> keys; };
    std::vector<PendingTrack> tracks;
    std::size_t keys = 0;
    bool cumulative = false, additive = false;
    std::vector<std::string> linked;
    std::uint16_t id; std::uint32_t len;
    while (R.Peek(id, len)) {
        if (id == sb::kBlendMode) {
            R.Header();
            cumulative = R.r.U16() == 1;
        } else if (id == sb::kBone) {
            R.Header();
            if (raw.file.size() >= kMaxBones) return Fail(err, "too many bones");
            SkBone k;
            if (!R.Line(k.name)) return Fail(err, R.err);
            if (k.name.empty()) return Fail(err, "a bone has no name");
            k.id = (long)R.r.U16();
            if (!sb::Vec(R, k.pos) || !sb::Rot(R, k.rot)) return Fail(err, R.r.bad ? "a bone is cut off" : "bone '" + k.name + "' has a number that is not finite");
            if ((std::uint64_t)len >= (std::uint64_t)sb::kBoneNoScale + k.name.size() + 1 + 12 && !sb::Vec(R, k.scl))
                return Fail(err, R.r.bad ? "a bone is cut off" : "bone '" + k.name + "' has a scale that is not finite");
            if (R.r.bad) return Fail(err, "a bone is cut off");
            if (!byName.emplace(k.name, (int)raw.file.size()).second) return Fail(err, "two bones are named '" + k.name + "'");
            if (!byHandle.emplace(k.id, (int)raw.file.size()).second) return Fail(err, "two bones have the handle " + std::to_string(k.id));
            raw.file.push_back(std::move(k));
        } else if (id == sb::kParent) {
            R.Header();
            Link l; l.child = R.r.U16(); l.parent = R.r.U16();
            if (links.size() >= kMaxBones * 2) return Fail(err, "too many bone parents");
            links.push_back(l);
        } else if (id == sb::kAnim) {
            R.Header();
            if (raw.clips.size() >= kMaxClips) return Fail(err, "too many animations");
            RawClip clip;
            if (!R.Line(clip.name)) return Fail(err, R.err);
            R.r.F32();   // the length: the keys give the clip's span
            const std::size_t ci = raw.clips.size();
            raw.clips.push_back(std::move(clip));
            while (R.Peek(id, len) && (id == sb::kAnimBase || id == sb::kTrack)) {
                R.Header();
                if (id == sb::kAnimBase) {
                    std::string base;
                    if (!R.Line(base)) return Fail(err, R.err);
                    R.r.F32();
                    additive = true;
                    continue;
                }
                PendingTrack t;
                t.clip = ci;
                t.handle = R.r.U16();
                while (R.Peek(id, len) && id == sb::kKey) {
                    R.Header();
                    if (++keys > kMaxKeys) return Fail(err, "too many keyframes");
                    RawKey k{0, Vec3{0, 0, 0}, Quat{0, 0, 0, 1}, Vec3{1, 1, 1}};
                    const float tm = R.r.F32();
                    const bool fin = sb::Rot(R, k.r) && sb::Vec(R, k.t) && (len < sb::kKeyNoScale + 12 || sb::Vec(R, k.s));
                    if (R.r.bad) return Fail(err, "a keyframe is cut off");
                    if (!fin || !std::isfinite(tm) || tm < 0) return Fail(err, "a keyframe of animation '" + raw.clips[ci].name + "' has a bad number");
                    k.time = tm;
                    t.keys.push_back(k);
                }
                if (R.r.bad) return Fail(err, "a track is cut off");
                tracks.push_back(std::move(t));
            }
        } else if (id == sb::kLink) {
            R.Header();
            std::string name;
            if (!R.Line(name)) return Fail(err, R.err);
            R.r.F32();
            if (linked.size() < 8) linked.push_back(name);
        } else {
            if (!R.SkipChunk(len)) return Fail(err, R.err);   // a chunk this reader does not know, skipped by its length
        }
        if (R.r.bad) return Fail(err, "a chunk is cut off");
    }
    if (R.r.Left() != 0) return Fail(err, "the file ends in the middle of a chunk header");
    if (raw.file.empty()) return Fail(err, "the skeleton has no bones");

    raw.par.assign(raw.file.size(), -1);
    for (const Link& l : links) {
        const auto c = byHandle.find(l.child), p = byHandle.find(l.parent);
        if (c == byHandle.end() || p == byHandle.end())
            return Fail(err, "a bone parent names a handle the skeleton does not have (" + std::to_string(l.child) + " under " + std::to_string(l.parent) + ")");
        if (raw.par[(std::size_t)c->second] >= 0) return Fail(err, "bone '" + raw.file[(std::size_t)c->second].name + "' has two parents");
        raw.par[(std::size_t)c->second] = p->second;
    }
    int shown = 0;
    for (PendingTrack& t : tracks) {
        const auto b = byHandle.find(t.handle);
        if (b == byHandle.end()) {
            if (shown++ < 8) raw.notes.push_back("OGRE: animation '" + raw.clips[t.clip].name + "' has a track for bone handle " + std::to_string(t.handle) + ", which the skeleton does not have (skipped)");
            continue;
        }
        raw.clips[t.clip].tracks.push_back(RawTrack{b->second, std::move(t.keys)});
    }
    if (cumulative) raw.notes.push_back("OGRE: the skeleton asks for cumulative clip blending; the clips are imported as they are, the blend mode is not kept");
    if (additive) raw.notes.push_back("OGRE: an animation names a base pose (an additive clip); it is imported relative to the bind pose, the base is not applied");
    for (const std::string& l : linked) raw.notes.push_back("OGRE: the clips of the linked skeleton '" + l + "' are not read");
    if (version != "[Serializer_v1.10]" && version != "[Serializer_v1.80]")
        raw.notes.push_back("OGRE: binary skeleton version '" + version.substr(0, 40) + "' was not checked against a real file");
    return FinishSkeleton(raw, sk, err);
}

// A skeleton in either form: the binary one starts with its 16-bit header id (either byte order), the XML one with text.
bool ParseSkeleton(const std::string& src, Skel& sk, std::string* err) {
    if (src.size() >= 2 && (((unsigned char)src[0] == 0x00 && (unsigned char)src[1] == 0x10) || ((unsigned char)src[0] == 0x10 && (unsigned char)src[1] == 0x00)))
        return ParseSkeletonBin(src, sk, err);
    return ParseSkeletonXml(src, sk, err);
}

// The file readers also look for the binary skeleton a mesh links to. On by default since 2026-11-23 (rule 19d: the own path is the
// default; Assimp read the binary skeleton too). OKAY_OWN_OGRE_SKELBIN=0 (or false / off / no) switches it off.
bool SkelBinSwitchOn() {
    const char* v = std::getenv("OKAY_OWN_OGRE_SKELBIN");
    if (!v || !*v) return true;
    const std::string s = detail::txt::Lower(v);
    return !(s == "0" || s == "false" || s == "off" || s == "no");
}

// The skeleton a mesh file links to, next to the mesh: `<link>.xml` first (the form the XML tools write), then, with the switch
// on, the binary `<link>` itself. A link that already names the XML file (`fish.skeleton.xml`, as the XML converter writes it
// into a .mesh.xml) is read as it is. False when none is there.
bool LoadLinkedSkeleton(const std::string& baseDir, const std::string& link, std::string& text) {
    std::string path, e2;
    if (detail::ResolveInsideDir(baseDir, link + ".xml", path, e2) && detail::txt::ReadFileCapped(path, kMaxFileBytes, text, &e2)) return true;
    text.clear();
    const bool namesXml = link.size() > 4 && detail::txt::Lower(link.substr(link.size() - 4)) == ".xml";
    path.clear(); e2.clear();
    if (namesXml && detail::ResolveInsideDir(baseDir, link, path, e2) && detail::txt::ReadFileCapped(path, kMaxFileBytes, text, &e2)) return true;
    text.clear();
    if (!SkelBinSwitchOn()) return false;
    path.clear(); e2.clear();
    if (detail::ResolveInsideDir(baseDir, link, path, e2) && detail::txt::ReadFileCapped(path, kMaxFileBytes, text, &e2)) return true;
    text.clear();
    return false;
}

} // namespace ogre
} // namespace

static bool ReadOgreImpl(std::string text, const std::string& baseDir, ImportedModel& out, std::string* err, const std::vector<std::string>* scripts,
                         const std::string* skeletonXml = nullptr, bool findSkeleton = false) {
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

    // the skeleton the mesh links to: given (XML or binary), or (file reads) the one next to the mesh (LoadLinkedSkeleton)
    Skel skelObj;
    Skin skin;
    {
        std::string skText;
        const std::string* skSrc = skeletonXml;
        const int link = doc.First(0, "skeletonlink");
        std::string why2;
        if (!skSrc && findSkeleton && link >= 0 && LoadLinkedSkeleton(baseDir, doc.AttrOr(link, "name", ""), skText)) skSrc = &skText;
        if (skSrc) {
            if (ParseSkeleton(*skSrc, skelObj, &why2)) { skin.sk = &skelObj; AddSkeletonToModel(skelObj, out); }
            else out.notes.push_back("OGRE: the skeleton could not be read (" + why2 + "): the mesh is imported rigid");
        }
    }
    std::vector<Asg> sharedAsg;
    bool sharedAsgRead = false;

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
        std::vector<Asg> ownAsg;
        const std::vector<Asg>* asgUse = nullptr;
        if (skin.sk) {
            if (useShared) {
                if (!sharedAsgRead) { sharedAsgRead = true; if (!ReadAssignments(doc, doc.First(0, "boneassignments"), shared.count, skin, sharedAsg, err)) return false; }
                asgUse = &sharedAsg;
            } else {
                if (!ReadAssignments(doc, doc.First(s, "boneassignments"), g->count, skin, ownAsg, err)) return false;
                asgUse = &ownAsg;
            }
        }
        if (!AddSubmesh(b, *g, faceIdx, myIdx, doc.AttrOr(s, "material", ""), doc.First(s, "boneassignments") >= 0, err, skin.sk ? &skin : nullptr, asgUse)) return false;
    }
    if (out.parts.empty()) return Fail(err, "no submesh has both vertices and triangles");
    out.backend = "own";

    const int skel = doc.First(0, "skeletonlink");
    const std::string skelName = skel >= 0 ? doc.AttrOr(skel, "name", "") : std::string();
    AddNotes(b, skel >= 0 ? &skelName : nullptr, doc.First(0, "boneassignments") >= 0, doc.First(0, "poses") >= 0 || doc.First(0, "animations") >= 0,
             doc.First(0, "levelofdetail") >= 0, !scripts || scripts->empty(), skin.sk != nullptr);
    if (skin.sk) {
        if (skin.bad) out.notes.push_back("OGRE: " + std::to_string(skin.bad) + " bone assignment(s) skipped (a vertex or bone that does not exist, or a weight that is not positive)");
        if (skin.dropped) out.notes.push_back("OGRE: " + std::to_string(skin.dropped) + " vertex(es) had more than 4 bone weights; the 4 strongest were kept");
        if (skin.singular) out.notes.push_back("OGRE: " + std::to_string(skin.singular) + " bone(s) with a flat scale have no inverse bind pose (the identity is used)");
    }
    return true;
}

static bool ReadOgreBinImpl(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err, const std::vector<std::string>* scripts,
                            const std::string* skeletonXml = nullptr, bool findSkeleton = false) {
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
    std::vector<RawAsg> sharedRaw;
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
            R.Header();
            if (sharedRaw.size() >= kMaxAssign) return Fail(err, "too many bone assignments");
            RawAsg a; a.v = R.r.U32(); a.bone = R.r.U16(); a.w = R.r.F32();
            if (R.r.bad) return Fail(err, "a bone assignment is cut off");
            sharedRaw.push_back(a); meshBone = true;
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
    // the skeleton the mesh links to: given (XML or binary), or (file reads) the one next to the mesh (LoadLinkedSkeleton)
    Skel skelObj;
    Skin skin;
    {
        std::string skText;
        const std::string* skSrc = skeletonXml;
        if (!skSrc && findSkeleton && haveSkeleton && LoadLinkedSkeleton(baseDir, skeleton, skText)) skSrc = &skText;
        std::string why2;
        if (skSrc) {
            if (ParseSkeleton(*skSrc, skelObj, &why2)) { skin.sk = &skelObj; AddSkeletonToModel(skelObj, out); }
            else out.notes.push_back("OGRE: the skeleton could not be read (" + why2 + "): the mesh is imported rigid");
        }
    }
    std::vector<Asg> sharedAsg;
    bool sharedAsgRead = false;
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
        std::vector<Asg> ownAsg;
        const std::vector<Asg>* asgUse = nullptr;
        if (skin.sk) {
            if (s.shared) {
                if (!sharedAsgRead) { sharedAsgRead = true; FilterAssignments(sharedRaw, shared.count, skin, sharedAsg); }
                asgUse = &sharedAsg;
            } else {
                FilterAssignments(s.asg, g->count, skin, ownAsg);
                asgUse = &ownAsg;
            }
        }
        if (!AddSubmesh(b, *g, faceIdx, myIdx, s.material, s.boneAssign, err, skin.sk ? &skin : nullptr, asgUse)) return false;
        s.geom = Geom{};   // freed as soon as its part is built
    }
    if (out.parts.empty()) return Fail(err, "no submesh has both vertices and triangles");
    out.backend = "own";
    AddNotes(b, haveSkeleton ? &skeleton : nullptr, meshBone, poses, lod, !scripts || scripts->empty(), skin.sk != nullptr);
    if (skin.sk) {
        if (skin.bad) out.notes.push_back("OGRE: " + std::to_string(skin.bad) + " bone assignment(s) skipped (a vertex or bone that does not exist, or a weight that is not positive)");
        if (skin.dropped) out.notes.push_back("OGRE: " + std::to_string(skin.dropped) + " vertex(es) had more than 4 bone weights; the 4 strongest were kept");
        if (skin.singular) out.notes.push_back("OGRE: " + std::to_string(skin.singular) + " bone(s) with a flat scale have no inverse bind pose (the identity is used)");
    }
    if (cutShort) out.notes.push_back("OGRE: a chunk after the submeshes has a length past the end of the file: the rest of the file was not read");
    if (version != "[MeshSerializer_v1.8]" && version != "[MeshSerializer_v1.10]" && version != "[MeshSerializer_v1.100]")
        out.notes.push_back("OGRE: binary mesh version '" + version.substr(0, 40) + "' was not checked against a real file (1.8 was)");
    return true;
}

bool ReadOgreSkeletonXml(const std::string& text, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    if (text.size() > ogre::kMaxFileBytes) { ogre::Fail(err, "the file is larger than " + std::to_string(ogre::kMaxFileBytes >> 20) + " MB"); return false; }
    ogre::Skel sk;
    if (!ogre::ParseSkeletonXml(text, sk, err)) return false;
    ImportedNode root;
    root.name = "OGRE";
    out.nodes.push_back(root);
    ogre::AddSkeletonToModel(sk, out);
    out.backend = "own";
    return true;
}

bool ReadOgreSkeletonBinary(const std::string& bytes, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    ogre::Skel sk;
    if (!ogre::ParseSkeletonBin(bytes, sk, err)) return false;
    ImportedNode root;
    root.name = "OGRE";
    out.nodes.push_back(root);
    ogre::AddSkeletonToModel(sk, out);
    out.backend = "own";
    return true;
}

bool ReadOgreXmlBuffer(const std::string& text, const std::string& baseDir, ImportedModel& out, std::string* err, const std::vector<std::string>* materialScripts,
                       const std::string* skeletonXml) {
    out = ImportedModel{};
    const bool ok = ReadOgreImpl(text, baseDir, out, err, materialScripts, skeletonXml);
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
    const bool ok = ReadOgreImpl(std::move(bytes), dir, out, err, &scripts, nullptr, true);
    if (!ok) out = ImportedModel{};
    return ok;
}

bool ReadOgreBinaryBuffer(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err, const std::vector<std::string>* materialScripts,
                          const std::string* skeletonXml) {
    out = ImportedModel{};
    const bool ok = ReadOgreBinImpl(bytes, baseDir, out, err, materialScripts, skeletonXml);
    if (!ok) out = ImportedModel{};
    return ok;
}

bool ReadOgreBinaryFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, ogre::kMaxFileBytes, bytes, err)) return false;
    std::string dir;
    std::vector<std::string> scripts = ogre::ScriptsNextTo(path, 5, dir);   // ".mesh"
    const bool ok = ReadOgreBinImpl(bytes, dir, out, err, &scripts, nullptr, true);
    if (!ok) out = ImportedModel{};
    return ok;
}

} // namespace okay
