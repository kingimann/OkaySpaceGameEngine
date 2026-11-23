// okay::fbx layer 2: element tree -> Scene (nodes, meshes, skins, blend shapes,
// materials, baked animation). See FbxReader.hpp and docs/content/model_readers.md.
// Written from the public description of the FBX object model; no third-party code.
#include "okay/Core/SafeCast.hpp"
#include "okay/Core/Base64.hpp"
#include "okay/Render/FbxReader.hpp"

#include <algorithm>
#include <array>
#include <iterator>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include "FbxSceneInternal.hpp"

namespace okay {
namespace fbx {
using namespace scene_build;

namespace {
// ---------------------------------------------------------------------------
// the import step: polygons -> parts (BuildParts)
// ---------------------------------------------------------------------------
Layer LayerOf(const MeshLayer& m) {
    Layer L; L.dim = m.dim;
    switch (m.mapping) {
        case LayerMapping::ByPolygonVertex: L.map = kByPolyVert; break;
        case LayerMapping::ByControlPoint: L.map = kByVertex; break;
        case LayerMapping::ByPolygon: L.map = kByPoly; break;
        case LayerMapping::AllSame: L.map = kAllSame; break;
        case LayerMapping::ByEdge: L.map = kByEdge; break;
        default: return L;
    }
    L.vals = m.values.present ? &m.values.a : nullptr;
    L.indexed = m.indexed && m.indices.present;
    L.idx = L.indexed ? &m.indices.a : nullptr;
    return L;
}

bool PartsOf(Mesh& m, const Limits& lim, std::string& err) {
    const Polygons& P = m.polygons;
    if (P.empty()) return true;   // an empty mesh: nothing to emit
    const Array* verts = &P.positions.a;
    const Array* pvi = &P.polygonVertexIndex.a;
    const double scale = P.positionScale;
    const std::size_t ncp = verts->size() / 3;
    // Positions (unit-scaled) and polygon corners are read straight from the kept arrays where they are used: no
    // per-mesh copies (on big meshes the fresh memory for copies cost more time than the copying itself).
    auto pos = [&](std::size_t cp, int k) -> double {
        const std::size_t at = cp * 3 + (std::size_t)k;
        if (verts->type == 'd') { double v; std::memcpy(&v, verts->data + at * 8, 8); return v * scale; }
        return (*verts)[at] * scale;
    };
    const std::size_t npv = pvi->size();
    if (!PolygonIndicesInRange(*pvi)) { err = "polygon index out of range"; return false; }
    auto cornerAt = [&](std::size_t i, bool& end) -> std::int32_t { return PolyCorner(*pvi, i, ncp, end); };
    // Walks the polygons in order: fn(polygon number, first corner, corner count, all corners in range).
    auto forEachPolygon = [&](auto&& fn) -> bool {
        std::size_t start = 0, fi = 0; bool good = true;
        for (std::size_t i = 0; i < npv; ++i) {
            bool end; const std::int32_t c = cornerAt(i, end);
            if (c < 0) good = false;
            if (end) { if (!fn(fi++, start, i + 1 - start, good)) return false; start = i + 1; good = true; }
        }
        if (start < npv) return fn(fi, start, npv - start, good);   // unterminated last polygon
        return true;
    };
    auto cpOf = [&](std::size_t i) { bool e; return cornerAt(i, e); };
    const Layer ln = LayerOf(P.normals), lu = LayerOf(P.uvs), lc = LayerOf(P.colors), lm = LayerOf(P.materials);
    // Extra UV sets: `xw` floats per corner (2 per set), kept next to the corners; with none the code below is the
    // one-set code it was before.
    std::vector<Layer> lx;
    for (const MeshLayer& ml : P.extraUVs) lx.push_back(LayerOf(ml));
    const std::size_t xw = lx.size() * 2;
    bool anyByEdge = false;
    for (const Layer& L : lx) if (L.map == kByEdge) anyByEdge = true;
    std::vector<int> edgeOfCorner;   // only a ByEdge layer reads it
    if (ln.map == kByEdge || lu.map == kByEdge || lc.map == kByEdge || lm.map == kByEdge || anyByEdge)
        if (P.edges.present) {
            const Array* ed = &P.edges.a;
            edgeOfCorner.assign(npv, -1);
            for (std::size_t e = 0; e < ed->size(); ++e) { const double v = (*ed)[e]; if (v >= 0 && v < (double)npv) edgeOfCorner[(std::size_t)v] = (int)e; }
        }
    auto slotOfPolygon = [&](std::size_t fi, std::size_t start) {
        int slot = 0;
        double mv;
        if (lm.Ok() && Fetch(lm, start, fi, (std::size_t)cpOf(start), edgeOfCorner, &mv) && mv >= 0 && mv < 1e6) slot = (int)mv;
        return slot;
    };

    // First pass: how many corners each material slot will get, so each part's index list is allocated once at its
    // final size. A polygon of n corners gives n - 2 triangles (Triangulate); polygons with fewer than 3 corners
    // or a corner out of range are skipped (in both passes).
    std::map<int, std::size_t> cornersOfSlot;
    forEachPolygon([&](std::size_t fi, std::size_t start, std::size_t count, bool good) {
        if (count >= 3 && good) cornersOfSlot[slotOfPolygon(fi, start)] += (count - 2) * 3;
        return true;
    });
    std::size_t total = 0;
    for (const auto& kv : cornersOfSlot) total += kv.second;
    if (total > lim.maxCorners) { err = "mesh too large"; return false; }   // (the reader already refused such a file)
    // Each part keeps its distinct corners and an index list. Equal corners (byte for byte) are found with an
    // open-addressing table of vertex numbers (power-of-two size, at most two thirds full, linear probing; the size
    // changes only the work, never which vertex a corner finds).
    struct PartBuild { Part part; std::vector<std::uint32_t> table; std::uint32_t no = 0xFFFFFFFFu; std::vector<float> xs; };   // no: index in partList; xs: xw floats per vertex
    const std::uint32_t kEmpty = 0xFFFFFFFFu;
    std::map<int, PartBuild> parts;
    for (const auto& kv : cornersOfSlot) {
        PartBuild& pb = parts[kv.first];
        pb.part.slot = kv.first;
        pb.part.indices.reserve(kv.second);
        const std::size_t guess = std::min(kv.second, ncp + 16);   // distinct corners: usually about the vertex count
        pb.part.vertices.reserve(guess);
        if (xw) pb.xs.reserve(guess * xw);
        std::size_t cap = 64;
        while (cap * 2 < guess * 3) cap <<= 1;
        pb.table.assign(cap, kEmpty);
    }
    auto hashCorner = [](const Corner& c) {   // six 64-bit words and the last 32 bits, each times its own odd constant
        std::uint64_t w[6]; std::uint32_t last;
        std::memcpy(w, &c, sizeof(w)); std::memcpy(&last, (const unsigned char*)&c + sizeof(w), 4);
        std::uint64_t h = w[0] * 0x9E3779B97F4A7C15ull + w[1] * 0xC2B2AE3D27D4EB4Full + w[2] * 0x165667B19E3779F9ull +
                          w[3] * 0xD6E8FEB86659FD93ull + w[4] * 0xFF51AFD7ED558CCDull + w[5] * 0xC4CEB9FE1A85EC53ull +
                          (std::uint64_t)last * 0x9FB21C651E98DF25ull;
        h ^= h >> 32; h *= 0xD6E8FEB86659FD93ull; h ^= h >> 29;
        return h;
    };
    // A corner's hash with its extra UV values (x: xw floats) mixed in; without extra sets it is hashCorner alone.
    auto hashFull = [&](const Corner& c, const float* x) {
        std::uint64_t h = hashCorner(c);
        for (std::size_t k = 0; k < xw; ++k) {
            std::uint32_t b; std::memcpy(&b, x + k, 4);
            h = (h ^ b) * 0x9E3779B97F4A7C15ull; h ^= h >> 31;
        }
        return h;
    };
    auto addCorner = [&](PartBuild& pb, const Corner& c, const float* x) {
        std::vector<Corner>& vs = pb.part.vertices;
        if ((vs.size() + 1) * 3 > pb.table.size() * 2) {   // keep the table at most 2/3 full: double it and re-insert
            pb.table.assign(pb.table.size() * 2, kEmpty);
            const std::size_t mask = pb.table.size() - 1;
            for (std::size_t v = 0; v < vs.size(); ++v) {
                std::size_t at = (std::size_t)(xw ? hashFull(vs[v], pb.xs.data() + v * xw) : hashCorner(vs[v])) & mask;
                while (pb.table[at] != kEmpty) at = (at + 1) & mask;
                pb.table[at] = (std::uint32_t)v;
            }
        }
        const std::size_t mask = pb.table.size() - 1;
        std::size_t at = (std::size_t)(xw ? hashFull(c, x) : hashCorner(c)) & mask;
        for (;;) {
            const std::uint32_t u = pb.table[at];
            if (u == kEmpty) {
                pb.table[at] = (std::uint32_t)vs.size(); pb.part.indices.push_back((std::uint32_t)vs.size()); vs.push_back(c);
                if (xw) pb.xs.insert(pb.xs.end(), x, x + xw);
                return;
            }
            if (std::memcmp(&vs[u], &c, sizeof(Corner)) == 0 &&
                (!xw || std::memcmp(pb.xs.data() + (std::size_t)u * xw, x, xw * sizeof(float)) == 0)) { pb.part.indices.push_back(u); return; }
            at = (at + 1) & mask;
        }
    };
    std::vector<std::array<double, 3>> pts;
    std::vector<int> tris;
    // Corners are made in batches and then looked up in the tables: measured about a third faster on big meshes
    // than looking each one up as it is made (the lookups then do not compete with the reading of the layer arrays).
    const std::size_t kBatch = 2048;
    struct Pending { Corner c; std::uint32_t part; };   // 56 bytes (a pointer next to the corner would make it 64)
    std::vector<Pending> pending;
    std::vector<float> pendingX;        // xw floats per entry of `pending` (extra UV sets only)
    std::vector<PartBuild*> partList;   // `part` above indexes this
    pending.reserve(std::min(total, kBatch));   // flushed as soon as it reaches kBatch
    if (xw) pendingX.reserve(std::min(total, kBatch) * xw);
    auto flush = [&]() {
        for (std::size_t i = 0; i < pending.size(); ++i) addCorner(*partList[pending[i].part], pending[i].c, xw ? pendingX.data() + i * xw : nullptr);
        pending.clear(); pendingX.clear();
    };
    forEachPolygon([&](std::size_t fi, std::size_t start, std::size_t count, bool good) {
        if (count < 3 || !good) return true;
        const int slot = slotOfPolygon(fi, start);
        tris.clear();
        double fn[3] = {0, 0, 0};   // polygon (Newell) normal, used when the file has none
        if (count == 3 && ln.Ok()) { tris.push_back(0); tris.push_back(1); tris.push_back(2); }   // what Triangulate gives
        else {
            pts.clear();
            for (std::size_t k = 0; k < count; ++k) {
                const std::size_t c = (std::size_t)cpOf(start + k);
                pts.push_back({pos(c, 0), pos(c, 1), pos(c, 2)});
            }
            if (!ln.Ok()) for (std::size_t k = 0; k < pts.size(); ++k) {
                const auto& pa = pts[k]; const auto& pb = pts[k + 1 == pts.size() ? 0 : k + 1];
                fn[0] += (pa[1] - pb[1]) * (pa[2] + pb[2]); fn[1] += (pa[2] - pb[2]) * (pa[0] + pb[0]); fn[2] += (pa[0] - pb[0]) * (pa[1] + pb[1]);
            }
            Triangulate(pts, tris, false);
        }
        PartBuild& part = parts[slot]; part.part.slot = slot;
        if (part.no == 0xFFFFFFFFu) { part.no = (std::uint32_t)partList.size(); partList.push_back(&part); }
        const std::uint32_t partNo = part.no;
        for (int t : tris) {
            const std::size_t k = (std::size_t)t, pv = start + k;
            const std::size_t cp = (std::size_t)cpOf(pv);
            Corner c;
            for (int a = 0; a < 3; ++a) c.p[a] = (float)pos(cp, a);
            c.cp = (std::uint32_t)cp;
            double nn[3] = {0, 0, 0};
            if (ln.Ok()) { if (!Fetch(ln, pv, fi, cp, edgeOfCorner, nn)) { nn[0] = nn[1] = nn[2] = 0; } }
            else { nn[0] = fn[0]; nn[1] = fn[1]; nn[2] = fn[2]; }
            const double l = std::sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]);
            if (l > 1e-12) for (int a = 0; a < 3; ++a) c.n[a] = (float)(nn[a] / l);
            double uv[2];
            if (lu.Ok() && Fetch(lu, pv, fi, cp, edgeOfCorner, uv)) { c.uv[0] = (float)uv[0]; c.uv[1] = (float)uv[1]; }
            double col[4];
            if (lc.Ok() && Fetch(lc, pv, fi, cp, edgeOfCorner, col)) for (int a = 0; a < 4; ++a) c.color[a] = (float)col[a];
            pending.push_back({c, partNo});
            for (const Layer& L : lx) {   // extra UV sets: (0, 0) where a set does not cover the corner, as for set 0
                double xu[2];
                if (Fetch(L, pv, fi, cp, edgeOfCorner, xu)) { pendingX.push_back((float)xu[0]); pendingX.push_back((float)xu[1]); }
                else { pendingX.push_back(0.0f); pendingX.push_back(0.0f); }
            }
            if (pending.size() >= kBatch) flush();
        }
        return true;
    });
    flush();   // (in the order they were made: the numbering is the same)
    for (auto& kv : parts) {
        PartBuild& pb = kv.second;
        if (pb.part.indices.empty()) continue;
        if (xw) {   // split the interleaved values into one array per set
            const std::size_t nv = pb.part.vertices.size();
            pb.part.extraUVs.assign(lx.size(), std::vector<float>());
            for (std::size_t k = 0; k < lx.size(); ++k) {
                std::vector<float>& a = pb.part.extraUVs[k];
                a.resize(nv * 2);
                for (std::size_t v = 0; v < nv; ++v) { a[v * 2] = pb.xs[v * xw + k * 2]; a[v * 2 + 1] = pb.xs[v * xw + k * 2 + 1]; }
            }
        }
        m.parts.push_back(std::move(pb.part));
    }
    return true;
}

} // namespace

namespace {
bool BuildFrom(const Document& doc, Document* steal, Scene& out, std::string* err, const BuildOptions& opt) {
    out = Scene();
    Builder b(doc, out, opt);
    b.steal = steal;
    const bool ok = b.Run();
    if (!ok) { out = Scene(); if (err) *err = b.err.empty() ? "FBX build failed" : b.err; }
    return ok;
}
} // namespace

bool Build(const Document& doc, Scene& out, std::string* err, const BuildOptions& opt) {
    return BuildFrom(doc, nullptr, out, err, opt);
}

bool BuildParts(Mesh& m, std::string* err, const Limits& lim, bool releasePolygons) {
    if (m.parts.empty()) {
        std::string e;
        if (!PartsOf(m, lim, e)) { m.parts.clear(); if (err) *err = e; return false; }
    }
    if (releasePolygons) m.polygons = Polygons();
    return true;
}


bool BuildParts(Scene& s, std::string* err, const Limits& lim, bool releasePolygons) {
    for (Mesh& m : s.meshes) if (!BuildParts(m, err, lim, releasePolygons)) return false;
    return true;
}

bool LoadMemory(const std::uint8_t* data, std::size_t size, Scene& out, std::string* err, const BuildOptions& opt) {
    Document doc;
    if (!Parse(data, size, doc, err, opt.limits)) { out = Scene(); return false; }
    // The mesh arrays the Document decoded (zlib, ASCII) are handed to the scene instead of copied.
    return BuildFrom(doc, &doc, out, err, opt);
}

bool LoadFile(const std::string& path, Scene& out, std::string* err, const BuildOptions& opt) {
    {   // a folder opens as a stream on some systems and reports a huge size: the resize below threw std::bad_alloc
        std::error_code ec;
        if (!std::filesystem::is_regular_file(std::filesystem::u8path(path), ec)) { if (err) *err = "not a file: " + path; return false; }
    }
    std::ifstream f(std::filesystem::u8path(path), std::ios::binary);
    if (!f) { if (err) *err = "cannot open " + path; return false; }
    // One read of the whole file at its size (a byte-by-byte stream iterator grew the buffer by doubling, slowly), into
    // a buffer that is not zero-filled first (the read writes every byte that is kept).
    ByteBuffer buf;
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    f.seekg(0, std::ios::beg);
    if (size > 0) {
        buf.resize((std::size_t)size);
        f.read((char*)buf.data(), (std::streamsize)size);
        buf.resize((std::size_t)f.gcount());
    } else {   // size unknown (not a regular file): fall back to reading until the end
        f.clear(); f.seekg(0, std::ios::beg);
        buf.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    return LoadMemory(buf.data(), buf.size(), out, err, opt);
}

bool FileKeysDefault() {
    static const bool v = [] { const char* e = std::getenv("OKAY_FBX_BAKE30"); return !(e && e[0] == '1'); }();
    return v;
}

} // namespace fbx
} // namespace okay
