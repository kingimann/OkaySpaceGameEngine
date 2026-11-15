// Our own HMP (3D GameStudio terrain model, "HMP7") reader: the height grid as one static part, a regular grid of
// vertices with two triangles per cell, smooth normals computed from the heights, and texture coordinates spread over the
// grid. The embedded skin picture is skipped (our materials name texture files, they cannot carry pixels); it is
// reported in `out.notes`.
// Written from the field-by-field analysis of the two real files of the format that were available (a plain terrain and
// the same terrain with an embedded skin) and from the layout those files share with the Quake / GameStudio model header
// (docs/own_hmp_reader.md says what was derived from what, and what is guessed). No other project's importer source was read.
// Only the HMP7 flavour of the format is read. Bounds-checked, per-file budgets, no recursion.
// See okay/Render/ModelData.hpp and docs/own_hmp_reader.md.
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
namespace hmp {

using detail::bin::Br;

constexpr std::size_t kMaxFileBytes = 128u * 1024u * 1024u;
constexpr std::uint32_t kHeaderBytes = 84, kFrameHeaderBytes = 36, kVertexBytes = 4;
constexpr std::uint64_t kMaxVerts = 1048576;       // 1024 x 1024 grid points: 2 million triangles
constexpr std::uint32_t kMaxSkins = 64;
constexpr std::uint64_t kMaxSkinPixels = 64ull * 1024 * 1024;
constexpr double kMaxMagnitude = 1e9;

bool Fail(std::string* err, const std::string& m) { if (err) *err = "HMP: " + m; return false; }
bool GoodFloat(float f) { return std::isfinite(f) && std::fabs(f) <= kMaxMagnitude; }

} // namespace hmp
} // namespace

static bool ReadHmpImpl(const std::string& bytes, ImportedModel& out, std::string* err) {
    using namespace hmp;
    out = ImportedModel{};
    Br b(bytes);
    if (b.n < kHeaderBytes) return Fail(err, "file too short");
    if (std::memcmp(b.d, "HMP", 3) != 0) return Fail(err, "not an HMP file (the magic text HMP is missing)");
    if (b.d[3] != '7') return Fail(err, std::string("unsupported HMP version \"") + (char)(b.d[3] >= 32 && b.d[3] < 127 ? b.d[3] : '?') + "\" (only HMP7 is read)");
    b.pos = 4;
    b.I32();   // a version-like number, 0 in every file seen
    float scale[3], origin[3];
    for (float& f : scale) f = b.F32();
    for (float& f : origin) f = b.F32();
    b.F32();   // bounding radius
    const float stepX = b.F32(), stepY = b.F32(), numX = b.F32();   // the size of one grid cell in x and y; the number of grid points in a row
    const std::int32_t numSkins = b.I32();
    b.I32(); b.I32();   // skin width and height of the header (0 in every file seen; each skin carries its own)
    const std::int32_t numVerts = b.I32();
    b.I32();   // numtris (0 in every file seen)
    const std::int32_t numFrames = b.I32();
    for (int i = 0; i < 3; ++i) b.I32();   // synctype, flags, size
    for (float f : scale) if (!GoodFloat(f)) return Fail(err, "a scale value is not finite or is out of range");
    for (float f : origin) if (!GoodFloat(f)) return Fail(err, "an origin value is not finite or is out of range");
    if (!GoodFloat(stepX) || !GoodFloat(stepY) || !(stepX > 0) || !(stepY > 0)) return Fail(err, "the grid cell size is not a positive, finite number");
    if (!std::isfinite(numX) || numX < 2.0f || numX > (float)kMaxVerts || numX != std::floor(numX)) return Fail(err, "the number of grid points per row is not a whole number of 2 or more");
    if (numVerts < 4 || (std::uint64_t)numVerts > kMaxVerts) return Fail(err, "bad vertex count " + std::to_string(numVerts) + " (budget: 4 to " + std::to_string(kMaxVerts) + " grid points per file)");
    if (numSkins < 0 || (std::uint32_t)numSkins > kMaxSkins) return Fail(err, "bad skin count " + std::to_string(numSkins));
    if (numFrames < 1 || numFrames > 65535) return Fail(err, "bad frame count " + std::to_string(numFrames));
    const std::uint32_t nx = (std::uint32_t)numX;
    if ((std::uint32_t)numVerts % nx != 0) return Fail(err, "the vertex count is not a multiple of the row length");
    const std::uint32_t ny = (std::uint32_t)numVerts / nx;
    if (ny < 2) return Fail(err, "the grid has only one row");

    // skins: a type, a width and a height, then the pixels. Only type 4 (3 bytes per pixel) is known; the skin is skipped.
    std::uint32_t skinPixels = 0;
    for (std::int32_t i = 0; i < numSkins; ++i) {
        const std::int32_t type = b.I32(), w = b.I32(), h = b.I32();
        if (b.bad) return Fail(err, "the file ends inside a skin header");
        if (type != 4) return Fail(err, "skin " + std::to_string(i) + " has type " + std::to_string(type) + " (only type 4, 24 bit colour, is understood)");
        if (w <= 0 || h <= 0 || (std::uint64_t)w * (std::uint64_t)h > kMaxSkinPixels) return Fail(err, "skin " + std::to_string(i) + " has a bad size");
        const std::uint64_t bytesNeeded = (std::uint64_t)w * (std::uint64_t)h * 3u;
        if (!b.Has(bytesNeeded)) return Fail(err, "the file ends inside skin " + std::to_string(i));
        b.Skip(bytesNeeded);
        ++skinPixels;
    }
    // the first frame: a header (36 bytes, not used) and the grid points, 4 bytes each: a 16 bit height and two normal bytes
    if (!b.Has(kFrameHeaderBytes)) return Fail(err, "the file ends inside the frame header");
    b.Skip(kFrameHeaderBytes);
    if (!b.Fits((std::uint64_t)numVerts, kVertexBytes)) return Fail(err, "the file ends inside the height grid");

    ImportedPart part;
    Mesh& me = part.mesh;
    me.vertices.resize((std::size_t)numVerts);
    me.uvs.resize((std::size_t)numVerts);
    for (std::uint32_t r = 0; r < ny; ++r) {
        for (std::uint32_t c = 0; c < nx; ++c) {
            const std::uint16_t h = b.U16();
            b.U8(); b.U8();   // the stored normal of the point: not read, the normals are computed from the heights
            const std::size_t i = (std::size_t)r * nx + c;
            const double x = (double)origin[0] + (double)c * (double)stepX;
            const double y = (double)origin[1] + (double)r * (double)stepY;
            const double z = (double)origin[2] + (double)h * (double)scale[2];
            me.vertices[i] = Vec3{(float)x, (float)y, (float)z};
            me.uvs[i] = Vec2{(float)c / (float)(nx - 1), 1.0f - (float)r / (float)(ny - 1)};
        }
    }
    // two triangles per cell, counter clockwise seen from above (+Z)
    me.triangles.reserve((std::size_t)(nx - 1) * (ny - 1) * 6);
    for (std::uint32_t r = 0; r + 1 < ny; ++r)
        for (std::uint32_t c = 0; c + 1 < nx; ++c) {
            const int v00 = (int)(r * nx + c), v10 = v00 + 1, v01 = v00 + (int)nx, v11 = v01 + 1;
            const int tri[6] = {v00, v10, v11, v00, v11, v01};
            me.triangles.insert(me.triangles.end(), tri, tri + 6);
        }
    // smooth normals: the sum of the (area weighted) face normals of the triangles at each point
    me.normals.assign((std::size_t)numVerts, Vec3{0, 0, 0});
    for (std::size_t t = 0; t + 2 < me.triangles.size(); t += 3) {
        const Vec3 &p0 = me.vertices[(std::size_t)me.triangles[t]], &p1 = me.vertices[(std::size_t)me.triangles[t + 1]], &p2 = me.vertices[(std::size_t)me.triangles[t + 2]];
        const double ux = (double)p1.x - p0.x, uy = (double)p1.y - p0.y, uz = (double)p1.z - p0.z;
        const double vx = (double)p2.x - p0.x, vy = (double)p2.y - p0.y, vz = (double)p2.z - p0.z;
        const Vec3 fn{(float)(uy * vz - uz * vy), (float)(uz * vx - ux * vz), (float)(ux * vy - uy * vx)};
        for (int k = 0; k < 3; ++k) {
            Vec3& n = me.normals[(std::size_t)me.triangles[t + (std::size_t)k]];
            n = Vec3{n.x + fn.x, n.y + fn.y, n.z + fn.z};
        }
    }
    for (Vec3& n : me.normals) {
        const double l = std::sqrt((double)n.x * n.x + (double)n.y * n.y + (double)n.z * n.z);
        n = l > 1e-20 && std::isfinite(l) ? Vec3{(float)(n.x / l), (float)(n.y / l), (float)(n.z / l)} : Vec3{0, 0, 1};
    }
    if (!detail::rc::PartInRange(part)) return Fail(err, "a vertex is out of range");

    ImportedNode root;
    root.name = "HMP";
    out.nodes.push_back(root);
    part.node = 0;
    part.material = 0;
    me.name = "terrain";
    ImportedMaterial mat;
    mat.name = "terrain";
    mat.hasDiffuse = true;
    mat.diffuse = Color{1, 1, 1, 1};
    out.materials.push_back(std::move(mat));
    out.parts.push_back(std::move(part));
    detail::rc::ApplyUpAxis(out, true, false);   // GameStudio is Z-up
    out.backend = "own";

    auto note = [&](const std::string& s) { out.notes.push_back("HMP: " + s); };
    if (skinPixels) note(std::to_string(skinPixels) + " embedded skin picture(s) skipped (a material cannot carry pixels); the texture coordinates still cover the whole grid");
    if (numFrames > 1) note(std::to_string(numFrames) + " frames: only the first is read");
    note("the stored normals are not read: smooth normals are computed from the heights; the texture coordinates are generated (u along x, v from the last row to the first)");
    return true;
}

bool ReadHmpBuffer(const std::string& bytes, ImportedModel& out, std::string* err) {
    const bool ok = ReadHmpImpl(bytes, out, err);
    if (!ok) out = ImportedModel{};   // a failed read leaves nothing behind
    return ok;
}

bool ReadHmpFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, hmp::kMaxFileBytes, bytes, err)) return false;
    return ReadHmpBuffer(bytes, out, err);
}

} // namespace okay
