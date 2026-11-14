// Our own B3D (Blitz3D model, binary) reader: nodes with position, scale and rotation, meshes with normals and
// texture coordinates, brushes (materials) and textures, bone weights and keyframe animation. Written from the public
// description of the format (the Blitz3D file format notes by Mark Sibly, "b3dfile_specs.txt"). Bounds-checked, capped,
// the chunk tree is walked with an explicit stack (no recursion). See okay/Render/ModelData.hpp and
// docs/own_b3d_reader.md.
//
// Blitz3D is left-handed. The model is turned into the engine's right-handed space by mirroring Z (positions, normals,
// translations, rotations; the winding of each triangle is reversed). A rotation (w, x, y, z) in the file becomes
// (x, y, -z, w) in the engine: the file's quaternions follow Blitz3D's matrix convention, which turns the opposite
// way to the usual one, and the Z mirror turns it a second time (checked against Assimp's importer on the real files).
#include "BinReader.hpp"
#include "ReaderCommon.hpp"
#include "TextReaderUtil.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

namespace {
namespace b3d {

using detail::bin::Br;

constexpr std::size_t kMaxFileBytes = 256u * 1024u * 1024u;
constexpr std::size_t kMaxNodes = 100000, kMaxVerts = 20000000, kMaxTris = 20000000, kMaxKeys = 20000000, kMaxTextures = 100000, kMaxBrushes = 100000;
constexpr std::size_t kMaxDepth = 10000;
constexpr double kMaxCoord = 1e15;

struct Vtx { Vec3 p{0, 0, 0}, n{0, 0, 0}; Vec2 uv{0, 0}; int bone[4] = {-1, -1, -1, -1}; float w[4] = {0, 0, 0, 0}; };
struct Brush { std::string name; float rgb[3] = {1, 1, 1}, alpha = 1, shiny = 0; int tex0 = -1; };
struct NodeRec { std::string name; int parent = -1; Vec3 pos{0, 0, 0}, scl{1, 1, 1}; Quat rot{0, 0, 0, 1}; };
struct MeshRec { std::size_t v0 = 0, nv = 0, endVerts = 0; int node = 0, brush = -1; bool haveN = false, haveUv = false; };
struct TrisRec { int mesh = 0, brush = -1; std::vector<std::uint32_t> idx; };
struct Keys { std::vector<ImportedKeyV3> pos, scl; std::vector<ImportedKeyQ> rot; std::vector<float> posT, sclT, rotT; };
struct Frame { char tag[4]; std::size_t end; int node; int mesh; };

bool Fail(std::string* err, const std::string& m) { if (err) *err = "B3D: " + m; return false; }
bool Fin(float f) { return std::isfinite(f) && std::fabs(f) <= (float)kMaxCoord; }

// file quaternion (w, x, y, z) -> engine quaternion, see the top of the file
Quat ConvertQuat(float w, float x, float y, float z, bool& zero) {
    const float l = std::sqrt(w * w + x * x + y * y + z * z);
    zero = !(l > 1e-12f) || !std::isfinite(l);
    if (zero) return Quat{0, 0, 0, 1};
    return Quat{x / l, y / l, -z / l, w / l};
}

} // namespace b3d
} // namespace

static bool ReadB3dImpl(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err) {
    using namespace b3d;
    Br b(bytes);
    if (b.n < 8) return Fail(err, "file too short");
    if (std::memcmp(b.d, "BB3D", 4) != 0) return Fail(err, "not a B3D file (the magic text BB3D is missing)");

    std::vector<std::string> textures;
    std::vector<Brush> brushes;
    std::vector<NodeRec> nodes;
    std::vector<Vtx> verts;
    std::vector<MeshRec> meshes;
    std::vector<TrisRec> tris;
    std::vector<Keys> keys;            // per node (only when it has KEYS)
    std::vector<int> keysOf;           // node -> index into `keys` or -1
    std::vector<char> hasBone;         // node -> has a BONE chunk
    int animFrames = -1; float animFps = 0; int animChunks = 0;
    int zeroQuats = 0, badBoneRefs = 0, droppedWeights = 0, badBrush = 0, badTex = 0, badKeys = 0, unsortedKeys = 0, topNodes = 0, badVertexFormat = 0, skippedChunks = 0, badTris = 0;
    std::size_t totalTris = 0, totalKeys = 0;
    int version = 0;

    // ---- the chunk tree, walked with an explicit stack ----
    std::vector<Frame> st;
    {
        b.pos = 4;
        const std::uint32_t sz = b.U32();
        const std::uint64_t end = (std::uint64_t)b.pos + sz;
        if (end > b.n) return Fail(err, "the BB3D chunk is larger than the file");
        version = b.I32();
        if (b.bad) return Fail(err, "the file ends inside the header");
        Frame f; std::memcpy(f.tag, "BB3D", 4); f.end = (std::size_t)end; f.node = -1; f.mesh = -1;
        st.push_back(f);
    }
    while (!st.empty()) {
        Frame top = st.back();
        if (b.pos >= top.end) { st.pop_back(); continue; }
        if (top.end - b.pos < 8) { b.pos = top.end; continue; }   // a few stray bytes after the last chunk
        char tag[4];
        std::memcpy(tag, b.d + b.pos, 4);
        b.pos += 4;
        const std::uint32_t sz = b.U32();
        const std::uint64_t cend64 = (std::uint64_t)b.pos + sz;
        if (cend64 > top.end) return Fail(err, std::string("a '") + std::string(tag, 4) + "' chunk runs past the end of its parent");
        const std::size_t cend = (std::size_t)cend64;
        auto is = [&](const char* t) { return std::memcmp(tag, t, 4) == 0; };
        Br c(b.d, cend); c.pos = b.pos;
        const bool inBB3D = std::memcmp(top.tag, "BB3D", 4) == 0, inNode = std::memcmp(top.tag, "NODE", 4) == 0, inMesh = std::memcmp(top.tag, "MESH", 4) == 0;
        if (inBB3D && is("TEXS")) {
            while (c.Left() > 0) {
                std::string name = c.CStr(c.Left());
                c.I32(); c.I32(); for (int i = 0; i < 5; ++i) c.F32();   // flags, blend, position, scale, rotation
                if (c.bad) return Fail(err, "the TEXS chunk ends inside a texture");
                if (textures.size() >= kMaxTextures) return Fail(err, "too many textures");
                for (char& ch : name) if (ch == '\\') ch = '/';
                textures.push_back(std::move(name));
            }
        } else if (inBB3D && is("BRUS")) {
            const std::int32_t nTex = c.I32();
            if (c.bad || nTex < 0 || nTex > 8) return Fail(err, "bad texture count in the BRUS chunk");
            while (c.Left() > 0) {
                Brush br;
                br.name = c.CStr(c.Left());
                for (float& f : br.rgb) f = c.F32();
                br.alpha = c.F32(); br.shiny = c.F32();
                c.I32(); c.I32();   // blend, effects
                for (std::int32_t i = 0; i < nTex; ++i) {
                    const std::int32_t t = c.I32();
                    if (i == 0) br.tex0 = t;
                }
                if (c.bad) return Fail(err, "the BRUS chunk ends inside a brush");
                for (float f : br.rgb) if (!Fin(f)) return Fail(err, "a brush colour is not finite or is out of range");
                if (!Fin(br.alpha) || !Fin(br.shiny)) return Fail(err, "a brush value is not finite or is out of range");
                if (brushes.size() >= kMaxBrushes) return Fail(err, "too many brushes");
                brushes.push_back(std::move(br));
            }
        } else if ((inBB3D || inNode) && is("NODE")) {
            if (st.size() >= kMaxDepth) return Fail(err, "nodes nested too deeply");
            if (nodes.size() >= kMaxNodes) return Fail(err, "too many nodes");
            NodeRec nd;
            nd.name = c.CStr(c.Left());
            float t[3], s[3], q[4];
            for (float& f : t) f = c.F32();
            for (float& f : s) f = c.F32();
            for (float& f : q) f = c.F32();
            if (c.bad) return Fail(err, "the file ends inside a node header");
            for (float f : t) if (!Fin(f)) return Fail(err, "a node position is not finite or is out of range");
            for (float f : s) if (!Fin(f)) return Fail(err, "a node scale is not finite or is out of range");
            for (float f : q) if (!std::isfinite(f)) return Fail(err, "a node rotation is not finite");
            bool zero;
            nd.rot = ConvertQuat(q[0], q[1], q[2], q[3], zero);
            if (zero) ++zeroQuats;
            nd.pos = Vec3{t[0], t[1], -t[2]};
            nd.scl = Vec3{s[0], s[1], s[2]};
            nd.parent = inNode ? top.node : -1;
            if (!inNode) ++topNodes;
            const int id = (int)nodes.size();
            nodes.push_back(std::move(nd));
            keysOf.push_back(-1);
            hasBone.push_back(0);
            Frame f; std::memcpy(f.tag, "NODE", 4); f.end = cend; f.node = id; f.mesh = -1;
            b.pos = c.pos;
            st.push_back(f);
            continue;
        } else if (inNode && is("MESH")) {
            MeshRec m;
            m.node = top.node;
            m.brush = c.I32();
            if (c.bad) return Fail(err, "the file ends inside a MESH chunk");
            m.v0 = verts.size();
            m.endVerts = m.v0;
            const int id = (int)meshes.size();
            meshes.push_back(m);
            Frame f; std::memcpy(f.tag, "MESH", 4); f.end = cend; f.node = top.node; f.mesh = id;
            b.pos = c.pos;
            st.push_back(f);
            continue;
        } else if (inMesh && is("VRTS")) {
            MeshRec& m = meshes[(std::size_t)top.mesh];
            const std::int32_t flags = c.I32(), tcSets = c.I32(), tcSize = c.I32();
            if (c.bad || tcSets < 0 || tcSets > 4 || tcSize < 0 || tcSize > 4) return Fail(err, "bad texture coordinate data in a VRTS chunk");
            if (m.endVerts != m.v0) { ++badVertexFormat; b.pos = cend; continue; }   // a second VRTS in one mesh: ignored
            const std::size_t rec = 12 + ((flags & 1) ? 12 : 0) + ((flags & 2) ? 16 : 0) + (std::size_t)tcSets * (std::size_t)tcSize * 4;
            const std::size_t n = c.Left() / rec;
            if (verts.size() + n > kMaxVerts) return Fail(err, "too many vertices");
            m.haveN = (flags & 1) != 0;
            m.haveUv = tcSets > 0 && tcSize >= 2;
            verts.reserve(verts.size() + n);
            for (std::size_t i = 0; i < n; ++i) {
                Vtx v;
                float p[3];
                for (float& f : p) f = c.F32();
                if (!Fin(p[0]) || !Fin(p[1]) || !Fin(p[2])) return Fail(err, "a vertex position is not finite or is out of range");
                v.p = Vec3{p[0], p[1], -p[2]};
                if (flags & 1) {
                    float nn[3];
                    for (float& f : nn) f = c.F32();
                    if (!std::isfinite(nn[0]) || !std::isfinite(nn[1]) || !std::isfinite(nn[2]) || std::fabs(nn[0]) > 1e15f || std::fabs(nn[1]) > 1e15f || std::fabs(nn[2]) > 1e15f) { nn[0] = nn[1] = nn[2] = 0; ++badVertexFormat; }
                    v.n = Vec3{nn[0], nn[1], -nn[2]};
                }
                if (flags & 2) c.Skip(16);   // vertex colour: not imported
                for (std::int32_t j = 0; j < tcSets; ++j) {
                    float t[4] = {0, 0, 0, 0};
                    for (std::int32_t k = 0; k < tcSize; ++k) t[k] = c.F32();
                    if (j == 0) {
                        if (!std::isfinite(t[0]) || !std::isfinite(t[1]) || std::fabs(t[0]) > 1e9f || std::fabs(t[1]) > 1e9f) { t[0] = t[1] = 0; ++badVertexFormat; }
                        v.uv = Vec2{t[0], t[1]};
                    }
                }
                verts.push_back(v);
            }
            if (c.bad) return Fail(err, "the VRTS chunk ends inside a vertex");
            m.nv = n;
            m.endVerts = m.v0 + n;
        } else if (inMesh && is("TRIS")) {
            TrisRec t;
            t.mesh = top.mesh;
            t.brush = c.I32();
            const std::size_t n = c.Left() / 12;
            if (c.bad) return Fail(err, "the file ends inside a TRIS chunk");
            if (totalTris + n > kMaxTris) return Fail(err, "too many triangles");
            totalTris += n;
            t.idx.reserve(n * 3);
            for (std::size_t i = 0; i < n * 3; ++i) t.idx.push_back(c.U32());
            tris.push_back(std::move(t));
        } else if (inNode && is("BONE")) {
            const std::size_t n = c.Left() / 8;
            hasBone[(std::size_t)top.node] = 1;
            for (std::size_t i = 0; i < n; ++i) {
                const std::uint32_t vi = c.U32();
                const float w = c.F32();
                if (vi >= verts.size() || !std::isfinite(w)) { ++badBoneRefs; continue; }
                if (!(w > 0.0f)) continue;
                Vtx& v = verts[vi];
                int slot = -1;
                for (int k = 0; k < 4; ++k) if (v.bone[k] == top.node) { slot = k; break; }
                if (slot >= 0) { v.w[slot] += w; continue; }
                for (int k = 0; k < 4; ++k) if (v.bone[k] < 0) { slot = k; break; }
                if (slot < 0) {   // all four taken: replace the weakest if this one is stronger
                    int weakest = 0;
                    for (int k = 1; k < 4; ++k) if (v.w[k] < v.w[weakest]) weakest = k;
                    ++droppedWeights;
                    if (!(w > v.w[weakest])) continue;
                    slot = weakest;
                }
                v.bone[slot] = top.node;
                v.w[slot] = w;
            }
        } else if (inNode && is("ANIM")) {
            c.I32();
            const std::int32_t frames = c.I32();
            const float fps = c.F32();
            if (c.bad) return Fail(err, "the file ends inside an ANIM chunk");
            ++animChunks;
            if (animChunks == 1) { animFrames = frames; animFps = fps; }
        } else if (inNode && is("KEYS")) {
            const std::int32_t flags = c.I32();
            if (c.bad) return Fail(err, "the file ends inside a KEYS chunk");
            int& ki = keysOf[(std::size_t)top.node];
            if (ki < 0) { ki = (int)keys.size(); keys.emplace_back(); }
            Keys& k = keys[(std::size_t)ki];
            const std::size_t rec = 4 + ((flags & 1) ? 12 : 0) + ((flags & 2) ? 12 : 0) + ((flags & 4) ? 16 : 0);
            const std::size_t n = (flags & 7) ? c.Left() / rec : 0;
            if (totalKeys + n > kMaxKeys) return Fail(err, "too many animation keys");
            totalKeys += n;
            for (std::size_t i = 0; i < n; ++i) {
                const std::int32_t frame = c.I32();
                bool ok = frame >= 0;
                if (flags & 1) { float v[3]; for (float& f : v) f = c.F32(); if (!Fin(v[0]) || !Fin(v[1]) || !Fin(v[2])) ok = false; else if (ok) { k.pos.push_back({(float)frame, Vec3{v[0], v[1], -v[2]}}); } }
                if (flags & 2) { float v[3]; for (float& f : v) f = c.F32(); if (!Fin(v[0]) || !Fin(v[1]) || !Fin(v[2])) ok = false; else if (ok) { k.scl.push_back({(float)frame, Vec3{v[0], v[1], v[2]}}); } }
                if (flags & 4) { float q[4]; for (float& f : q) f = c.F32(); if (!std::isfinite(q[0]) || !std::isfinite(q[1]) || !std::isfinite(q[2]) || !std::isfinite(q[3])) ok = false; else if (ok) { bool z; k.rot.push_back({(float)frame, ConvertQuat(q[0], q[1], q[2], q[3], z)}); } }
                if (!ok) ++badKeys;
            }
            if (c.bad) return Fail(err, "the KEYS chunk ends inside a key");
        } else {
            ++skippedChunks;   // unknown chunk (or a known one in the wrong place): its size says how to skip it
        }
        b.pos = cend;
    }

    if (nodes.empty()) return Fail(err, "the file has no nodes");

    // ---- node tree: one top-level node is the root, several get a synthetic root ----
    const bool synthRoot = topNodes != 1;
    std::vector<std::string> nodeName(nodes.size());
    {
        std::unordered_map<std::string, int> used;
        used["B3D"] = synthRoot ? 1 : 0;
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            std::string nm = nodes[i].name.empty() ? "node" + std::to_string(i) : nodes[i].name;
            int& u = used[nm];
            if (u++) { nm += "_" + std::to_string(u - 1); used[nm] = 1; }
            nodeName[i] = nm;
        }
    }
    const int shift = synthRoot ? 1 : 0;
    if (synthRoot) { ImportedNode r; r.name = "B3D"; out.nodes.push_back(r); }
    std::vector<Mat4> world(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        ImportedNode nd;
        nd.name = nodeName[i];
        nd.parent = nodes[i].parent >= 0 ? nodes[i].parent + shift : (synthRoot ? 0 : -1);
        nd.position = nodes[i].pos; nd.rotation = nodes[i].rot; nd.scale = nodes[i].scl;
        const Mat4 l = Mat4::TRS(nd.position, nd.rotation, nd.scale);
        world[i] = nodes[i].parent >= 0 ? world[(std::size_t)nodes[i].parent] * l : l;
        for (int k = 0; k < 16; ++k) if (!std::isfinite(world[i].m[k]) || std::fabs(world[i].m[k]) > 1e30f) return Fail(err, "node transforms overflow");
        out.nodes.push_back(std::move(nd));
    }

    // ---- materials ----
    for (const Brush& br : brushes) {
        ImportedMaterial im;
        im.name = br.name;
        im.hasDiffuse = true;
        im.diffuse = Color{br.rgb[0], br.rgb[1], br.rgb[2], std::min(1.0f, std::max(0.0f, br.alpha))};
        if (br.shiny > 0) im.shininess = br.shiny * 128.0f;
        if (br.tex0 >= 0) {
            if ((std::size_t)br.tex0 < textures.size()) {
                std::string t = textures[(std::size_t)br.tex0];
                if (t.compare(0, 2, "./") == 0) t.erase(0, 2);
                im.texture = (!baseDir.empty() && !t.empty() && t[0] != '/' && t.find(':') == std::string::npos) ? baseDir + t : t;
            } else ++badTex;
        }
        out.materials.push_back(std::move(im));
    }

    // ---- skeleton: every node with a BONE chunk is a bone; bind pose = inverse of its rest world matrix ----
    std::vector<int> boneOfNode(nodes.size(), -1);
    std::vector<ImportedBone> boneList;
    for (std::size_t i = 0; i < nodes.size(); ++i)
        if (hasBone[i]) { boneOfNode[i] = (int)boneList.size(); ImportedBone ib; ib.name = nodeName[i]; ib.inverseBind = world[i].Inverse(); boneList.push_back(ib); }
    int unweighted = 0;
    bool needRoot = false;
    int rootBone = -1;

    // ---- parts: one per TRIS chunk ----
    std::size_t outVerts = 0;
    int skinnedParts = 0;
    for (const TrisRec& t : tris) {
        const MeshRec& m = meshes[(std::size_t)t.mesh];
        ImportedPart part;
        std::vector<int> remap(m.nv, -1);
        const bool skinnedMesh = !boneList.empty();
        bool anyWeight = false;
        if (skinnedMesh) for (std::size_t i = 0; i < m.nv && !anyWeight; ++i) if (verts[m.v0 + i].bone[0] >= 0) anyWeight = true;
        const bool skinned = skinnedMesh && anyWeight;
        const Mat4& wm = world[(std::size_t)m.node];
        const Mat4 wInv = wm.Inverse();
        for (std::size_t i = 0; i + 2 < t.idx.size(); i += 3) {
            const std::uint32_t a = t.idx[i], bq = t.idx[i + 1], cc = t.idx[i + 2];
            if (a >= m.nv || bq >= m.nv || cc >= m.nv) { ++badTris; continue; }
            if (a == bq || bq == cc || a == cc) { ++badTris; continue; }
            for (std::uint32_t k : {a, cc, bq}) {   // winding reversed (the Z mirror turns it)
                if (remap[k] < 0) {
                    if (++outVerts > kMaxVerts) return Fail(err, "too many output vertices");
                    const Vtx& v = verts[m.v0 + k];
                    remap[k] = (int)part.mesh.vertices.size();
                    Vec3 p = v.p, n = v.n;
                    if (skinned) {   // skinned vertices are placed in model space at the rest pose: the mesh's own node transform is baked in
                        p = wm.MultiplyPoint(p);
                        n = Vec3{wInv.at(0, 0) * n.x + wInv.at(0, 1) * n.y + wInv.at(0, 2) * n.z, wInv.at(1, 0) * n.x + wInv.at(1, 1) * n.y + wInv.at(1, 2) * n.z, wInv.at(2, 0) * n.x + wInv.at(2, 1) * n.y + wInv.at(2, 2) * n.z};
                    }
                    part.mesh.vertices.push_back(p);
                    if (m.haveN) part.mesh.normals.push_back(n);
                    if (m.haveUv) part.mesh.uvs.push_back(v.uv);
                    if (skinned) {
                        std::array<int, 4> ji{0, 0, 0, 0};
                        std::array<float, 4> jw{0, 0, 0, 0};
                        int cnt = 0; float sum = 0;
                        for (int q = 0; q < 4; ++q) if (v.bone[q] >= 0 && v.w[q] > 0 && boneOfNode[(std::size_t)v.bone[q]] >= 0) { ji[(std::size_t)cnt] = boneOfNode[(std::size_t)v.bone[q]]; jw[(std::size_t)cnt] = v.w[q]; sum += v.w[q]; ++cnt; }
                        if (cnt == 0 || !(sum > 0)) {   // no weight: stays where it is, tied to the root node
                            ++unweighted; needRoot = true; ji = {-2, 0, 0, 0}; jw = {1, 0, 0, 0};
                        } else for (float& w : jw) w /= sum;
                        part.jointIdx.push_back(ji);
                        part.jointWt.push_back(jw);
                    }
                }
                part.mesh.triangles.push_back(remap[k]);
            }
        }
        if (part.mesh.triangles.empty()) continue;
        if (m.haveN) {
            bool bad = false;
            for (Vec3& n : part.mesh.normals) { const float l = n.Magnitude(); if (!(l > 1e-9f) || !std::isfinite(l)) { bad = true; break; } n = n * (1.0f / l); }
            if (bad) part.mesh.ComputeSmoothNormals();
        } else part.mesh.ComputeSmoothNormals();
        if (!detail::rc::PartInRange(part)) return Fail(err, "a vertex is out of range");
        part.mesh.name = nodeName[(std::size_t)m.node];
        part.node = skinned ? shift : m.node + shift;
        const int brush = t.brush >= 0 ? t.brush : m.brush;
        if (brush >= 0 && (std::size_t)brush < out.materials.size()) part.material = brush;
        else if (brush >= 0) ++badBrush;
        if (skinned) ++skinnedParts;
        out.parts.push_back(std::move(part));
    }
    if (out.parts.empty()) return Fail(err, "the file has no triangles");
    if (needRoot) {   // a skinned part has vertices without a weight: they follow the root node
        rootBone = (int)boneList.size();
        ImportedBone rb; rb.name = out.nodes[0].name; rb.inverseBind = (synthRoot ? Mat4::Identity() : world[0].Inverse());
        boneList.push_back(rb);
    }
    for (ImportedPart& p : out.parts) {
        if (p.jointIdx.empty()) continue;
        for (auto& ji : p.jointIdx) for (int& x : ji) if (x == -2) x = rootBone;
        p.bones = boneList;
    }

    // ---- animation: times in frames at the ANIM chunk's rate ----
    if (!keys.empty()) {
        float fps = animFps;
        const bool haveAnim = animChunks > 0;
        if (!(fps > 0.0f) || !std::isfinite(fps) || fps > 1e6f) fps = 60.0f;
        ImportedClip clip;
        clip.name = "animation";
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (keysOf[i] < 0) continue;
            Keys& k = keys[(std::size_t)keysOf[i]];
            ImportedChannel ch;
            ch.node = nodeName[i];
            auto sortKeys = [&](auto& v) {
                bool sorted = true;
                for (std::size_t q = 1; q < v.size(); ++q) if (v[q].time < v[q - 1].time) sorted = false;
                if (!sorted) { ++unsortedKeys; std::stable_sort(v.begin(), v.end(), [](const auto& a, const auto& b2) { return a.time < b2.time; }); }
                for (auto& e : v) e.time = e.time / fps;
            };
            sortKeys(k.pos); sortKeys(k.scl); sortKeys(k.rot);
            ch.position = std::move(k.pos); ch.scale = std::move(k.scl); ch.rotation = std::move(k.rot);
            if (!ch.position.empty() || !ch.scale.empty() || !ch.rotation.empty()) clip.channels.push_back(std::move(ch));
        }
        if (!clip.channels.empty()) out.clips.push_back(std::move(clip));
        if (!haveAnim) out.notes.push_back("B3D: key frames without an ANIM chunk: played at 60 frames per second");
        (void)animFrames;
    }

    out.backend = "own";
    auto note = [&](const std::string& s) { out.notes.push_back("B3D: " + s); };
    note("Blitz3D is left-handed: Z was mirrored and the winding reversed to make the model right-handed");
    if (version != 1) note("file version " + std::to_string(version) + " (1 is the version the notes describe): read the same way");
    if (synthRoot) note(std::to_string(topNodes) + " top-level nodes: they were put under a new root node");
    if (skinnedParts) note("skinned vertices are placed in model space at the rest pose (the mesh's own node transform is baked in)");
    if (unweighted) note(std::to_string(unweighted) + " vertex(es) of skinned meshes have no weight and follow the root node");
    if (droppedWeights) note(std::to_string(droppedWeights) + " bone weight(s) beyond four per vertex: the four strongest were kept");
    if (badBoneRefs) note(std::to_string(badBoneRefs) + " bone weight(s) naming a vertex that does not exist or a weight that is not finite skipped");
    if (badTris) note(std::to_string(badTris) + " triangle(s) with an out-of-range or repeated index skipped");
    if (badBrush) note(std::to_string(badBrush) + " mesh(es) naming a brush that does not exist left without a material");
    if (badTex) note(std::to_string(badTex) + " brush(es) naming a texture that does not exist left without one");
    if (badKeys) note(std::to_string(badKeys) + " animation key(s) with a negative frame or a value that is not finite skipped");
    if (unsortedKeys) note(std::to_string(unsortedKeys) + " key list(s) out of time order were sorted");
    if (zeroQuats) note(std::to_string(zeroQuats) + " rotation(s) of zero length were set to no rotation");
    if (badVertexFormat) note(std::to_string(badVertexFormat) + " vertex value(s) that were not finite were set to 0, or a second VRTS chunk was ignored");
    if (animChunks > 1) note(std::to_string(animChunks) + " ANIM chunks: the first one gives the frame rate");
    if (skippedChunks) note(std::to_string(skippedChunks) + " chunk(s) of unknown type or in an unexpected place skipped");
    note("vertex colours, texture blend modes, flags and transforms, further texture coordinate sets, brush effects (two-sided, fullbright ...) and the brush's specular are not imported");
    return true;
}

bool ReadB3dBuffer(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    const bool ok = ReadB3dImpl(bytes, baseDir, out, err);
    if (!ok) out = ImportedModel{};
    return ok;
}

bool ReadB3dFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, b3d::kMaxFileBytes, bytes, err)) return false;
    std::string dir;
    const std::size_t sl = path.find_last_of("/\\");
    if (sl != std::string::npos) dir = path.substr(0, sl + 1);
    return ReadB3dBuffer(bytes, dir, out, err);
}

} // namespace okay
