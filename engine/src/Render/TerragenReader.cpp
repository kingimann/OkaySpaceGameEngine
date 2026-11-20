// Our own Terragen terrain (.ter) reader.
//
// Written from Planetside's published "Terragen Terrain file specification" (the wiki page "Terragen .TER Format") only:
// the 16-byte identifier, the 4-byte-aligned chunks without a length field (SIZE, XPTS, YPTS, SCAL, CRAD, CRVM, ALTW,
// "EOF "), the elevation formula BaseHeight + Elevation * HeightScale / 65536, and the curve mode 1 sphere. No importer
// source (Assimp, GDAL, the reference library the page links, or any other) was read. See docs/content/model_readers.md.
//
// The output is one mesh: a regular grid in metres (SCAL is metres per terrain unit), turned from Terragen's Z-up to the
// engine's Y-up as (x, y, z) -> (x, z, -y), with normals from the height grid and u, v running 0..1 across it.
// Hostile input is the normal case: every read is bounds checked, the grid is capped, no recursion.
#include "TextReaderUtil.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace okay {

namespace {
namespace ter {

constexpr std::size_t kMaxFileBytes = 64u * 1024u * 1024u;
constexpr std::size_t kMaxSide      = 4097;                   // points along one side (Terragen's own largest terrains)
constexpr std::uint64_t kMaxPoints  = 2049ull * 2049ull;      // whole grid: about 4.2 million points, about 270 MB while reading
constexpr double kMinScale          = 1e-6;                   // metres per terrain unit: with both limits every slope,
constexpr double kMaxScale          = 1e6;                    // and so every normal, stays inside float range
constexpr double kDefaultScale      = 30.0;                   // the specification's default
constexpr double kDefaultRadiusKm   = 6370.0;                 // the specification's default planet radius

std::uint16_t U16(const unsigned char* p) { return (std::uint16_t)(p[0] | (p[1] << 8)); }
std::int16_t  I16(const unsigned char* p) { return (std::int16_t)U16(p); }
std::uint32_t U32(const unsigned char* p) {
    return (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) | ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24);
}
float F32(const unsigned char* p) {   // "intel-ordered" (little endian) IEEE single
    const std::uint32_t u = U32(p);
    float f;
    std::memcpy(&f, &u, sizeof f);
    return f;
}

struct Reader {
    std::string err;
    std::vector<std::string> notes;
    bool Fail(const std::string& m) { if (err.empty()) err = "Terragen: " + m; return false; }
    void Note(const std::string& m) { notes.push_back("Terragen: " + m); }

    bool Run(const std::string& bytes, ImportedModel& out) {
        const unsigned char* b = reinterpret_cast<const unsigned char*>(bytes.data());
        const std::size_t n = bytes.size();
        if (n > kMaxFileBytes) return Fail("file too large");
        if (n < 16 || std::memcmp(b, "TERRAGEN", 8) != 0 || std::memcmp(b + 8, "TERRAIN ", 8) != 0)
            return Fail("not a Terragen terrain file (it must start with \"TERRAGENTERRAIN \")");

        bool haveSize = false, haveX = false, haveY = false, haveAltw = false, haveEof = false;
        bool haveCrad = false, lateOptional = false;
        std::uint32_t sizeVal = 0, xVal = 0, yVal = 0, curveMode = 0;
        double scale[3] = {kDefaultScale, kDefaultScale, kDefaultScale};
        double radiusKm = kDefaultRadiusKm;
        std::size_t gridX = 0, gridY = 0, altwData = 0;
        double heightScale = 0, baseHeight = 0;

        std::size_t pos = 16;
        auto have = [&](std::size_t k) { return n - pos >= k; };
        while (pos < n) {
            if (!have(4)) { Note(std::to_string(n - pos) + " byte(s) at the end are too short for a chunk and were ignored"); break; }
            const unsigned char* m = b + pos;
            const std::string tag(reinterpret_cast<const char*>(m), 4);
            if (tag == "SIZE" || tag == "XPTS" || tag == "YPTS") {
                if (!have(8)) return Fail("the file ends inside the " + tag + " chunk");
                if (haveAltw) return Fail(tag + " comes after the elevation data, which needs it first");
                const std::uint32_t v = U16(m + 4);   // then 2 bytes of padding
                if (tag == "SIZE") { sizeVal = v; haveSize = true; }
                else if (tag == "XPTS") { xVal = v; haveX = true; }
                else { yVal = v; haveY = true; }
                pos += 8;
            } else if (tag == "SCAL") {
                if (!have(16)) return Fail("the file ends inside the SCAL chunk");
                for (int k = 0; k < 3; ++k) {
                    const double s = F32(m + 4 + 4 * k);
                    if (!std::isfinite(s) || s < kMinScale || s > kMaxScale) return Fail("bad SCAL value (metres per terrain unit must be between 1e-6 and 1e6)");
                    scale[k] = s;
                }
                if (haveAltw) lateOptional = true;
                pos += 16;
            } else if (tag == "CRAD") {
                if (!have(8)) return Fail("the file ends inside the CRAD chunk");
                radiusKm = F32(m + 4);
                haveCrad = true;
                if (haveAltw) lateOptional = true;
                pos += 8;
            } else if (tag == "CRVM") {
                if (!have(8)) return Fail("the file ends inside the CRVM chunk");
                curveMode = U32(m + 4);
                if (haveAltw) lateOptional = true;
                pos += 8;
            } else if (tag == "ALTW") {
                if (haveAltw) return Fail("two ALTW (elevation) chunks");
                if (!haveSize) return Fail("the ALTW chunk comes before the SIZE chunk");
                const std::size_t side = (std::size_t)sizeVal + 1;   // SIZE holds n - 1, n = points on the shortest side
                gridX = haveX ? xVal : side;
                gridY = haveY ? yVal : side;
                if (haveX != haveY) Note(std::string("only ") + (haveX ? "XPTS" : "YPTS") + " is given; the other side was taken from SIZE");
                if (gridX < 2 || gridY < 2) return Fail("the terrain needs at least 2 points on each side");
                if (gridX > kMaxSide || gridY > kMaxSide || (std::uint64_t)gridX * gridY > kMaxPoints)
                    return Fail("the terrain has " + std::to_string(gridX) + " by " + std::to_string(gridY) + " points; the limit is 2049 by 2049 (about 4.2 million points)");
                if ((haveX || haveY) && std::min(gridX, gridY) != side)
                    Note("SIZE says " + std::to_string(side) + " points on the shortest side but XPTS/YPTS say " + std::to_string(gridX) + " by " + std::to_string(gridY) + "; XPTS and YPTS were used");
                const std::size_t dataBytes = gridX * gridY * 2;
                if (!have(8 + dataBytes)) return Fail("the file ends inside the elevation data (" + std::to_string(gridX) + " by " + std::to_string(gridY) + " points need " + std::to_string(dataBytes) + " bytes)");
                heightScale = I16(m + 4);
                baseHeight = I16(m + 6);
                altwData = pos + 8;
                haveAltw = true;
                const std::size_t end = pos + 8 + dataBytes;
                // Chunks are aligned to 4 bytes, so 2 bytes of padding follow an odd point count. A writer that left the
                // padding out is still read when "EOF " follows straight away.
                if (end % 4 != 0 && n - end >= 4 && std::memcmp(b + end, "EOF ", 4) == 0) { pos = end; Note("no padding after the elevation data"); }
                else pos = std::min(n, (end + 3) / 4 * 4);
            } else if (tag == "EOF ") {
                haveEof = true;
                pos += 4;
                break;
            } else {
                // A terrain chunk has no length field, so a chunk we do not know cannot be skipped.
                std::string shown;
                for (unsigned char c : tag) shown += (c >= 32 && c < 127) ? (char)c : '?';
                if (!haveAltw) return Fail("unknown chunk '" + shown + "' before the elevation data (terrain chunks carry no length, so it cannot be skipped)");
                Note("unknown data after the elevations (" + std::to_string(n - pos) + " bytes, starting with '" + shown + "') was ignored");
                pos = n;
                break;
            }
        }
        if (!haveAltw) return Fail(haveSize ? "no ALTW (elevation) chunk" : "no SIZE chunk and no elevation data");
        if (!haveEof) Note("no 'EOF ' marker at the end (only old versions of Terragen need it)");
        else if (pos < n) Note(std::to_string(n - pos) + " byte(s) after the 'EOF ' marker were ignored");
        if (lateOptional) Note("SCAL, CRAD or CRVM comes after the elevation data; it was still applied");
        if (scale[0] != scale[1] || scale[1] != scale[2]) Note("the scale is not the same on x, y and z (Terragen cannot use that); each axis was scaled by its own value");

        // Curve mode 1: the terrain is draped over a sphere of radius CRAD * 1000 / zscale terrain units whose top is at the
        // middle of the grid; x and y stay as they are and each point drops by the sphere's fall-off at its distance.
        bool curved = false;
        double radius = 0;
        if (curveMode == 1) {
            if (!std::isfinite(radiusKm) || radiusKm <= 0 || radiusKm > 1e9) return Fail("bad CRAD (planet radius) for a curved terrain");
            radius = radiusKm * 1000.0 / scale[2];
            curved = true;
            if (!haveCrad) Note("curve mode 1 without a CRAD chunk: the default radius (6370 km) was used");
        } else if (curveMode != 0) {
            Note("unknown curve mode " + std::to_string(curveMode) + " (only 0 and 1 are defined); the terrain is drawn flat");
        }
        const double midX = (double)(gridX - 1) / 2.0, midY = (double)(gridY - 1) / 2.0;
        if (curved) {
            const double far2 = midX * midX + midY * midY;
            if (far2 >= radius * radius) return Fail("the terrain is wider than the sphere of its curve mode (CRAD too small)");
        }

        // Heights in metres, row by row (the first row is y = 0, left to right).
        const std::size_t count = gridX * gridY;
        std::vector<double> h(count);
        for (std::size_t j = 0; j < gridY; ++j) {
            for (std::size_t i = 0; i < gridX; ++i) {
                const std::size_t k = j * gridX + i;
                double alt = baseHeight + (double)I16(b + altwData + 2 * k) * heightScale / 65536.0;   // terrain units
                if (curved) {
                    const double dx = (double)i - midX, dy = (double)j - midY;
                    alt += std::sqrt(radius * radius - dx * dx - dy * dy) - radius;
                }
                h[k] = alt * scale[2];
            }
        }

        Mesh mesh;
        mesh.name = "Terrain";
        mesh.vertices.resize(count);
        mesh.normals.resize(count);
        mesh.uvs.resize(count);
        const double sx = scale[0], sy = scale[1];
        for (std::size_t j = 0; j < gridY; ++j) {
            for (std::size_t i = 0; i < gridX; ++i) {
                const std::size_t k = j * gridX + i;
                mesh.vertices[k] = Vec3{(float)((double)i * sx), (float)h[k], (float)(0.0 - (double)j * sy)};
                // Normal of the height field Y = f(X, Z) with X = i * sx, Z = -j * sy: (-dY/dX, 1, -dY/dZ), from central
                // differences (one-sided at the edges).
                const std::size_t i0 = i > 0 ? i - 1 : i, i1 = i + 1 < gridX ? i + 1 : i;
                const std::size_t j0 = j > 0 ? j - 1 : j, j1 = j + 1 < gridY ? j + 1 : j;
                const double dYdX = (h[j * gridX + i1] - h[j * gridX + i0]) / ((double)(i1 - i0) * sx);
                const double dYdj = (h[j1 * gridX + i] - h[j0 * gridX + i]) / (double)(j1 - j0);
                const double dYdZ = -dYdj / sy;
                const double nx = -dYdX, ny = 1.0, nz = -dYdZ;
                const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
                mesh.normals[k] = Vec3{(float)(nx / len), (float)(ny / len), (float)(nz / len)};
                mesh.uvs[k] = Vec2{(float)((double)i / (double)(gridX - 1)), (float)((double)j / (double)(gridY - 1))};
            }
        }
        // Two triangles per grid cell, wound so their faces point up (+Y).
        mesh.triangles.reserve((gridX - 1) * (gridY - 1) * 6);
        for (std::size_t j = 0; j + 1 < gridY; ++j) {
            for (std::size_t i = 0; i + 1 < gridX; ++i) {
                const int a = (int)(j * gridX + i), bq = a + 1, d = (int)((j + 1) * gridX + i), c = d + 1;
                mesh.triangles.push_back(a); mesh.triangles.push_back(bq); mesh.triangles.push_back(c);
                mesh.triangles.push_back(a); mesh.triangles.push_back(c);  mesh.triangles.push_back(d);
            }
        }

        ImportedNode root;
        root.name = "Terragen";
        out.nodes.push_back(root);
        ImportedPart part;
        part.mesh = std::move(mesh);
        part.node = 0;
        out.parts.push_back(std::move(part));
        out.backend = "own";
        out.notes = std::move(notes);
        return true;
    }
};

} // namespace ter
} // namespace

bool ReadTerragenBuffer(const std::string& bytes, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    ter::Reader r;
    if (!r.Run(bytes, out)) {
        const std::string m = r.err.empty() ? "Terragen: could not read file" : r.err;
        out = ImportedModel{};
        if (err) *err = m;
        return false;
    }
    return true;
}

bool ReadTerragenFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, ter::kMaxFileBytes, bytes, err)) return false;
    return ReadTerragenBuffer(bytes, out, err);
}

} // namespace okay
