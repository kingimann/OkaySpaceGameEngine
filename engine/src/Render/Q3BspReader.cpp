// Our own Quake III Arena map reader: the BSP file (magic "IBSP", version 46) bare, or inside a .pk3 (a zip with maps/<name>.bsp).
// Written ONLY from the public description of the format (the "Unofficial Quake 3 Map Specs": the 144 byte header with 17
// lumps, the record sizes of the textures / vertexes / meshverts / faces / models lumps, the four face types and the biquadratic
// Bezier patch rule) and general knowledge of Bezier surfaces. No importer source of any other project was read.
// What is read: the render geometry of every model (model 0 is the world, the others become child nodes), as one part per
// (model, texture): polygon and mesh faces by their mesh vertices, patch faces tessellated at a fixed level, the stored vertex
// normals and the surface texture coordinates. Not read: lightmaps and their coordinates, vertex colours, the entity text, the
// brush (collision) data, the BSP tree and visibility, effects (fog), billboards. See docs/own_q3bsp_reader.md.
// Bounds-checked, every count is tested against the bytes that are there, per-file budgets on faces visited, triangles and
// vertices (a refusal that names the budget), no recursion.
#include "BinReader.hpp"
#include "ReaderCommon.hpp"
#include "TextReaderUtil.hpp"
#include "ZipLite.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

namespace {
namespace q3bsp {

using detail::bin::Br;

constexpr std::size_t kMaxFileBytes = 512u * 1024u * 1024u;
constexpr std::uint64_t kMaxFaceVisits = 8000000, kMaxBuiltTris = 4000000, kMaxBuiltVerts = 4000000;
constexpr std::size_t kMaxParts = 100000;
constexpr int kPatchLevel = 6;                     // each 3 x 3 patch becomes 6 x 6 quads (7 x 7 vertices)
constexpr std::uint32_t kMaxPatchSide = 1025;      // control points per side of one patch face
constexpr int kLumpCount = 17;
constexpr int kLumpTextures = 1, kLumpVertexes = 10, kLumpMeshverts = 11, kLumpFaces = 13, kLumpModels = 7;
constexpr std::uint32_t kSurfNoDraw = 0x80, kSurfHint = 0x100, kSurfSkip = 0x200;

bool Fail(std::string* err, const std::string& m) { if (err) *err = "Q3BSP: " + m; return false; }

struct Lump { std::uint32_t off = 0, len = 0; };

struct Vert { float p[3], st[2], n[3]; };

struct PartBuild {
    int texture = -1;
    Mesh mesh;
    std::unordered_map<std::uint32_t, int> local;   // vertex number in the file -> vertex in this part
    bool hasStoredNormal = false;
};

inline float Safe(float f) { return (std::isfinite(f) && std::fabs(f) < 1e8f) ? f : 0.0f; }

// Where a vertex normal is the zero vector (patches, or a hostile file), the triangles around it give one.
void FixNormals(Mesh& m) {
    const std::size_t nv = m.vertices.size();
    std::vector<Vec3> acc(nv, Vec3{0, 0, 0});
    std::vector<std::uint8_t> bad(nv, 0);
    bool any = false;
    for (std::size_t i = 0; i < nv; ++i) {
        const Vec3 n = m.normals[i];
        const double l = std::sqrt((double)n.x * n.x + (double)n.y * n.y + (double)n.z * n.z);
        if (!(l > 1e-6)) { bad[i] = 1; any = true; }
        else m.normals[i] = Vec3{(float)(n.x / l), (float)(n.y / l), (float)(n.z / l)};
    }
    if (!any) return;
    for (std::size_t t = 0; t + 2 < m.triangles.size(); t += 3) {
        const int a = m.triangles[t], b = m.triangles[t + 1], c = m.triangles[t + 2];
        const Vec3 pa = m.vertices[(std::size_t)a], pb = m.vertices[(std::size_t)b], pc = m.vertices[(std::size_t)c];
        const double ux = (double)pb.x - pa.x, uy = (double)pb.y - pa.y, uz = (double)pb.z - pa.z;
        const double vx = (double)pc.x - pa.x, vy = (double)pc.y - pa.y, vz = (double)pc.z - pa.z;
        const Vec3 fn{(float)(uy * vz - uz * vy), (float)(uz * vx - ux * vz), (float)(ux * vy - uy * vx)};
        for (int v : {a, b, c}) if (bad[(std::size_t)v]) acc[(std::size_t)v] = Vec3{acc[(std::size_t)v].x + fn.x, acc[(std::size_t)v].y + fn.y, acc[(std::size_t)v].z + fn.z};
    }
    for (std::size_t i = 0; i < nv; ++i) if (bad[i]) {
        const Vec3 n = acc[i];
        const double l = std::sqrt((double)n.x * n.x + (double)n.y * n.y + (double)n.z * n.z);
        m.normals[i] = l > 1e-20 ? Vec3{(float)(n.x / l), (float)(n.y / l), (float)(n.z / l)} : Vec3{0, 0, 1};
    }
}

} // namespace q3bsp
} // namespace

// `archiveNames` (may be null) lists the entry names of the .pk3 the map came from, to find texture files.
static void Note(ImportedModel& m, const std::string& s) { m.notes.push_back("Q3BSP: " + s); }

static bool ReadQ3bspImpl(const std::string& bytes, const std::vector<std::string>* archiveNames, ImportedModel& out, std::string* err) {
    using namespace q3bsp;
    out = ImportedModel{};
    if (bytes.size() < 8 || std::memcmp(bytes.data(), "IBSP", 4) != 0) return Fail(err, "not a Quake III map (the magic text IBSP is missing)");
    Br h(bytes);
    h.pos = 4;
    const std::int32_t version = h.I32();
    if (version != 46) return Fail(err, "unsupported BSP version " + std::to_string(version) + " (46, Quake III Arena, is read)");
    if (bytes.size() < 8 + (std::size_t)kLumpCount * 8) return Fail(err, "the file ends inside the lump directory");
    Lump lump[kLumpCount];
    for (int i = 0; i < kLumpCount; ++i) {
        const std::int32_t o = h.I32(), l = h.I32();
        if (o < 0 || l < 0 || (std::uint64_t)o + (std::uint64_t)l > (std::uint64_t)bytes.size()) return Fail(err, "lump " + std::to_string(i) + " lies outside the file");
        lump[i].off = (std::uint32_t)o; lump[i].len = (std::uint32_t)l;
    }
    auto counted = [&](int li, std::uint32_t rec, const char* what, std::uint32_t& n) {
        if (lump[li].len % rec != 0) return Fail(err, std::string("the ") + what + " lump size is not a multiple of " + std::to_string(rec));
        n = lump[li].len / rec;
        return true;
    };
    std::uint32_t nTex = 0, nVert = 0, nMv = 0, nFace = 0, nModel = 0;
    if (!counted(kLumpTextures, 72, "textures", nTex) || !counted(kLumpVertexes, 44, "vertexes", nVert) || !counted(kLumpMeshverts, 4, "meshverts", nMv) ||
        !counted(kLumpFaces, 104, "faces", nFace) || !counted(kLumpModels, 40, "models", nModel)) return false;
    if (nFace == 0) return Fail(err, "the map has no faces");

    // ---- vertexes ----
    std::vector<Vert> verts(nVert);
    {
        Br v(bytes);
        v.Seek(lump[kLumpVertexes].off);
        for (Vert& x : verts) {
            for (float& f : x.p) f = v.F32();
            x.st[0] = v.F32(); x.st[1] = v.F32();
            v.Skip(8);                              // lightmap coordinates
            for (float& f : x.n) f = v.F32();
            v.Skip(4);                              // colour
            for (float& f : x.p) f = Safe(f);
            for (float& f : x.n) f = Safe(f);
            x.st[0] = Safe(x.st[0]); x.st[1] = Safe(x.st[1]);
        }
        if (v.bad) return Fail(err, "the file ends inside the vertexes");
    }
    std::vector<std::int32_t> meshverts(nMv);
    {
        Br v(bytes);
        v.Seek(lump[kLumpMeshverts].off);
        for (std::int32_t& x : meshverts) x = v.I32();
    }
    struct Tex { std::string name; std::uint32_t flags = 0; };
    std::vector<Tex> texs(nTex);
    {
        Br v(bytes);
        v.Seek(lump[kLumpTextures].off);
        for (Tex& t : texs) { t.name = v.Str(64); t.flags = (std::uint32_t)v.I32(); v.I32(); }
    }
    struct Model { std::uint32_t firstFace = 0, nFaces = 0; };
    std::vector<Model> models;
    {
        Br v(bytes);
        v.Seek(lump[kLumpModels].off);
        for (std::uint32_t i = 0; i < nModel; ++i) {
            v.Skip(24);                             // bounding box
            const std::int32_t f = v.I32(), n = v.I32();
            v.Skip(8);                              // brushes
            if (f < 0 || n < 0 || (std::uint64_t)f + (std::uint64_t)n > nFace) { Note(out, "model " + std::to_string(i) + " names faces outside the faces lump: skipped"); continue; }
            models.push_back({(std::uint32_t)f, (std::uint32_t)n});
        }
    }
    if (models.empty()) { models.push_back({0, nFace}); Note(out, "no usable models lump: all faces are read as one model"); }

    ImportedNode root;
    root.name = "Q3BSP";
    out.nodes.push_back(root);

    std::vector<int> matOf(nTex, -1);
    auto materialFor = [&](std::uint32_t ti) {
        if (matOf[ti] >= 0) return matOf[ti];
        ImportedMaterial m;
        m.name = texs[ti].name;
        m.texture = texs[ti].name;
        if (archiveNames) {
            for (const char* ext : {".tga", ".jpg", ".jpeg", ".png"}) {
                const std::string want = detail::rc::Lower(texs[ti].name + ext);
                bool hit = false;
                for (const std::string& n : *archiveNames) if (detail::rc::Lower(n) == want) { hit = true; break; }
                if (hit) { m.texture = texs[ti].name + ext; break; }
            }
        }
        out.materials.push_back(m);
        return matOf[ti] = (int)out.materials.size() - 1;
    };

    std::uint64_t visits = 0, tris = 0, builtVerts = 0;
    int skippedFlags = 0, badFaces = 0, billboards = 0, degenerate = 0, badPatches = 0, noNormalPatches = 0;
    std::size_t partsBuilt = 0;

    for (std::size_t mi = 0; mi < models.size(); ++mi) {
        int node = 0;
        if (mi > 0) {
            ImportedNode n;
            n.name = "model_" + std::to_string(mi);
            n.parent = 0;
            out.nodes.push_back(n);
            node = (int)out.nodes.size() - 1;
        }
        std::map<std::uint32_t, PartBuild> parts;   // by texture, so the parts come out in a fixed order
        for (std::uint32_t fi = 0; fi < models[mi].nFaces; ++fi) {
            if (++visits > kMaxFaceVisits) return Fail(err, "more than " + std::to_string(kMaxFaceVisits) + " face visits (the budget): the models name the same faces too often");
            Br f(bytes);
            f.Seek((std::uint64_t)lump[kLumpFaces].off + (std::uint64_t)(models[mi].firstFace + fi) * 104);
            const std::int32_t tex = f.I32();
            f.I32();                                // effect
            const std::int32_t type = f.I32(), vstart = f.I32(), vcount = f.I32(), mvstart = f.I32(), mvcount = f.I32();
            f.Skip(4 + 8 + 8 + 12 + 24);            // lightmap index, start, size, origin, vectors
            float fnrm[3];
            for (float& x : fnrm) x = Safe(f.F32());
            const std::int32_t sz0 = f.I32(), sz1 = f.I32();
            (void)fnrm;
            if (tex < 0 || (std::uint32_t)tex >= nTex || vstart < 0 || vcount < 0 || (std::uint64_t)vstart + (std::uint64_t)vcount > nVert) { ++badFaces; continue; }
            if (type == 4) { ++billboards; continue; }
            if (type < 1 || type > 3) { ++badFaces; continue; }
            if (texs[(std::size_t)tex].flags & (kSurfNoDraw | kSurfHint | kSurfSkip)) { ++skippedFlags; continue; }
            PartBuild& pb = parts[(std::uint32_t)tex];
            pb.texture = tex;
            if (type == 1 || type == 3) {
                if (mvstart < 0 || mvcount < 0 || (std::uint64_t)mvstart + (std::uint64_t)mvcount > nMv || mvcount % 3 != 0) { ++badFaces; continue; }
                tris += (std::uint64_t)mvcount / 3;
                if (tris > kMaxBuiltTris) return Fail(err, "more than " + std::to_string(kMaxBuiltTris) + " triangles (the budget)");
                for (std::int32_t k = 0; k + 2 < mvcount; k += 3) {
                    std::uint32_t g[3];
                    bool ok = true;
                    for (int c = 0; c < 3; ++c) {
                        const std::int32_t o = meshverts[(std::size_t)(mvstart + k + c)];
                        if (o < 0 || o >= vcount) { ok = false; break; }
                        g[c] = (std::uint32_t)vstart + (std::uint32_t)o;
                    }
                    if (!ok) { ++badFaces; continue; }
                    // geometric size test: a triangle of zero area is dropped
                    const Vert &a = verts[g[0]], &b = verts[g[1]], &c = verts[g[2]];
                    const double ux = (double)b.p[0] - a.p[0], uy = (double)b.p[1] - a.p[1], uz = (double)b.p[2] - a.p[2];
                    const double vx = (double)c.p[0] - a.p[0], vy = (double)c.p[1] - a.p[1], vz = (double)c.p[2] - a.p[2];
                    const double cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
                    if (!(cx * cx + cy * cy + cz * cz > 1e-24)) { ++degenerate; continue; }
                    // Quake III winds clockwise: two corners swap (the same rule as our MD3 reader)
                    for (int c2 : {0, 2, 1}) {
                        const std::uint32_t gi = g[c2];
                        auto it = pb.local.find(gi);
                        if (it == pb.local.end()) {
                            if (++builtVerts > kMaxBuiltVerts) return Fail(err, "more than " + std::to_string(kMaxBuiltVerts) + " vertices (the budget)");
                            const Vert& v = verts[gi];
                            const int idx = (int)pb.mesh.vertices.size();
                            pb.local.emplace(gi, idx);
                            pb.mesh.vertices.push_back(Vec3{v.p[0], v.p[1], v.p[2]});
                            pb.mesh.normals.push_back(Vec3{v.n[0], v.n[1], v.n[2]});
                            pb.mesh.uvs.push_back(Vec2{v.st[0], v.st[1]});   // top-down, as written (the engine's Quake convention)
                            pb.mesh.triangles.push_back(idx);
                        } else pb.mesh.triangles.push_back(it->second);
                    }
                }
            } else {   // type 2: a patch grid of sz0 x sz1 control points, cut into 3 x 3 biquadratic Bezier patches
                if (sz0 < 3 || sz1 < 3 || (sz0 & 1) == 0 || (sz1 & 1) == 0 || (std::uint32_t)sz0 > kMaxPatchSide || (std::uint32_t)sz1 > kMaxPatchSide ||
                    (std::uint64_t)sz0 * (std::uint64_t)sz1 != (std::uint64_t)vcount) { ++badPatches; continue; }
                const int pw = (sz0 - 1) / 2, ph = (sz1 - 1) / 2;
                const std::uint64_t np = (std::uint64_t)pw * (std::uint64_t)ph;
                tris += np * 2ull * kPatchLevel * kPatchLevel;
                builtVerts += np * (kPatchLevel + 1) * (kPatchLevel + 1);
                if (tris > kMaxBuiltTris) return Fail(err, "more than " + std::to_string(kMaxBuiltTris) + " triangles (the budget; patches count as " + std::to_string(2 * kPatchLevel * kPatchLevel) + " each)");
                if (builtVerts > kMaxBuiltVerts) return Fail(err, "more than " + std::to_string(kMaxBuiltVerts) + " vertices (the budget)");
                // the stored normals of the control points tell the front side; without them the (column x row) side is the front
                double sn[3] = {0, 0, 0};
                for (std::int32_t k = 0; k < vcount; ++k) { const Vert& v = verts[(std::size_t)vstart + (std::size_t)k]; sn[0] += v.n[0]; sn[1] += v.n[1]; sn[2] += v.n[2]; }
                const bool haveSn = std::sqrt(sn[0] * sn[0] + sn[1] * sn[1] + sn[2] * sn[2]) > 0.05 * (double)vcount;
                if (!haveSn) ++noNormalPatches;
                for (int py = 0; py < ph; ++py) for (int px = 0; px < pw; ++px) {
                    const Vert* cp[3][3];
                    for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) cp[r][c] = &verts[(std::size_t)vstart + (std::size_t)(2 * py + r) * (std::size_t)sz0 + (std::size_t)(2 * px + c)];
                    // flip decided on the middle of the patch
                    auto basis = [](double t, double* b, double* d) {
                        b[0] = (1 - t) * (1 - t); b[1] = 2 * t * (1 - t); b[2] = t * t;
                        d[0] = -2 * (1 - t); d[1] = 2 - 4 * t; d[2] = 2 * t;
                    };
                    auto eval = [&](double u, double v, double* P, double* Pu, double* Pv, double* S) {
                        double bu[3], du[3], bv[3], dv[3];
                        basis(u, bu, du); basis(v, bv, dv);
                        for (int k = 0; k < 3; ++k) P[k] = Pu[k] = Pv[k] = 0;
                        S[0] = S[1] = 0;
                        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) {
                            const Vert& q = *cp[r][c];
                            for (int k = 0; k < 3; ++k) { P[k] += bv[r] * bu[c] * q.p[k]; Pu[k] += bv[r] * du[c] * q.p[k]; Pv[k] += dv[r] * bu[c] * q.p[k]; }
                            S[0] += bv[r] * bu[c] * q.st[0]; S[1] += bv[r] * bu[c] * q.st[1];
                        }
                    };
                    double s = 1;
                    if (haveSn) {
                        double P[3], Pu[3], Pv[3], S[2];
                        eval(0.5, 0.5, P, Pu, Pv, S);
                        const double nx = Pu[1] * Pv[2] - Pu[2] * Pv[1], ny = Pu[2] * Pv[0] - Pu[0] * Pv[2], nz = Pu[0] * Pv[1] - Pu[1] * Pv[0];
                        if (nx * sn[0] + ny * sn[1] + nz * sn[2] < 0) s = -1;
                    }
                    const int base = (int)pb.mesh.vertices.size();
                    for (int a = 0; a <= kPatchLevel; ++a) for (int b = 0; b <= kPatchLevel; ++b) {
                        double P[3], Pu[3], Pv[3], S[2];
                        eval((double)b / kPatchLevel, (double)a / kPatchLevel, P, Pu, Pv, S);
                        double n[3] = {(Pu[1] * Pv[2] - Pu[2] * Pv[1]) * s, (Pu[2] * Pv[0] - Pu[0] * Pv[2]) * s, (Pu[0] * Pv[1] - Pu[1] * Pv[0]) * s};
                        const double l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                        if (l > 1e-9) { n[0] /= l; n[1] /= l; n[2] /= l; } else n[0] = n[1] = n[2] = 0;   // a pinched corner: FixNormals fills it from its triangles
                        pb.mesh.vertices.push_back(Vec3{(float)P[0], (float)P[1], (float)P[2]});
                        pb.mesh.normals.push_back(Vec3{(float)n[0], (float)n[1], (float)n[2]});
                        pb.mesh.uvs.push_back(Vec2{(float)S[0], (float)S[1]});
                    }
                    for (int a = 0; a < kPatchLevel; ++a) for (int b = 0; b < kPatchLevel; ++b) {
                        const int g00 = base + a * (kPatchLevel + 1) + b, g01 = g00 + 1, g10 = g00 + (kPatchLevel + 1), g11 = g10 + 1;
                        const int t0[3] = {g00, g01, g10}, t1[3] = {g01, g11, g10};
                        for (const int* t : {t0, t1}) {
                            if (s > 0) { pb.mesh.triangles.push_back(t[0]); pb.mesh.triangles.push_back(t[1]); pb.mesh.triangles.push_back(t[2]); }
                            else { pb.mesh.triangles.push_back(t[0]); pb.mesh.triangles.push_back(t[2]); pb.mesh.triangles.push_back(t[1]); }
                        }
                    }
                }
            }
        }
        for (auto& kv : parts) {
            PartBuild& pb = kv.second;
            if (pb.mesh.triangles.empty()) continue;
            if (++partsBuilt > kMaxParts) return Fail(err, "more than " + std::to_string(kMaxParts) + " parts (the budget)");
            FixNormals(pb.mesh);
            ImportedPart part;
            part.mesh = std::move(pb.mesh);
            part.mesh.name = texs[(std::size_t)pb.texture].name;
            part.node = node;
            part.material = materialFor((std::uint32_t)pb.texture);
            out.parts.push_back(std::move(part));
        }
    }
    out.backend = "own";
    if (out.parts.empty()) return Fail(err, "no drawable geometry (every face was skipped or invalid)");
    if (skippedFlags) Note(out, std::to_string(skippedFlags) + " faces with the nodraw, hint or skip surface flags were left out");
    if (billboards) Note(out, std::to_string(billboards) + " billboard faces were not read");
    if (badFaces) Note(out, std::to_string(badFaces) + " faces or triangles with bad indices were skipped");
    if (badPatches) Note(out, std::to_string(badPatches) + " patch faces with a bad control grid were skipped");
    if (degenerate) Note(out, std::to_string(degenerate) + " triangles of zero area were dropped");
    if (noNormalPatches) Note(out, std::to_string(noNormalPatches) + " patch faces had no stored normals: their front side is the (column x row) side, an assumption");
    Note(out, "lightmaps, vertex colours, entities, brushes, the BSP tree, visibility and effects are not read; units are Quake units, patches are cut into " + std::to_string(kPatchLevel) + " x " + std::to_string(kPatchLevel) + " quads");
    // Z up (Quake) to Y up: the root turns by -90 degrees about X, like the 3DS reader
    {
        const float hh = 0.70710678118654752f;
        out.nodes[0].rotation = Quat{-hh, 0, 0, hh};
    }
    return true;
}

bool ReadQ3bspBuffer(const std::string& bytes, ImportedModel& out, std::string* err) {
    const bool ok = ReadQ3bspImpl(bytes, nullptr, out, err);
    if (!ok) out = ImportedModel{};   // a refusal leaves nothing behind
    return ok;
}

bool ReadQ3bspFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, q3bsp::kMaxFileBytes, bytes, err)) { if (err) *err = "Q3BSP: " + *err; return false; }
    if (bytes.size() >= 4 && std::memcmp(bytes.data(), "PK\3\4", 4) == 0) {
        detail::zip::Archive zip;
        if (!zip.Open(bytes)) return q3bsp::Fail(err, zip.err);
        // the first map (by name) under maps/
        int best = -1;
        std::vector<std::string> names;
        names.reserve(zip.entries.size());
        for (std::size_t i = 0; i < zip.entries.size(); ++i) {
            names.push_back(zip.entries[i].name);
            const std::string n = detail::rc::Lower(zip.entries[i].name);
            if (n.size() > 9 && n.compare(0, 5, "maps/") == 0 && n.compare(n.size() - 4, 4, ".bsp") == 0)
                if (best < 0 || n < detail::rc::Lower(zip.entries[(std::size_t)best].name)) best = (int)i;
        }
        if (best < 0) return q3bsp::Fail(err, "the .pk3 has no maps/<name>.bsp");
        std::string bsp;
        if (!zip.Extract(best, bsp)) return q3bsp::Fail(err, zip.err);
        const bool ok = ReadQ3bspImpl(bsp, &names, out, err);
        if (!ok) out = ImportedModel{};
        if (ok) {
            int maps = 0;
            for (const std::string& n : names) { const std::string l = detail::rc::Lower(n); if (l.size() > 4 && l.compare(0, 5, "maps/") == 0 && l.compare(l.size() - 4, 4, ".bsp") == 0) ++maps; }
            out.nodes[0].name = "Q3BSP " + zip.entries[(std::size_t)best].name;
            if (maps > 1) Note(out, "the .pk3 holds " + std::to_string(maps) + " maps: only " + zip.entries[(std::size_t)best].name + " (first by name) was read");
            Note(out, "texture names point into the .pk3; the pictures are not extracted");
        }
        return ok;
    }
    return ReadQ3bspBuffer(bytes, out, err);
}

} // namespace okay
