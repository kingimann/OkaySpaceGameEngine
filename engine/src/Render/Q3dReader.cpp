// Our own Q3D reader: Quick3D objects (.q3o, signature "quick3Do") and scenes (.q3s, signature "quick3Ds"). Written ONLY from
// the public format description (Paul Bourke's "Quick3D Object File" page: the 22 byte header, the 'm' meshes / 'c' materials /
// 't' textures / 's' scene chunks and the field lists of each record). No importer source of any other project was read. The
// description does not say which byte order the numbers use, so the reader tries little endian first and, if that does not
// parse, big endian. Where the description leaves the layout open, the choices were checked against the bytes of the four Quick3D
// files of the model corpus (docs/content/model_readers.md, "Checked against real files").
// Bounds-checked, every count is checked against the bytes that are really there before anything is allocated, capped, no recursion.
#include "PolygonTriangulate.hpp"
#include "ReaderCommon.hpp"
#include "TextReaderUtil.hpp"
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
namespace q3d {

constexpr std::size_t kMaxFileBytes = 256u * 1024u * 1024u;
constexpr std::int32_t kMaxMeshes = 100000, kMaxMaterials = 100000, kMaxTextures = 100000;
constexpr std::int32_t kMaxMeshVerts = 4000000, kMaxMeshFaces = 4000000, kMaxFaceCorners = 4096;
// Per-file budgets over all meshes: corners read from the file, triangles and vertices built.
constexpr std::uint64_t kMaxCorners = 16000000;
constexpr std::size_t kMaxOutVerts = 4000000, kMaxBuiltTris = 4000000;
// Ear clipping a concave polygon costs about n^3 steps. Only polygons of at most kMaxEarCorners corners are ear clipped, and the sum of
// n^3 over the file may not pass kMaxEarWork; any polygon beyond either limit is fanned (the corner order of the file), with a note.
constexpr std::size_t kMaxEarCorners = 32;
constexpr std::uint64_t kMaxEarWork = 400000000ull;

// A bounds-checked cursor with a chosen byte order. A failed read latches `bad`, returns zero and moves nothing.
struct Cur {
    const unsigned char* d; std::size_t n, pos = 0; bool be, bad = false;
    Cur(const std::string& s, bool bigEndian) : d((const unsigned char*)s.data()), n(s.size()), be(bigEndian) {}
    std::size_t Left() const { return n - pos; }
    bool Has(std::uint64_t k) { if (k > (std::uint64_t)Left()) { bad = true; return false; } return true; }
    bool Fits(std::uint64_t count, std::uint64_t rec) { if (rec && count > (std::uint64_t)Left() / rec) { bad = true; return false; } return true; }
    void Skip(std::uint64_t k) { if (Has(k)) pos += (std::size_t)k; }
    std::uint8_t U8() { if (!Has(1)) return 0; return d[pos++]; }
    std::uint16_t U16() { if (!Has(2)) return 0; const unsigned a = d[pos], b = d[pos + 1]; pos += 2; return (std::uint16_t)(be ? (a << 8) | b : (b << 8) | a); }
    std::uint32_t U32() {
        if (!Has(4)) return 0;
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v = be ? (v << 8) | d[pos + (std::size_t)i] : v | ((std::uint32_t)d[pos + (std::size_t)i] << (8 * i));
        pos += 4;
        return v;
    }
    std::int32_t I32() { return (std::int32_t)U32(); }
    float F32() { const std::uint32_t u = U32(); float f; std::memcpy(&f, &u, 4); return f; }
    // Text up to a NUL (consumed); fails when there is none within maxLen bytes.
    std::string CStr(std::size_t maxLen) {
        std::size_t e = 0;
        while (pos + e < n && e < maxLen && d[pos + e] != 0) ++e;
        if (pos + e >= n || e >= maxLen) { bad = true; return std::string(); }
        std::string s((const char*)d + pos, e);
        pos += e + 1;
        return s;
    }
};

struct Mesh1 {
    std::vector<Vec3> v, nrm;
    std::vector<std::uint16_t> shape;
    std::vector<std::int32_t> face, mat, tface;
    std::vector<Vec2> tc;
    bool haveTex = false;
};

struct Stats { int fanned = 0; std::uint64_t earWork = 0; int badFace = 0, shortFace = 0, badUv = 0, badNormals = 0, uvCountNoData = 0, clearMats = 0; std::uint64_t corners = 0; };

bool ReadMesh(Cur& c, std::int32_t numTextures, Mesh1& m, Stats& st, std::string& why) {
    const std::int32_t nv = c.I32();
    if (c.bad || nv < 0 || nv > kMaxMeshVerts) { why = "bad vertex count"; return false; }
    if (!c.Fits((std::uint64_t)nv, 12)) { why = "the file ends inside the vertices"; return false; }
    m.v.resize((std::size_t)nv);
    for (Vec3& p : m.v) { p.x = c.F32(); p.y = c.F32(); p.z = c.F32(); if (!(std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z)) || std::fabs(p.x) > 1e15f || std::fabs(p.y) > 1e15f || std::fabs(p.z) > 1e15f) { why = "a vertex is not finite or out of range"; return false; } }
    const std::int32_t nf = c.I32();
    if (c.bad || nf < 0 || nf > kMaxMeshFaces) { why = "bad face count"; return false; }
    if (!c.Fits((std::uint64_t)nf, 2)) { why = "the file ends inside the face shapes"; return false; }
    m.shape.resize((std::size_t)nf);
    std::uint64_t corners = 0;
    for (std::uint16_t& s : m.shape) { s = c.U16(); if (s > kMaxFaceCorners) { why = "a face with " + std::to_string(s) + " corners"; return false; } corners += s; }
    st.corners += corners;
    if (st.corners > kMaxCorners) { why = "over the budget of " + std::to_string(kMaxCorners) + " face corners for one file"; return false; }
    if (!c.Fits(corners, 4)) { why = "the file ends inside the faces"; return false; }
    m.face.resize((std::size_t)corners);
    for (std::int32_t& i : m.face) i = c.I32();
    if (!c.Fits((std::uint64_t)nf, 4)) { why = "the file ends inside the face materials"; return false; }
    m.mat.resize((std::size_t)nf);
    for (std::int32_t& i : m.mat) i = c.I32();
    const std::int32_t nn = c.I32();
    if (c.bad || nn < 0 || nn > kMaxMeshVerts) { why = "bad normal count"; return false; }
    if (!c.Fits((std::uint64_t)nn, 12)) { why = "the file ends inside the normals"; return false; }
    m.nrm.resize((std::size_t)nn);
    for (Vec3& p : m.nrm) { p.x = c.F32(); p.y = c.F32(); p.z = c.F32(); }
    // The texture coordinate count is always there (the four corpus files, which have no textures, all carry it; one of them
    // has 230 there and no coordinates). The coordinates and the per-corner texture indices follow only when the header names
    // textures.
    const std::int32_t nt = c.I32();
    if (c.bad || nt < 0 || nt > kMaxMeshVerts) { why = "bad texture coordinate count"; return false; }
    if (numTextures == 0 && nt > 0) ++st.uvCountNoData;
    if (numTextures > 0) {
        m.haveTex = true;
        if (!c.Fits((std::uint64_t)nt, 8)) { why = "the file ends inside the texture coordinates"; return false; }
        m.tc.resize((std::size_t)nt);
        for (Vec2& t : m.tc) { t.x = c.F32(); t.y = c.F32(); if (!(std::isfinite(t.x) && std::isfinite(t.y) && std::fabs(t.x) <= 1e15f && std::fabs(t.y) <= 1e15f)) t = Vec2{NAN, NAN}; }   // marked bad, used as (0, 0) below
        if (!c.Fits(corners, 4)) { why = "the file ends inside the texture indices"; return false; }
        m.tface.resize((std::size_t)corners);
        for (std::int32_t& i : m.tface) i = c.I32();
    }
    c.Skip(12 + 24);   // centre of mass, bounding box: recomputed by the engine when needed
    if (c.bad) { why = "the file ends inside a mesh"; return false; }
    return true;
}

} // namespace q3d
} // namespace

namespace {
bool Q3dTry(const std::string& bytes, bool be, ImportedModel& out, std::string& why) {
    using namespace q3d;
    out = ImportedModel{};
    Cur c(bytes, be);
    if (bytes.size() < 22) { why = "file too short"; return false; }
    bool scene = std::memcmp(bytes.data(), "quick3Ds", 8) == 0;   // a scene chunk also makes it a scene (WusonOrange.q3s says "quick3Do")
    c.pos = 8;
    const std::string version((const char*)c.d + 8, 2);
    c.pos = 10;
    const std::int32_t nMesh = c.I32(), nMat = c.I32(), nTex = c.I32();
    if (nMesh < 0 || nMesh > kMaxMeshes || nMat < 0 || nMat > kMaxMaterials || nTex < 0 || nTex > kMaxTextures) { why = "a count in the header is negative or over the limit"; return false; }

    std::vector<Mesh1> meshes;
    std::vector<ImportedMaterial> mats;
    Stats st;
    bool seenM = false, seenC = false, seenT = false, seenS = false;
    int skippedTex = 0;
    while (c.pos < c.n && !seenS) {
        const unsigned char type = c.U8();
        if (type == 'm' && !seenM) {
            seenM = true;
            if (!c.Fits((std::uint64_t)nMesh, 48)) { why = "the file is too short for the meshes the header names"; return false; }
            meshes.resize((std::size_t)nMesh);   // nMesh <= 100000; each empty Mesh1 is small, and each mesh below is checked against the bytes left
            for (Mesh1& m : meshes) if (!ReadMesh(c, nTex, m, st, why)) return false;
        } else if (type == 'c' && !seenC) {
            seenC = true;
            // name (NUL ended), ambient / diffuse / specular colour (3 floats each), transparency, texture index (-1 = none):
            // at least 1 + 36 + 4 + 4 bytes
            if (!c.Fits((std::uint64_t)nMat, 45)) { why = "the file ends inside the materials"; return false; }
            for (std::int32_t i = 0; i < nMat; ++i) {
                ImportedMaterial mt;
                mt.name = c.CStr(4096);
                float col[9];
                for (float& f : col) f = c.F32();
                const float tr = c.F32();
                (void)c.I32();   // texture index; the embedded textures are not decoded (see the notes)
                if (c.bad) { why = "the file ends inside a material"; return false; }
                for (float f : col) if (!std::isfinite(f)) { why = "a material colour is not finite"; return false; }
                auto cl = [](float f) { return std::min(1.0f, std::max(0.0f, f)); };
                // Glass materials carry a small value here (0.12, 0.18); WusonOrange's only material carries exactly 1, which
                // would make the whole model invisible: a fully clear material is read as opaque, with a note.
                float alpha = std::isfinite(tr) ? 1.0f - cl(tr) : 1.0f;
                if (alpha <= 0.0f) { alpha = 1.0f; ++st.clearMats; }
                mt.hasDiffuse = true;
                mt.diffuse = Color{cl(col[3]), cl(col[4]), cl(col[5]), alpha};
                mats.push_back(std::move(mt));
            }
        } else if (type == 't' && !seenT) {
            seenT = true;
            for (std::int32_t i = 0; i < nTex; ++i) {
                c.CStr(4096);
                const std::int32_t w = c.I32(), h = c.I32();
                if (c.bad || w < 0 || h < 0) { why = "bad texture"; return false; }
                if (!c.Has((std::uint64_t)w * (std::uint64_t)h * 3u)) { why = "the file ends inside a texture"; return false; }   // w, h < 2^31: the product fits 64 bits
                c.Skip((std::uint64_t)w * (std::uint64_t)h * 3u);
                ++skippedTex;
            }
        } else if (type == 's' && !seenS) {
            seenS = true;   // the scene chunk is last; camera, lights and background are not used
            scene = true;
        } else { why = std::string("an unexpected or repeated chunk type ") + std::to_string((int)type); return false; }
    }
    if (nMesh > 0 && !seenM) { why = "the header names meshes but there is no meshes chunk"; return false; }
    if (nMat > 0 && !seenC) { why = "the header names materials but there is no materials chunk"; return false; }
    if (nTex > 0 && !seenT) { why = "the header names textures but there is no textures chunk"; return false; }

    out.materials = mats;
    std::size_t outVerts = 0, builtTris = 0;
    for (std::size_t mi = 0; mi < meshes.size(); ++mi) {
        const Mesh1& m = meshes[mi];
        // faces grouped by material
        std::map<int, std::vector<std::uint32_t>> byMat;
        std::vector<std::size_t> start(m.shape.size() + 1, 0);
        for (std::size_t f = 0; f < m.shape.size(); ++f) start[f + 1] = start[f] + m.shape[f];
        const bool useNrm = m.nrm.size() == m.v.size() && !m.v.empty();
        bool nrmOk = useNrm;
        std::vector<Vec3> nrm;
        if (useNrm) {
            nrm = m.nrm;
            for (Vec3& n : nrm) { const float l = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z); if (!(l > 1e-6f) || !std::isfinite(l)) { nrmOk = false; break; } n = Vec3{n.x / l, n.y / l, n.z / l}; }
            if (!nrmOk) ++st.badNormals;
        }
        for (std::size_t f = 0; f < m.shape.size(); ++f) {
            if (m.shape[f] < 3) { ++st.shortFace; continue; }
            bool ok = true;
            for (std::size_t k = start[f]; k < start[f + 1]; ++k) if (m.face[k] < 0 || (std::size_t)m.face[k] >= m.v.size()) ok = false;
            if (!ok) { ++st.badFace; continue; }
            const std::int32_t mat = m.mat[f];
            byMat[mat >= 0 && (std::size_t)mat < mats.size() ? mat : -1].push_back((std::uint32_t)f);
        }
        for (const auto& grp : byMat) {
            detail::rc::Soup soup;
            std::unordered_map<std::uint32_t, std::uint32_t> remap;
            std::vector<Vec3> poly;
            std::vector<std::array<int, 3>> tris;
            std::vector<Vec2> uv;
            const bool wantUv = m.haveTex && !m.tc.empty();
            for (std::uint32_t f : grp.second) {
                poly.clear();
                for (std::size_t k = start[f]; k < start[f + 1]; ++k) poly.push_back(m.v[(std::size_t)m.face[k]]);
                const std::uint64_t cost = (std::uint64_t)poly.size() * poly.size() * poly.size();
                if (poly.size() == 3) { tris.assign(1, std::array<int, 3>{0, 1, 2}); }
                else if (poly.size() > kMaxEarCorners || st.earWork + cost > kMaxEarWork) {
                    tris.clear();
                    for (int k = 1; k + 1 < (int)poly.size(); ++k) tris.push_back({0, k, k + 1});
                    if (poly.size() > 4) ++st.fanned;
                } else { st.earWork += cost; detail::TriangulatePolygon(poly, tris); }
                builtTris += tris.size();
                if (builtTris > kMaxBuiltTris) { why = "over the budget of " + std::to_string(kMaxBuiltTris) + " triangles for one file"; return false; }
                for (const auto& t : tris) for (int k : t) {
                    const std::size_t corner = start[f] + (std::size_t)k;
                    const std::uint32_t vi = (std::uint32_t)m.face[corner];
                    auto it = remap.find(vi);
                    if (it == remap.end()) { it = remap.emplace(vi, (std::uint32_t)soup.pos.size()).first; soup.pos.push_back(m.v[vi]); }
                    soup.ci.push_back(it->second);
                    if (nrmOk) soup.cnrm.push_back(nrm[vi]);
                    if (wantUv) {
                        const std::int32_t ti = m.tface[corner];
                        if (ti >= 0 && (std::size_t)ti < m.tc.size() && std::isfinite(m.tc[(std::size_t)ti].x)) soup.cuv.push_back(m.tc[(std::size_t)ti]);
                        else { soup.cuv.push_back(Vec2{0, 0}); ++st.badUv; }
                    }
                }
            }
            ImportedPart part;
            detail::rc::BuildStats bs;
            if (!detail::rc::BuildMesh(soup, 180.0, part.mesh, &bs, kMaxOutVerts - std::min(outVerts, kMaxOutVerts))) { why = "over the budget of " + std::to_string(kMaxOutVerts) + " vertices for one file"; return false; }
            if (part.mesh.triangles.empty()) continue;
            outVerts += part.mesh.vertices.size();
            if (!detail::rc::PartInRange(part)) { why = "a vertex is out of range"; return false; }
            part.mesh.name = "Q3D mesh " + std::to_string(mi);
            ImportedNode node;
            node.name = part.mesh.name;
            out.nodes.push_back(node);
            part.node = (int)out.nodes.size() - 1;
            part.material = grp.first;
            out.parts.push_back(std::move(part));
        }
    }
    if (out.parts.empty()) { why = "the file has no usable triangles"; return false; }
    out.backend = "own";
    auto note = [&](const std::string& s) { out.notes.push_back("Q3D: " + s); };
    if (version != "30") note("file version \"" + version + "\" (the description is of version 30): read as version 30");
    note(std::string("read as ") + (be ? "big" : "little") + " endian (the description does not name the byte order)");
    if (scene) note("scene file: the scene chunk (placement, camera, lights, background) is not used");
    if (skippedTex) note(std::to_string(skippedTex) + " embedded texture(s) not imported; the description gives no link from a material to a texture");
    note("ambient and specular colours are not imported");
    if (st.fanned) note(std::to_string(st.fanned) + " polygon(s) with more than " + std::to_string(kMaxEarCorners) + " corners, or past the per-file ear clipping budget, were fanned from their first corner (a concave one may come out wrong)");
    if (st.badFace) note(std::to_string(st.badFace) + " face(s) with an out-of-range vertex index skipped");
    if (st.shortFace) note(std::to_string(st.shortFace) + " face(s) with fewer than 3 corners skipped");
    if (st.badUv) note(std::to_string(st.badUv) + " corner(s) with a bad texture coordinate index got (0, 0)");
    if (st.badNormals) note("a mesh has zero or invalid stored normals: normals were generated");
    if (st.uvCountNoData) note(std::to_string(st.uvCountNoData) + " mesh(es) name texture coordinates but the file has no textures, so none are stored: no UVs");
    if (st.clearMats) note(std::to_string(st.clearMats) + " material(s) with transparency 1 (fully clear) read as opaque");
    return true;
}
} // namespace

bool ReadQ3dBuffer(const std::string& bytes, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    if (bytes.size() < 22) { if (err) *err = "Q3D: file too short"; return false; }
    if (std::memcmp(bytes.data(), "quick3Do", 8) != 0 && std::memcmp(bytes.data(), "quick3Ds", 8) != 0) {
        if (err) *err = "Q3D: not a Quick3D file (the signature quick3Do / quick3Ds is missing)";
        return false;
    }
    std::string wl, wb;
    if (Q3dTry(bytes, false, out, wl)) return true;
    if (Q3dTry(bytes, true, out, wb)) return true;
    out = ImportedModel{};
    if (err) *err = "Q3D: " + wl + (wl == wb ? "" : " (as big endian: " + wb + ")");
    return false;
}

bool ReadQ3dFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, q3d::kMaxFileBytes, bytes, err)) return false;
    return ReadQ3dBuffer(bytes, out, err);
}

} // namespace okay
