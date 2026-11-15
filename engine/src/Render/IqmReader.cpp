// Our own IQM (Inter-Quake Model, version 2, binary) reader: meshes with positions, normals, texture coordinates and
// four skin weights per vertex, a joint hierarchy, and frame animations stored as per-pose channel masks.
// Written from my memory of the IQM specification text (Lee Salzman's iqm.txt: the header, the iqmmesh / iqmtriangle / iqmjoint /
// iqmpose / iqmanim / iqmvertexarray records and the frame-channel packing rule; it was not fetched during the work), with every layout detail
// checked against a hex dump of one real file; no code from any other importer was read. Bounds-checked, every count is checked
// against the file size and a per-file budget before anything is allocated, no recursion, no unbounded allocation.
// See okay/Render/ModelData.hpp and docs/own_iqm_reader.md.
#include "BinReader.hpp"
#include "ReaderCommon.hpp"
#include "SceneRefPath.hpp"
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
namespace iqm {

using detail::bin::Br;

constexpr std::size_t kMaxFileBytes = 128u * 1024u * 1024u;
// Per-file budgets. The vertices and triangles are counted over all meshes (two meshes may name the same range of the
// shared arrays, so each mesh counts again). A mesh is one part and every part carries a copy of the whole bone list.
constexpr std::uint32_t kMaxVerts = 4000000, kMaxTris = 4000000, kMaxJoints = 10000, kMaxMeshes = 10000, kMaxAnims = 10000, kMaxVertexArrays = 64;
constexpr std::uint64_t kMaxJointFrames = 500000;   // frames x poses over all animations (each costs up to three keys)
constexpr std::uint32_t kMaxOutVerts = 1000000, kMaxOutTris = 1000000;   // FILE-WIDE output: summed over all meshes, so one small range named by many meshes counts each time
constexpr std::uint64_t kMaxBoneRefs = 100000;      // meshes x joints
constexpr std::uint64_t kMaxBoneNameBytes = 16u * 1024u * 1024u;   // meshes x the joint names' bytes (every part carries a copy of the bone list)
constexpr double kMaxCoord = 1e15;
constexpr std::size_t kMaxName = 256;

enum { kPosition = 0, kTexcoord = 1, kNormal = 2, kTangent = 3, kBlendIndexes = 4, kBlendWeights = 5, kColor = 6 };
enum { kByte = 0, kUByte, kShort, kUShort, kInt, kUInt, kHalf, kFloat, kDouble };

bool Fail(std::string* err, const std::string& m) { if (err) *err = "IQM: " + m; return false; }

std::uint32_t FormatBytes(std::uint32_t f) {
    switch (f) { case kByte: case kUByte: return 1; case kShort: case kUShort: case kHalf: return 2; case kInt: case kUInt: case kFloat: return 4; case kDouble: return 8; }
    return 0;
}

float HalfToFloat(std::uint16_t h) {
    const int s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
    float v;
    if (e == 0) v = std::ldexp((float)m, -24);
    else if (e == 31) v = m ? NAN : INFINITY;
    else v = std::ldexp((float)(1024 + m), e - 25);
    return s ? -v : v;
}

struct VArr {
    bool present = false;
    std::uint32_t fmt = 0, size = 0;
    std::uint64_t off = 0;
};

// One component of element `i` (components per element: a.size) as a number. The array was checked to lie in the file.
double Comp(const std::string& b, const VArr& a, std::uint64_t i, std::uint32_t k) {
    const std::uint32_t fs = FormatBytes(a.fmt);
    const unsigned char* p = (const unsigned char*)b.data() + a.off + (i * a.size + k) * fs;
    switch (a.fmt) {
        case kByte: return (double)(std::int8_t)p[0];
        case kUByte: return (double)p[0];
        case kShort: return (double)(std::int16_t)(p[0] | (p[1] << 8));
        case kUShort: return (double)(std::uint16_t)(p[0] | (p[1] << 8));
        case kInt: { std::uint32_t u = (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) | ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24); return (double)(std::int32_t)u; }
        case kUInt: { std::uint32_t u = (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) | ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24); return (double)u; }
        case kHalf: return (double)HalfToFloat((std::uint16_t)(p[0] | (p[1] << 8)));
        case kFloat: { std::uint32_t u = (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) | ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24); float f; std::memcpy(&f, &u, 4); return (double)f; }
        case kDouble: { std::uint64_t u = 0; for (int q = 7; q >= 0; --q) u = (u << 8) | p[q]; double d; std::memcpy(&d, &u, 8); return d; }
    }
    return 0.0;
}

bool Reasonable(double v) { return std::isfinite(v) && std::fabs(v) <= kMaxCoord; }

struct Joint { std::string name; int parent = -1; Vec3 t{0, 0, 0}; Quat r{0, 0, 0, 1}; Vec3 s{1, 1, 1}; };
struct Pose { std::uint32_t mask = 0; float off[10] = {}, scale[10] = {}; };
struct Anim { std::string name; std::uint32_t first = 0, count = 0; float rate = 0; };
struct MeshRec { std::string name, material; std::uint32_t firstVert = 0, numVerts = 0, firstTri = 0, numTris = 0; };

Quat NormalizedQ(float x, float y, float z, float w, bool* zero = nullptr) {
    const double l = std::sqrt((double)x * x + (double)y * y + (double)z * z + (double)w * w);
    if (!(l > 1e-12) || !std::isfinite(l)) { if (zero) *zero = true; return Quat{0, 0, 0, 1}; }
    return Quat{(float)(x / l), (float)(y / l), (float)(z / l), (float)(w / l)};
}

bool IsImageName(const std::string& s) {
    const std::size_t d = s.find_last_of('.');
    if (d == std::string::npos) return false;
    const std::string e = detail::txt::Lower(s.substr(d + 1));
    for (const char* k : {"png", "jpg", "jpeg", "tga", "bmp", "gif", "dds", "tif", "tiff", "webp", "pcx", "hdr", "ktx"}) if (e == k) return true;
    return false;
}

} // namespace iqm
} // namespace

static bool ReadIqmImpl(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err) {
    using namespace iqm;
    const std::size_t n = bytes.size();
    Br b(bytes);
    if (n < 124) return Fail(err, "the file is too short for an IQM header");
    if (std::memcmp(bytes.data(), "INTERQUAKEMODEL\0", 16) != 0) return Fail(err, "not an IQM file (the magic text INTERQUAKEMODEL is missing)");
    b.Skip(16);
    std::uint32_t h[27];
    for (std::uint32_t& v : h) v = b.U32();
    const std::uint32_t version = h[0], filesize = h[1];
    const std::uint32_t numText = h[3], ofsText = h[4], numMeshes = h[5], ofsMeshes = h[6], numVA = h[7], numVerts = h[8], ofsVA = h[9];
    const std::uint32_t numTris = h[10], ofsTris = h[11], numJoints = h[13], ofsJoints = h[14], numPoses = h[15], ofsPoses = h[16];
    const std::uint32_t numAnims = h[17], ofsAnims = h[18], numFrames = h[19], numFrameCh = h[20], ofsFrames = h[21];
    if (version != 2) return Fail(err, "unsupported version " + std::to_string(version) + " (version 2 is read; version 1 files store joint rotations as Euler angles and are not supported)");
    if (filesize > n) return Fail(err, "the file is truncated (the header says " + std::to_string(filesize) + " bytes, " + std::to_string(n) + " are there)");

    // budgets first, then the table bounds, so a hostile count is refused before it can size anything
    if (numMeshes > kMaxMeshes) return Fail(err, "too many meshes (more than " + std::to_string(kMaxMeshes) + ")");
    if (numVerts > kMaxVerts) return Fail(err, "too many vertices (more than " + std::to_string(kMaxVerts) + ")");
    if (numTris > kMaxTris) return Fail(err, "too many triangles (more than " + std::to_string(kMaxTris) + ")");
    if (numJoints > kMaxJoints) return Fail(err, "too many joints (more than " + std::to_string(kMaxJoints) + ")");
    if (numPoses > kMaxJoints) return Fail(err, "too many poses (more than " + std::to_string(kMaxJoints) + ")");
    if (numAnims > kMaxAnims) return Fail(err, "too many animations (more than " + std::to_string(kMaxAnims) + ")");
    if (numVA > kMaxVertexArrays) return Fail(err, "too many vertex arrays");
    auto inFile = [&](std::uint64_t ofs, std::uint64_t count, std::uint64_t rec) { return ofs <= n && (rec == 0 || count <= (n - ofs) / rec); };
    if (!inFile(ofsText, numText, 1)) return Fail(err, "the text block lies outside the file");
    if (!inFile(ofsMeshes, numMeshes, 24)) return Fail(err, "the mesh table lies outside the file");
    if (!inFile(ofsVA, numVA, 20)) return Fail(err, "the vertex array table lies outside the file");
    if (!inFile(ofsTris, numTris, 12)) return Fail(err, "the triangle table lies outside the file");
    if (!inFile(ofsJoints, numJoints, 48)) return Fail(err, "the joint table lies outside the file");
    if (!inFile(ofsPoses, numPoses, 88)) return Fail(err, "the pose table lies outside the file");
    if (!inFile(ofsAnims, numAnims, 20)) return Fail(err, "the animation table lies outside the file");
    if (numFrames > 0 && !inFile(ofsFrames, numFrames, 2ull * numFrameCh)) return Fail(err, "the frame data lies outside the file");

    int badNames = 0;
    auto text = [&](std::uint32_t off) -> std::string {
        if (off >= numText) { if (off != 0) ++badNames; return std::string(); }
        const char* p = bytes.data() + ofsText + off;
        std::size_t len = 0;
        const std::size_t room = numText - off;
        while (len < room && len < kMaxName && p[len]) ++len;
        return std::string(p, len);
    };

    // ---- meshes ----
    std::vector<MeshRec> meshes(numMeshes);
    {
        b.Seek(ofsMeshes);
        std::uint64_t vsum = 0, tsum = 0;
        for (MeshRec& m : meshes) {
            const std::uint32_t name = b.U32(), mat = b.U32();
            m.firstVert = b.U32(); m.numVerts = b.U32(); m.firstTri = b.U32(); m.numTris = b.U32();
            m.name = text(name); m.material = text(mat);
            if ((std::uint64_t)m.firstVert + m.numVerts > numVerts) return Fail(err, "a mesh names vertices that are not in the vertex arrays");
            if ((std::uint64_t)m.firstTri + m.numTris > numTris) return Fail(err, "a mesh names triangles that are not in the triangle table");
            vsum += m.numVerts; tsum += m.numTris;
            if (vsum > kMaxOutVerts) return Fail(err, "the meshes together make too many output vertices (more than " + std::to_string(kMaxOutVerts) + "; a range named by several meshes counts each time)");
            if (tsum > kMaxOutTris) return Fail(err, "the meshes together make too many output triangles (more than " + std::to_string(kMaxOutTris) + "; a range named by several meshes counts each time)");
        }
    }
    if ((std::uint64_t)numMeshes * numJoints > kMaxBoneRefs) return Fail(err, "meshes x joints is over the budget of " + std::to_string(kMaxBoneRefs) + " (every mesh carries the whole bone list)");

    // ---- vertex arrays ----
    VArr pos, uv, nrm, bi, bw;
    int ignoredArrays = 0, dupArrays = 0;
    {
        b.Seek(ofsVA);
        for (std::uint32_t i = 0; i < numVA; ++i) {
            const std::uint32_t type = b.U32(), flags = b.U32(), fmt = b.U32(), size = b.U32(), off = b.U32();
            (void)flags;
            VArr* slot = type == kPosition ? &pos : type == kTexcoord ? &uv : type == kNormal ? &nrm : type == kBlendIndexes ? &bi : type == kBlendWeights ? &bw : nullptr;
            if (!slot) { ++ignoredArrays; continue; }
            if (slot->present) { ++dupArrays; continue; }
            const std::uint32_t fs = FormatBytes(fmt);
            const std::uint32_t need = type == kPosition || type == kNormal ? 3 : type == kTexcoord ? 2 : 1;
            if (fs == 0) return Fail(err, "a vertex array has an unknown data format (" + std::to_string(fmt) + ")");
            if (size < need || size > 16) return Fail(err, "a vertex array has " + std::to_string(size) + " components, which this kind cannot have");
            if ((type == kPosition || type == kNormal || type == kTexcoord) && fmt != kFloat && fmt != kHalf && fmt != kDouble)
                return Fail(err, "positions, normals and texture coordinates must be floating point");
            if (numVerts > 0 && ((std::uint64_t)off > n || (std::uint64_t)numVerts * size * fs > n - off)) return Fail(err, "a vertex array lies outside the file");
            slot->present = true; slot->fmt = fmt; slot->size = size; slot->off = off;
        }
    }
    if (numMeshes > 0 && numVerts > 0 && !pos.present) return Fail(err, "the file has no position vertex array");
    const bool haveSkinData = bi.present && bw.present && numJoints > 0;

    // ---- joints ----
    std::vector<Joint> joints(numJoints);
    int zeroQuats = 0;
    {
        b.Seek(ofsJoints);
        for (Joint& j : joints) {
            const std::uint32_t name = b.U32();
            j.parent = b.I32();
            j.t = Vec3{b.F32(), b.F32(), b.F32()};
            const float qx = b.F32(), qy = b.F32(), qz = b.F32(), qw = b.F32();
            j.s = Vec3{b.F32(), b.F32(), b.F32()};
            j.name = text(name);
            if (!Reasonable(j.t.x) || !Reasonable(j.t.y) || !Reasonable(j.t.z) || !Reasonable(j.s.x) || !Reasonable(j.s.y) || !Reasonable(j.s.z) ||
                !std::isfinite(qx) || !std::isfinite(qy) || !std::isfinite(qz) || !std::isfinite(qw))
                return Fail(err, "a joint transform is not finite or is out of range");
            bool z = false;
            j.r = NormalizedQ(qx, qy, qz, qw, &z);
            if (z) ++zeroQuats;
        }
    }
    {   // the bone list is copied into every part: its size in bytes is part of the budget
        std::uint64_t nameBytes = 0;
        for (const Joint& j : joints) nameBytes += j.name.size() + 24;
        if (nameBytes * numMeshes > kMaxBoneNameBytes) return Fail(err, "meshes x joint name bytes is over the budget of " + std::to_string(kMaxBoneNameBytes) + " (every mesh carries the whole bone list)");
    }
    // ---- poses ----
    std::vector<Pose> poses(numPoses);
    std::uint64_t sumBits = 0;
    {
        b.Seek(ofsPoses);
        for (Pose& p : poses) {
            b.I32();   // the pose's parent: the joint table's hierarchy is the one used
            p.mask = b.U32() & 0x3FFu;
            for (float& f : p.off) f = b.F32();
            for (float& f : p.scale) f = b.F32();
            for (int k = 0; k < 10; ++k) if (!std::isfinite(p.off[k]) || !std::isfinite(p.scale[k])) return Fail(err, "a pose channel offset or scale is not finite");
            for (int k = 0; k < 10; ++k) if (p.mask & (1u << k)) ++sumBits;
        }
    }
    if (numPoses > numJoints) return Fail(err, "the file has more poses than joints");

    // ---- skeleton: nodes parent first (a parent loop or a parent index out of range is cut) ----
    int badParents = 0, loops = 0, dupNames = 0;
    std::vector<std::string> nodeName(numJoints);
    std::vector<int> nodeOf(numJoints, 0), order;
    std::vector<Mat4> abs(numJoints);
    ImportedNode root;
    root.name = "IQM";
    root.rotation = Quat::AngleAxis(-90.0f, Vec3{1, 0, 0});   // Z up to Y up: (x, y, z) -> (x, z, -y)
    out.nodes.push_back(root);
    if (numJoints > 0) {
        std::vector<int> par(numJoints, -1);
        for (std::uint32_t i = 0; i < numJoints; ++i) {
            const int p = joints[i].parent;
            if (p < 0) continue;
            if ((std::uint32_t)p >= numJoints || (std::uint32_t)p == i) { ++badParents; continue; }
            par[i] = p;
        }
        loops = detail::txt::OrderParentsFirst(par, order);
        std::unordered_map<std::string, int> used;
        used["IQM"] = 1;
        for (int ji : order) {
            const Joint& j = joints[(std::size_t)ji];
            std::string nm = j.name.empty() ? "joint" + std::to_string(ji) : j.name;
            int& u = used[nm];
            if (u++) { ++dupNames; std::string cand; do { cand = nm + "_" + std::to_string(u++); } while (used.count(cand)); nm = cand; used[nm] = 1; }
            nodeName[(std::size_t)ji] = nm;
            ImportedNode nd;
            nd.name = nm;
            nd.parent = par[(std::size_t)ji] < 0 ? 0 : nodeOf[(std::size_t)par[(std::size_t)ji]];
            nd.position = j.t; nd.rotation = j.r; nd.scale = j.s;
            const Mat4 l = Mat4::TRS(j.t, j.r, j.s);
            abs[(std::size_t)ji] = par[(std::size_t)ji] < 0 ? l : abs[(std::size_t)par[(std::size_t)ji]] * l;
            nodeOf[(std::size_t)ji] = (int)out.nodes.size();
            out.nodes.push_back(std::move(nd));
        }
        for (const Mat4& m : abs) for (int k = 0; k < 16; ++k) if (!std::isfinite(m.m[k]) || std::fabs(m.m[k]) > 1e30f) return Fail(err, "the joint transforms multiply out to values that are too large");
    }

    // ---- materials (one per distinct material name; a name that is an image file is the texture) ----
    std::unordered_map<std::string, int> matOf;
    int texRefused = 0;
    for (const MeshRec& m : meshes) {
        if (m.material.empty() || matOf.count(m.material)) continue;
        ImportedMaterial im;
        im.name = m.material;
        if (IsImageName(m.material)) {
            std::string joined, why;
            if (detail::ResolveInsideDir(baseDir, m.material, joined, why)) im.texture = joined; else ++texRefused;
        }
        matOf[m.material] = (int)out.materials.size();
        out.materials.push_back(std::move(im));
    }

    // ---- meshes -> parts ----
    std::vector<ImportedBone> bones;
    int singularJoints = 0;
    if (haveSkinData) {
        bones.resize(numJoints);
        for (std::uint32_t i = 0; i < numJoints; ++i) { bones[i].name = nodeName[i]; if (detail::rc::Singular3(abs[i])) { bones[i].inverseBind = Mat4::Identity(); ++singularJoints; } else bones[i].inverseBind = abs[i].Inverse(); }
    }
    bool needRootBone = false;
    int badTris = 0, degenerate = 0, badNormals = 0, badBlend = 0, unweighted = 0, emptyMeshes = 0;
    {
        b.Seek(ofsTris);
        // triangles are read straight from the file per mesh
        for (const MeshRec& m : meshes) {
            if (m.numTris == 0 || m.numVerts == 0) { ++emptyMeshes; continue; }
            ImportedPart part;
            part.mesh.name = m.name;
            part.mesh.vertices.reserve(m.numVerts);
            if (uv.present) part.mesh.uvs.reserve(m.numVerts);
            if (nrm.present) part.mesh.normals.reserve(m.numVerts);
            bool recompute = !nrm.present;
            for (std::uint32_t v = 0; v < m.numVerts; ++v) {
                const std::uint64_t gi = (std::uint64_t)m.firstVert + v;
                const double x = Comp(bytes, pos, gi, 0), y = Comp(bytes, pos, gi, 1), z = Comp(bytes, pos, gi, 2);
                if (!Reasonable(x) || !Reasonable(y) || !Reasonable(z)) return Fail(err, "a vertex position is not finite or is out of range");
                part.mesh.vertices.push_back(Vec3{(float)x, (float)y, (float)z});
                if (uv.present) {
                    const double s = Comp(bytes, uv, gi, 0), t = Comp(bytes, uv, gi, 1);
                    if (!Reasonable(s) || !Reasonable(t)) return Fail(err, "a texture coordinate is not finite or is out of range");
                    part.mesh.uvs.push_back(Vec2{(float)s, (float)t});
                }
                if (nrm.present) {
                    const double nx = Comp(bytes, nrm, gi, 0), ny = Comp(bytes, nrm, gi, 1), nz = Comp(bytes, nrm, gi, 2);
                    const double l = std::sqrt(nx * nx + ny * ny + nz * nz);
                    if (!(l > 1e-6) || !std::isfinite(l) || l > 1e15) { ++badNormals; recompute = true; part.mesh.normals.push_back(Vec3{0, 1, 0}); }
                    else part.mesh.normals.push_back(Vec3{(float)(nx / l), (float)(ny / l), (float)(nz / l)});
                }
                if (haveSkinData) {
                    std::array<int, 4> ji{0, 0, 0, 0};
                    std::array<float, 4> jw{0, 0, 0, 0};
                    int cnt = 0;
                    float used = 0;
                    for (std::uint32_t k = 0; k < 4; ++k) {
                        const std::uint32_t ik = k < bi.size ? k : 0, wk = k < bw.size ? k : 0;
                        if (k >= bi.size || k >= bw.size) continue;
                        double w = Comp(bytes, bw, gi, wk);
                        if (bw.fmt == kUByte) w /= 255.0; else if (bw.fmt == kUShort) w /= 65535.0;
                        const double idx = Comp(bytes, bi, gi, ik);
                        if (!(w > 0.0) || !std::isfinite(w)) continue;
                        if (!(idx >= 0.0) || idx >= (double)numJoints) { ++badBlend; continue; }
                        const int bj = (int)idx;
                        bool merged = false;
                        for (int q = 0; q < cnt; ++q) if (ji[(std::size_t)q] == bj) { jw[(std::size_t)q] += (float)w; used += (float)w; merged = true; break; }
                        if (!merged && cnt < 4) { ji[(std::size_t)cnt] = bj; jw[(std::size_t)cnt] = (float)w; ++cnt; used += (float)w; }
                    }
                    if (cnt == 0 || !(used > 0.0f) || !std::isfinite(used)) {   // no weight: the vertex stays where it is, tied to the root node
                        needRootBone = true; ++unweighted;
                        ji = {(int)numJoints, 0, 0, 0}; jw = {1, 0, 0, 0};
                    } else for (float& w : jw) w /= used;
                    part.jointIdx.push_back(ji);
                    part.jointWt.push_back(jw);
                }
            }
            for (std::uint32_t t = 0; t < m.numTris; ++t) {
                b.Seek(ofsTris + (std::uint64_t)(m.firstTri + t) * 12);
                const std::uint32_t a = b.U32(), bb = b.U32(), c = b.U32();
                const std::uint32_t id[3] = {a, bb, c};
                bool ok = true;
                for (std::uint32_t q : id) if (q < m.firstVert || q - m.firstVert >= m.numVerts) ok = false;
                if (!ok) { ++badTris; continue; }
                if (a == bb || bb == c || a == c) { ++degenerate; continue; }
                for (std::uint32_t q : id) part.mesh.triangles.push_back((int)(q - m.firstVert));
            }
            if (part.mesh.triangles.empty()) { ++emptyMeshes; continue; }
            if (recompute) part.mesh.ComputeSmoothNormals();
            part.node = 0;
            if (!m.material.empty()) part.material = matOf[m.material];
            out.parts.push_back(std::move(part));
        }
    }
    if (haveSkinData) {
        if (needRootBone) { ImportedBone rb; rb.name = "IQM"; rb.inverseBind = Mat4::Identity(); bones.push_back(rb); }
        for (ImportedPart& p : out.parts) p.bones = bones;
    }

    // ---- animations ----
    int badRates = 0, keyBad = 0, framesIgnored = 0;
    {
        std::vector<Anim> anims(numAnims);
        b.Seek(ofsAnims);
        std::uint64_t jf = 0;
        for (Anim& a : anims) {
            a.name = text(b.U32());
            a.first = b.U32(); a.count = b.U32(); a.rate = b.F32();
            b.U32();   // flags (loop): the engine's clip type has no loop flag
            if (a.count == 0) continue;
            if ((std::uint64_t)a.first + a.count > numFrames) return Fail(err, "an animation names frames that are not in the file");
            jf += (std::uint64_t)a.count * numPoses;
            if (jf > kMaxJointFrames) return Fail(err, "the animations together hold too many joint frames (over " + std::to_string(kMaxJointFrames) + ")");
        }
        if (numAnims == 0 && numFrames > 0) framesIgnored = 1;
        if (numAnims > 0 && numFrames > 0 && numFrameCh < sumBits) return Fail(err, "num_framechannels is smaller than the channels the poses' masks ask for");
        std::vector<std::uint64_t> chanBase(numPoses, 0);
        { std::uint64_t acc = 0; for (std::uint32_t j = 0; j < numPoses; ++j) { chanBase[j] = acc; for (int k = 0; k < 10; ++k) if (poses[j].mask & (1u << k)) ++acc; } }
        const std::uint64_t stride = numFrameCh;
        auto raw = [&](std::uint64_t frame, std::uint64_t idx) -> std::uint32_t {
            const std::uint64_t o = ofsFrames + (frame * stride + idx) * 2;
            return (std::uint32_t)((unsigned char)bytes[(std::size_t)o] | ((unsigned char)bytes[(std::size_t)o + 1] << 8));
        };
        for (std::size_t ai = 0; ai < anims.size(); ++ai) {
            const Anim& a = anims[ai];
            if (a.count == 0 || numPoses == 0) continue;
            float rate = a.rate;
            if (!(rate > 0.0f) || !std::isfinite(rate) || rate > 100000.0f) { rate = 30.0f; ++badRates; }
            ImportedClip clip;
            clip.name = a.name.empty() ? "animation" + std::to_string(ai) : a.name;
            for (std::uint32_t j = 0; j < numPoses; ++j) {
                const Pose& P = poses[j];
                const Joint& J = joints[j];
                const bool varT = (P.mask & 7u) != 0, varR = (P.mask & (15u << 3)) != 0, varS = (P.mask & (7u << 7)) != 0;
                ImportedChannel ch;
                ch.node = nodeName[j];
                // a channel group the mask does not animate is constant: it gets a key only when it differs from the joint's rest value,
                // and then at the first and last frame only
                auto frameIdxs = [&](bool var, std::vector<std::uint32_t>& fl) {
                    fl.clear();
                    if (var) { fl.reserve(a.count); for (std::uint32_t f = 0; f < a.count; ++f) fl.push_back(f); }
                    else { fl.push_back(0); if (a.count > 1) fl.push_back(a.count - 1); }
                };
                auto value = [&](std::uint32_t f, int c) -> double {
                    double v = P.off[c];
                    if (P.mask & (1u << c)) {
                        std::uint64_t idx = chanBase[j];
                        for (int k = 0; k < c; ++k) if (P.mask & (1u << k)) ++idx;
                        v += (double)raw((std::uint64_t)a.first + f, idx) * (double)P.scale[c];
                    }
                    return v;
                };
                std::vector<std::uint32_t> fl;
                {   // position
                    frameIdxs(varT, fl);
                    const double v0[3] = {value(0, 0), value(0, 1), value(0, 2)};
                    const bool same = !varT && std::fabs(v0[0] - J.t.x) < 1e-6 && std::fabs(v0[1] - J.t.y) < 1e-6 && std::fabs(v0[2] - J.t.z) < 1e-6;
                    if (!same) {
                        ch.position.reserve(fl.size());
                        for (std::uint32_t f : fl) {
                            const double x = value(f, 0), y = value(f, 1), z = value(f, 2);
                            if (!Reasonable(x) || !Reasonable(y) || !Reasonable(z)) { ++keyBad; continue; }
                            ch.position.push_back({(float)f / rate, Vec3{(float)x, (float)y, (float)z}});
                        }
                    }
                }
                {   // rotation
                    frameIdxs(varR, fl);
                    const Quat q0 = NormalizedQ((float)value(0, 3), (float)value(0, 4), (float)value(0, 5), (float)value(0, 6));
                    const bool same = !varR && std::fabs(Quat::Dot(q0, J.r)) > 0.999999f;
                    if (!same) {
                        ch.rotation.reserve(fl.size());
                        for (std::uint32_t f : fl) {
                            const double x = value(f, 3), y = value(f, 4), z = value(f, 5), w = value(f, 6);
                            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(w)) { ++keyBad; continue; }
                            Quat q = NormalizedQ((float)x, (float)y, (float)z, (float)w);
                            if (!ch.rotation.empty() && Quat::Dot(q, ch.rotation.back().value) < 0.0f) q = Quat{-q.x, -q.y, -q.z, -q.w};   // stay on one side so interpolation takes the short way
                            ch.rotation.push_back({(float)f / rate, q});
                        }
                    }
                }
                {   // scale
                    frameIdxs(varS, fl);
                    const double v0[3] = {value(0, 7), value(0, 8), value(0, 9)};
                    const bool same = !varS && std::fabs(v0[0] - J.s.x) < 1e-6 && std::fabs(v0[1] - J.s.y) < 1e-6 && std::fabs(v0[2] - J.s.z) < 1e-6;
                    if (!same) {
                        ch.scale.reserve(fl.size());
                        for (std::uint32_t f : fl) {
                            const double x = value(f, 7), y = value(f, 8), z = value(f, 9);
                            if (!Reasonable(x) || !Reasonable(y) || !Reasonable(z)) { ++keyBad; continue; }
                            ch.scale.push_back({(float)f / rate, Vec3{(float)x, (float)y, (float)z}});
                        }
                    }
                }
                if (!ch.position.empty() || !ch.rotation.empty() || !ch.scale.empty()) clip.channels.push_back(std::move(ch));
            }
            if (!clip.channels.empty()) out.clips.push_back(std::move(clip));
        }
    }

    if (out.parts.empty() && out.clips.empty() && out.nodes.size() <= 1) return Fail(err, "the file has no mesh, joint or animation");

    auto note = [&](const std::string& s) { out.notes.push_back("IQM: " + s); };
    note("the root node turns Z-up into Y-up; units, winding (counter clockwise) and texture coordinates are kept as written");
    if (!out.clips.empty()) note("animation frames play at each animation's framerate; a constant channel gets keys only when it differs from the joint's rest value; the loop flag is not kept");
    if (ignoredArrays) note(std::to_string(ignoredArrays) + " vertex array(s) (tangents, colours, custom data) not imported");
    if (dupArrays) note(std::to_string(dupArrays) + " repeated vertex array(s) of a kind already read ignored");
    if (numVerts > 0 && !uv.present) note("the file has no texture coordinates");
    if (numJoints > 0 && !haveSkinData) note("the file has joints but no blend index / weight arrays: the meshes are not skinned");
    if (numTris > 0 && numVerts > 0 && !nrm.present) note("the file has no normals: smooth normals were generated");
    if (badParents) note(std::to_string(badParents) + " joint(s) with a parent index out of range were attached to the root");
    if (loops) note(std::to_string(loops) + " joint parent loop(s) were cut");
    if (dupNames) note(std::to_string(dupNames) + " joint name(s) are repeated: the later ones got a numeric suffix");
    if (zeroQuats) note(std::to_string(zeroQuats) + " joint rotation(s) of zero length were read as no rotation");
    if (singularJoints) note(std::to_string(singularJoints) + " joint(s) with a zero scale (or flat axes) cannot be inverted: their inverse bind matrix is the identity, so skinning to them is wrong");
    if (badNames) note(std::to_string(badNames) + " name(s) point outside the text block and are empty");
    if (badTris) note(std::to_string(badTris) + " triangle(s) with a vertex outside their mesh's range skipped");
    if (degenerate) note(std::to_string(degenerate) + " triangle(s) with a repeated vertex skipped");
    if (emptyMeshes) note(std::to_string(emptyMeshes) + " mesh(es) without usable triangles skipped");
    if (badNormals) note(std::to_string(badNormals) + " zero or invalid normal(s): smooth normals were generated for those meshes");
    if (badBlend) note(std::to_string(badBlend) + " skin weight(s) naming a joint that does not exist ignored");
    if (unweighted) note(std::to_string(unweighted) + " vertex(es) without a skin weight stay in place (tied to the root node)");
    if (badRates) note(std::to_string(badRates) + " animation(s) with a bad framerate played at 30 frames per second");
    if (keyBad) note(std::to_string(keyBad) + " animation key(s) with a non-finite or out-of-range value skipped");
    if (framesIgnored) note("the file holds frame data but no animation entry: no clip was made");
    if (texRefused) note(std::to_string(texRefused) + " texture path(s) refused (a model may only name files inside its own folder)");
    return true;
}

bool ReadIqmBuffer(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    const bool ok = ReadIqmImpl(bytes, baseDir, out, err);
    if (!ok) { out = ImportedModel{}; return false; }
    out.backend = "own";
    return true;
}

bool ReadIqmFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, iqm::kMaxFileBytes, bytes, err)) return false;
    std::string dir;
    const std::size_t sl = path.find_last_of("/\\");
    if (sl != std::string::npos) dir = path.substr(0, sl + 1);
    return ReadIqmBuffer(bytes, dir, out, err);
}

} // namespace okay
