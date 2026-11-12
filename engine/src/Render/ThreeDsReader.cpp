// Our own Autodesk 3DS (.3ds) reader, written from the public description of the
// chunk format: every chunk is a 2-byte id and a 4-byte little-endian length (which
// counts the 6 header bytes), followed by its data and then its child chunks.
// Reads meshes (vertices, faces, UVs, per-face material, smoothing groups, the
// object's local-axes matrix) and materials (colours, shininess, transparency,
// diffuse / specular / bump texture files with UV tiling). Anything it does not
// read is listed in ImportedModel::notes. See okay/Render/ModelData.hpp and
// docs/model_import.md. Off by default: the seam only calls it when asked.
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace okay {

namespace {
namespace tds {

constexpr std::size_t kMaxFileBytes = 512u * 1024u * 1024u;
constexpr std::size_t kMaxVertices  = 4000000;   // all objects together
constexpr std::size_t kMaxFaces     = 4000000;   // all objects together
constexpr std::size_t kMaxObjects   = 100000;
constexpr std::size_t kMaxMaterials = 100000;
constexpr std::size_t kMaxName      = 1024;
constexpr std::size_t kSmoothBudget = 20000000;  // corner comparisons per object before smoothing falls back to flat

// Chunk ids (public format description).
enum : std::uint16_t {
    C_MAIN = 0x4D4D, C_VERSION = 0x0002, C_EDITOR = 0x3D3D, C_MESHVER = 0x3D3E, C_MASTERSCALE = 0x0100,
    C_OBJECT = 0x4000, C_TRIMESH = 0x4100, C_VERTS = 0x4110, C_FACES = 0x4120, C_FACEMAT = 0x4130,
    C_UVS = 0x4140, C_SMOOTH = 0x4150, C_MATRIX = 0x4160, C_LIGHT = 0x4600, C_CAMERA = 0x4700,
    C_MATERIAL = 0xAFFF, C_MATNAME = 0xA000, C_AMBIENT = 0xA010, C_DIFFUSE = 0xA020, C_SPECULAR = 0xA030,
    C_SHININESS = 0xA040, C_TRANSP = 0xA050, C_TEXMAP = 0xA200, C_SPECMAP = 0xA204, C_BUMPMAP = 0xA230,
    C_MAPFILE = 0xA300, C_MAPUSCALE = 0xA354, C_MAPVSCALE = 0xA356, C_MAPUOFF = 0xA358, C_MAPVOFF = 0xA35A,
    C_MAPROT = 0xA35C, C_KEYFRAME = 0xB000,
    C_COLOR_F = 0x0010, C_COLOR_B = 0x0011, C_COLOR_LB = 0x0012, C_COLOR_LF = 0x0013,
    C_PCT_I = 0x0030, C_PCT_F = 0x0031
};

struct Bytes {
    const unsigned char* d;
    std::size_t n;
    std::uint16_t U16(std::size_t p) const { return (std::uint16_t)(d[p] | (d[p + 1] << 8)); }
    std::uint32_t U32(std::size_t p) const { return (std::uint32_t)d[p] | ((std::uint32_t)d[p + 1] << 8) | ((std::uint32_t)d[p + 2] << 16) | ((std::uint32_t)d[p + 3] << 24); }
    float F32(std::size_t p) const { const std::uint32_t u = U32(p); float f; std::memcpy(&f, &u, 4); return f; }
};

struct MatDef {
    ImportedMaterial m;
    float uScale = 1, vScale = 1, uOff = 0, vOff = 0;   // diffuse map tiling
};

struct MeshDef {
    std::string name;
    std::vector<Vec3> verts;
    std::vector<Vec2> uvs;                      // empty or one per vertex
    std::vector<std::array<std::uint16_t, 3>> faces;
    std::vector<std::uint32_t> smooth;          // per face, empty when the chunk is absent
    std::vector<int> faceMat;                   // per face: index into groupNames, -1 none
    std::vector<std::string> groupNames;        // material names of the face groups (resolved after the whole file is read)
    bool hasMatrix = false;
    float mat[12] = {0};
    bool sawVerts = false, sawFaces = false, sawUvs = false;
};

struct Reader {
    Bytes b{nullptr, 0};
    std::string baseDir;
    bool flatten = false;
    ImportedModel* out = nullptr;
    std::string err;

    std::vector<MatDef> mats;
    std::vector<MeshDef> meshes;
    float masterScale = 1.0f;
    std::size_t totalVerts = 0, totalFaces = 0;
    std::map<std::uint16_t, int> unread;        // chunk id -> count, for the notes
    int lights = 0, cameras = 0, keyframes = 0, unitless = 0;
    int ambientSpec = 0, unknownMatName = 0, multiMat = 0, badMatrix = 0, rotMaps = 0, otherMaps = 0, uvMismatch = 0, noFaces = 0, smoothFallback = 0;
    bool sawEditor = false;
    std::size_t trailing = 0;

    bool Fail(const std::string& m) { if (err.empty()) err = "3DS: " + m; return false; }

    // Calls fn(id, dataBegin, dataEnd) for every chunk in [begin, end). Every length is checked against the
    // parent: a chunk is at least 6 bytes and may not run past `end`.
    template <class F>
    bool Children(std::size_t begin, std::size_t end, F&& fn) {
        std::size_t p = begin;
        while (p < end) {
            if (end - p < 6) return Fail("truncated chunk header");
            const std::uint16_t id = b.U16(p);
            const std::uint32_t len = b.U32(p + 2);
            if (len < 6) return Fail("chunk length below 6");
            if ((std::size_t)len > end - p) return Fail("chunk runs past its parent");
            if (!fn(id, p + 6, p + (std::size_t)len)) return false;
            p += (std::size_t)len;
        }
        return true;
    }

    // A zero-terminated name inside [p, end). Advances p past the terminator.
    bool Str(std::size_t& p, std::size_t end, std::string& s) {
        s.clear();
        while (p < end && b.d[p] != 0) {
            if (s.size() >= kMaxName) return Fail("name too long");
            s += (char)b.d[p++];
        }
        if (p >= end) return Fail("unterminated name");
        ++p;
        return true;
    }

    bool Percent(std::size_t begin, std::size_t end, float& v) {   // 0..1
        bool got = false;
        const bool ok = Children(begin, end, [&](std::uint16_t id, std::size_t s, std::size_t e) {
            if (id == C_PCT_I) { if (e - s < 2) return Fail("percentage chunk too short"); v = b.U16(s) / 100.0f; got = true; }
            else if (id == C_PCT_F) { if (e - s < 4) return Fail("percentage chunk too short"); v = b.F32(s) / 100.0f; got = true; }
            else ++unread[id];
            return true;
        });
        if (!ok) return false;
        if (got && !std::isfinite(v)) v = 0.0f;
        v = std::fmax(0.0f, std::fmin(1.0f, v));
        return true;
    }

    // Colour chunk: floats (0x0010, 0x0013) are preferred over bytes (0x0011, 0x0012).
    bool ColorOf(std::size_t begin, std::size_t end, Color& c, bool& has) {
        has = false;
        bool gotF = false;
        const bool ok = Children(begin, end, [&](std::uint16_t id, std::size_t s, std::size_t e) {
            if (id == C_COLOR_F || id == C_COLOR_LF) {
                if (e - s < 12) return Fail("colour chunk too short");
                const float r = b.F32(s), g = b.F32(s + 4), bl = b.F32(s + 8);
                if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(bl)) return Fail("non-finite colour");
                c = Color(std::fmax(0.f, std::fmin(1.f, r)), std::fmax(0.f, std::fmin(1.f, g)), std::fmax(0.f, std::fmin(1.f, bl)), 1.0f);
                has = gotF = true;
            } else if (id == C_COLOR_B || id == C_COLOR_LB) {
                if (e - s < 3) return Fail("colour chunk too short");
                if (!gotF) { c = Color(b.d[s] / 255.0f, b.d[s + 1] / 255.0f, b.d[s + 2] / 255.0f, 1.0f); has = true; }
            } else ++unread[id];
            return true;
        });
        return ok;
    }

    std::string ResolvePath(std::string tp) const {
        for (char& ch : tp) if (ch == '\\') ch = '/';
        const bool abs = !tp.empty() && (tp[0] == '/' || (tp.size() > 1 && tp[1] == ':'));
        return abs ? tp : baseDir + tp;
    }

    bool Map(std::size_t begin, std::size_t end, std::string& file, MatDef* tiling) {
        return Children(begin, end, [&](std::uint16_t id, std::size_t s, std::size_t e) {
            if (id == C_MAPFILE) {
                std::size_t p = s; std::string name;
                if (!Str(p, e, name)) return false;
                if (!name.empty()) file = ResolvePath(name);
            } else if (id == C_MAPUSCALE || id == C_MAPVSCALE || id == C_MAPUOFF || id == C_MAPVOFF || id == C_MAPROT) {
                if (e - s < 4) return Fail("texture parameter chunk too short");
                float v = b.F32(s);
                if (!std::isfinite(v)) v = (id == C_MAPUSCALE || id == C_MAPVSCALE) ? 1.0f : 0.0f;
                if (id == C_MAPROT) { if (tiling && v != 0.0f) ++rotMaps; }
                else if (tiling) {
                    if (id == C_MAPUSCALE) tiling->uScale = v; else if (id == C_MAPVSCALE) tiling->vScale = v;
                    else if (id == C_MAPUOFF) tiling->uOff = v; else tiling->vOff = v;
                }
            } else ++unread[id];
            return true;
        });
    }

    bool ReadMaterial(std::size_t begin, std::size_t end) {
        if (mats.size() >= kMaxMaterials) return Fail("too many materials");
        MatDef md;
        const bool ok = Children(begin, end, [&](std::uint16_t id, std::size_t s, std::size_t e) {
            switch (id) {
                case C_MATNAME: { std::size_t p = s; return Str(p, e, md.m.name); }
                case C_DIFFUSE: { Color c; bool h; if (!ColorOf(s, e, c, h)) return false; if (h) { md.m.hasDiffuse = true; c.a = md.m.diffuse.a; md.m.diffuse = c; } return true; }
                case C_AMBIENT: case C_SPECULAR: { Color c; bool h; if (!ColorOf(s, e, c, h)) return false; if (h) ++ambientSpec; return true; }
                case C_SHININESS: { float v = 0; if (!Percent(s, e, v)) return false; md.m.shininess = v * 128.0f; return true; }
                case C_TRANSP: { float v = 0; if (!Percent(s, e, v)) return false; md.m.diffuse.a = 1.0f - v; return true; }
                case C_TEXMAP: return Map(s, e, md.m.texture, &md);
                case C_SPECMAP: return Map(s, e, md.m.specularMap, nullptr);
                case C_BUMPMAP: return Map(s, e, md.m.normalMap, nullptr);
                case 0xA210: case 0xA220: case 0xA33A: case 0xA33C: case 0xA33D:
                    ++otherMaps; return true;   // opacity, reflection, second diffuse, shininess, self-illumination, tint maps
                default: ++unread[id]; return true;
            }
        });
        if (!ok) return false;
        if (md.m.name.empty()) md.m.name = "Material" + std::to_string(mats.size());
        mats.push_back(std::move(md));
        return true;
    }

    bool ReadMesh(std::size_t begin, std::size_t end, MeshDef& md) {
        return Children(begin, end, [&](std::uint16_t id, std::size_t s, std::size_t e) {
            switch (id) {
                case C_VERTS: {
                    if (md.sawVerts) return Fail("mesh has two vertex lists");
                    if (e - s < 2) return Fail("vertex list too short");
                    const std::size_t n = b.U16(s);
                    if (n > kMaxVertices || totalVerts + n > kMaxVertices) return Fail("too many vertices");
                    if ((e - s - 2) / 12 < n) return Fail("vertex list longer than its chunk");
                    md.verts.resize(n);
                    for (std::size_t i = 0; i < n; ++i) {
                        const std::size_t q = s + 2 + i * 12;
                        const float x = b.F32(q), y = b.F32(q + 4), z = b.F32(q + 8);
                        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return Fail("non-finite vertex");
                        md.verts[i] = Vec3{x, y, z};
                    }
                    totalVerts += n; md.sawVerts = true;
                    return true;
                }
                case C_FACES: {
                    if (md.sawFaces) return Fail("mesh has two face lists");
                    if (e - s < 2) return Fail("face list too short");
                    const std::size_t n = b.U16(s);
                    if (n > kMaxFaces || totalFaces + n > kMaxFaces) return Fail("too many faces");
                    if ((e - s - 2) / 8 < n) return Fail("face list longer than its chunk");
                    md.faces.resize(n);
                    for (std::size_t i = 0; i < n; ++i) {   // 3 indices, then a flags word (edge visibility, texture wrap) that is not used
                        const std::size_t q = s + 2 + i * 8;
                        md.faces[i] = {b.U16(q), b.U16(q + 2), b.U16(q + 4)};
                    }
                    totalFaces += n; md.sawFaces = true;
                    md.faceMat.assign(n, -1);
                    // Sub-chunks follow the face records: material groups, smoothing groups.
                    const std::size_t subBegin = s + 2 + n * 8;
                    return Children(subBegin, e, [&](std::uint16_t sid, std::size_t ss, std::size_t se) {
                        if (sid == C_FACEMAT) {
                            std::size_t p = ss; std::string mname;
                            if (!Str(p, se, mname)) return false;
                            if (se - p < 2) return Fail("material group too short");
                            const std::size_t cnt = b.U16(p); p += 2;
                            if ((se - p) / 2 < cnt) return Fail("material group longer than its chunk");
                            const int mi = (int)md.groupNames.size();
                            md.groupNames.push_back(mname);
                            for (std::size_t k = 0; k < cnt; ++k) {
                                const std::size_t f = b.U16(p + k * 2);
                                if (f >= n) return Fail("material group names a face that does not exist");
                                if (md.faceMat[f] >= 0) { ++multiMat; continue; }
                                md.faceMat[f] = mi;
                            }
                            return true;
                        }
                        if (sid == C_SMOOTH) {
                            if ((se - ss) / 4 < n) return Fail("smoothing group list shorter than the face list");
                            md.smooth.resize(n);
                            for (std::size_t k = 0; k < n; ++k) md.smooth[k] = b.U32(ss + k * 4);
                            return true;
                        }
                        ++unread[sid];
                        return true;
                    });
                }
                case C_UVS: {
                    if (md.sawUvs) return Fail("mesh has two UV lists");
                    if (e - s < 2) return Fail("UV list too short");
                    const std::size_t n = b.U16(s);
                    if ((e - s - 2) / 8 < n) return Fail("UV list longer than its chunk");
                    md.uvs.resize(n);
                    for (std::size_t i = 0; i < n; ++i) {
                        const float u = b.F32(s + 2 + i * 8), v = b.F32(s + 6 + i * 8);
                        if (!std::isfinite(u) || !std::isfinite(v)) return Fail("non-finite UV");
                        md.uvs[i] = Vec2{u, v};
                    }
                    md.sawUvs = true;
                    return true;
                }
                case C_MATRIX: {
                    if (e - s < 48) return Fail("transform matrix too short");
                    for (int i = 0; i < 12; ++i) {
                        md.mat[i] = b.F32(s + (std::size_t)i * 4);
                        if (!std::isfinite(md.mat[i])) return Fail("non-finite transform matrix");
                    }
                    md.hasMatrix = true;
                    return true;
                }
                default: ++unread[id]; return true;
            }
        });
    }

    bool ReadEditor(std::size_t begin, std::size_t end) {
        std::size_t objects = 0;
        return Children(begin, end, [&](std::uint16_t id, std::size_t s, std::size_t e) {
            switch (id) {
                case C_MESHVER: return true;
                case C_MASTERSCALE: {
                    if (e - s < 4) return Fail("master scale too short");
                    const float v = b.F32(s);
                    if (std::isfinite(v) && v > 0.0f) masterScale = v; else ++unitless;
                    return true;
                }
                case C_MATERIAL: return ReadMaterial(s, e);
                case C_OBJECT: {
                    if (++objects > kMaxObjects) return Fail("too many objects");
                    std::size_t p = s; std::string name;
                    if (!Str(p, e, name)) return false;
                    const bool ok = Children(p, e, [&](std::uint16_t oid, std::size_t os, std::size_t oe) {
                        if (oid == C_TRIMESH) {
                            if (meshes.size() >= kMaxObjects) return Fail("too many meshes");
                            meshes.emplace_back();
                            meshes.back().name = name;
                            return ReadMesh(os, oe, meshes.back());
                        }
                        if (oid == C_LIGHT) ++lights;
                        else if (oid == C_CAMERA) ++cameras;
                        else ++unread[oid];
                        return true;
                    });
                    return ok;
                }
                default: ++unread[id]; return true;
            }
        });
    }

    static Mat4 ZUpToYUp() {
        Mat4 r;   // (x, y, z) -> (x, z, -y)
        r.at(0, 0) = 1; r.at(1, 1) = 0; r.at(2, 1) = 1; r.at(1, 2) = -1; r.at(2, 2) = 0;
        return r;
    }

    static Vec3 Apply(const Mat4& w, const Vec3& p) {
        return Vec3{w.at(0, 0) * p.x + w.at(1, 0) * p.y + w.at(2, 0) * p.z + w.at(3, 0),
                    w.at(0, 1) * p.x + w.at(1, 1) * p.y + w.at(2, 1) * p.z + w.at(3, 1),
                    w.at(0, 2) * p.x + w.at(1, 2) * p.y + w.at(2, 2) * p.z + w.at(3, 2)};
    }
    static Vec3 ApplyDir(const Mat4& w, const Vec3& p) {
        return Vec3{w.at(0, 0) * p.x + w.at(1, 0) * p.y + w.at(2, 0) * p.z,
                    w.at(0, 1) * p.x + w.at(1, 1) * p.y + w.at(2, 1) * p.z,
                    w.at(0, 2) * p.x + w.at(1, 2) * p.y + w.at(2, 2) * p.z};
    }
    static float Det3(const Mat4& w) {
        return w.at(0, 0) * (w.at(1, 1) * w.at(2, 2) - w.at(2, 1) * w.at(1, 2)) -
               w.at(1, 0) * (w.at(0, 1) * w.at(2, 2) - w.at(2, 1) * w.at(0, 2)) +
               w.at(2, 0) * (w.at(0, 1) * w.at(1, 2) - w.at(1, 1) * w.at(0, 2));
    }

    // Builds the parts of one mesh. `local` is the vertex list in the object's own space.
    bool BuildMesh(const MeshDef& md, const std::vector<Vec3>& local, int node, const Mat4* bake) {
        const std::size_t nf = md.faces.size(), nv = local.size();
        if (nf == 0) { ++noFaces; return true; }
        for (const auto& f : md.faces)
            if (f[0] >= nv || f[1] >= nv || f[2] >= nv) return Fail("face names a vertex that does not exist");
        const bool uvOk = md.sawUvs && md.uvs.size() == nv;
        if (md.sawUvs && !uvOk) ++uvMismatch;

        // Face normals (area weighted), and for smoothing the faces around each vertex.
        std::vector<Vec3> fn(nf);
        for (std::size_t f = 0; f < nf; ++f) {
            const Vec3& a = local[md.faces[f][0]]; const Vec3& bb = local[md.faces[f][1]]; const Vec3& c = local[md.faces[f][2]];
            const Vec3 e1{bb.x - a.x, bb.y - a.y, bb.z - a.z}, e2{c.x - a.x, c.y - a.y, c.z - a.z};
            fn[f] = Vec3{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
        }
        std::vector<std::uint32_t> start(nv + 1, 0), adj;
        const bool useGroups = !md.smooth.empty();
        if (useGroups) {
            for (const auto& f : md.faces) for (int k = 0; k < 3; ++k) ++start[f[(std::size_t)k] + 1];
            for (std::size_t v = 0; v < nv; ++v) start[v + 1] += start[v];
            adj.assign(start[nv], 0);
            std::vector<std::uint32_t> fill(start.begin(), start.end() - 1);
            for (std::size_t f = 0; f < nf; ++f) for (int k = 0; k < 3; ++k) adj[fill[md.faces[f][(std::size_t)k]]++] = (std::uint32_t)f;
        }

        // Which faces go to which part: one part per material, unassigned faces last.
        std::vector<int> groupMat(md.groupNames.size(), -1);
        for (std::size_t g = 0; g < groupMat.size(); ++g) {
            for (std::size_t k = 0; k < mats.size(); ++k) if (mats[k].m.name == md.groupNames[g]) { groupMat[g] = (int)k; break; }
            if (groupMat[g] < 0) ++unknownMatName;
        }
        std::map<int, std::vector<std::size_t>> byMat;
        for (std::size_t f = 0; f < nf; ++f) {
            const int mi = md.faceMat[f] < 0 ? -1 : groupMat[(std::size_t)md.faceMat[f]];
            byMat[mi < 0 ? (int)mats.size() : mi].push_back(f);
        }

        std::size_t budget = kSmoothBudget;
        bool flatFallback = false;
        for (const auto& kv : byMat) {
            ImportedPart part;
            part.node = node;
            const int mi = kv.first >= (int)mats.size() ? -1 : kv.first;
            part.material = mi;
            Mesh& m = part.mesh;
            m.name = md.name;
            float us = 1, vs = 1, uo = 0, vo = 0;
            if (mi >= 0) { us = mats[(std::size_t)mi].uScale; vs = mats[(std::size_t)mi].vScale; uo = mats[(std::size_t)mi].uOff; vo = mats[(std::size_t)mi].vOff; }
            std::map<std::array<int, 4>, int> weld;
            auto emit = [&](std::uint16_t vi, const Vec3& nrm) -> int {
                const std::array<int, 4> key{(int)vi, (int)std::lround(nrm.x * 4096.0f), (int)std::lround(nrm.y * 4096.0f), (int)std::lround(nrm.z * 4096.0f)};
                auto it = weld.find(key);
                if (it != weld.end()) return it->second;
                const int idx = (int)m.vertices.size();
                weld.emplace(key, idx);
                m.vertices.push_back(local[vi]);
                m.normals.push_back(nrm);
                if (uvOk) m.uvs.push_back(Vec2{md.uvs[vi].x * us + uo, 1.0f - (md.uvs[vi].y * vs + vo)});   // v flipped like the other bottom-up formats
                return idx;
            };
            auto unit = [](Vec3 v) {
                const float l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
                return l > 1e-20f ? Vec3{v.x / l, v.y / l, v.z / l} : Vec3{0, 1, 0};
            };
            if (!useGroups) {
                // No smoothing-group chunk: weld per vertex and smooth over the whole mesh below.
                for (std::size_t f : kv.second)
                    for (int k = 0; k < 3; ++k) m.triangles.push_back(emit(md.faces[f][(std::size_t)k], Vec3{0, 0, 0}));
            } else {
                for (std::size_t f : kv.second) {
                    const std::uint32_t sg = md.smooth[f];
                    for (int k = 0; k < 3; ++k) {
                        const std::uint16_t vi = md.faces[f][(std::size_t)k];
                        Vec3 acc = fn[f];
                        if (sg != 0 && !flatFallback) {
                            const std::uint32_t lo = start[vi], hi = start[vi + 1];
                            if (hi - lo > budget) { flatFallback = true; ++smoothFallback; }
                            else {
                                budget -= hi - lo;
                                for (std::uint32_t q = lo; q < hi; ++q) {
                                    const std::uint32_t g = adj[q];
                                    if (g != f && (md.smooth[g] & sg) != 0) acc = Vec3{acc.x + fn[g].x, acc.y + fn[g].y, acc.z + fn[g].z};
                                }
                            }
                        }
                        m.triangles.push_back(emit(vi, unit(acc)));
                    }
                }
            }
            if (!useGroups) {
                // Smooth normals (what the other own readers do when a file has none): average over
                // vertices that share a position.
                std::vector<Vec3> acc(m.vertices.size(), Vec3{0, 0, 0});
                std::map<std::array<int, 3>, std::size_t> rep;
                std::vector<std::size_t> grp(m.vertices.size());
                for (std::size_t i = 0; i < m.vertices.size(); ++i) {
                    const Vec3& p = m.vertices[i];
                    const std::array<int, 3> k{(int)std::lround(p.x * 2048.0f), (int)std::lround(p.y * 2048.0f), (int)std::lround(p.z * 2048.0f)};
                    auto it = rep.find(k);
                    grp[i] = it == rep.end() ? (rep[k] = i) : it->second;
                }
                for (std::size_t t = 0; t + 2 < m.triangles.size(); t += 3) {
                    const Vec3& p0 = m.vertices[(std::size_t)m.triangles[t]]; const Vec3& p1 = m.vertices[(std::size_t)m.triangles[t + 1]]; const Vec3& p2 = m.vertices[(std::size_t)m.triangles[t + 2]];
                    const Vec3 e1{p1.x - p0.x, p1.y - p0.y, p1.z - p0.z}, e2{p2.x - p0.x, p2.y - p0.y, p2.z - p0.z};
                    const Vec3 cr{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
                    for (int k = 0; k < 3; ++k) { Vec3& a = acc[grp[(std::size_t)m.triangles[t + (std::size_t)k]]]; a = Vec3{a.x + cr.x, a.y + cr.y, a.z + cr.z}; }
                }
                for (std::size_t i = 0; i < m.vertices.size(); ++i) m.normals[i] = unit(acc[grp[i]]);
            }
            if (bake) {
                for (Vec3& p : m.vertices) p = Apply(*bake, p);
                for (Vec3& p : m.normals) {
                    const Vec3 q = ApplyDir(*bake, p);
                    const float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
                    p = l > 0.0f ? Vec3{q.x / l, q.y / l, q.z / l} : Vec3{0, 1, 0};
                }
                if (Det3(*bake) < 0.0f)   // mirrored: keep the faces facing out
                    for (std::size_t t = 0; t + 2 < m.triangles.size(); t += 3) std::swap(m.triangles[t + 1], m.triangles[t + 2]);
            }
            out->parts.push_back(std::move(part));
        }
        return true;
    }

    bool Run(const std::string& bytes) {
        b.d = reinterpret_cast<const unsigned char*>(bytes.data());
        b.n = bytes.size();
        if (b.n > kMaxFileBytes) return Fail("file too large");
        if (b.n < 6) return Fail("file too short to be a 3DS file");
        if (b.U16(0) != C_MAIN) return Fail("not a 3DS file (no main chunk)");
        const std::uint32_t mlen = b.U32(2);
        if (mlen < 6) return Fail("main chunk length below 6");
        if ((std::size_t)mlen > b.n) return Fail("file is shorter than its main chunk (truncated)");
        const bool ok = Children(6, (std::size_t)mlen, [&](std::uint16_t id, std::size_t s, std::size_t e) {
            if (id == C_EDITOR) { sawEditor = true; return ReadEditor(s, e); }
            if (id == C_VERSION) return true;
            if (id == C_KEYFRAME) { ++keyframes; return true; }   // animation, hierarchy and pivots: not read
            ++unread[id];
            return true;
        });
        if (!ok) return false;
        if (!sawEditor) return Fail("no editor chunk (no objects)");
        trailing = b.n - (std::size_t)mlen;

        // Root node: the master scale, then the rotation that makes +Y up (3DS is Z-up, the engine is Y-up).
        Mat4 root = Mat4::Scale(Vec3{masterScale, masterScale, masterScale});
        root = ZUpToYUp() * root;
        ImportedNode rootNode; rootNode.name = "3DS Scene";
        root.Decompose(rootNode.position, rootNode.rotation, rootNode.scale);
        out->nodes.push_back(rootNode);
        out->materials.reserve(mats.size());
        for (const MatDef& m : mats) out->materials.push_back(m.m);

        for (const MeshDef& md : meshes) {
            if (!md.sawVerts || md.verts.empty()) { if (md.sawFaces && !md.faces.empty()) return Fail("mesh has faces but no vertices"); ++noFaces; continue; }
            // The vertices in a 3DS file are in world space; the 0x4160 matrix holds the object's local axes
            // and origin. Put the vertices in that local space and the matrix on the object's node, which gives
            // the same world positions and keeps the object's own pivot and orientation.
            std::vector<Vec3> local = md.verts;
            Mat4 M;                      // local -> world
            bool useNode = false;
            if (md.hasMatrix) {
                for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) M.at(r, c) = md.mat[r * 3 + c];
                M.at(3, 0) = md.mat[9]; M.at(3, 1) = md.mat[10]; M.at(3, 2) = md.mat[11];
                const float det = Det3(M);
                const Mat4 inv = M.Inverse();
                bool finite = std::isfinite(det) && det > 1e-12f;
                for (int i = 0; i < 16; ++i) if (!std::isfinite(inv.m[i])) finite = false;
                Vec3 t, s; Quat q;
                if (finite && M.Decompose(t, q, s)) {
                    // Only a plain rotation + scale + move round-trips through a node; shear does not.
                    const Mat4 back = Mat4::TRS(t, q, s);
                    float d = 0; for (int i = 0; i < 16; ++i) d = std::fmax(d, std::fabs(back.m[i] - M.m[i]));
                    if (d < 1e-3f * (1.0f + std::fabs(M.at(3, 0)) + std::fabs(M.at(3, 1)) + std::fabs(M.at(3, 2)))) {
                        for (Vec3& p : local) p = Apply(inv, p);
                        useNode = true;
                    }
                }
                if (!useNode) ++badMatrix;
            }
            int node = 0;
            Mat4 objWorld = root;
            if (useNode) objWorld = root * M;
            if (!flatten && useNode) {
                ImportedNode n; n.name = md.name.empty() ? "Object" : md.name; n.parent = 0;
                M.Decompose(n.position, n.rotation, n.scale);
                node = (int)out->nodes.size();
                out->nodes.push_back(n);
            } else if (!flatten) {
                ImportedNode n; n.name = md.name.empty() ? "Object" : md.name; n.parent = 0;
                node = (int)out->nodes.size();
                out->nodes.push_back(n);
            }
            const std::size_t before = out->parts.size();
            if (!BuildMesh(md, local, flatten ? 0 : node, flatten ? &objWorld : nullptr)) return false;
            if (out->parts.size() == before && !flatten && node == (int)out->nodes.size() - 1) out->nodes.pop_back();   // object had no faces
        }
        if (out->parts.empty()) return Fail("the file has no mesh with faces");
        if (flatten) { out->nodes.assign(1, ImportedNode{}); out->nodes[0].name = rootNode.name; }
        EmitNotes();
        return true;
    }

    void EmitNotes() {
        auto note = [&](const std::string& s) { out->notes.push_back("3DS: " + s); };
        if (trailing) note(std::to_string(trailing) + " byte(s) after the main chunk ignored");
        if (keyframes) note("keyframe data (animation, object hierarchy, pivot points) not read; objects are flat children of the root");
        if (lights) note(std::to_string(lights) + " light(s) not imported");
        if (cameras) note(std::to_string(cameras) + " camera(s) not imported");
        if (noFaces) note(std::to_string(noFaces) + " object(s) without faces skipped");
        if (ambientSpec) note(std::to_string(ambientSpec) + " ambient / specular colour(s) read but not kept (no field for them in the imported material)");
        if (otherMaps) note(std::to_string(otherMaps) + " opacity / reflection / second / shininess / self-illumination map(s) not read");
        if (rotMaps) note(std::to_string(rotMaps) + " texture rotation(s) not applied (scale and offset are baked into the UVs)");
        if (unknownMatName) note(std::to_string(unknownMatName) + " face group(s) name a material that is not defined; those faces have no material");
        if (multiMat) note(std::to_string(multiMat) + " face(s) assigned to more than one material; the first assignment is kept");
        if (badMatrix) note(std::to_string(badMatrix) + " object transform matrix(es) singular, mirrored or sheared; the vertices stay in file space on an identity node");
        if (uvMismatch) note(std::to_string(uvMismatch) + " object(s) have a UV list that does not match the vertex count; UVs dropped");
        if (unitless) note("master scale was not a positive number; ignored");
        if (smoothFallback) note("smoothing groups too expensive on " + std::to_string(smoothFallback) + " object(s); flat normals used");
        if (!unread.empty()) {
            int total = 0; std::string ids; int shown = 0;
            for (const auto& kv : unread) {
                total += kv.second;
                if (shown++ < 8) { char tmp[16]; std::snprintf(tmp, sizeof(tmp), "%s0x%04X", ids.empty() ? "" : ", ", (unsigned)kv.first); ids += tmp; }
            }
            note(std::to_string(total) + " other chunk(s) not read (ids " + ids + (unread.size() > 8 ? ", ..." : "") + ")");
        }
    }
};

} // namespace tds
} // namespace

bool Read3dsBuffer(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err, bool flatten) {
    out = ImportedModel{};
    tds::Reader r;
    r.baseDir = baseDir;
    r.flatten = flatten;
    r.out = &out;
    out.backend = "own";
    if (!r.Run(bytes)) {
        const std::string m = r.err.empty() ? "3DS: could not read file" : r.err;
        out = ImportedModel{};
        if (err) *err = m;
        return false;
    }
    return true;
}

bool Read3dsFile(const std::string& path, ImportedModel& out, std::string* err, bool flatten) {
    out = ImportedModel{};
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) { if (err) *err = "cannot open file"; return false; }
    std::string bytes;
    char buf[65536];
    for (;;) {
        const std::size_t n = std::fread(buf, 1, sizeof(buf), fp);
        if (n == 0) break;
        if (bytes.size() + n > tds::kMaxFileBytes) { std::fclose(fp); if (err) *err = "file too large"; return false; }
        bytes.append(buf, n);
    }
    std::fclose(fp);
    const std::size_t sl = path.find_last_of("/\\");
    return Read3dsBuffer(bytes, sl == std::string::npos ? std::string() : path.substr(0, sl + 1), out, err, flatten);
}

} // namespace okay
