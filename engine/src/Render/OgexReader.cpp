// Our own OpenGEX (.ogex) reader: Open Game Engine Exchange, a scene format written in OpenDDL text. Written from the public
// OpenGEX specification (the structure list, their properties and the conventions the specification states) and the OpenDDL
// grammar (OpenDdl.hpp); no code from any other importer was read, copied or ported. Reads the node tree (Node, BoneNode,
// GeometryNode, CameraNode and LightNode as nodes) with Transform / Translation / Rotation / Scale, GeometryObjects (triangle,
// quad and triangle-strip meshes; position, normal and the first texture coordinate set), materials (colours, specular power,
// textures), skins (bone count, index and weight arrays) and Animation tracks (as one clip per clip index, sampled on the union
// of the key times of a node's tracks). Bounded: see the budgets below; the parser is iterative. Texture paths are only taken
// when they lie inside the model's own folder. See okay/Render/ModelData.hpp and docs/own_ogex_reader.md.
#include "AffineInverse.hpp"
#include "OpenDdl.hpp"
#include "ReaderCommon.hpp"
#include "SceneRefPath.hpp"
#include "TextReaderUtil.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

namespace {
namespace ogex {

using namespace detail::ddl;
namespace rc = detail::rc;

constexpr std::size_t kMaxFileBytes = 128u * 1024u * 1024u;
// Per-file budgets (the text parser has its own, in ddl::Limits).
constexpr std::size_t kMaxNodes = 200000;
constexpr std::size_t kMaxMaterials = 100000;
constexpr std::size_t kMaxBuiltVerts = 2000000, kMaxBuiltTris = 4000000;   // all geometry objects built (each built once)
constexpr std::size_t kMaxPlacedVerts = 6000000, kMaxPlacedTris = 4000000;   // everything placed in the scene (an object copied per node)
constexpr std::size_t kMaxBones = 4096;
constexpr std::size_t kMaxKeys = 4000000, kMaxClips = 4096, kMaxTracks = 1000000;
constexpr std::size_t kMaxTextures = 4096;

bool Fail(std::string* err, const std::string& m) { if (err) *err = "OpenGEX: " + m; return false; }

enum class FK { Transform, Translation, Rotation, Scale };
struct Factor {
    FK kind = FK::Transform;
    int sub = 3;                 // Translation / Scale: 0 x, 1 y, 2 z, 3 xyz; Rotation: 0 x, 1 y, 2 z, 3 axis, 4 quaternion
    std::vector<double> base;
    bool objectOnly = false;
    std::uint32_t ddl = kNone;
    std::string local;           // the local name (%name) or empty
    std::size_t Params() const {
        if (kind == FK::Transform) return 16;
        if (kind == FK::Rotation) return sub == 3 || sub == 4 ? 4 : 1;
        return sub == 3 ? 3 : 1;
    }
};

rc::M4 FactorMatrix(const Factor& f, const double* v, double angleScale) {
    switch (f.kind) {
    case FK::Transform: { rc::M4 m; for (int i = 0; i < 16; ++i) m.m[i] = v[i]; return m; }
    case FK::Translation: return f.sub == 3 ? rc::Translate(v[0], v[1], v[2]) : f.sub == 0 ? rc::Translate(v[0], 0, 0) : f.sub == 1 ? rc::Translate(0, v[0], 0) : rc::Translate(0, 0, v[0]);
    case FK::Scale: return f.sub == 3 ? rc::ScaleM(v[0], v[1], v[2]) : f.sub == 0 ? rc::ScaleM(v[0], 1, 1) : f.sub == 1 ? rc::ScaleM(1, v[0], 1) : rc::ScaleM(1, 1, v[0]);
    case FK::Rotation:
        if (f.sub == 0) return rc::AxisAngle(1, 0, 0, v[0] * angleScale);
        if (f.sub == 1) return rc::AxisAngle(0, 1, 0, v[0] * angleScale);
        if (f.sub == 2) return rc::AxisAngle(0, 0, 1, v[0] * angleScale);
        if (f.sub == 3) return rc::AxisAngle(v[0], v[1], v[2], v[3] * angleScale);
        else {   // quaternion x y z w
            double x = v[0], y = v[1], z = v[2], w = v[3];
            const double l = std::sqrt(x * x + y * y + z * z + w * w);
            rc::M4 r;
            if (!(l > 1e-12)) return r;
            x /= l; y /= l; z /= l; w /= l;
            r.at(0, 0) = 1 - 2 * (y * y + z * z); r.at(1, 0) = 2 * (x * y - w * z);     r.at(2, 0) = 2 * (x * z + w * y);
            r.at(0, 1) = 2 * (x * y + w * z);     r.at(1, 1) = 1 - 2 * (x * x + z * z); r.at(2, 1) = 2 * (y * z - w * x);
            r.at(0, 2) = 2 * (x * z - w * y);     r.at(1, 2) = 2 * (y * z + w * x);     r.at(2, 2) = 1 - 2 * (x * x + y * y);
            return r;
        }
    }
    return rc::M4();
}

Mat4 ToMat4(const rc::M4& m) { Mat4 r; for (int i = 0; i < 16; ++i) r.m[i] = (float)m.m[i]; return r; }

struct NInfo {
    std::uint32_t ddl = kNone;
    int parent = -1;             // index into the info list, -1 for a top-level node
    std::string name;
    std::vector<Factor> f;
    rc::M4 local, world, objectM;
    bool hasObject = false;
};

struct SkinData {
    bool has = false;
    rc::M4 bindShape;
    std::vector<std::uint32_t> boneDdl;
    std::vector<std::array<int, 4>> ji;      // per vertex, indices into boneDdl; boneDdl.size() = the extra root bone
    std::vector<std::array<float, 4>> jw;
    bool needRoot = false;
};
struct PrimData { int material = 0; std::vector<std::uint32_t> tri; };
struct Geom {
    std::vector<Vec3> pos, nrm;
    std::vector<Vec2> uv;
    std::vector<PrimData> prims;
    SkinData skin;
    std::string name;
};

struct TrackData { std::size_t info = 0, factor = 0; std::vector<double> t, v; };

struct Reader {
    const Doc& d;
    const std::string& baseDir;
    ImportedModel& out;
    std::string* err;
    Reader(const Doc& doc, const std::string& dir, ImportedModel& o, std::string* e) : d(doc), baseDir(dir), out(o), err(e) {}

    std::vector<NInfo> infos;
    std::unordered_map<std::string, std::uint32_t> global;   // "$name" -> doc node
    std::unordered_map<std::uint32_t, std::size_t> infoOfDdl;
    std::unordered_map<std::uint32_t, int> matOfDdl;
    std::unordered_map<std::uint32_t, int> geomOfDdl;
    std::vector<Geom> geoms;
    std::map<int, std::string> clipNames;
    std::map<int, std::vector<TrackData>> clipTracks;
    double distance = 1, angle = 1, timeScale = 1;
    bool zUp = false;
    std::size_t builtVerts = 0, builtTris = 0, placedVerts = 0, placedTris = 0, keys = 0, tracks = 0, textures = 0;
    // notes
    std::map<std::string, int> counts;
    std::string firstTexRefusal;

    void Count(const std::string& what) { ++counts[what]; }

    // ---- data access ------------------------------------------------------------------------------------------------
    // The first numeric data child of `at`; nullptr when none.
    const Node* NumChild(std::uint32_t at) const {
        for (std::uint32_t c = d.nodes[at].firstChild; c != kNone; c = d.nodes[c].next) if (IsNumPrim(d.nodes[c].prim)) return &d.nodes[c];
        return nullptr;
    }
    const Node* StrChild(std::uint32_t at, Prim p = Prim::String) const {
        for (std::uint32_t c = d.nodes[at].firstChild; c != kNone; c = d.nodes[c].next) if (d.nodes[c].prim == p) return &d.nodes[c];
        return nullptr;
    }
    std::string FirstString(std::uint32_t at) const {
        const Node* s = StrChild(at);
        return s && s->dataCount >= 1 ? d.strs[(std::size_t)s->dataBegin] : std::string();
    }
    static std::string StripSigil(const std::string& s) { return !s.empty() && (s[0] == '$' || s[0] == '%') ? s.substr(1) : s; }

    // ---- metrics ----------------------------------------------------------------------------------------------------
    void ReadMetrics(std::uint32_t top) {
        const Node& n = d.nodes[top];
        const std::string key = d.PropStr(n, "key");
        if (key == "up") {
            const std::string v = FirstString(top);
            if (v == "z") zUp = true; else if (v == "y") zUp = false; else Count("a Metric \"up\" that is neither y nor z (y assumed)");
            return;
        }
        const Node* c = NumChild(top);
        if (!c || c->dataCount < 1) return;
        const double v = d.nums[(std::size_t)c->dataBegin];
        if (!(v > 0) || !std::isfinite(v) || v > 1e12 || v < 1e-12) { Count("a Metric with a value that is not a usable positive number (ignored)"); return; }
        if (key == "distance") distance = v;
        else if (key == "angle") angle = v;
        else if (key == "time") timeScale = v;
    }

    // ---- materials --------------------------------------------------------------------------------------------------
    std::string Texture(const std::string& path) {
        if (path.empty()) return std::string();
        if (++textures > kMaxTextures) { Count("texture references past the budget of " + std::to_string(kMaxTextures) + " (not resolved)"); return std::string(); }
        std::string full, why;
        if (!detail::ResolveInsideDir(baseDir, path, full, why)) {
            if (firstTexRefusal.empty()) firstTexRefusal = "'" + path.substr(0, 80) + "': " + why;
            Count("texture path(s) refused (outside the model's folder)");
            return std::string();
        }
        return full;
    }
    bool ReadMaterial(std::uint32_t at) {
        ImportedMaterial m;
        const Node& n = d.nodes[at];
        m.name = n.name != kNone ? StripSigil(d.texts[n.name]) : std::string();
        for (std::uint32_t c = n.firstChild; c != kNone; c = d.nodes[c].next) {
            const Node& cn = d.nodes[c];
            if (d.Is(cn, "Name")) { const std::string s = FirstString(c); if (!s.empty()) m.name = s; }
            else if (d.Is(cn, "Color") || d.Is(cn, "Param")) {
                const std::string a = d.PropStr(cn, "attrib");
                const Node* v = NumChild(c);
                if (!v || v->dataCount < 1) continue;
                const double* x = &d.nums[(std::size_t)v->dataBegin];
                if (d.Is(cn, "Color")) {
                    if (a == "diffuse" && v->dataCount >= 3) { m.hasDiffuse = true; m.diffuse = Color{(float)x[0], (float)x[1], (float)x[2], v->dataCount >= 4 ? (float)x[3] : m.diffuse.a}; }
                    else if (a == "emission" && v->dataCount >= 3) { m.hasEmissive = true; m.emissive = Color{(float)x[0], (float)x[1], (float)x[2], 1.0f}; }
                    else if (a == "opacity") { m.hasDiffuse = true; m.diffuse.a = (float)std::min(1.0, std::max(0.0, x[0])); }
                    else Count("material colour '" + a + "' not read");
                } else {
                    if (a == "specular_power") { if (x[0] > 0) m.shininess = (float)x[0]; }
                    else if (a == "opacity") { m.hasDiffuse = true; m.diffuse.a = (float)std::min(1.0, std::max(0.0, x[0])); }
                    else Count("material parameter '" + a + "' not read");
                }
            } else if (d.Is(cn, "Texture")) {
                const std::string a = d.PropStr(cn, "attrib");
                const std::string path = FirstString(c);
                if (a == "diffuse") m.texture = Texture(path);
                else if (a == "normal") m.normalMap = Texture(path);
                else if (a == "specular") m.specularMap = Texture(path);
                else Count("material texture '" + a + "' not read");
            }
        }
        if (out.materials.size() >= kMaxMaterials) return Fail(err, "more than " + std::to_string(kMaxMaterials) + " materials (budget)");
        if (n.name != kNone) matOfDdl.emplace(at, (int)out.materials.size());
        out.materials.push_back(std::move(m));
        return true;
    }

    // ---- geometry ---------------------------------------------------------------------------------------------------
    // A vertex array of `width` numbers per vertex (sub-arrays of that size, or a flat list that divides evenly).
    bool VertexData(const Node& arr, std::uint32_t width, std::vector<double>& flat, std::size_t& count) const {
        if (!IsNumPrim(arr.prim)) return false;
        if (arr.sub != 0 && arr.sub != width) return false;
        if (arr.dataCount % width) return false;
        flat.assign(d.nums.begin() + (std::ptrdiff_t)arr.dataBegin, d.nums.begin() + (std::ptrdiff_t)(arr.dataBegin + arr.dataCount));
        count = (std::size_t)(arr.dataCount / width);
        return true;
    }

    // Builds the geometry object at doc node `at` (once). -1 when it has no usable mesh; `fatal` on a budget.
    int BuildGeom(std::uint32_t at, bool& fatal) {
        auto it = geomOfDdl.find(at);
        if (it != geomOfDdl.end()) return it->second;
        geomOfDdl[at] = -1;
        const Node& gn = d.nodes[at];
        std::uint32_t mesh = kNone;
        int extraLod = 0;
        for (std::uint32_t c = gn.firstChild; c != kNone; c = d.nodes[c].next)
            if (d.Is(d.nodes[c], "Mesh")) {
                if (d.PropNum(d.nodes[c], "lod", 0) != 0) { ++extraLod; continue; }
                if (mesh == kNone) mesh = c; else ++extraLod;
            }
        if (extraLod) Count("level-of-detail meshes skipped (the first full-detail mesh is read)");
        if (mesh == kNone) { Count("a GeometryObject without a mesh"); return -1; }
        const Node& mn = d.nodes[mesh];
        const std::string primitive = d.Is(mn, "Mesh") && d.HasProp(mn, "primitive") ? d.PropStr(mn, "primitive") : "triangles";
        if (primitive != "triangles" && primitive != "quads" && primitive != "triangle_strip") { Count("a '" + primitive + "' mesh (only triangles, quads and triangle strips are read)"); return -1; }

        Geom g;
        g.name = gn.name != kNone ? StripSigil(d.texts[gn.name]) : std::string();
        std::vector<double> flat;
        std::size_t nv = 0;
        bool havePos = false, haveNrm = false, haveUv = false;
        std::vector<double> nflat, uflat;
        std::size_t nn = 0, nu = 0;
        for (std::uint32_t c = mn.firstChild; c != kNone; c = d.nodes[c].next) {
            const Node& cn = d.nodes[c];
            if (!d.Is(cn, "VertexArray")) continue;
            if (d.HasProp(cn, "morph")) { Count("morph target vertex arrays not read"); continue; }
            const std::string a = d.PropStr(cn, "attrib");
            const Node* v = NumChild(c);
            if (!v) continue;
            if (a == "position" && !havePos) {
                if (!VertexData(*v, 3, flat, nv)) return Count("a position array that is not float[3]"), -1;
                havePos = true;
            } else if (a == "normal" && !haveNrm) {
                if (!VertexData(*v, 3, nflat, nn)) { Count("a normal array that is not float[3] (ignored)"); continue; }
                haveNrm = true;
            } else if ((a == "texcoord" || a == "texcoord[0]") && !haveUv) {
                if (!VertexData(*v, 2, uflat, nu)) { Count("a texcoord array that is not float[2] (ignored)"); continue; }
                haveUv = true;
            } else Count("vertex attribute '" + a + "' not read");
        }
        if (!havePos || nv == 0) { Count("a mesh without positions"); return -1; }
        if (nv > kMaxBuiltVerts || builtVerts + nv > kMaxBuiltVerts) return Fail(err, "more than " + std::to_string(kMaxBuiltVerts) + " vertices over all meshes (budget)"), fatal = true, -1;
        builtVerts += nv;
        g.pos.resize(nv);
        for (std::size_t i = 0; i < nv; ++i) g.pos[i] = Vec3{(float)flat[i * 3], (float)flat[i * 3 + 1], (float)flat[i * 3 + 2]};
        if (haveNrm && nn == nv) { g.nrm.resize(nv); for (std::size_t i = 0; i < nv; ++i) g.nrm[i] = Vec3{(float)nflat[i * 3], (float)nflat[i * 3 + 1], (float)nflat[i * 3 + 2]}; }
        else if (haveNrm) Count("a normal array whose length differs from the positions (ignored)");
        if (haveUv && nu == nv) { g.uv.resize(nv); for (std::size_t i = 0; i < nv; ++i) g.uv[i] = Vec2{(float)uflat[i * 2], (float)uflat[i * 2 + 1]}; }
        else if (haveUv) Count("a texcoord array whose length differs from the positions (ignored)");

        // index arrays
        int badTri = 0;
        auto addPrim = [&](PrimData&& p) { g.prims.push_back(std::move(p)); };
        bool anyIndex = false;
        for (std::uint32_t c = mn.firstChild; c != kNone; c = d.nodes[c].next) {
            const Node& cn = d.nodes[c];
            if (!d.Is(cn, "IndexArray")) continue;
            anyIndex = true;
            const Node* v = NumChild(c);
            if (!v || !IsIntPrim(v->prim)) { Count("an index array that is not an integer array"); continue; }
            PrimData p;
            const double mat = d.PropNum(cn, "material", 0);
            p.material = mat >= 0 && mat < 1024 ? (int)mat : 0;
            const bool cw = d.PropStr(cn, "front") == "cw";
            if (!BuildPrim(*v, primitive, d.HasProp(cn, "restart"), d.PropNum(cn, "restart", 0), nv, cw, p, badTri, fatal)) { if (fatal) return -1; continue; }
            if (!p.tri.empty()) addPrim(std::move(p));
        }
        if (!anyIndex) {   // not indexed: the vertices in sequence
            PrimData p;
            std::vector<std::uint32_t> seq;
            if (primitive == "triangles") { for (std::size_t i = 0; i + 2 < nv; i += 3) for (int k = 0; k < 3; ++k) p.tri.push_back((std::uint32_t)(i + (std::size_t)k)); }
            else if (primitive == "quads") { for (std::size_t i = 0; i + 3 < nv; i += 4) { const std::uint32_t b = (std::uint32_t)i; for (std::uint32_t k : {0u, 1u, 2u, 0u, 2u, 3u}) p.tri.push_back(b + k); } }
            else { for (std::size_t i = 0; i + 2 < nv; ++i) { const std::uint32_t b = (std::uint32_t)i; if (i % 2 == 0) { p.tri.push_back(b); p.tri.push_back(b + 1); p.tri.push_back(b + 2); } else { p.tri.push_back(b + 1); p.tri.push_back(b); p.tri.push_back(b + 2); } } }
            if (builtTris + p.tri.size() / 3 > kMaxBuiltTris) return Fail(err, "more than " + std::to_string(kMaxBuiltTris) + " triangles over all meshes (budget)"), fatal = true, -1;
            builtTris += p.tri.size() / 3;
            if (!p.tri.empty()) addPrim(std::move(p));
        }
        if (badTri) Count(std::to_string(badTri) + " triangle(s) with an index past the vertex array skipped");
        if (g.prims.empty()) { Count("a mesh without triangles"); return -1; }

        // skin
        const std::uint32_t sk = d.Child(mesh, "Skin");
        if (sk != kNone) ReadSkin(sk, nv, g.skin);
        geoms.push_back(std::move(g));
        geomOfDdl[at] = (int)geoms.size() - 1;
        return (int)geoms.size() - 1;
    }

    // Triangles of one IndexArray. False with `fatal` on a budget.
    bool BuildPrim(const Node& v, const std::string& primitive, bool hasRestart, double restart, std::size_t nv, bool cw, PrimData& p, int& badTri, bool& fatal) {
        const std::uint64_t cnt = v.dataCount;
        const double* x = d.nums.data() + v.dataBegin;
        const std::size_t perPrim = primitive == "quads" ? 4 : 3;
        if (primitive != "triangle_strip") {
            if (cnt % perPrim) { Count("an index array whose length is not a multiple of " + std::to_string(perPrim) + " (the tail was dropped)"); }
            const std::uint64_t n = cnt / perPrim;
            const std::uint64_t outTris = n * (perPrim == 4 ? 2 : 1);
            if (builtTris + outTris > kMaxBuiltTris) { Fail(err, "more than " + std::to_string(kMaxBuiltTris) + " triangles over all meshes (budget)"); fatal = true; return false; }
            p.tri.reserve((std::size_t)outTris * 3);
            for (std::uint64_t i = 0; i < n; ++i) {
                std::uint32_t q[4];
                bool ok = true;
                for (std::size_t k = 0; k < perPrim; ++k) { const double iv = x[i * perPrim + k]; if (iv < 0 || iv >= (double)nv) ok = false; q[k] = ok ? (std::uint32_t)iv : 0; }
                if (!ok) { ++badTri; continue; }
                auto tri = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) { if (cw) std::swap(b, c); p.tri.push_back(a); p.tri.push_back(b); p.tri.push_back(c); };
                tri(q[0], q[1], q[2]);
                if (perPrim == 4) tri(q[0], q[2], q[3]);
            }
        } else {
            if (builtTris + (cnt >= 2 ? cnt - 2 : 0) > kMaxBuiltTris) { Fail(err, "more than " + std::to_string(kMaxBuiltTris) + " triangles over all meshes (budget)"); fatal = true; return false; }
            std::uint64_t runStart = 0;
            for (std::uint64_t i = 0; i < cnt; ++i) {
                if (hasRestart && x[i] == restart) { runStart = i + 1; continue; }
                if (i < runStart + 2) continue;
                const double a = x[i - 2], b = x[i - 1], c = x[i];
                if (a < 0 || b < 0 || c < 0 || a >= (double)nv || b >= (double)nv || c >= (double)nv) { ++badTri; continue; }
                std::uint32_t q0 = (std::uint32_t)a, q1 = (std::uint32_t)b, q2 = (std::uint32_t)c;
                if ((i - runStart) % 2 == 1) std::swap(q0, q1);
                if (cw) std::swap(q1, q2);
                p.tri.push_back(q0); p.tri.push_back(q1); p.tri.push_back(q2);
            }
        }
        builtTris += p.tri.size() / 3;
        return true;
    }

    void ReadSkin(std::uint32_t sk, std::size_t nv, SkinData& s) {
        const std::uint32_t bra = d.Child(sk, "BoneRefArray"), bca = d.Child(sk, "BoneCountArray"), bia = d.Child(sk, "BoneIndexArray"), bwa = d.Child(sk, "BoneWeightArray");
        if (bra == kNone || bca == kNone || bia == kNone || bwa == kNone) { Count("a skin without all of BoneRefArray, BoneCountArray, BoneIndexArray and BoneWeightArray (not read)"); return; }
        const Node* refs = StrChild(bra, Prim::Ref);
        const Node* cnt = NumChild(bca);
        const Node* idx = NumChild(bia);
        const Node* wts = NumChild(bwa);
        if (!refs || !cnt || !idx || !wts) { Count("a malformed skin (not read)"); return; }
        if (refs->dataCount == 0 || refs->dataCount > kMaxBones) { Count("a skin with no bones or more than " + std::to_string(kMaxBones) + " (not read)"); return; }
        if (cnt->dataCount != nv) { Count("a skin whose BoneCountArray length differs from the vertex count (not read)"); return; }
        if (idx->dataCount != wts->dataCount) { Count("a skin whose index and weight arrays differ in length (not read)"); return; }
        std::uint64_t total = 0;
        for (std::uint64_t i = 0; i < cnt->dataCount; ++i) { const double c = d.nums[(std::size_t)(cnt->dataBegin + i)]; if (c < 0) { Count("a skin with a negative bone count (not read)"); return; } total += (std::uint64_t)c; if (total > idx->dataCount) break; }
        if (total != idx->dataCount) { Count("a skin whose bone counts do not add up to the index array length (not read)"); return; }
        const std::size_t nb = (std::size_t)refs->dataCount;
        for (std::uint64_t i = 0; i < refs->dataCount; ++i) s.boneDdl.push_back(kNone);
        s.has = true;
        // the ref targets are resolved by the caller's global table
        for (std::uint64_t i = 0; i < refs->dataCount; ++i) {
            auto f = global.find(d.strs[(std::size_t)(refs->dataBegin + i)]);
            s.boneDdl[(std::size_t)i] = f == global.end() ? kNone : f->second;
        }
        const std::uint32_t tr = d.Child(sk, "Transform");
        if (tr != kNone) {
            const Node* m = NumChild(tr);
            if (m && m->dataCount >= 16) for (int i = 0; i < 16; ++i) s.bindShape.m[i] = d.nums[(std::size_t)m->dataBegin + (std::size_t)i];
        }
        s.ji.assign(nv, std::array<int, 4>{0, 0, 0, 0});
        s.jw.assign(nv, std::array<float, 4>{0, 0, 0, 0});
        std::uint64_t at = 0;
        int badIdx = 0;
        for (std::size_t v = 0; v < nv; ++v) {
            const std::uint64_t c = (std::uint64_t)d.nums[(std::size_t)(cnt->dataBegin + v)];
            std::array<int, 4> bi{0, 0, 0, 0};
            std::array<float, 4> bw{0, 0, 0, 0};
            for (std::uint64_t k = 0; k < c; ++k, ++at) {
                const double ib = d.nums[(std::size_t)(idx->dataBegin + at)], w = d.nums[(std::size_t)(wts->dataBegin + at)];
                if (ib < 0 || ib >= (double)nb) { ++badIdx; continue; }
                if (!(w > 0)) continue;
                // keep the four strongest
                int slot = -1;
                for (int q = 0; q < 4; ++q) if (bw[(std::size_t)q] == 0) { slot = q; break; }
                if (slot < 0) { slot = 0; for (int q = 1; q < 4; ++q) if (bw[(std::size_t)q] < bw[(std::size_t)slot]) slot = q; if (bw[(std::size_t)slot] >= (float)w) continue; }
                bi[(std::size_t)slot] = (int)ib;
                bw[(std::size_t)slot] = (float)w;
            }
            float sum = 0;
            for (float w : bw) sum += w;
            if (sum > 0) for (float& w : bw) w /= sum;
            else { bi = {(int)nb, 0, 0, 0}; bw = {1, 0, 0, 0}; s.needRoot = true; }
            s.ji[v] = bi;
            s.jw[v] = bw;
        }
        if (badIdx) Count(std::to_string(badIdx) + " skin weight(s) naming a bone past BoneRefArray skipped");
    }

    // ---- nodes ------------------------------------------------------------------------------------------------------
    static bool IsNodeKind(const Doc& d, const Node& n) {
        return d.Is(n, "Node") || d.Is(n, "BoneNode") || d.Is(n, "GeometryNode") || d.Is(n, "CameraNode") || d.Is(n, "LightNode");
    }

    bool ReadFactor(std::uint32_t c, Factor& f) {
        const Node& cn = d.nodes[c];
        const Node* v = NumChild(c);
        if (!v) return false;
        f.ddl = c;
        f.local = cn.name != kNone && d.texts[cn.name][0] == '%' ? d.texts[cn.name] : std::string();
        f.objectOnly = d.PropNum(cn, "object", 0) != 0;
        if (d.Is(cn, "Transform")) {
            f.kind = FK::Transform;
            if (v->dataCount < 16 || (v->sub != 0 && v->sub != 16)) return false;
        } else {
            const std::string kind = d.PropStr(cn, "kind");
            if (d.Is(cn, "Rotation")) {
                f.kind = FK::Rotation;
                f.sub = kind.empty() || kind == "axis" ? 3 : kind == "x" ? 0 : kind == "y" ? 1 : kind == "z" ? 2 : kind == "quaternion" ? 4 : -1;
            } else {
                f.kind = d.Is(cn, "Translation") ? FK::Translation : FK::Scale;
                f.sub = kind.empty() || kind == "xyz" ? 3 : kind == "x" ? 0 : kind == "y" ? 1 : kind == "z" ? 2 : -1;
            }
            if (f.sub < 0) return false;
            if (v->dataCount < f.Params()) return false;
        }
        f.base.assign(d.nums.begin() + (std::ptrdiff_t)v->dataBegin, d.nums.begin() + (std::ptrdiff_t)(v->dataBegin + f.Params()));
        return true;
    }

    bool BuildNodes() {
        struct Item { std::uint32_t ddl; int parent; };
        std::vector<Item> work;
        std::vector<std::uint32_t> tops;
        for (std::uint32_t i = 0; i < d.nodes.size(); ++i) if (d.nodes[i].parent == kNone && IsNodeKind(d, d.nodes[i])) tops.push_back(i);
        for (auto r = tops.rbegin(); r != tops.rend(); ++r) work.push_back({*r, -1});
        std::unordered_map<std::string, int> used;
        used["OpenGEX"] = 1;
        while (!work.empty()) {
            const Item it = work.back();
            work.pop_back();
            if (infos.size() >= kMaxNodes) return Fail(err, "more than " + std::to_string(kMaxNodes) + " nodes (budget)");
            const Node& n = d.nodes[it.ddl];
            NInfo ni;
            ni.ddl = it.ddl;
            ni.parent = it.parent;
            std::string nm;
            for (std::uint32_t c = n.firstChild; c != kNone; c = d.nodes[c].next) {
                const Node& cn = d.nodes[c];
                if (d.Is(cn, "Name") && nm.empty()) nm = FirstString(c);
                else if (d.Is(cn, "Transform") || d.Is(cn, "Translation") || d.Is(cn, "Rotation") || d.Is(cn, "Scale")) {
                    Factor f;
                    if (ReadFactor(c, f)) ni.f.push_back(std::move(f)); else Count("a transform structure with missing or malformed values (ignored)");
                }
            }
            if (nm.empty() && n.name != kNone) nm = StripSigil(d.texts[n.name]);
            if (nm.empty()) nm = "node" + std::to_string(infos.size());
            int& u = used[nm];
            if (u++) { nm += "_" + std::to_string(u - 1); used[nm] = 1; }
            ni.name = nm;
            ni.local = rc::M4();
            ni.objectM = rc::M4();
            for (const Factor& f : ni.f) {
                const rc::M4 m = FactorMatrix(f, f.base.data(), angle);
                if (f.objectOnly) { ni.objectM = rc::Mul(ni.objectM, m); ni.hasObject = true; }
                else ni.local = rc::Mul(ni.local, m);
            }
            if (!rc::Finite(ni.local) || !rc::Finite(ni.objectM)) return Fail(err, "a node transform that overflows");
            ni.world = ni.parent < 0 ? ni.local : rc::Mul(infos[(std::size_t)ni.parent].world, ni.local);
            if (!rc::Finite(ni.world)) return Fail(err, "node transforms that multiply out of range");
            const std::size_t me = infos.size();
            infoOfDdl[it.ddl] = me;
            infos.push_back(std::move(ni));
            // children in file order
            std::vector<std::uint32_t> kids;
            for (std::uint32_t c = n.firstChild; c != kNone; c = d.nodes[c].next) if (IsNodeKind(d, d.nodes[c])) kids.push_back(c);
            for (auto r = kids.rbegin(); r != kids.rend(); ++r) work.push_back({*r, (int)me});
        }
        for (std::size_t i = 0; i < infos.size(); ++i) {
            ImportedNode on;
            on.name = infos[i].name;
            on.parent = infos[i].parent < 0 ? 0 : infos[i].parent + 1;
            const Mat4 lm = ToMat4(infos[i].local);
            Vec3 t, s;
            Quat r;
            if (!lm.Decompose(t, r, s)) Count("a node transform that is not an affine matrix (approximated)");
            on.position = t; on.rotation = r; on.scale = s;
            out.nodes.push_back(std::move(on));
        }
        return true;
    }

    // ---- parts ------------------------------------------------------------------------------------------------------
    bool BuildParts() {
        for (std::size_t ii = 0; ii < infos.size(); ++ii) {
            NInfo& ni = infos[ii];
            const Node& n = d.nodes[ni.ddl];
            if (!d.Is(n, "GeometryNode")) continue;
            const std::uint32_t oref = d.Child(ni.ddl, "ObjectRef");
            if (oref == kNone) { Count("a GeometryNode without an ObjectRef"); continue; }
            const Node* rs = StrChild(oref, Prim::Ref);
            if (!rs || rs->dataCount < 1) continue;
            auto gf = global.find(d.strs[(std::size_t)rs->dataBegin]);
            if (gf == global.end() || !d.Is(d.nodes[gf->second], "GeometryObject")) { Count("a GeometryNode whose ObjectRef is not a GeometryObject in this file"); continue; }
            bool fatal = false;
            const int gi = BuildGeom(gf->second, fatal);
            if (fatal) return false;
            if (gi < 0) continue;
            // the node's materials by index
            std::vector<int> mats;
            for (std::uint32_t c = n.firstChild; c != kNone; c = d.nodes[c].next) {
                if (!d.Is(d.nodes[c], "MaterialRef")) continue;
                const double ix = d.PropNum(d.nodes[c], "index", 0);
                if (!(ix >= 0 && ix < 1024)) continue;
                const Node* r = StrChild(c, Prim::Ref);
                int mi = -1;
                if (r && r->dataCount >= 1) { auto f = global.find(d.strs[(std::size_t)r->dataBegin]); if (f != global.end()) { auto m = matOfDdl.find(f->second); if (m != matOfDdl.end()) mi = m->second; } }
                if ((std::size_t)ix >= mats.size()) mats.resize((std::size_t)ix + 1, -1);
                mats[(std::size_t)ix] = mi;
            }
            if (!PlaceGeom(geoms[(std::size_t)gi], ni, ii, mats)) return false;
        }
        return true;
    }

    bool PlaceGeom(const Geom& g, const NInfo& ni, std::size_t ii, const std::vector<int>& mats) {
        const std::size_t N = g.pos.size();
        const bool skinned = g.skin.has;
        // bones of a skin: the nodes the references name
        std::vector<ImportedBone> bones;
        if (skinned) {
            rc::M4 shapeInv;
            (void)shapeInv;
            for (std::size_t b = 0; b < g.skin.boneDdl.size(); ++b) {
                ImportedBone ib;
                auto f = g.skin.boneDdl[b] == kNone ? infoOfDdl.end() : infoOfDdl.find(g.skin.boneDdl[b]);
                if (f == infoOfDdl.end()) { Count("a bone reference that names no node of this file (bound to the root)"); ib.name = out.nodes[0].name; ib.inverseBind = Mat4::Identity(); }
                else {
                    ib.name = infos[f->second].name;
                    rc::M4 inv;
                    if (!rc::InvAffine(infos[f->second].world, inv)) { Count("a bone with a singular bind transform (identity used)"); ib.inverseBind = Mat4::Identity(); }
                    else ib.inverseBind = ToMat4(rc::Mul(inv, g.skin.bindShape));
                }
                bones.push_back(std::move(ib));
            }
            if (g.skin.needRoot) { ImportedBone rb; rb.name = out.nodes[0].name; rb.inverseBind = Mat4::Identity(); bones.push_back(std::move(rb)); }
        }
        std::vector<int> remap(N, -1);
        for (std::size_t pi = 0; pi < g.prims.size(); ++pi) {
            const PrimData& pr = g.prims[pi];
            const std::size_t tris = pr.tri.size() / 3;
            if (placedTris + tris > kMaxPlacedTris) return Fail(err, "more than " + std::to_string(kMaxPlacedTris) + " triangles placed in the scene (budget; a mesh used by many nodes counts for each)");
            ImportedPart part;
            std::vector<std::uint32_t> used;
            part.mesh.triangles.reserve(pr.tri.size());
            for (std::uint32_t v : pr.tri) {
                if (remap[v] < 0) {
                    if (placedVerts >= kMaxPlacedVerts) return Fail(err, "more than " + std::to_string(kMaxPlacedVerts) + " vertices placed in the scene (budget)");
                    ++placedVerts;
                    remap[v] = (int)used.size();
                    used.push_back(v);
                }
                part.mesh.triangles.push_back(remap[v]);
            }
            placedTris += tris;
            const bool objT = ni.hasObject && !skinned;
            // normal matrix of an object-only transform: the cofactor matrix of its 3x3 part
            double cof[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
            if (objT) {
                const rc::M4& a = ni.objectM;
                const double a00 = a.at(0, 0), a01 = a.at(1, 0), a02 = a.at(2, 0), a10 = a.at(0, 1), a11 = a.at(1, 1), a12 = a.at(2, 1), a20 = a.at(0, 2), a21 = a.at(1, 2), a22 = a.at(2, 2);
                cof[0] = a11 * a22 - a12 * a21; cof[1] = a12 * a20 - a10 * a22; cof[2] = a10 * a21 - a11 * a20;
                cof[3] = a02 * a21 - a01 * a22; cof[4] = a00 * a22 - a02 * a20; cof[5] = a01 * a20 - a00 * a21;
                cof[6] = a01 * a12 - a02 * a11; cof[7] = a02 * a10 - a00 * a12; cof[8] = a00 * a11 - a01 * a10;
            }
            bool badNormal = g.nrm.empty();
            for (std::uint32_t v : used) {
                Vec3 p = g.pos[v];
                if (objT) { double ox, oy, oz; rc::Apply(ni.objectM, p.x, p.y, p.z, ox, oy, oz); p = Vec3{(float)ox, (float)oy, (float)oz}; }
                part.mesh.vertices.push_back(p);
                if (!g.nrm.empty()) {
                    Vec3 nn = g.nrm[v];
                    if (objT) nn = Vec3{(float)(cof[0] * nn.x + cof[3] * nn.y + cof[6] * nn.z), (float)(cof[1] * nn.x + cof[4] * nn.y + cof[7] * nn.z), (float)(cof[2] * nn.x + cof[5] * nn.y + cof[8] * nn.z)};
                    const float l = nn.Magnitude();
                    if (!(l > 1e-12f) || !std::isfinite(l)) { badNormal = true; nn = Vec3{0, 1, 0}; } else nn = nn * (1.0f / l);
                    part.mesh.normals.push_back(nn);
                }
                if (!g.uv.empty()) part.mesh.uvs.push_back(g.uv[v]);
                if (skinned) { part.jointIdx.push_back(g.skin.ji[v]); part.jointWt.push_back(g.skin.jw[v]); }
            }
            if (badNormal) { part.mesh.normals.clear(); part.mesh.ComputeSmoothNormals(); if (!g.nrm.empty()) Count("a mesh with zero or invalid normals (smooth normals made)"); }
            if (!rc::PartInRange(part)) return Fail(err, "a vertex is out of range after its transform");
            if (skinned) part.bones = bones;
            part.mesh.name = g.prims.size() > 1 ? ni.name + "_" + std::to_string(pi) : ni.name;
            part.node = skinned ? 0 : (int)ii + 1;
            part.material = pr.material >= 0 && (std::size_t)pr.material < mats.size() ? mats[(std::size_t)pr.material] : -1;
            for (std::uint32_t v : used) remap[v] = -1;
            out.parts.push_back(std::move(part));
        }
        if (skinned && ni.hasObject) Count("an object-only transform on a skinned mesh (not applied)");
        return true;
    }

    // ---- animation --------------------------------------------------------------------------------------------------
    // The factor a Track's target names: "%local" inside the node that holds the animation, or "$node%local".
    bool ResolveTarget(const std::string& ref, std::size_t holder, std::size_t& info, std::size_t& factor) {
        std::size_t inf = holder;
        std::string loc;
        const std::size_t pc = ref.find('%', 1);
        if (!ref.empty() && ref[0] == '%') loc = ref;
        else if (pc != std::string::npos) {
            auto g = global.find(ref.substr(0, pc));
            if (g == global.end()) return false;
            auto f = infoOfDdl.find(g->second);
            if (f == infoOfDdl.end()) return false;
            inf = f->second;
            loc = ref.substr(pc);
        } else return false;
        const NInfo& ni = infos[inf];
        for (std::size_t k = 0; k < ni.f.size(); ++k) if (ni.f[k].local == loc) { info = inf; factor = k; return true; }
        return false;
    }

    bool ReadAnimations() {
        for (std::size_t ii = 0; ii < infos.size(); ++ii) {
            const Node& n = d.nodes[infos[ii].ddl];
            for (std::uint32_t c = n.firstChild; c != kNone; c = d.nodes[c].next) {
                if (!d.Is(d.nodes[c], "Animation")) continue;
                const double cl = d.PropNum(d.nodes[c], "clip", 0);
                const int clip = cl >= 0 && cl < 1e6 ? (int)cl : 0;
                for (std::uint32_t t = d.nodes[c].firstChild; t != kNone; t = d.nodes[t].next) {
                    if (!d.Is(d.nodes[t], "Track")) continue;
                    if (++tracks > kMaxTracks) return Fail(err, "more than " + std::to_string(kMaxTracks) + " animation tracks (budget)");
                    std::size_t inf = 0, fi = 0;
                    const std::string tgt = d.PropStr(d.nodes[t], "target");
                    if (!ResolveTarget(tgt, ii, inf, fi)) { Count("an animation track whose target is not a named transform of the node (skipped)"); continue; }
                    const std::uint32_t tm = d.Child(t, "Time"), vl = d.Child(t, "Value");
                    if (tm == kNone || vl == kNone) { Count("an animation track without both Time and Value (skipped)"); continue; }
                    const Node* tk = KeyData(tm);
                    const Node* vk = KeyData(vl);
                    const Factor& f = infos[inf].f[fi];
                    const std::size_t k = f.Params();
                    if (!tk || !vk || tk->dataCount == 0 || vk->dataCount != tk->dataCount * k || (vk->sub != 0 && vk->sub != k)) { Count("an animation track whose key counts do not match (skipped)"); continue; }
                    if (keys + tk->dataCount > kMaxKeys) return Fail(err, "more than " + std::to_string(kMaxKeys) + " animation keys (budget)");
                    keys += (std::size_t)tk->dataCount;
                    TrackData td;
                    td.info = inf; td.factor = fi;
                    td.t.assign(d.nums.begin() + (std::ptrdiff_t)tk->dataBegin, d.nums.begin() + (std::ptrdiff_t)(tk->dataBegin + tk->dataCount));
                    td.v.assign(d.nums.begin() + (std::ptrdiff_t)vk->dataBegin, d.nums.begin() + (std::ptrdiff_t)(vk->dataBegin + vk->dataCount));
                    const std::string curve = d.PropStr(d.nodes[tm], "curve");
                    const std::string vcurve = d.PropStr(d.nodes[vl], "curve");
                    if (curve == "bezier" || vcurve == "bezier" || vcurve == "tcb") Count("bezier and TCB curves read as straight lines between the value keys");
                    for (double& x : td.t) x *= timeScale;
                    // keys sorted by time
                    bool sorted = true;
                    for (std::size_t q = 1; q < td.t.size(); ++q) if (td.t[q] < td.t[q - 1]) sorted = false;
                    if (!sorted) {
                        std::vector<std::size_t> ord(td.t.size());
                        for (std::size_t q = 0; q < ord.size(); ++q) ord[q] = q;
                        std::stable_sort(ord.begin(), ord.end(), [&](std::size_t a, std::size_t b) { return td.t[a] < td.t[b]; });
                        std::vector<double> t2, v2;
                        for (std::size_t q : ord) { t2.push_back(td.t[q]); for (std::size_t j = 0; j < k; ++j) v2.push_back(td.v[q * k + j]); }
                        td.t.swap(t2); td.v.swap(v2);
                        Count("animation keys that were out of time order were sorted");
                    }
                    bool good = true;
                    for (double x : td.t) if (!(x >= 0) || x > 1e9) good = false;
                    if (!good) { Count("an animation track with negative or huge times (skipped)"); continue; }
                    if (clipTracks.size() >= kMaxClips && clipTracks.find(clip) == clipTracks.end()) { Count("more than " + std::to_string(kMaxClips) + " clips (the rest skipped)"); continue; }
                    clipTracks[clip].push_back(std::move(td));
                }
            }
        }
        return true;
    }
    // The Key child of a Time / Value structure that holds the values (kind "value", or no kind).
    const Node* KeyData(std::uint32_t at) const {
        for (std::uint32_t c = d.nodes[at].firstChild; c != kNone; c = d.nodes[c].next) {
            if (!d.Is(d.nodes[c], "Key")) continue;
            const std::string k = d.PropStr(d.nodes[c], "kind");
            if (!k.empty() && k != "value") continue;
            return NumChild(c);
        }
        return nullptr;
    }

    // The factor's parameters at time t from a track (linear; quaternions by normalised lerp).
    void Eval(const Factor& f, const TrackData& td, double t, double* v) const {
        const std::size_t k = f.Params(), n = td.t.size();
        std::size_t hi = (std::size_t)(std::upper_bound(td.t.begin(), td.t.end(), t) - td.t.begin());
        if (hi == 0) { for (std::size_t j = 0; j < k; ++j) v[j] = td.v[j]; return; }
        if (hi >= n) { for (std::size_t j = 0; j < k; ++j) v[j] = td.v[(n - 1) * k + j]; return; }
        const std::size_t lo = hi - 1;
        const double span = td.t[hi] - td.t[lo];
        const double u = span > 0 ? (t - td.t[lo]) / span : 0.0;
        const double* a = &td.v[lo * k];
        double b[16];
        for (std::size_t j = 0; j < k; ++j) b[j] = td.v[hi * k + j];
        if (f.kind == FK::Rotation && f.sub == 4) {
            double dot = 0;
            for (int j = 0; j < 4; ++j) dot += a[j] * b[j];
            if (dot < 0) for (int j = 0; j < 4; ++j) b[j] = -b[j];
        }
        for (std::size_t j = 0; j < k; ++j) v[j] = a[j] + (b[j] - a[j]) * u;
    }

    bool BuildClips() {
        for (auto& ct : clipTracks) {
            ImportedClip clip;
            const auto nm = clipNames.find(ct.first);
            clip.name = nm != clipNames.end() && !nm->second.empty() ? nm->second : (ct.first == 0 && clipTracks.size() == 1 ? "animation" : "clip" + std::to_string(ct.first));
            std::map<std::size_t, std::vector<const TrackData*>> byInfo;
            for (const TrackData& t : ct.second) byInfo[t.info].push_back(&t);
            for (auto& bi : byInfo) {
                const NInfo& ni = infos[bi.first];
                std::vector<double> times;
                std::size_t total = 0;
                for (const TrackData* t : bi.second) total += t->t.size();
                times.reserve(total);
                for (const TrackData* t : bi.second) times.insert(times.end(), t->t.begin(), t->t.end());
                std::sort(times.begin(), times.end());
                times.erase(std::unique(times.begin(), times.end()), times.end());
                if (keys + times.size() * 3 > kMaxKeys * 2) return Fail(err, "more than " + std::to_string(kMaxKeys * 2) + " sampled animation keys (budget)");
                keys += times.size() * 3;
                ImportedChannel ch;
                ch.node = ni.name;
                Quat prev{0, 0, 0, 1};
                bool havePrev = false;
                for (double t : times) {
                    rc::M4 m;
                    for (std::size_t fi = 0; fi < ni.f.size(); ++fi) {
                        const Factor& f = ni.f[fi];
                        if (f.objectOnly) continue;
                        double v[16];
                        const TrackData* tr = nullptr;
                        for (const TrackData* c : bi.second) if (c->factor == fi) { tr = c; break; }
                        if (tr) Eval(f, *tr, t, v); else for (std::size_t j = 0; j < f.base.size(); ++j) v[j] = f.base[j];
                        m = rc::Mul(m, FactorMatrix(f, v, angle));
                    }
                    if (!rc::Finite(m)) return Fail(err, "an animated transform that overflows");
                    Vec3 tp, sc;
                    Quat q;
                    ToMat4(m).Decompose(tp, q, sc);
                    if (havePrev && Quat::Dot(prev, q) < 0) q = Quat{-q.x, -q.y, -q.z, -q.w};
                    prev = q; havePrev = true;
                    const float ft = (float)t;
                    ch.position.push_back({ft, tp});
                    ch.rotation.push_back({ft, q});
                    ch.scale.push_back({ft, sc});
                }
                clip.channels.push_back(std::move(ch));
            }
            if (!clip.channels.empty()) out.clips.push_back(std::move(clip));
        }
        return true;
    }
};

bool ReadImpl(const std::string& text, const std::string& baseDir, ImportedModel& out, std::string* err) {
    std::string t = text;
    bool converted;
    std::string why;
    if (!detail::txt::NormalizeEncoding(t, converted, why, kMaxFileBytes)) return Fail(err, why);
    Doc doc;
    std::string perr;
    if (!Parse(t, doc, perr)) return Fail(err, perr);
    t.clear();
    t.shrink_to_fit();

    Reader r(doc, baseDir, out, err);
    ImportedNode root;
    root.name = "OpenGEX";
    out.nodes.push_back(root);

    // global names, then the top-level structures in order
    for (std::uint32_t i = 0; i < doc.nodes.size(); ++i) {
        const Node& n = doc.nodes[i];
        if (n.name != kNone && doc.texts[n.name][0] == '$') r.global.emplace(doc.texts[n.name], i);
    }
    std::size_t topNodes = 0, extensions = 0;
    for (std::uint32_t i = 0; i < doc.nodes.size(); ++i) {
        const Node& n = doc.nodes[i];
        if (n.parent != kNone) continue;
        if (doc.Is(n, "Metric")) r.ReadMetrics(i);
        else if (doc.Is(n, "Clip")) { const int idx = (int)doc.PropNum(n, "index", 0); const std::uint32_t nc = doc.Child(i, "Name"); if (nc != kNone) r.clipNames[idx] = r.FirstString(nc); }
        else if (doc.Is(n, "Material")) { if (!r.ReadMaterial(i)) return false; }
        else if (Reader::IsNodeKind(doc, n)) ++topNodes;
        else if (doc.Is(n, "Extension")) ++extensions;
        else if (doc.Is(n, "GeometryObject") || doc.Is(n, "CameraObject") || doc.Is(n, "LightObject")) {}
        else r.Count("top-level structure '" + doc.Ident(n) + "' not read");
    }
    if (!r.BuildNodes()) return false;
    if (!r.BuildParts()) return false;
    if (!r.ReadAnimations()) return false;
    if (!r.BuildClips()) return false;
    if (out.parts.empty() && out.nodes.size() <= 1) return Fail(err, "the file has no nodes and no meshes");
    if (out.parts.empty()) return Fail(err, "the file has no mesh with triangles");

    // metrics: the unit scale and the up axis on the root node
    if (r.distance != 1) out.nodes[0].scale = Vec3{(float)r.distance, (float)r.distance, (float)r.distance};
    rc::ApplyUpAxis(out, r.zUp, false);
    out.backend = "own";

    auto note = [&](const std::string& s) { out.notes.push_back("OpenGEX: " + s); };
    for (const auto& c : r.counts) note(c.second > 1 ? std::to_string(c.second) + " x " + c.first : c.first);
    if (!r.firstTexRefusal.empty()) note("first refused texture path " + r.firstTexRefusal);
    if (extensions) note(std::to_string(extensions) + " Extension structure(s) not read");
    int lights = 0, cameras = 0;
    for (const NInfo& ni : r.infos) { if (doc.Is(doc.nodes[ni.ddl], "LightNode")) ++lights; else if (doc.Is(doc.nodes[ni.ddl], "CameraNode")) ++cameras; }
    if (lights || cameras) note(std::to_string(lights) + " light node(s) and " + std::to_string(cameras) + " camera node(s) are empty nodes (their objects are not read)");
    note("texture coordinates are kept as written (OpenGEX puts the origin at the upper left); materials keep diffuse, emission, specular power, opacity and diffuse / normal / specular textures; vertex colours, tangents and morph targets are not read");
    return true;
}

} // namespace ogex
} // namespace

bool ReadOgexBuffer(const std::string& text, const std::string& baseDir, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    const bool ok = ogex::ReadImpl(text, baseDir, out, err);
    if (!ok) out = ImportedModel{};
    return ok;
}

bool ReadOgexFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, ogex::kMaxFileBytes, bytes, err)) return false;
    std::string dir;
    const std::size_t sl = path.find_last_of("/\\");
    if (sl != std::string::npos) dir = path.substr(0, sl + 1);
    return ReadOgexBuffer(bytes, dir, out, err);
}

} // namespace okay
