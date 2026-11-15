// Our own MDC (Return to Castle Wolfenstein model, binary) reader: the first frame of the vertex animation as one static
// part per surface (texture coordinates, the stored normals, the first shader name as the texture) and the tags
// (attachment points) of the first frame as empty child nodes. MDC is MD3 with "compressed" frames: a few full base
// frames and, for the other frames, small per-vertex offsets from a base frame.
// Written from the public description of the format (id Software's published model structures, mdcHeader_t /
// mdcSurface_t / mdcXyzCompressed_t, and the MD3 notes they build on) and from the field-by-field analysis of one real
// file (docs/own_mdc_reader.md says what was checked and what was not). No other project's importer source was read.
// Bounds-checked, per-file budgets, no recursion, no allocation from an unchecked count.
// See okay/Render/ModelData.hpp and docs/own_mdc_reader.md.
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
namespace mdc {

using detail::bin::Br;

constexpr std::size_t kMaxFileBytes = 128u * 1024u * 1024u;
constexpr std::uint32_t kHeaderBytes = 112, kSurfaceHeaderBytes = 124, kFrameBytes = 56, kTagNameBytes = 64, kTagBytes = 12;
// Per-file budgets. The game's own limits are 32 surfaces, 8192 triangles and 4096 vertices per surface; these are far above
// real files and far below what a small hostile file could otherwise ask for.
constexpr std::uint32_t kMaxFrames = 65535, kMaxTags = 4096, kMaxSurfaces = 1024;
constexpr std::uint64_t kMaxTotalTris = 500000, kMaxTotalVerts = 500000;
constexpr double kPi = 3.14159265358979323846;
constexpr double kXyzScale = 1.0 / 64.0;     // base-frame coordinates are 16 bit fixed point, 1/64 unit
constexpr double kOfsScale = 0.05;           // compressed offsets: units of 0.05, biased by 127
constexpr int kOfsBias = 127;

bool Fail(std::string* err, const std::string& m) { if (err) *err = "MDC: " + m; return false; }

// The stored normal is two bytes: latitude and longitude as 1/256 of a turn (same packing as MD3).
Vec3 DecodeNormal(std::uint16_t n) {
    const double lat = (double)(n >> 8) * (2.0 * kPi / 256.0), lng = (double)(n & 255) * (2.0 * kPi / 256.0);
    return Vec3{(float)(std::cos(lat) * std::sin(lng)), (float)(std::sin(lat) * std::sin(lng)), (float)std::cos(lng)};
}

} // namespace mdc
} // namespace

static bool ReadMdcImpl(const std::string& bytes, ImportedModel& out, std::string* err) {
    using namespace mdc;
    out = ImportedModel{};
    Br b(bytes);
    if (b.n < kHeaderBytes) return Fail(err, "file too short");
    if (std::memcmp(b.d, "IDPC", 4) != 0) return Fail(err, "not an MDC file (the magic text IDPC is missing)");
    b.pos = 4;
    const std::int32_t version = b.I32();
    if (version != 2) return Fail(err, "unsupported version " + std::to_string(version) + " (2 is read)");
    const std::string modelName = b.Str(64);
    b.I32();   // flags
    const std::int32_t numFrames = b.I32(), numTags = b.I32(), numSurfaces = b.I32();
    b.I32();   // numSkins
    const std::int32_t ofsFrames = b.I32(), ofsTagNames = b.I32(), ofsTags = b.I32(), ofsSurfaces = b.I32(), ofsEnd = b.I32();
    if (numSurfaces <= 0 || (std::uint32_t)numSurfaces > kMaxSurfaces) return Fail(err, "bad surface count " + std::to_string(numSurfaces) + " (budget: 1 to 1024 surfaces)");
    if (numTags < 0 || (std::uint32_t)numTags > kMaxTags) return Fail(err, "bad tag count " + std::to_string(numTags) + " (budget: at most 4096 tags)");
    if (numFrames <= 0 || (std::uint32_t)numFrames > kMaxFrames) return Fail(err, "bad frame count " + std::to_string(numFrames) + " (budget: 1 to 65535 frames)");
    if (ofsFrames < (std::int32_t)kHeaderBytes || ofsTagNames < 0 || ofsTags < 0 || ofsSurfaces < (std::int32_t)kHeaderBytes || ofsEnd < (std::int32_t)kHeaderBytes)
        return Fail(err, "a table offset is out of range");
    if ((std::uint64_t)ofsEnd > (std::uint64_t)b.n) return Fail(err, "the file is shorter than its own end offset");
    {   // the frame table (bounds, origin, radius, name per frame) is not used, but it must be inside the file
        Br f(bytes);
        if (!f.Seek((std::uint64_t)ofsFrames) || !f.Fits((std::uint64_t)numFrames, kFrameBytes)) return Fail(err, "the file ends inside the frame table");
    }

    ImportedNode root;
    root.name = modelName.empty() ? "MDC" : modelName;
    out.nodes.push_back(root);

    int badVert = 0, emptySurfaces = 0, degenerate = 0, noShader = 0, nonFiniteUv = 0, compressedFirst = 0;
    std::uint64_t totalVerts = 0, totalTris = 0;
    std::uint64_t surf = (std::uint64_t)ofsSurfaces;
    for (std::int32_t si = 0; si < numSurfaces; ++si) {
        Br s(bytes);
        const std::string at = "surface " + std::to_string(si);
        if (!s.Seek(surf) || !s.Has(kSurfaceHeaderBytes)) return Fail(err, "the file ends inside the header of " + at);
        s.pos += 4;   // the ident of a surface: not checked (the real file read has 7 here, not a magic text)
        const std::string surfName = s.Str(64);
        s.I32();   // flags
        const std::int32_t nComp = s.I32(), nBase = s.I32(), nShaders = s.I32(), nVerts = s.I32(), nTris = s.I32();
        const std::int32_t oTris = s.I32(), oShaders = s.I32(), oSt = s.I32(), oXyzN = s.I32(), oXyzC = s.I32(), oFrBase = s.I32(), oFrComp = s.I32(), oEnd = s.I32();
        if (nVerts < 0 || nTris < 0 || nComp < 0 || nBase < 0 || nShaders < 0) return Fail(err, at + " has a negative count");
        if (oEnd < (std::int32_t)kSurfaceHeaderBytes) return Fail(err, at + " has a bad end offset");
        if (oTris < (std::int32_t)kSurfaceHeaderBytes || oShaders < (std::int32_t)kSurfaceHeaderBytes || oSt < (std::int32_t)kSurfaceHeaderBytes ||
            oXyzN < (std::int32_t)kSurfaceHeaderBytes || oXyzC < (std::int32_t)kSurfaceHeaderBytes || oFrBase < (std::int32_t)kSurfaceHeaderBytes || oFrComp < (std::int32_t)kSurfaceHeaderBytes)
            return Fail(err, at + " has a bad table offset");
        const std::uint64_t next = surf + (std::uint64_t)oEnd;
        if (nVerts == 0 || nTris == 0) { ++emptySurfaces; surf = next; continue; }
        // the budgets come before any allocation
        totalVerts += (std::uint64_t)nVerts; totalTris += (std::uint64_t)nTris;
        if (totalVerts > kMaxTotalVerts) return Fail(err, "over the vertex budget (" + std::to_string(kMaxTotalVerts) + " vertices per file)");
        if (totalTris > kMaxTotalTris) return Fail(err, "over the triangle budget (" + std::to_string(kMaxTotalTris) + " triangles per file)");
        if (nBase < 1) return Fail(err, at + " has no base frame");

        // which base frame and which compressed frame make frame 0: two 16 bit numbers each
        Br fb(bytes), fc(bytes);
        if (!fb.Seek(surf + (std::uint64_t)oFrBase) || !fb.Has(2)) return Fail(err, "the file ends inside the frame table of " + at);
        const int baseIdx = fb.I16();
        int compIdx = -1;
        if (!fc.Seek(surf + (std::uint64_t)oFrComp) || !fc.Has(2)) return Fail(err, "the file ends inside the compressed-frame table of " + at);
        compIdx = fc.I16();
        if (baseIdx < 0 || baseIdx >= nBase) return Fail(err, at + " names a base frame that does not exist");
        if (compIdx < -1 || compIdx >= nComp) return Fail(err, at + " names a compressed frame that does not exist");

        // base-frame vertices: x, y, z in 1/64 units and the packed normal (8 bytes each)
        Br v(bytes);
        if (!v.Seek(surf + (std::uint64_t)oXyzN) || !v.Fits((std::uint64_t)nBase * (std::uint64_t)nVerts, 8)) return Fail(err, "the file ends inside the vertices of " + at);
        v.Skip((std::uint64_t)baseIdx * (std::uint64_t)nVerts * 8);
        detail::rc::Soup soup;
        std::vector<Vec3> nrm((std::size_t)nVerts);
        soup.pos.resize((std::size_t)nVerts);
        for (std::int32_t i = 0; i < nVerts; ++i) {
            const double x = v.I16() * kXyzScale, y = v.I16() * kXyzScale, z = v.I16() * kXyzScale;
            soup.pos[(std::size_t)i] = Vec3{(float)x, (float)y, (float)z};
            nrm[(std::size_t)i] = DecodeNormal(v.U16());
        }
        if (compIdx >= 0) {   // frame 0 is a compressed frame: add its offsets to the base vertices
            Br c(bytes);
            if (!c.Seek(surf + (std::uint64_t)oXyzC) || !c.Fits((std::uint64_t)nComp * (std::uint64_t)nVerts, 4)) return Fail(err, "the file ends inside the compressed vertices of " + at);
            c.Skip((std::uint64_t)compIdx * (std::uint64_t)nVerts * 4);
            for (std::int32_t i = 0; i < nVerts; ++i) {
                const int dx = c.U8() - kOfsBias, dy = c.U8() - kOfsBias, dz = c.U8() - kOfsBias;
                c.U8();   // the normal index of the compressed vertex (an index into a fixed table of 256 directions: not read)
                Vec3& p = soup.pos[(std::size_t)i];
                p = Vec3{(float)(p.x + dx * kOfsScale), (float)(p.y + dy * kOfsScale), (float)(p.z + dz * kOfsScale)};
            }
            ++compressedFirst;
        }
        // texture coordinates, one pair per vertex
        Br t(bytes);
        if (!t.Seek(surf + (std::uint64_t)oSt) || !t.Fits((std::uint64_t)nVerts, 8)) return Fail(err, "the file ends inside the texture coordinates of " + at);
        std::vector<Vec2> uv((std::size_t)nVerts);
        for (Vec2& u : uv) {
            float a = t.F32(), c = t.F32();
            if (!std::isfinite(a) || !std::isfinite(c) || std::fabs(a) > 1e9f || std::fabs(c) > 1e9f) { a = 0; c = 0; ++nonFiniteUv; }
            u = Vec2{a, c};   // as written, like MD3
        }
        // triangles: three vertex numbers each; Quake III models wind clockwise, so two corners swap
        Br tr(bytes);
        if (!tr.Seek(surf + (std::uint64_t)oTris) || !tr.Fits((std::uint64_t)nTris, 12)) return Fail(err, "the file ends inside the triangles of " + at);
        soup.ci.reserve((std::size_t)nTris * 3);
        soup.cuv.reserve((std::size_t)nTris * 3);
        if (compIdx < 0) soup.cnrm.reserve((std::size_t)nTris * 3);
        for (std::int32_t i = 0; i < nTris; ++i) {
            const std::uint32_t a = tr.U32(), c = tr.U32(), d = tr.U32();
            if (a >= (std::uint32_t)nVerts || c >= (std::uint32_t)nVerts || d >= (std::uint32_t)nVerts) { ++badVert; continue; }
            for (std::uint32_t k : {a, d, c}) {
                soup.ci.push_back(k);
                soup.cuv.push_back(uv[k]);
                if (compIdx < 0) soup.cnrm.push_back(nrm[k]);
            }
        }
        // first shader: a 64 byte name and a number
        std::string shader;
        if (nShaders > 0) {
            Br sh(bytes);
            if (sh.Seek(surf + (std::uint64_t)oShaders) && sh.Has(68)) shader = sh.Str(64);
        }
        ImportedPart part;
        detail::rc::BuildStats st;
        if (!detail::rc::BuildMesh(soup, 180.0, part.mesh, &st, (std::size_t)(kMaxTotalVerts * 3))) return Fail(err, "over the output vertex budget");
        degenerate += st.degenerate;
        surf = next;
        if (part.mesh.triangles.empty()) { ++emptySurfaces; continue; }
        part.mesh.name = surfName;
        if (!detail::rc::PartInRange(part)) return Fail(err, "a vertex is out of range");
        ImportedMaterial mat;
        mat.name = surfName;
        for (char& ch : shader) if (ch == '\\') ch = '/';
        mat.texture = shader;
        if (shader.empty()) ++noShader;
        mat.hasDiffuse = true;
        mat.diffuse = Color{1, 1, 1, 1};
        part.node = 0;
        part.material = (int)out.materials.size();
        out.materials.push_back(std::move(mat));
        out.parts.push_back(std::move(part));
    }
    if (out.parts.empty()) return Fail(err, "the file has no triangles");

    // tags of frame 0: a name each, and an origin (3 x 16 bit, 1/64 units) and three angles (not read)
    if (numTags > 0) {
        Br nm(bytes), g(bytes);
        if (!nm.Seek((std::uint64_t)ofsTagNames) || !nm.Fits((std::uint64_t)numTags, kTagNameBytes)) return Fail(err, "the file ends inside the tag names");
        if (!g.Seek((std::uint64_t)ofsTags) || !g.Fits((std::uint64_t)numTags, kTagBytes)) return Fail(err, "the file ends inside the tag list");
        for (std::int32_t i = 0; i < numTags; ++i) {
            ImportedNode nd;
            nd.name = nm.Str(kTagNameBytes);
            const double x = g.I16() * kXyzScale, y = g.I16() * kXyzScale, z = g.I16() * kXyzScale;
            g.Skip(6);   // pitch, yaw, roll as 16 bit numbers
            nd.parent = 0;
            nd.position = Vec3{(float)x, (float)y, (float)z};
            if (nd.name.empty()) nd.name = "tag" + std::to_string(i);
            out.nodes.push_back(std::move(nd));
        }
    }
    detail::rc::ApplyUpAxis(out, true, false);   // Quake III engine models are Z-up
    out.backend = "own";

    auto note = [&](const std::string& s) { out.notes.push_back("MDC: " + s); };
    if (numFrames > 1) note(std::to_string(numFrames) + " frames of vertex animation: only the first frame is read (the engine has no vertex animation)");
    if (numTags > 0) note(std::to_string(numTags) + " tag(s) became empty child nodes of the root at their first-frame position; their rotation angles and their animation are not read");
    note("texture names are the shader names as written (relative to the game folder, not to the model); .skin and .shader files are not read");
    if (compressedFirst) note(std::to_string(compressedFirst) + " surface(s) whose first frame is a compressed frame: offsets applied to the base vertices, smooth normals generated (the compressed normal table is not read)");
    if (noShader) note(std::to_string(noShader) + " surface(s) name no shader: no texture");
    if (badVert) note(std::to_string(badVert) + " triangle(s) with an out-of-range vertex index skipped");
    if (degenerate) note(std::to_string(degenerate) + " degenerate triangle(s) skipped");
    if (emptySurfaces) note(std::to_string(emptySurfaces) + " surface(s) without vertices or triangles skipped");
    if (nonFiniteUv) note(std::to_string(nonFiniteUv) + " texture coordinate pair(s) that were not finite were set to 0");
    return true;
}

bool ReadMdcBuffer(const std::string& bytes, ImportedModel& out, std::string* err) {
    const bool ok = ReadMdcImpl(bytes, out, err);
    if (!ok) out = ImportedModel{};   // a failed read leaves nothing behind
    return ok;
}

bool ReadMdcFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, mdc::kMaxFileBytes, bytes, err)) return false;
    return ReadMdcBuffer(bytes, out, err);
}

} // namespace okay
