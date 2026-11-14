// Our own MD3 (Quake III Arena model, binary) reader: the first frame of the vertex animation as one static part per
// surface, with texture coordinates, the stored normals, the first shader name of each surface as its texture, and the
// tags (attachment points) as empty child nodes. Written from the public description of the format (id Software's
// Quake III Arena source, qfiles.h, and the format notes built on it). Bounds-checked, capped, no recursion.
// See okay/Render/ModelData.hpp and docs/own_md3_reader.md.
#include "BinReader.hpp"
#include "ReaderCommon.hpp"
#include "TextReaderUtil.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

namespace {
namespace md3 {

using detail::bin::Br;

constexpr std::size_t kMaxFileBytes = 256u * 1024u * 1024u;
constexpr std::uint32_t kMaxSurfaces = 4096, kMaxVerts = 2000000, kMaxTris = 4000000, kMaxTags = 100000;
constexpr std::size_t kMaxOutVerts = 20000000;
constexpr double kPi = 3.14159265358979323846;

bool Fail(std::string* err, const std::string& m) { if (err) *err = "MD3: " + m; return false; }

// The stored normal is two bytes: latitude and longitude as 1/256 of a turn.
Vec3 DecodeNormal(std::uint16_t n) {
    const double lat = (double)(n >> 8) * (2.0 * kPi / 256.0), lng = (double)(n & 255) * (2.0 * kPi / 256.0);
    return Vec3{(float)(std::cos(lat) * std::sin(lng)), (float)(std::sin(lat) * std::sin(lng)), (float)std::cos(lng)};
}

// Rotation matrix with the given column vectors to a quaternion. Returns false when the columns are not a rotation
// (not unit length, not perpendicular, or a mirror).
bool QuatFromColumns(const double c[3][3], Quat& q) {
    for (int i = 0; i < 3; ++i) {
        const double l = std::sqrt(c[i][0] * c[i][0] + c[i][1] * c[i][1] + c[i][2] * c[i][2]);
        if (!(std::fabs(l - 1.0) < 0.01)) return false;
        for (int j = i + 1; j < 3; ++j) if (std::fabs(c[i][0] * c[j][0] + c[i][1] * c[j][1] + c[i][2] * c[j][2]) > 0.01) return false;
    }
    // right handed? c0 x c1 must point along c2
    const double cx = c[0][1] * c[1][2] - c[0][2] * c[1][1], cy = c[0][2] * c[1][0] - c[0][0] * c[1][2], cz = c[0][0] * c[1][1] - c[0][1] * c[1][0];
    if (cx * c[2][0] + cy * c[2][1] + cz * c[2][2] < 0.9) return false;
    // matrix m[row][col] = c[col][row]
    const double m00 = c[0][0], m10 = c[0][1], m20 = c[0][2], m01 = c[1][0], m11 = c[1][1], m21 = c[1][2], m02 = c[2][0], m12 = c[2][1], m22 = c[2][2];
    const double tr = m00 + m11 + m22;
    double x, y, z, w;
    if (tr > 0) { const double s = std::sqrt(tr + 1.0) * 2; w = 0.25 * s; x = (m21 - m12) / s; y = (m02 - m20) / s; z = (m10 - m01) / s; }
    else if (m00 > m11 && m00 > m22) { const double s = std::sqrt(1.0 + m00 - m11 - m22) * 2; w = (m21 - m12) / s; x = 0.25 * s; y = (m01 + m10) / s; z = (m02 + m20) / s; }
    else if (m11 > m22) { const double s = std::sqrt(1.0 + m11 - m00 - m22) * 2; w = (m02 - m20) / s; x = (m01 + m10) / s; y = 0.25 * s; z = (m12 + m21) / s; }
    else { const double s = std::sqrt(1.0 + m22 - m00 - m11) * 2; w = (m10 - m01) / s; x = (m02 + m20) / s; y = (m12 + m21) / s; z = 0.25 * s; }
    q = Quat{(float)x, (float)y, (float)z, (float)w}.Normalized();
    return true;
}

} // namespace md3
} // namespace

static bool ReadMd3Impl(const std::string& bytes, ImportedModel& out, std::string* err) {
    using namespace md3;
    out = ImportedModel{};
    Br b(bytes);
    if (b.n < 108) return Fail(err, "file too short");
    if (std::memcmp(b.d, "IDP3", 4) != 0) return Fail(err, "not an MD3 file (the magic text IDP3 is missing)");
    b.pos = 4;
    const std::int32_t version = b.I32();
    if (version != 15) return Fail(err, "unsupported version " + std::to_string(version) + " (15 is read)");
    const std::string modelName = b.Str(64);
    b.I32();   // flags
    std::int32_t numFrames = b.I32(); const std::int32_t numTags = b.I32(), numSurfaces = b.I32();
    b.I32();   // numSkins
    const std::int32_t ofsFrames = b.I32(), ofsTags = b.I32(), ofsSurfaces = b.I32();
    (void)ofsFrames;
    if (numSurfaces <= 0 || (std::uint32_t)numSurfaces > kMaxSurfaces) return Fail(err, "bad surface count " + std::to_string(numSurfaces));
    if (numTags < 0 || (std::uint32_t)numTags > kMaxTags) return Fail(err, "bad tag count " + std::to_string(numTags));
    if (numFrames <= 0) return Fail(err, "the file has no frames");
    if (ofsSurfaces < 108 || ofsTags < 0) return Fail(err, "a table offset is out of range");

    ImportedNode root;
    root.name = modelName.empty() ? "MD3" : modelName;
    out.nodes.push_back(root);

    int badTagRot = 0, badVert = 0, emptySurfaces = 0, degenerate = 0, noShader = 0, nonFiniteUv = 0;
    std::size_t outVerts = 0;
    std::uint64_t surf = (std::uint64_t)ofsSurfaces;
    for (std::int32_t si = 0; si < numSurfaces; ++si) {
        Br s(bytes);
        if (!s.Seek(surf) || !s.Has(108)) return Fail(err, "the file ends inside the header of surface " + std::to_string(si));
        if (std::memcmp(s.d + s.pos, "IDP3", 4) != 0) return Fail(err, "surface " + std::to_string(si) + " does not start with IDP3");
        s.pos += 4;
        const std::string surfName = s.Str(64);
        s.I32();   // flags
        const std::int32_t sFrames = s.I32(), nShaders = s.I32(), nVerts = s.I32(), nTris = s.I32(), oTris = s.I32(), oShaders = s.I32(), oSt = s.I32(), oXyz = s.I32(), oEnd = s.I32();
        if (nVerts < 0 || (std::uint32_t)nVerts > kMaxVerts || nTris < 0 || (std::uint32_t)nTris > kMaxTris || sFrames <= 0 || nShaders < 0)
            return Fail(err, "surface " + std::to_string(si) + " has a bad count");
        if (oEnd < 108 || oTris < 0 || oShaders < 0 || oSt < 0 || oXyz < 0) return Fail(err, "surface " + std::to_string(si) + " has a bad offset");
        const std::uint64_t next = surf + (std::uint64_t)oEnd;
        if (nVerts == 0 || nTris == 0) { ++emptySurfaces; surf = next; continue; }

        // vertices of frame 0: x, y, z in 1/64 units and the packed normal
        Br v(bytes);
        if (!v.Seek(surf + (std::uint64_t)oXyz) || !v.Fits((std::uint64_t)nVerts, 8)) return Fail(err, "the file ends inside the vertices of surface " + std::to_string(si));
        detail::rc::Soup soup;
        std::vector<Vec3> nrm((std::size_t)nVerts);
        soup.pos.resize((std::size_t)nVerts);
        for (std::int32_t i = 0; i < nVerts; ++i) {
            const float x = (float)v.I16(), y = (float)v.I16(), z = (float)v.I16();
            soup.pos[(std::size_t)i] = Vec3{x / 64.0f, y / 64.0f, z / 64.0f};
            nrm[(std::size_t)i] = DecodeNormal(v.U16());
        }
        // texture coordinates, one pair per vertex
        Br t(bytes);
        if (!t.Seek(surf + (std::uint64_t)oSt) || !t.Fits((std::uint64_t)nVerts, 8)) return Fail(err, "the file ends inside the texture coordinates of surface " + std::to_string(si));
        std::vector<Vec2> uv((std::size_t)nVerts);
        for (Vec2& u : uv) {
            float a = t.F32(), c = t.F32();
            if (!std::isfinite(a) || !std::isfinite(c) || std::fabs(a) > 1e9f || std::fabs(c) > 1e9f) { a = 0; c = 0; ++nonFiniteUv; }
            u = Vec2{a, 1.0f - c};
        }
        // triangles: three vertex numbers each; Quake III winds clockwise, so two corners swap
        Br tr(bytes);
        if (!tr.Seek(surf + (std::uint64_t)oTris) || !tr.Fits((std::uint64_t)nTris, 12)) return Fail(err, "the file ends inside the triangles of surface " + std::to_string(si));
        soup.ci.reserve((std::size_t)nTris * 3);
        soup.cuv.reserve((std::size_t)nTris * 3);
        soup.cnrm.reserve((std::size_t)nTris * 3);
        for (std::int32_t i = 0; i < nTris; ++i) {
            const std::uint32_t a = tr.U32(), c = tr.U32(), d = tr.U32();
            if (a >= (std::uint32_t)nVerts || c >= (std::uint32_t)nVerts || d >= (std::uint32_t)nVerts) { ++badVert; continue; }
            for (std::uint32_t k : {a, d, c}) { soup.ci.push_back(k); soup.cuv.push_back(uv[k]); soup.cnrm.push_back(nrm[k]); }
        }
        // first shader name
        std::string shader;
        if (nShaders > 0) {
            Br sh(bytes);
            if (sh.Seek(surf + (std::uint64_t)oShaders) && sh.Has(68)) shader = sh.Str(64);
        }
        ImportedPart part;
        detail::rc::BuildStats st;
        if (!detail::rc::BuildMesh(soup, 180.0, part.mesh, &st, kMaxOutVerts - outVerts)) return Fail(err, "too many output vertices");
        degenerate += st.degenerate;
        outVerts += part.mesh.vertices.size();
        if (part.mesh.triangles.empty()) { ++emptySurfaces; surf = next; continue; }
        part.mesh.name = surfName;
        if (!detail::rc::PartInRange(part)) return Fail(err, "a vertex is out of range");
        ImportedMaterial mat;
        mat.name = surfName;
        for (char& c : shader) if (c == '\\') c = '/';
        mat.texture = shader;
        if (shader.empty()) ++noShader;
        mat.hasDiffuse = true;
        mat.diffuse = Color{1, 1, 1, 1};
        part.node = 0;
        part.material = (int)out.materials.size();
        out.materials.push_back(std::move(mat));
        out.parts.push_back(std::move(part));
        surf = next;
        if (sFrames > numFrames) numFrames = sFrames;
    }
    if (out.parts.empty()) return Fail(err, "the file has no triangles");

    // tags of frame 0: an origin and three axis vectors (the columns of the tag's rotation)
    if (numTags > 0) {
        Br g(bytes);
        if (!g.Seek((std::uint64_t)ofsTags) || !g.Fits((std::uint64_t)numTags, 112)) return Fail(err, "the file ends inside the tag list");
        for (std::int32_t i = 0; i < numTags; ++i) {
            ImportedNode nd;
            nd.name = g.Str(64);
            double o[3], ax[3][3];
            for (double& x : o) x = g.F32();
            for (auto& r : ax) for (double& x : r) x = g.F32();
            bool fin = true;
            for (double x : o) if (!std::isfinite(x) || std::fabs(x) > 1e9) fin = false;
            for (auto& r : ax) for (double x : r) if (!std::isfinite(x) || std::fabs(x) > 1e9) fin = false;
            if (!fin) return Fail(err, "tag " + std::to_string(i) + " has a position or axis that is not finite or is out of range");
            nd.parent = 0;
            nd.position = Vec3{(float)o[0], (float)o[1], (float)o[2]};
            if (!QuatFromColumns(ax, nd.rotation)) { ++badTagRot; nd.rotation = Quat{0, 0, 0, 1}; }
            if (nd.name.empty()) nd.name = "tag" + std::to_string(i);
            out.nodes.push_back(std::move(nd));
        }
    }
    detail::rc::ApplyUpAxis(out, true, false);   // Quake III is Z-up
    out.backend = "own";

    auto note = [&](const std::string& s) { out.notes.push_back("MD3: " + s); };
    if (numFrames > 1) note(std::to_string(numFrames) + " frames of vertex animation: only the first frame is read (the engine has no vertex animation)");
    if (numTags > 0) note(std::to_string(numTags) + " tag(s) became empty child nodes of the root (attachment points of the first frame; tag animation is not read)");
    note("texture names are the shader names as written (relative to the game folder, not to the model); .skin and .shader files are not read");
    if (noShader) note(std::to_string(noShader) + " surface(s) name no shader: no texture");
    if (badTagRot) note(std::to_string(badTagRot) + " tag(s) whose axes are not a rotation (scaled, skewed or mirrored): left unrotated");
    if (badVert) note(std::to_string(badVert) + " triangle(s) with an out-of-range vertex index skipped");
    if (degenerate) note(std::to_string(degenerate) + " degenerate triangle(s) skipped");
    if (emptySurfaces) note(std::to_string(emptySurfaces) + " surface(s) without vertices or triangles skipped");
    if (nonFiniteUv) note(std::to_string(nonFiniteUv) + " texture coordinate pair(s) that were not finite were set to 0");
    return true;
}

bool ReadMd3Buffer(const std::string& bytes, ImportedModel& out, std::string* err) {
    const bool ok = ReadMd3Impl(bytes, out, err);
    if (!ok) out = ImportedModel{};   // a failed read leaves nothing behind
    return ok;
}

bool ReadMd3File(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, md3::kMaxFileBytes, bytes, err)) return false;
    return ReadMd3Buffer(bytes, out, err);
}

} // namespace okay
