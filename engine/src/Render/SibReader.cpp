// Our own SIB reader: Nevercenter Silo models (.sib). There is no public description of the format. Written ONLY from our own
// byte-by-byte inspection of the one sample file we have (heffalump.sib by Richard Mitton, released into the public domain; its
// readme says so; from the test/models/SIB data folder of Assimp's repository, fetched as data with a sparse checkout that had
// no code in it) and of the structure that inspection shows: the file is a list of chunks, each a four letter tag, a 32 bit
// little endian size and that many bytes. No importer source of any project was read: every field below was worked out from
// the bytes of that file and checked against it (docs/content/model_readers.md, section SIB, says how, and which fields are
// assumptions). Read: the materials (MATR); the shapes (SHAP): positions (VRTS), n-gon faces (FACS), the texture coordinates
// of every face corner (FTVS), the material of each face (FAMA) and the name (SNAM); the instances (INST: a copy of a shape
// placed by its own axes (AXIS) and scaled by its SMTX). Not read: lights, groups, texture previews (TEXP), the mirrored half
// of a shape with a mirror plane, edges. Bounds-checked, every count is tested against the bytes that are there, per-file
// budgets, no recursion.
#include "PolygonTriangulate.hpp"
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
namespace sib {

constexpr std::size_t kMaxFileBytes = 256u * 1024u * 1024u;
constexpr std::size_t kMaxChunks = 2000000, kMaxShapes = 100000, kMaxMaterials = 100000;
constexpr std::uint64_t kMaxFaceCorners = 16000000;
constexpr std::size_t kMaxOutVerts = 4000000, kMaxBuiltTris = 4000000;
constexpr std::size_t kMaxEarCorners = 32;
constexpr std::uint64_t kMaxEarWork = 400000000ull;

bool Fail(std::string* err, const std::string& m) { if (err) *err = "SIB: " + m; return false; }

struct Chunk { char tag[4]; std::size_t off, len; };

std::uint32_t U32(const std::string& b, std::size_t o) {
    return (std::uint32_t)(unsigned char)b[o] | ((std::uint32_t)(unsigned char)b[o + 1] << 8) | ((std::uint32_t)(unsigned char)b[o + 2] << 16) | ((std::uint32_t)(unsigned char)b[o + 3] << 24);
}
float F32(const std::string& b, std::size_t o) { const std::uint32_t u = U32(b, o); float f; std::memcpy(&f, &u, 4); return f; }

// The chunks of [off, end): tag, size, data. False when one runs past `end` or there are too many.
bool Split(const std::string& b, std::size_t off, std::size_t end, std::vector<Chunk>& out, std::string& why) {
    out.clear();
    while (off < end) {
        if (end - off < 8) { why = "a chunk header is cut off"; return false; }
        Chunk c;
        std::memcpy(c.tag, b.data() + off, 4);
        const std::uint32_t n = U32(b, off + 4);
        off += 8;
        if (n > end - off) { why = std::string("chunk ") + std::string(c.tag, 4) + " is longer than the file"; return false; }
        c.off = off; c.len = n;
        off += n;
        if (out.size() >= kMaxChunks) { why = "too many chunks"; return false; }
        out.push_back(c);
    }
    return true;
}
bool Is(const Chunk& c, const char* t) { return std::memcmp(c.tag, t, 4) == 0; }

// UTF-16 little endian text of `len` bytes to UTF-8 (surrogate pairs combined; a lone surrogate or an odd length gives "?").
std::string Utf8(const std::string& b, std::size_t off, std::size_t len) {
    std::string s;
    std::vector<std::uint32_t> u;
    for (std::size_t i = 0; i + 1 < len; i += 2) u.push_back((std::uint32_t)(unsigned char)b[off + i] | ((std::uint32_t)(unsigned char)b[off + i + 1] << 8));
    for (std::size_t i = 0; i < u.size(); ++i) {
        std::uint32_t c = u[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < u.size() && u[i + 1] >= 0xDC00 && u[i + 1] < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (u[i + 1] - 0xDC00); ++i; }
        else if (c >= 0xD800 && c < 0xE000) c = '?';
        if (c == 0) break;
        if (c < 0x80) s += (char)c;
        else if (c < 0x800) { s += (char)(0xC0 | (c >> 6)); s += (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { s += (char)(0xE0 | (c >> 12)); s += (char)(0x80 | ((c >> 6) & 63)); s += (char)(0x80 | (c & 63)); }
        else { s += (char)(0xF0 | (c >> 18)); s += (char)(0x80 | ((c >> 12) & 63)); s += (char)(0x80 | ((c >> 6) & 63)); s += (char)(0x80 | (c & 63)); }
    }
    return s;
}

// A string in a MATR chunk: a 32 bit byte count, then that many bytes of UTF-16.
bool Str(const std::string& b, std::size_t& p, std::size_t end, std::string& out) {
    if (end - p < 4) return false;
    const std::uint32_t n = U32(b, p);
    p += 4;
    if (n > end - p || (n & 1)) return false;
    out = Utf8(b, p, n);
    p += n;
    return true;
}

} // namespace sib
} // namespace

static bool ReadSibImpl(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err) {
    using namespace sib;
    out = ImportedModel{};
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "HEAD", 4) != 0 || U32(bytes, 4) != 4) return Fail(err, "not a Silo file (the HEAD chunk is missing)");
    const std::uint32_t version = U32(bytes, 8);
    if (version != 1) return Fail(err, "unsupported file version " + std::to_string(version) + " (the one sample we have is version 1)");
    std::vector<Chunk> top;
    std::string why;
    if (!Split(bytes, 12, bytes.size(), top, why)) return Fail(err, why);

    // ---- materials ----
    std::size_t badMat = 0;
    for (const Chunk& c : top) {
        if (!Is(c, "MATR")) continue;
        if (out.materials.size() >= kMaxMaterials) return Fail(err, "more than " + std::to_string(kMaxMaterials) + " materials");
        ImportedMaterial m;
        const std::size_t end = c.off + c.len;
        std::size_t p = c.off;
        bool good = c.len >= 64 + 4;
        if (good) {
            // 16 floats (four colours of four), one 32 bit flags word, the name, the texture file name (empty = none)
            float col[4];
            for (int i = 0; i < 4; ++i) col[i] = F32(bytes, p + (std::size_t)i * 4);
            for (float& f : col) if (!std::isfinite(f) || f < -1e6f || f > 1e6f) f = 0.0f;
            m.hasDiffuse = true;
            m.diffuse = Color{col[0], col[1], col[2], 1.0f};
            p += 64 + 4;
            std::string tex;
            good = Str(bytes, p, end, m.name) && Str(bytes, p, end, tex);
            if (good && !tex.empty()) {
                std::string resolved, whyNot;
                if (detail::ResolveInsideDir(baseDir, tex, resolved, whyNot)) m.texture = resolved;
                else out.notes.push_back("material \"" + m.name + "\": texture \"" + tex + "\" not used: " + whyNot);
            }
        }
        if (!good) { ++badMat; m = ImportedMaterial{}; m.name = "material " + std::to_string(out.materials.size()); }
        out.materials.push_back(m);
    }

    // ---- shapes ----
    ImportedNode root;
    root.name = "SIB";
    out.nodes.push_back(root);
    std::vector<std::vector<std::size_t>> shapeParts;   // shape number -> its parts in out.parts
    std::vector<std::array<float, 12>> shapeAxes;       // shape number -> its AXIS (origin, then the three axis vectors); all zero = none
    std::size_t nShapes = 0, badShapes = 0, badFaces = 0, shortFaces = 0, badUv = 0, mirrored = 0, noUv = 0, badRuns = 0;
    std::uint64_t corners = 0, earWork = 0, tris = 0, fanned = 0;
    std::size_t outVerts = 0;
    for (const Chunk& sc : top) {
        if (!Is(sc, "SHAP")) continue;
        if (++nShapes > kMaxShapes) return Fail(err, "more than " + std::to_string(kMaxShapes) + " shapes");
        shapeParts.emplace_back();
        shapeAxes.push_back(std::array<float, 12>{});
        std::vector<Chunk> sub;
        if (!Split(bytes, sc.off, sc.off + sc.len, sub, why)) { ++badShapes; continue; }
        const Chunk *vrts = nullptr, *facs = nullptr, *ftvs = nullptr, *fama = nullptr, *snam = nullptr, *imrp = nullptr;
        for (const Chunk& c : sub)
            if (Is(c, "AXIS") && c.len == 48) for (int k = 0; k < 12; ++k) { const float f = F32(bytes, c.off + (std::size_t)k * 4); shapeAxes.back()[(std::size_t)k] = std::isfinite(f) && std::fabs(f) < 1e8f ? f : 0.0f; }
        for (const Chunk& c : sub) {
            if (Is(c, "VRTS")) vrts = &c; else if (Is(c, "FACS")) facs = &c; else if (Is(c, "FTVS")) ftvs = &c;
            else if (Is(c, "FAMA")) fama = &c; else if (Is(c, "SNAM")) snam = &c; else if (Is(c, "IMRP")) imrp = &c;
        }
        if (!vrts || !facs || vrts->len % 12 != 0 || facs->len % 4 != 0) { ++badShapes; continue; }
        const std::size_t nv = vrts->len / 12;
        if (nv > kMaxOutVerts) return Fail(err, "a shape has more than " + std::to_string(kMaxOutVerts) + " vertices (the budget)");
        std::vector<Vec3> pos(nv);
        for (std::size_t i = 0; i < nv; ++i) {
            float v[3];
            for (int k = 0; k < 3; ++k) { v[k] = F32(bytes, vrts->off + i * 12 + (std::size_t)k * 4); if (!std::isfinite(v[k]) || std::fabs(v[k]) > 1e15f) v[k] = 0.0f; }
            pos[i] = Vec3{v[0], v[1], v[2]};
        }
        if (imrp && imrp->len >= 8 && U32(bytes, imrp->off + 4) != 0) ++mirrored;
        // faces: a count and that many vertex numbers
        struct Face { std::size_t at; std::uint32_t n; };
        std::vector<Face> faces;
        {
            std::size_t p = facs->off;
            const std::size_t end = facs->off + facs->len;
            while (p < end) {
                const std::uint32_t n = U32(bytes, p);
                p += 4;
                if (n > (end - p) / 4) { ++badShapes; faces.clear(); break; }
                corners += n;
                if (corners > kMaxFaceCorners) return Fail(err, "more than " + std::to_string(kMaxFaceCorners) + " face corners (the budget)");
                faces.push_back({p, n});
                p += (std::size_t)n * 4;
            }
        }
        // texture coordinates: per face an index word, a corner count, then that many (u, v) pairs; same order as the faces
        std::vector<std::size_t> uvAt(faces.size(), 0);
        std::vector<std::uint32_t> uvN(faces.size(), 0);
        bool haveUv = false;
        if (ftvs) {
            std::size_t p = ftvs->off;
            const std::size_t end = ftvs->off + ftvs->len;
            std::size_t f = 0;
            haveUv = true;
            while (p < end && f < faces.size()) {
                if (end - p < 8) { haveUv = false; break; }
                const std::uint32_t n = U32(bytes, p + 4);
                p += 8;
                if (n > (end - p) / 8) { haveUv = false; break; }
                uvAt[f] = p; uvN[f] = n;
                p += (std::size_t)n * 8;
                ++f;
            }
            if (f != faces.size() || p != end) haveUv = haveUv && f == faces.size();   // tolerate trailing bytes, not a missing face
        }
        if (!haveUv) ++noUv;
        // material runs: (first face, material) pairs
        std::vector<std::pair<std::uint32_t, std::int32_t>> runs;
        if (fama && fama->len % 8 == 0)
            for (std::size_t p = fama->off; p < fama->off + fama->len; p += 8) runs.push_back({U32(bytes, p), (std::int32_t)U32(bytes, p + 4)});
        else if (fama) ++badRuns;
        std::stable_sort(runs.begin(), runs.end(), [](const std::pair<std::uint32_t, std::int32_t>& a, const std::pair<std::uint32_t, std::int32_t>& b) { return a.first < b.first; });
        auto materialOf = [&](std::size_t face) {
            std::int32_t m = -1;
            for (const auto& r : runs) { if (r.first <= face) m = r.second; else break; }
            return (m >= 0 && (std::size_t)m < out.materials.size()) ? m : -1;
        };

        // one part per material
        std::vector<int> order;   // distinct materials in first-use order
        std::vector<detail::rc::Soup> soups;
        std::vector<std::unordered_map<std::uint32_t, std::uint32_t>> remaps;   // shape vertex -> position in that soup (only the used ones are copied)
        auto soupFor = [&](int m) -> std::size_t {
            for (std::size_t i = 0; i < order.size(); ++i) if (order[i] == m) return i;
            order.push_back(m);
            soups.emplace_back();
            remaps.emplace_back();
            return order.size() - 1;
        };
        std::vector<Vec3> poly;
        std::vector<std::array<int, 3>> tr;
        for (std::size_t fi = 0; fi < faces.size(); ++fi) {
            const Face& f = faces[fi];
            if (f.n < 3) { ++shortFaces; continue; }
            poly.clear();
            bool ok = true;
            std::vector<std::uint32_t> idx(f.n);
            for (std::uint32_t k = 0; k < f.n; ++k) {
                const std::uint32_t v = U32(bytes, f.at + (std::size_t)k * 4);
                if (v >= nv) { ok = false; break; }
                idx[k] = v; poly.push_back(pos[v]);
            }
            if (!ok) { ++badFaces; continue; }
            const bool uvOk = haveUv && uvN[fi] == f.n;
            const std::uint64_t cost = (std::uint64_t)f.n * f.n * f.n;
            if (f.n <= kMaxEarCorners && earWork + cost <= kMaxEarWork) { earWork += cost; detail::TriangulatePolygon(poly, tr, kMaxEarCorners); }
            else { ++fanned; tr.clear(); for (std::uint32_t k = 1; k + 1 < f.n; ++k) tr.push_back({0, (int)k, (int)k + 1}); }
            tris += tr.size();
            if (tris > kMaxBuiltTris) return Fail(err, "more than " + std::to_string(kMaxBuiltTris) + " triangles (the budget)");
            const std::size_t si = soupFor(materialOf(fi));
            detail::rc::Soup& s = soups[si];
            for (const auto& t : tr) for (int c : t) {
                auto it = remaps[si].find(idx[(std::size_t)c]);
                if (it == remaps[si].end()) { it = remaps[si].emplace(idx[(std::size_t)c], (std::uint32_t)s.pos.size()).first; s.pos.push_back(pos[idx[(std::size_t)c]]); }
                s.ci.push_back(it->second);
                if (uvOk) {
                    const float u = F32(bytes, uvAt[fi] + (std::size_t)c * 8), v = F32(bytes, uvAt[fi] + (std::size_t)c * 8 + 4);
                    if (std::isfinite(u) && std::isfinite(v) && std::fabs(u) < 1e8f && std::fabs(v) < 1e8f) s.cuv.push_back(Vec2{u, 1.0f - v});   // v up in the file: flipped like the other bottom-up formats
                    else { s.cuv.push_back(Vec2{0, 0}); ++badUv; }
                } else s.cuv.push_back(Vec2{0, 0});
            }
        }
        std::string name = snam ? Utf8(bytes, snam->off, snam->len) : std::string();
        if (name.empty()) name = "shape " + std::to_string(nShapes - 1);
        ImportedNode node;
        node.name = name;
        node.parent = 0;
        out.nodes.push_back(node);
        const int nodeIdx = (int)out.nodes.size() - 1;
        for (std::size_t i = 0; i < soups.size(); ++i) {
            if (!haveUv) soups[i].cuv.clear();
            ImportedPart part;
            detail::rc::BuildStats bs;
            if (!detail::rc::BuildMesh(soups[i], 180.0, part.mesh, &bs, kMaxOutVerts - std::min(outVerts, kMaxOutVerts))) return Fail(err, "over the budget of " + std::to_string(kMaxOutVerts) + " vertices for one file");
            if (part.mesh.triangles.empty()) continue;
            outVerts += part.mesh.vertices.size();
            if (!detail::rc::PartInRange(part)) return Fail(err, "a vertex is out of range");
            part.mesh.name = name;
            part.node = nodeIdx;
            part.material = order[i];
            shapeParts.back().push_back(out.parts.size());
            out.parts.push_back(std::move(part));
        }
        if (out.nodes.size() > kMaxShapes + 1) return Fail(err, "too many nodes");
    }
    if (out.parts.empty()) return Fail(err, "the file has no usable shapes");

    // ---- instances: INSI names a shape (its number among the SHAP chunks); the shape's positions are taken into the shape's
    // own frame (AXIS: an origin, then the x, y and z axis vectors), scaled by the 3 x 3 part of SMTX, and put back in the
    // instance's frame (its AXIS). Worked out from the sample: the shape "Original" becomes an exact cube of +-0.2959 in its
    // frame, and the four instances then stand on the ground or on each other.
    std::size_t placed = 0, badInst = 0, smtxMove = 0;
    for (const Chunk& ic : top) {
        if (!Is(ic, "INST")) continue;
        std::vector<Chunk> sub;
        if (!Split(bytes, ic.off, ic.off + ic.len, sub, why)) { ++badInst; continue; }
        const Chunk *insi = nullptr, *axis = nullptr, *smtx = nullptr, *snam = nullptr;
        for (const Chunk& c : sub) { if (Is(c, "INSI")) insi = &c; else if (Is(c, "AXIS")) axis = &c; else if (Is(c, "SMTX")) smtx = &c; else if (Is(c, "SNAM")) snam = &c; }
        if (!insi || insi->len < 4 || !axis || axis->len != 48) { ++badInst; continue; }
        const std::uint32_t si = U32(bytes, insi->off);
        if (si >= shapeParts.size() || shapeParts[si].empty()) { ++badInst; continue; }
        double A[12], S[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        bool ok = true;
        for (int k = 0; k < 12; ++k) { const float f = F32(bytes, axis->off + (std::size_t)k * 4); if (!std::isfinite(f) || std::fabs(f) > 1e8f) ok = false; A[k] = f; }
        if (smtx && smtx->len == 64) {
            float m[16];
            for (int k = 0; k < 16; ++k) { m[k] = F32(bytes, smtx->off + (std::size_t)k * 4); if (!std::isfinite(m[k]) || std::fabs(m[k]) > 1e8f) ok = false; }
            for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c) S[r * 3 + c] = m[r * 4 + c];
            if (m[3] != 0 || m[7] != 0 || m[11] != 0 || m[12] != 0 || m[13] != 0 || m[14] != 0) ++smtxMove;
        }
        if (!ok) { ++badInst; continue; }
        const std::array<float, 12>& B = shapeAxes[si];
        // M = Ainst^T * S * Bshape (axis vectors are the rows), t = oinst - M * oshape
        double M[9] = {0};
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) {
                double v = 0;
                for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) v += A[3 + i * 3 + r] * S[i * 3 + j] * (double)B[(std::size_t)(3 + j * 3 + c)];
                M[r * 3 + c] = v;
            }
        const double det = M[0] * (M[4] * M[8] - M[5] * M[7]) - M[1] * (M[3] * M[8] - M[5] * M[6]) + M[2] * (M[3] * M[7] - M[4] * M[6]);
        if (!std::isfinite(det) || std::fabs(det) < 1e-12) { ++badInst; continue; }
        double t[3];
        for (int r = 0; r < 3; ++r) t[r] = A[r] - (M[r * 3] * B[0] + M[r * 3 + 1] * B[1] + M[r * 3 + 2] * B[2]);
        // normals: the inverse transpose (the cofactor matrix divided by the determinant)
        const double C[9] = {M[4] * M[8] - M[5] * M[7], M[5] * M[6] - M[3] * M[8], M[3] * M[7] - M[4] * M[6],
                             M[2] * M[7] - M[1] * M[8], M[0] * M[8] - M[2] * M[6], M[1] * M[6] - M[0] * M[7],
                             M[1] * M[5] - M[2] * M[4], M[2] * M[3] - M[0] * M[5], M[0] * M[4] - M[1] * M[3]};
        std::size_t addVerts = 0, addTris = 0;
        for (std::size_t pi : shapeParts[si]) { addVerts += out.parts[pi].mesh.vertices.size(); addTris += out.parts[pi].mesh.triangles.size() / 3; }
        tris += addTris;
        if (tris > kMaxBuiltTris) return Fail(err, "more than " + std::to_string(kMaxBuiltTris) + " triangles (the budget, with the instances)");
        if (outVerts + addVerts > kMaxOutVerts) return Fail(err, "over the budget of " + std::to_string(kMaxOutVerts) + " vertices for one file (with the instances)");
        outVerts += addVerts;
        if (out.nodes.size() > kMaxShapes * 2) return Fail(err, "too many nodes");
        ImportedNode node;
        node.name = snam ? Utf8(bytes, snam->off, snam->len) : std::string();
        if (node.name.empty()) node.name = "instance " + std::to_string(placed);
        node.parent = 0;
        out.nodes.push_back(node);
        const int nodeIdx = (int)out.nodes.size() - 1;
        const std::vector<std::size_t> src = shapeParts[si];
        for (std::size_t pi : src) {
            ImportedPart part = out.parts[pi];
            for (Vec3& v : part.mesh.vertices) {
                const double x = v.x, y = v.y, z = v.z;
                v = Vec3{(float)(M[0] * x + M[1] * y + M[2] * z + t[0]), (float)(M[3] * x + M[4] * y + M[5] * z + t[1]), (float)(M[6] * x + M[7] * y + M[8] * z + t[2])};
            }
            for (Vec3& n : part.mesh.normals) {
                const double x = n.x, y = n.y, z = n.z;
                double a = C[0] * x + C[1] * y + C[2] * z, b = C[3] * x + C[4] * y + C[5] * z, c = C[6] * x + C[7] * y + C[8] * z;
                if (det < 0) { a = -a; b = -b; c = -c; }
                const double l = std::sqrt(a * a + b * b + c * c);
                n = l > 1e-30 && std::isfinite(l) ? Vec3{(float)(a / l), (float)(b / l), (float)(c / l)} : Vec3{0, 1, 0};
            }
            if (det < 0) for (std::size_t k = 0; k + 2 < part.mesh.triangles.size(); k += 3) std::swap(part.mesh.triangles[k + 1], part.mesh.triangles[k + 2]);
            if (!detail::rc::PartInRange(part)) return Fail(err, "an instance vertex is out of range");
            part.node = nodeIdx;
            part.mesh.name = node.name;
            out.parts.push_back(std::move(part));
        }
        ++placed;
    }
    out.backend = "own";
    auto note = [&](const std::string& s) { out.notes.push_back("SIB: " + s); };
    std::size_t inst = 0, lights = 0, groups = 0, previews = 0;
    for (const Chunk& c : top) { if (Is(c, "INST")) ++inst; else if (Is(c, "LGHT")) ++lights; else if (Is(c, "GRPS")) ++groups; else if (Is(c, "TEXP")) ++previews; }
    if (placed) note(std::to_string(placed) + " instance(s) placed as copies of their shape (the instance frame was worked out from the one sample file: an assumption)");
    if (badInst) note(std::to_string(badInst) + " instance(s) with a missing or bad shape number, frame or scale skipped");
    if (smtxMove) note(std::to_string(smtxMove) + " instance scale matrix(es) also hold a move, which is not applied");
    (void)inst;
    if (lights) note(std::to_string(lights) + " light(s) not read");
    if (groups) note("groups (GRPS) not read");
    if (previews) note(std::to_string(previews) + " texture preview mesh(es) (TEXP) not read");
    if (mirrored) note(std::to_string(mirrored) + " shape(s) have a mirror plane switched on: the mirrored half is not generated");
    note("positions are read as world positions; the per-shape axes are not applied; only the diffuse colour (the first colour of a material) is used, and the file has no normals, so smooth normals were generated; texture v is flipped (an assumption)");
    if (badShapes) note(std::to_string(badShapes) + " shape(s) with missing or inconsistent chunks skipped");
    if (badMat) note(std::to_string(badMat) + " material(s) with a bad record replaced by an empty one");
    if (badFaces) note(std::to_string(badFaces) + " face(s) with an out-of-range vertex number skipped");
    if (shortFaces) note(std::to_string(shortFaces) + " face(s) with fewer than 3 corners skipped");
    if (badUv) note(std::to_string(badUv) + " corner(s) with a bad texture coordinate got (0, 0)");
    if (noUv) note(std::to_string(noUv) + " shape(s) with no usable texture coordinates");
    if (badRuns) note("a material table of a shape has a bad size and was ignored");
    if (fanned) note(std::to_string(fanned) + " polygon(s) with more than " + std::to_string(kMaxEarCorners) + " corners, or past the per-file ear clipping budget, were fanned from their first corner");
    // Y up (the sample stands on y = 0): positions are used as they are, no axis turn
    return true;
}

bool ReadSibBuffer(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    const bool ok = ReadSibImpl(bytes, baseDir, out, err);
    if (!ok) out = ImportedModel{};
    return ok;
}

bool ReadSibFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, sib::kMaxFileBytes, bytes, err)) return false;
    std::string dir;
    const std::size_t sl = path.find_last_of("/\\");
    if (sl != std::string::npos) dir = path.substr(0, sl + 1);
    return ReadSibBuffer(bytes, dir, out, err);
}

} // namespace okay
