// Our own MS3D (MilkShape 3D, binary) reader: groups of triangles with per-corner normals and texture coordinates,
// materials, a joint hierarchy with vertex weights and keyframe animation. Written from the public description of the
// format (Mete Ciragan's MS3D file format notes). Bounds-checked, capped, no recursion. See okay/Render/ModelData.hpp
// and docs/own_ms3d_reader.md.
#include "TextReaderUtil.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

namespace {
namespace ms3d {

using namespace detail::txt;

constexpr std::size_t kMaxFileBytes = 256u * 1024u * 1024u;
constexpr std::size_t kMaxKeys      = 20000000;   // rotation + translation keys over all joints
constexpr std::size_t kMaxOutVerts  = 3u * 65535u * 16u;
constexpr double      kMaxCoord     = 1e15;

// Little endian reader over a buffer; every read is bounds checked, a failed read latches `bad`.
struct Br {
    const unsigned char* d = nullptr;
    std::size_t n = 0, pos = 0;
    bool bad = false;
    std::size_t Left() const { return n - pos; }
    bool Has(std::size_t k) { if (k > Left()) { bad = true; return false; } return true; }
    std::uint8_t U8() { if (!Has(1)) return 0; return d[pos++]; }
    std::int8_t I8() { return (std::int8_t)U8(); }
    std::uint16_t U16() { if (!Has(2)) return 0; const std::uint16_t v = (std::uint16_t)(d[pos] | (d[pos + 1] << 8)); pos += 2; return v; }
    std::uint32_t U32() { if (!Has(4)) return 0; const std::uint32_t v = (std::uint32_t)d[pos] | ((std::uint32_t)d[pos + 1] << 8) | ((std::uint32_t)d[pos + 2] << 16) | ((std::uint32_t)d[pos + 3] << 24); pos += 4; return v; }
    std::int32_t I32() { return (std::int32_t)U32(); }
    float F32() { const std::uint32_t u = U32(); float f; std::memcpy(&f, &u, 4); return f; }
    void Skip(std::size_t k) { if (Has(k)) pos += k; }
    // fixed-size, NUL-padded text field
    std::string Str(std::size_t len) {
        if (!Has(len)) return std::string();
        std::size_t e = 0;
        while (e < len && d[pos + e] != 0) ++e;
        std::string s((const char*)d + pos, e);
        pos += len;
        return s;
    }
};

struct Vtx { Vec3 p; int bone = -1; int extraBone[3] = {-1, -1, -1}; float extraW[3] = {0, 0, 0}; bool hasExtra = false; };
struct Tri { int v[3]; Vec3 n[3]; float s[3], t[3]; };
struct Grp { std::string name; std::vector<int> tris; int mat = -1; };
struct Mat {
    std::string name, texture, alphamap;
    float ambient[4], diffuse[4], specular[4], emissive[4];
    float shininess = 0, transparency = 1;
};
struct KeyV { float time; Vec3 v; };
struct Jnt {
    std::string name, parent;
    Vec3 rot{0, 0, 0}, pos{0, 0, 0};
    std::vector<KeyV> rotKeys, posKeys;
};

// MilkShape / Quake angles (radians about X, Y, Z): rotation = Rz * Ry * Rx
static Quat FromEuler(const Vec3& a) {
    const float sr = std::sin(a.x * 0.5f), cr = std::cos(a.x * 0.5f);
    const float sp = std::sin(a.y * 0.5f), cp = std::cos(a.y * 0.5f);
    const float sy = std::sin(a.z * 0.5f), cy = std::cos(a.z * 0.5f);
    return Quat{sr * cp * cy - cr * sp * sy, cr * sp * cy + sr * cp * sy, cr * cp * sy - sr * sp * cy, cr * cp * cy + sr * sp * sy}.Normalized();
}

static bool FiniteV(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
static bool Reasonable(const Vec3& v) { return FiniteV(v) && std::fabs(v.x) <= kMaxCoord && std::fabs(v.y) <= kMaxCoord && std::fabs(v.z) <= kMaxCoord; }

struct Reader {
    std::string err;
    std::string baseDir;
    bool Fail(const std::string& m) { if (err.empty()) err = "MS3D: " + m; return false; }

    std::vector<Vtx> verts;
    std::vector<Tri> tris;
    std::vector<Grp> groups;
    std::vector<Mat> mats;
    std::vector<Jnt> joints;
    float fps = 24.0f;
    int version = 0;
    int commentsIgnored = 0;
    bool extraBad = false, extraWeights = false;
    int badTris = 0, badGroupTris = 0, badMatIdx = 0, badBoneRefs = 0, badKeys = 0, unsortedKeys = 0, dupJointNames = 0, orphanTris = 0, badWeightSums = 0, unassigned = 0;

    bool Parse(const std::string& bytes) {
        Br b;
        b.d = (const unsigned char*)bytes.data();
        b.n = bytes.size();
        if (b.n < 14) return Fail("file too short");
        if (std::memcmp(b.d, "MS3D000000", 10) != 0) return Fail("not an MS3D file (the magic text MS3D000000 is missing)");
        b.pos = 10;
        version = b.I32();
        if (version != 3 && version != 4) return Fail("unsupported version " + std::to_string(version) + " (3 and 4 are read)");

        // vertices: flags, xyz, bone id, reference count (15 bytes each)
        const std::size_t nv = b.U16();
        if (!b.Has(nv * 15)) return Fail("the file ends inside the vertex list");
        verts.resize(nv);
        for (Vtx& v : verts) {
            b.U8();
            v.p = Vec3{b.F32(), b.F32(), b.F32()};
            v.bone = b.I8();
            b.U8();
            if (!Reasonable(v.p)) return Fail("a vertex position is not finite or is out of range");
        }
        // triangles: flags, 3 indices, 3 normals, 3 s, 3 t, smoothing group, group (70 bytes each)
        const std::size_t nt = b.U16();
        if (!b.Has(nt * 70)) return Fail("the file ends inside the triangle list");
        tris.resize(nt);
        for (Tri& t : tris) {
            b.U16();
            for (int k = 0; k < 3; ++k) t.v[k] = b.U16();
            for (int k = 0; k < 3; ++k) t.n[k] = Vec3{b.F32(), b.F32(), b.F32()};
            for (int k = 0; k < 3; ++k) t.s[k] = b.F32();
            for (int k = 0; k < 3; ++k) t.t[k] = b.F32();
            b.U8(); b.U8();
            for (int k = 0; k < 3; ++k) if (!std::isfinite(t.s[k]) || !std::isfinite(t.t[k]) || std::fabs(t.s[k]) > kMaxCoord || std::fabs(t.t[k]) > kMaxCoord) return Fail("a texture coordinate is not finite or is out of range");
        }
        // groups
        const std::size_t ng = b.U16();
        if (!b.Has(ng * 36)) return Fail("the file ends inside the group list");
        groups.resize(ng);
        for (Grp& g : groups) {
            b.U8();
            g.name = b.Str(32);
            const std::size_t k = b.U16();
            if (!b.Has(k * 2 + 1)) return Fail("the file ends inside a group");
            g.tris.resize(k);
            for (std::size_t i = 0; i < k; ++i) g.tris[i] = b.U16();
            g.mat = b.I8();
        }
        // materials (361 bytes each)
        const std::size_t nm = b.U16();
        if (!b.Has(nm * 361)) return Fail("the file ends inside the material list");
        mats.resize(nm);
        for (Mat& m : mats) {
            m.name = b.Str(32);
            for (float* c : {m.ambient, m.diffuse, m.specular, m.emissive}) for (int k = 0; k < 4; ++k) c[k] = b.F32();
            m.shininess = b.F32(); m.transparency = b.F32();
            b.U8();
            m.texture = b.Str(128);
            m.alphamap = b.Str(128);
            for (float* c : {m.ambient, m.diffuse, m.specular, m.emissive}) for (int k = 0; k < 4; ++k) if (!std::isfinite(c[k])) c[k] = 0.0f;
            if (!std::isfinite(m.shininess)) m.shininess = 0.0f;
            if (!std::isfinite(m.transparency)) m.transparency = 1.0f;
        }
        // animation settings and joints
        if (!b.Has(14)) return Fail("the file ends before the animation settings");
        fps = b.F32();
        b.F32();
        b.U32();
        if (!(fps > 0.0f) || !std::isfinite(fps) || fps > 100000.0f) fps = 24.0f;
        const std::size_t nj = b.U16();
        if (!b.Has(nj * 93)) return Fail("the file ends inside the joint list");
        joints.resize(nj);
        std::size_t keyTotal = 0;
        for (Jnt& j : joints) {
            b.U8();
            j.name = b.Str(32);
            j.parent = b.Str(32);
            j.rot = Vec3{b.F32(), b.F32(), b.F32()};
            j.pos = Vec3{b.F32(), b.F32(), b.F32()};
            const std::size_t nr = b.U16(), np = b.U16();
            keyTotal += nr + np;
            if (keyTotal > kMaxKeys) return Fail("too many animation keys");
            if (!b.Has((nr + np) * 16)) return Fail("the file ends inside a joint's keys");
            j.rotKeys.resize(nr); j.posKeys.resize(np);
            for (KeyV& k : j.rotKeys) { k.time = b.F32(); k.v = Vec3{b.F32(), b.F32(), b.F32()}; }
            for (KeyV& k : j.posKeys) { k.time = b.F32(); k.v = Vec3{b.F32(), b.F32(), b.F32()}; }
            if (!FiniteV(j.rot) || !Reasonable(j.pos)) return Fail("a joint transform is not finite or is out of range");
        }
        if (b.bad) return Fail("the file is truncated");
        ParseExtras(b);
        return true;
    }

    // Comments, then the optional per-vertex extra bone weights. Anything wrong here leaves the rest of the model as read.
    void ParseExtras(Br& b) {
        if (b.Left() <= 4) return;
        const std::uint32_t sub = b.U32();
        if (sub != 1) { extraBad = true; return; }
        auto comments = [&](bool indexed) -> bool {
            const std::uint32_t n = b.U32();
            if (b.bad || n > b.Left() / 4) return false;
            for (std::uint32_t i = 0; i < n; ++i) {
                if (indexed) b.I32();
                const std::uint32_t len = b.U32();
                if (b.bad || len > b.Left()) return false;
                if (len) ++commentsIgnored;
                b.Skip(len);
            }
            return !b.bad;
        };
        if (!comments(true) || !comments(true) || !comments(true) || !comments(false)) { extraBad = true; return; }
        if (b.Left() >= 4) {
            const std::uint32_t sub2 = b.U32();
            if (sub2 >= 1 && sub2 <= 3) {
                const std::size_t rec = 6 + (sub2 - 1) * 4;
                if (b.Left() < rec * verts.size()) { extraBad = true; return; }
                for (Vtx& v : verts) {
                    for (int k = 0; k < 3; ++k) v.extraBone[k] = b.I8();
                    for (int k = 0; k < 3; ++k) v.extraW[k] = (float)b.U8() / 100.0f;   // percent
                    b.Skip((sub2 - 1) * 4);
                    v.hasExtra = true;
                }
                extraWeights = true;
            } else extraBad = true;
        }
    }

    bool Build(ImportedModel& out) {
        const std::size_t nj = joints.size();
        ImportedNode root;
        root.name = "MS3D";
        out.nodes.push_back(root);

        // ---- skeleton ----
        std::vector<std::string> nodeName(nj);
        std::vector<int> nodeOf(nj, 0), par(nj, -1);
        std::vector<Mat4> abs(nj);
        {
            std::unordered_map<std::string, int> first;
            for (std::size_t i = 0; i < nj; ++i) { auto it = first.find(joints[i].name); if (it == first.end()) first.emplace(joints[i].name, (int)i); else ++dupJointNames; }
            int missingParent = 0;
            for (std::size_t i = 0; i < nj; ++i) {
                if (joints[i].parent.empty()) continue;
                auto it = first.find(joints[i].parent);
                if (it != first.end() && it->second != (int)i) par[i] = it->second; else ++missingParent;
            }
            std::vector<int> order;
            const int loops = OrderParentsFirst(par, order);
            std::unordered_map<std::string, int> used;
            used["MS3D"] = 1;
            for (int ji : order) {
                std::string nm = joints[(std::size_t)ji].name.empty() ? "joint" + std::to_string(ji) : joints[(std::size_t)ji].name;
                int& u = used[nm];
                if (u++) { nm += "_" + std::to_string(u - 1); used[nm] = 1; }
                nodeName[(std::size_t)ji] = nm;
                ImportedNode nd;
                nd.name = nm;
                nd.parent = par[(std::size_t)ji] < 0 ? 0 : nodeOf[(std::size_t)par[(std::size_t)ji]];
                nd.position = joints[(std::size_t)ji].pos;
                nd.rotation = FromEuler(joints[(std::size_t)ji].rot);
                const Mat4 l = Mat4::TRS(nd.position, nd.rotation, Vec3{1, 1, 1});
                abs[(std::size_t)ji] = par[(std::size_t)ji] < 0 ? l : abs[(std::size_t)par[(std::size_t)ji]] * l;
                nodeOf[(std::size_t)ji] = (int)out.nodes.size();
                out.nodes.push_back(std::move(nd));
            }
            if (missingParent) skelNote.push_back(std::to_string(missingParent) + " joint(s) name a parent that does not exist and were attached to the root");
            if (loops) skelNote.push_back(std::to_string(loops) + " joint parent loop(s) were cut");
            for (const Mat4& m : abs) for (int k = 0; k < 16; ++k) if (!std::isfinite(m.m[k]) || std::fabs(m.m[k]) > 1e30f) return Fail("joint transforms overflow");
        }

        // ---- materials ----
        for (const Mat& m : mats) {
            ImportedMaterial im;
            im.name = m.name;
            std::string tex = m.texture;
            for (char& c : tex) if (c == '\\') c = '/';
            if (tex.compare(0, 2, "./") == 0) tex.erase(0, 2);
            if (!tex.empty()) im.texture = (!baseDir.empty() && tex[0] != '/' && tex.find(':') == std::string::npos) ? baseDir + tex : tex;
            im.hasDiffuse = true;
            im.diffuse = Color{m.diffuse[0], m.diffuse[1], m.diffuse[2], std::min(1.0f, std::max(0.0f, m.transparency))};
            if (m.emissive[0] > 0 || m.emissive[1] > 0 || m.emissive[2] > 0) { im.hasEmissive = true; im.emissive = Color{m.emissive[0], m.emissive[1], m.emissive[2], 1.0f}; }
            if (m.shininess > 0) im.shininess = m.shininess;
            if (!m.alphamap.empty()) ++alphamaps;
            if (m.transparency < 0.999f) ++transparent;
            out.materials.push_back(std::move(im));
        }

        // ---- meshes: one part per group ----
        const bool skinned = nj > 0;
        std::vector<ImportedBone> bones;
        bool needRootBone = false;
        if (skinned) {
            bones.resize(nj);
            for (std::size_t i = 0; i < nj; ++i) { bones[i].name = nodeName[i]; bones[i].inverseBind = abs[i].Inverse(); }
        }
        std::vector<char> inGroup(tris.size(), 0);
        std::size_t outVerts = 0;
        std::vector<ImportedPart> parts;
        for (const Grp& g : groups) {
            ImportedPart part;
            std::unordered_map<std::string, int> weld;
            bool badNormal = false;
            for (int ti : g.tris) {
                if (ti < 0 || (std::size_t)ti >= tris.size()) { ++badGroupTris; continue; }
                inGroup[(std::size_t)ti] = 1;
                const Tri& t = tris[(std::size_t)ti];
                if (t.v[0] >= (int)verts.size() || t.v[1] >= (int)verts.size() || t.v[2] >= (int)verts.size()) { ++badTris; continue; }
                if (t.v[0] == t.v[1] || t.v[1] == t.v[2] || t.v[0] == t.v[2]) { ++degenerate; continue; }
                for (int k = 0; k < 3; ++k) {
                    Vec3 n = t.n[k];
                    const float nm = n.Magnitude();
                    if (!(nm > 1e-6f) || !std::isfinite(nm) || nm > 1e15f) { badNormal = true; n = Vec3{0, 1, 0}; } else n = n * (1.0f / nm);
                    std::uint32_t key[7];
                    key[0] = (std::uint32_t)t.v[k];
                    const float f[6] = {n.x, n.y, n.z, t.s[k], t.t[k], 0.0f};
                    for (int q = 0; q < 5; ++q) std::memcpy(&key[1 + q], &f[q], 4);
                    key[6] = 0;
                    const std::string ks((const char*)key, sizeof key);
                    auto it = weld.find(ks);
                    int vi;
                    if (it == weld.end()) {
                        if (++outVerts > kMaxOutVerts) return Fail("too many output vertices");
                        vi = (int)part.mesh.vertices.size();
                        const Vtx& v = verts[(std::size_t)t.v[k]];
                        part.mesh.vertices.push_back(v.p);
                        part.mesh.normals.push_back(n);
                        part.mesh.uvs.push_back(Vec2{t.s[k], t.t[k]});
                        if (skinned) {
                            std::array<int, 4> ji{0, 0, 0, 0};
                            std::array<float, 4> jw{0, 0, 0, 0};
                            int cnt = 0;
                            float used = 0;
                            auto add = [&](int bone, float w) {
                                if (bone < 0 || w <= 0.0f) return;
                                if ((std::size_t)bone >= nj) { ++badBoneRefs; return; }
                                for (int q = 0; q < cnt; ++q) if (ji[(std::size_t)q] == bone) { jw[(std::size_t)q] += w; used += w; return; }
                                if (cnt < 4) { ji[(std::size_t)cnt] = bone; jw[(std::size_t)cnt] = w; ++cnt; used += w; }
                            };
                            if (v.hasExtra) {
                                float sumExtra = 0;
                                for (int q = 0; q < 3; ++q) if (v.extraBone[q] >= 0) sumExtra += v.extraW[q];
                                if (sumExtra > 1.0f + 1e-4f) ++badWeightSums;
                                const float w0 = std::max(0.0f, 1.0f - sumExtra);
                                add(v.bone, w0);
                                for (int q = 0; q < 3; ++q) add(v.extraBone[q], v.extraW[q]);
                            } else add(v.bone, 1.0f);
                            if (cnt == 0 || !(used > 0.0f)) {   // not bound to a joint: stays where it is, tied to the root
                                needRootBone = true;
                                ++unassigned;
                                ji = {(int)nj, 0, 0, 0}; jw = {1, 0, 0, 0};
                            } else for (float& w : jw) w /= used;
                            part.jointIdx.push_back(ji);
                            part.jointWt.push_back(jw);
                        }
                        weld.emplace(ks, vi);
                    } else vi = it->second;
                    part.mesh.triangles.push_back(vi);
                }
            }
            if (part.mesh.triangles.empty()) { ++emptyGroups; continue; }
            part.mesh.name = g.name;
            if (badNormal) { part.mesh.ComputeSmoothNormals(); ++badNormalParts; }
            part.node = 0;
            if (g.mat >= 0 && (std::size_t)g.mat < out.materials.size()) part.material = g.mat;
            else if (g.mat >= 0) ++badMatIdx;
            parts.push_back(std::move(part));
        }
        for (std::size_t i = 0; i < tris.size(); ++i) if (!inGroup[i]) ++orphanTris;
        if (needRootBone) { ImportedBone rb; rb.name = "MS3D"; rb.inverseBind = Mat4::Identity(); bones.push_back(rb); }
        for (ImportedPart& p : parts) { if (skinned) p.bones = bones; out.parts.push_back(std::move(p)); }
        if (out.parts.empty()) return Fail("the file has no triangles in any group");

        // ---- animation: every key is relative to the joint's own rest values ----
        if (nj > 0) {
            ImportedClip clip;
            clip.name = "animation";
            for (std::size_t i = 0; i < nj; ++i) {
                const Jnt& j = joints[i];
                if (j.rotKeys.empty() && j.posKeys.empty()) continue;
                ImportedChannel ch;
                ch.node = nodeName[i];
                const Quat rest = FromEuler(j.rot);
                std::vector<KeyV> rk = j.rotKeys, pk = j.posKeys;
                for (std::vector<KeyV>* ks : {&rk, &pk}) {
                    std::vector<KeyV> good;
                    for (const KeyV& k : *ks) { if (!std::isfinite(k.time) || k.time < 0.0f || k.time > 1e7f || !Reasonable(k.v)) { ++badKeys; continue; } good.push_back(k); }
                    bool sorted = true;
                    for (std::size_t q = 1; q < good.size(); ++q) if (good[q].time < good[q - 1].time) sorted = false;
                    if (!sorted) { ++unsortedKeys; std::stable_sort(good.begin(), good.end(), [](const KeyV& a, const KeyV& b) { return a.time < b.time; }); }
                    ks->swap(good);
                }
                for (const KeyV& k : rk) ch.rotation.push_back({k.time, (rest * FromEuler(k.v)).Normalized()});
                for (const KeyV& k : pk) ch.position.push_back({k.time, j.pos + k.v});
                if (!ch.rotation.empty() || !ch.position.empty()) clip.channels.push_back(std::move(ch));
            }
            if (!clip.channels.empty()) out.clips.push_back(std::move(clip));
        }

        auto note = [&](const std::string& s) { out.notes.push_back("MS3D: " + s); };
        if (version == 3) note("version 3 file: read with the version 4 layout (no real version 3 file was available to check)");
        if (!out.clips.empty()) note("key times are seconds as written; each key is applied on top of the joint's rest rotation and position");
        for (const std::string& s : skelNote) note(s);
        if (dupJointNames) note(std::to_string(dupJointNames) + " joint name(s) are repeated: a parent name picks the first joint with that name, the others got a numeric suffix");
        if (extraBad) note("the extra data after the joints (comments, per-vertex weights) is malformed or of an unknown sub-version and was ignored");
        if (commentsIgnored) note(std::to_string(commentsIgnored) + " comment(s) not imported");
        if (nj > 0 && !extraWeights) note("no extra vertex weights in the file: each vertex follows its one joint");
        if (badWeightSums) note(std::to_string(badWeightSums) + " vertex(es) whose extra weights add up to more than 100 percent: the first joint gets nothing and the weights were normalised");
        if (unassigned) note(std::to_string(unassigned) + " vertex(es) without a joint stay in place (tied to the root node)");
        if (badBoneRefs) note(std::to_string(badBoneRefs) + " vertex joint reference(s) to missing joints ignored");
        if (badTris) note(std::to_string(badTris) + " triangle(s) with an out-of-range vertex index skipped");
        if (badGroupTris) note(std::to_string(badGroupTris) + " group entries naming a triangle that does not exist skipped");
        if (degenerate) note(std::to_string(degenerate) + " degenerate triangle(s) skipped");
        if (orphanTris) note(std::to_string(orphanTris) + " triangle(s) in no group are not imported");
        if (emptyGroups) note(std::to_string(emptyGroups) + " group(s) without usable triangles skipped");
        if (badMatIdx) note(std::to_string(badMatIdx) + " group(s) naming a material that does not exist were left without one");
        if (badNormalParts) note(std::to_string(badNormalParts) + " group(s) had zero or invalid normals: smooth normals were generated");
        if (badKeys) note(std::to_string(badKeys) + " animation key(s) with a negative or non-finite time or value skipped");
        if (unsortedKeys) note(std::to_string(unsortedKeys) + " key list(s) out of time order were sorted");
        if (alphamaps) note(std::to_string(alphamaps) + " alpha map(s) not imported");
        if (transparent) note(std::to_string(transparent) + " material(s) with transparency: it is stored in the diffuse alpha");
        if (!mats.empty()) note("ambient and specular colors, the material mode and the smoothing groups are not imported (normals come per corner)");
        if (!mats.empty()) note("texture coordinates are kept as written; texture paths use '/' and are relative to the model's folder");
        return true;
    }
    std::vector<std::string> skelNote;
    int alphamaps = 0, transparent = 0, degenerate = 0, emptyGroups = 0, badNormalParts = 0;
};

} // namespace ms3d
} // namespace

bool ReadMs3dBuffer(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    ms3d::Reader r;
    r.baseDir = baseDir;
    if (!r.Parse(bytes) || !r.Build(out)) {
        const std::string m = r.err.empty() ? "MS3D: could not read file" : r.err;
        out = ImportedModel{};
        if (err) *err = m;
        return false;
    }
    out.backend = "own";
    return true;
}

bool ReadMs3dFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, ms3d::kMaxFileBytes, bytes, err)) return false;
    std::string dir;
    const std::size_t sl = path.find_last_of("/\\");
    if (sl != std::string::npos) dir = path.substr(0, sl + 1);
    return ReadMs3dBuffer(bytes, dir, out, err);
}

} // namespace okay
