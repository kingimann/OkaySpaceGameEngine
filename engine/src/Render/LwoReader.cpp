// Our own LightWave object reader (LWO2 and LWO1 / LWOB). Written from the public LWO2 / LWOB format description,
// no code from any other importer. The planar / cylindrical / spherical UV projection (`ProjectUvs`) was rewritten from
// the geometry of those projections, not from any other importer's code. See docs/own_lwo_reader.md.
#include "FileOpen.hpp"
#include "PolygonTriangulate.hpp"
#include "ReaderCommon.hpp"
#include "okay/Render/LwoReader.hpp"
#include <map>
#include <set>
#include <unordered_map>

namespace okay {

namespace {
namespace lwo {

using namespace okay::detail;

constexpr std::size_t kMaxFileBytes = 512u * 1024u * 1024u;
constexpr std::size_t kMaxChunks    = 5000000;
constexpr std::size_t kMaxLayers    = 10000;
constexpr std::size_t kMaxSurfaces  = 100000;
constexpr std::size_t kMaxPoints    = 20000000;
constexpr std::size_t kMaxPolys     = 20000000;
constexpr std::size_t kMaxTris      = 20000000;
constexpr std::size_t kMaxParts     = 200000;
constexpr std::size_t kMaxUvs       = 40000000;
constexpr std::size_t kMaxNameBytes = 4096;

struct Cur {   // a bounds-checked big-endian cursor over [p, e)
    const unsigned char* p; const unsigned char* e; bool bad = false;
    std::size_t Left() const { return (std::size_t)(e - p); }
    bool Need(std::size_t n) { if (Left() < n) { bad = true; return false; } return true; }
    unsigned U8() { if (!Need(1)) return 0; return *p++; }
    unsigned U16() { if (!Need(2)) return 0; const unsigned v = (unsigned)p[0] << 8 | p[1]; p += 2; return v; }
    std::uint32_t U32() { if (!Need(4)) return 0; const std::uint32_t v = (std::uint32_t)p[0] << 24 | (std::uint32_t)p[1] << 16 | (std::uint32_t)p[2] << 8 | p[3]; p += 4; return v; }
    int I16() { const unsigned v = U16(); return v >= 0x8000 ? (int)v - 0x10000 : (int)v; }
    float F32() { const std::uint32_t v = U32(); float f; std::memcpy(&f, &v, 4); return f; }
    std::uint32_t VX() { if (!Need(1)) return 0; if (*p == 0xFF) { const std::uint32_t v = U32(); return v & 0x00FFFFFFu; } return U16(); }
    std::string S0() {   // a null terminated string padded to an even length
        const unsigned char* q = p;
        while (q < e && *q) ++q;
        if (q >= e) { bad = true; p = e; return std::string(); }
        std::string s((const char*)p, std::min<std::size_t>((std::size_t)(q - p), kMaxNameBytes));
        std::size_t used = (std::size_t)(q - p) + 1;
        if (used & 1) ++used;
        p += std::min(used, Left());
        return s;
    }
    void Skip(std::size_t n) { if (!Need(n)) { p = e; return; } p += n; }
};
inline std::string Id4(const unsigned char* p) { return std::string((const char*)p, 4); }

struct Surf {
    std::string name, texture, uvMap;
    float col[3] = {0.78f, 0.78f, 0.78f}, diffuse = 1.0f, lum = 0, spec = 0, gloss = -1, tran = 0, crease = 0;
    bool hasTexture = false, projNotUv = false;
    int proj = 5, axis = 0;                    // image map projection (0 planar, 1 cylindrical, 2 spherical, 3 cubic, 4 front, 5 UV) and its main axis (0 X, 1 Y, 2 Z)
    float wrapW = 1.0f, wrapH = 1.0f;          // wrap amounts of a cylindrical or spherical map
    bool hasTmap = false;                      // the map's own centre and size (TMAP CNTR / SIZE, or LWOB TCTR / TSIZ), in the file's space
    float tmapCenter[3] = {0, 0, 0}, tmapSize[3] = {1, 1, 1};
    float glossLwob = -1;
};
struct Layer {
    int number = 0, parent = -1; bool hidden = false; std::string name; float pivot[3] = {0, 0, 0};
    std::vector<Vec3> pts;
    struct Poly { std::vector<std::uint32_t> v; int surf = -1; };
    std::vector<Poly> polys;
    std::map<std::string, std::map<std::uint32_t, Vec2>> vmap;
    std::map<std::string, std::map<std::uint64_t, Vec2>> vmad;   // key = point << 32 | polygon
};

// UVs for a surface whose image map is planar (0), cylindrical (1) or spherical (2). Written from the geometry of the
// three projections (see docs/own_lwo_reader.md); no code from any other importer.
//
// `pos` are the surface's points (already in the engine's right-handed space) and `ci` lists three corners per triangle;
// `cuv` is parallel to `ci` and is overwritten with (u, 1 - v). The map is worked out in the file's own left-handed space
// (z negated back), because that is the space the map's centre and size are written in. Per surface the centre and
// size are those of the map's TMAP / TCTR+TSIZ when the file has them, else the centre and extent of the surface's own box.
//   planar:      u and v are the two coordinates across the main axis (X: z and y, Y: x and z, Z: x and y), each
//                (a - centre) / size + 1/2.
//   cylindrical: round the main axis by longitude lon = atan2(second, first) of the two cross coordinates (same pairs as
//                the planar map), t = lon / 2pi + 1/2, and the height along the axis as v = (h - centre) / size + 1/2.
//   spherical:   the same t for longitude and v = latitude / pi + 1/2 (latitude = atan2(h, radius)).
//   The wrap amounts then multiply u (WRPW) and v (WRPH) of cylindrical and spherical maps.
// Seam: t jumps from 1 to 0 at one half-plane. A triangle is mapped as a whole: of its corners, the one that gives the
// shortest arc going round the circle starts it, and every other corner is moved by whole turns to follow it. So a
// triangle that crosses the seam gets t values such as 0.99, 1.01, 1.02 (above 1 is fine for a repeating texture) and
// no vertex is duplicated, because the corner UVs are stored per corner. A corner on the axis (a pole of the sphere, or a
// point on the cylinder's axis) has no longitude; it takes the average of the triangle's other corners. A triangle with
// no usable longitude at all gets t = 1/2.
// Bad numbers: a point that is not finite counts as 0; the arithmetic is done in double; a result that is not finite or is
// beyond +-1e15 is pulled back to that range (a NaN becomes 0).
void ProjectUvs(const std::vector<Vec3>& pos, const std::vector<std::uint32_t>& ci, const Surf& sf, std::vector<Vec2>& cuv) {
    constexpr double kTwoPi = 6.283185307179586476925286766559, kPi = 3.14159265358979323846264338327950;
    const auto num = [](double d) { return std::isfinite(d) ? d : 0.0; };
    const int ax = sf.axis < 0 || sf.axis > 2 ? 0 : sf.axis;
    static const int kU[3] = {2, 0, 0}, kV[3] = {1, 2, 1};   // planar: the two coordinates across each main axis
    const int ua = kU[ax], va = kV[ax];
    auto file = [&](std::uint32_t i, double o[3]) {   // a point in the file's left-handed space
        o[0] = num((double)pos[i].x); o[1] = num((double)pos[i].y); o[2] = -num((double)pos[i].z);
    };
    // the box of the surface's points, the default centre and size
    double lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
    bool first = true;
    for (std::uint32_t i : ci) {
        if (i >= pos.size()) continue;
        double p[3]; file(i, p);
        for (int k = 0; k < 3; ++k) { if (first || p[k] < lo[k]) lo[k] = p[k]; if (first || p[k] > hi[k]) hi[k] = p[k]; }
        first = false;
    }
    double cen[3], siz[3], scale = 0;
    for (int k = 0; k < 3; ++k) {
        const double ext = num(hi[k] - lo[k]);
        scale = std::max(scale, ext);
        cen[k] = sf.hasTmap ? num(sf.tmapCenter[k]) : num(0.5 * lo[k] + 0.5 * hi[k]);
        const double want = sf.hasTmap ? std::fabs(num(sf.tmapSize[k])) : ext;
        siz[k] = want > 1e-30 ? want : (ext > 1e-30 ? ext : 1.0);   // a size of 0 would divide by 0
    }
    const double axisEps = 1e-7 * (scale > 0 ? scale : 1.0);   // nearer to the axis than this has no longitude
    const double wrapW = std::isfinite(sf.wrapW) ? (double)sf.wrapW : 1.0, wrapH = std::isfinite(sf.wrapH) ? (double)sf.wrapH : 1.0;
    const auto out = [](double d) { return (float)(std::isfinite(d) ? std::max(-1e15, std::min(1e15, d)) : 0.0); };

    const std::size_t tris = std::min(ci.size(), cuv.size()) / 3;
    for (std::size_t t = 0; t < tris; ++t) {
        double tt[3] = {0, 0, 0}, vv[3] = {0, 0, 0};
        bool lonOk[3] = {false, false, false};
        for (int k = 0; k < 3; ++k) {
            const std::uint32_t i = ci[3 * t + (std::size_t)k];
            if (i >= pos.size()) continue;
            double p[3]; file(i, p);
            if (sf.proj == 0) {
                tt[k] = (p[ua] - cen[ua]) / siz[ua] + 0.5;
                vv[k] = (p[va] - cen[va]) / siz[va] + 0.5;
                continue;
            }
            const double d1 = p[ua] - cen[ua], d2 = p[va] - cen[va], h = p[ax] - cen[ax];
            const double r = std::sqrt(d1 * d1 + d2 * d2);
            if (r > axisEps) { tt[k] = std::atan2(d2, d1) / kTwoPi + 0.5; lonOk[k] = true; }
            vv[k] = sf.proj == 1 ? h / siz[ax] + 0.5 : std::atan2(h, r) / kPi + 0.5;
        }
        if (sf.proj != 0) {
            // the shortest arc over the usable longitudes: start at the corner that makes it shortest
            int best = -1; double bestSpan = 1e30;
            for (int s = 0; s < 3; ++s) {
                if (!lonOk[s]) continue;
                double span = 0;
                for (int j = 0; j < 3; ++j) {
                    if (!lonOk[j]) continue;
                    double d = std::fmod(tt[j] - tt[s], 1.0); if (d < 0) d += 1.0;
                    span = std::max(span, d);
                }
                if (span < bestSpan) { bestSpan = span; best = s; }
            }
            double mean = 0.5;
            if (best >= 0) {
                const double base = tt[best];
                double sum = 0; int n = 0;
                for (int j = 0; j < 3; ++j) {
                    if (!lonOk[j]) continue;
                    double d = std::fmod(tt[j] - base, 1.0); if (d < 0) d += 1.0;
                    tt[j] = base + d; sum += tt[j]; ++n;
                }
                mean = sum / n;
            }
            for (int j = 0; j < 3; ++j) { if (!lonOk[j]) tt[j] = mean; tt[j] *= wrapW; vv[j] *= wrapH; }
        }
        for (int k = 0; k < 3; ++k) cuv[3 * t + (std::size_t)k] = Vec2{out(tt[k]), out(1.0 - vv[k])};
    }
}

struct Reader {
    std::string err, baseDir;
    ImportedModel* out = nullptr;
    std::vector<std::string> tags;
    std::vector<Surf> surfs;
    std::vector<Layer> layers;
    std::map<std::uint32_t, std::string> clips;   // clip index -> file
    std::vector<std::string> lwobSurfNames;
    bool lwob = false;
    std::string firstUvMap;
    std::size_t chunks = 0, totalPts = 0, totalPolys = 0, totalTris = 0, uvCount = 0;
    int envelopes = 0, procBlocks = 0, nonUvProj = 0, projApplied = 0, hiddenLayers = 0, pivots = 0, curves = 0, patches = 0, smgp = 0, badPolys = 0, badIdx = 0, noSurf = 0, colorMaps = 0, otherVmaps = 0, detailPolys = 0, badUvs = 0, degenerate = 0, otherTex = 0, unknownChunks = 0, lwo3 = 0;
    std::set<std::string> unknown;

    bool Fail(const std::string& m) { if (err.empty()) err = "LWO: " + m; return false; }

    Layer* CurLayer() {
        if (layers.empty()) { Layer l; l.number = 0; l.name = "Layer1"; layers.push_back(l); }
        return &layers.back();
    }

    // ---- LWO2 ----
    bool Lwo2Surf(Cur c) {
        Surf s;
        s.name = c.S0();
        c.S0();   // the parent surface (inheritance is not followed)
        if (c.bad) return true;
        while (c.Left() >= 6) {
            const std::string id = Id4(c.p); c.Skip(4);
            const unsigned len = c.U16();
            if (c.Left() < len) break;
            Cur sub{c.p, c.p + len};
            c.Skip(std::min<std::size_t>(c.Left(), (std::size_t)len + (len & 1u)));   // sub-chunk data is padded to an even length
            if (id == "COLR") { s.col[0] = sub.F32(); s.col[1] = sub.F32(); s.col[2] = sub.F32(); if (sub.Left() >= 2) { if (sub.VX() != 0) ++envelopes; } }
            else if (id == "DIFF") { s.diffuse = sub.F32(); if (sub.Left() >= 2 && sub.VX() != 0) ++envelopes; }
            else if (id == "LUMI") { s.lum = sub.F32(); if (sub.Left() >= 2 && sub.VX() != 0) ++envelopes; }
            else if (id == "SPEC") { s.spec = sub.F32(); if (sub.Left() >= 2 && sub.VX() != 0) ++envelopes; }
            else if (id == "GLOS") { s.gloss = sub.F32(); if (sub.Left() >= 2 && sub.VX() != 0) ++envelopes; }
            else if (id == "TRAN") { s.tran = sub.F32(); if (sub.Left() >= 2 && sub.VX() != 0) ++envelopes; }
            else if (id == "SMAN") s.crease = sub.F32();
            else if (id == "BLOK") {
                const std::string head = sub.Left() >= 6 ? Id4(sub.p) : std::string();
                if (head != "IMAP") { ++procBlocks; continue; }
                sub.Skip(4);
                const unsigned hl = sub.U16();
                if (sub.Left() < hl) continue;
                Cur h{sub.p, sub.p + hl};
                sub.Skip(std::min<std::size_t>(sub.Left(), (std::size_t)hl + (hl & 1u)));
                h.S0();
                std::string chan = "COLR";
                while (h.Left() >= 6) { const std::string hid = Id4(h.p); h.Skip(4); const unsigned hlen = h.U16(); if (h.Left() < hlen) break; if (hid == "CHAN" && hlen >= 4) chan = Id4(h.p); h.Skip(std::min<std::size_t>(h.Left(), (std::size_t)hlen + (hlen & 1u))); }
                int proj = 5, axis = 0; float wrapW = 1.0f, wrapH = 1.0f; float tmCen[3] = {0, 0, 0}, tmSiz[3] = {1, 1, 1}; bool hasTm = false; std::uint32_t clip = 0xFFFFFFFFu; std::string vm;
                while (sub.Left() >= 6) {
                    const std::string bid = Id4(sub.p); sub.Skip(4); const unsigned blen = sub.U16();
                    if (sub.Left() < blen) break;
                    Cur b{sub.p, sub.p + blen};
                    sub.Skip(std::min<std::size_t>(sub.Left(), (std::size_t)blen + (blen & 1u)));
                    if (bid == "PROJ") proj = (int)b.U16();
                    else if (bid == "AXIS") axis = (int)b.U16();
                    else if (bid == "WRPW") { const float w = b.F32(); if (std::isfinite(w)) wrapW = w; }
                    else if (bid == "WRPH") { const float w = b.F32(); if (std::isfinite(w)) wrapH = w; }
                    else if (bid == "IMAG") clip = b.VX();
                    else if (bid == "TMAP") {   // sub-chunks CNTR (centre) and SIZE, each 3 floats and an envelope; ROTA is not applied
                        while (b.Left() >= 6) {
                            const std::string tid = Id4(b.p); b.Skip(4); const unsigned tlen = b.U16();
                            if (b.Left() < tlen) break;
                            Cur tc{b.p, b.p + tlen};
                            b.Skip(std::min<std::size_t>(b.Left(), (std::size_t)tlen + (tlen & 1u)));
                            if ((tid == "CNTR" || tid == "SIZE") && tlen >= 12) {
                                float* dst = tid == "CNTR" ? tmCen : tmSiz;
                                for (int k = 0; k < 3; ++k) dst[k] = tc.F32();
                                hasTm = true;
                            }
                        }
                    }
                    else if (bid == "VMAP") vm = b.S0();
                }
                if (chan != "COLR") { ++otherTex; continue; }
                if (s.hasTexture) { ++otherTex; continue; }   // only the first colour image map
                if (clip == 0xFFFFFFFFu) continue;
                s.hasTexture = true; s.uvMap = vm; s.texture = std::to_string(clip);
                s.proj = proj; s.axis = axis < 0 || axis > 2 ? 0 : axis; s.wrapW = wrapW; s.wrapH = wrapH;
                s.hasTmap = hasTm; for (int k = 0; k < 3; ++k) { s.tmapCenter[k] = tmCen[k]; s.tmapSize[k] = tmSiz[k]; }
                if (proj != 5) { s.projNotUv = true; if (proj > 2) ++nonUvProj; else ++projApplied; }
            }
        }
        if (surfs.size() >= kMaxSurfaces) return Fail("too many surfaces");
        surfs.push_back(std::move(s));
        return true;
    }

    bool Lwo2Pols(Cur c, Layer& L) {
        const std::string type = c.Left() >= 4 ? Id4(c.p) : std::string();
        c.Skip(4);
        if (type == "CURV" || type == "MBAL" || type == "BONE") { ++curves; return true; }
        if (type != "FACE" && type != "PTCH") { ++curves; return true; }
        if (type == "PTCH") ++patches;
        while (c.Left() >= 2) {
            const unsigned head = c.U16();
            const unsigned n = head & 0x3FFu;
            Layer::Poly poly;
            poly.v.reserve(n);
            for (unsigned i = 0; i < n; ++i) { if (c.Left() < 2) { c.bad = true; break; } poly.v.push_back(c.VX()); }
            if (c.bad) break;
            if (++totalPolys > kMaxPolys) return Fail("too many polygons");
            L.polys.push_back(std::move(poly));
        }
        return true;
    }

    bool Lwo2Chunks(const unsigned char* p, const unsigned char* e) {
        while ((std::size_t)(e - p) >= 8) {
            if (++chunks > kMaxChunks) return Fail("too many chunks");
            const std::string id = Id4(p);
            const std::uint32_t len = (std::uint32_t)p[4] << 24 | (std::uint32_t)p[5] << 16 | (std::uint32_t)p[6] << 8 | p[7];
            p += 8;
            std::size_t avail = (std::size_t)(e - p);
            if (len > avail) { /* truncated: read what is there */ }
            const std::size_t use = std::min<std::size_t>(len, avail);
            Cur c{p, p + use};
            const std::size_t adv = std::min<std::size_t>(avail, (std::size_t)len + (len & 1u));
            p += adv;
            if (id == "TAGS") { while (c.Left() > 0 && !c.bad) { std::string s = c.S0(); if (c.bad) break; tags.push_back(std::move(s)); if (tags.size() > 1000000) return Fail("too many tags"); } }
            else if (id == "LAYR") {
                if (layers.size() >= kMaxLayers) return Fail("too many layers");
                Layer l; l.number = (int)c.U16(); const unsigned flags = c.U16(); l.hidden = flags & 1;
                for (int k = 0; k < 3; ++k) l.pivot[k] = c.F32();
                l.name = c.S0();
                if (c.Left() >= 2) l.parent = (int)c.U16();
                if (l.name.empty()) l.name = "Layer" + std::to_string(layers.size() + 1);
                if (l.pivot[0] != 0 || l.pivot[1] != 0 || l.pivot[2] != 0) ++pivots;
                layers.push_back(std::move(l));
            }
            else if (id == "PNTS") {
                Layer* L = CurLayer();
                const std::size_t n = c.Left() / 12;
                totalPts += n;
                if (totalPts > kMaxPoints) return Fail("too many points");
                L->pts.clear(); L->pts.reserve(n);
                for (std::size_t i = 0; i < n; ++i) {
                    Vec3 v{c.F32(), c.F32(), c.F32()};
                    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) || std::fabs(v.x) > rc::kMaxCoord || std::fabs(v.y) > rc::kMaxCoord || std::fabs(v.z) > rc::kMaxCoord) return Fail("a point is not finite or out of range");
                    v.z = -v.z;   // left-handed (+Z forward) to right-handed, see Build()
                    L->pts.push_back(v);
                }
            }
            else if (id == "POLS") { if (!Lwo2Pols(c, *CurLayer())) return false; }
            else if (id == "PTAG") {
                Layer* L = CurLayer();
                const std::string type = c.Left() >= 4 ? Id4(c.p) : std::string();
                c.Skip(4);
                if (type == "SMGP") { ++smgp; continue; }
                if (type != "SURF") continue;
                while (c.Left() >= 3 && !c.bad) {
                    const std::uint32_t poly = c.VX();
                    const unsigned tag = c.U16();
                    if (c.bad) break;
                    if (poly < L->polys.size()) L->polys[poly].surf = (int)tag;
                    else ++badIdx;
                }
            }
            else if (id == "VMAP" || id == "VMAD") {
                Layer* L = CurLayer();
                const std::string type = c.Left() >= 4 ? Id4(c.p) : std::string();
                c.Skip(4);
                const unsigned dim = c.U16();
                const std::string name = c.S0();
                if (type != "TXUV" || dim != 2) { ++otherVmaps; continue; }
                if (firstUvMap.empty()) firstUvMap = name;
                if (id == "VMAP") {
                    auto& m = L->vmap[name];
                    while (c.Left() >= 10 && !c.bad) { const std::uint32_t pt = c.VX(); const float u = c.F32(), v = c.F32(); if (c.bad) break; if (++uvCount > kMaxUvs) return Fail("too many UV values"); m[pt] = Vec2{u, v}; }
                } else {
                    auto& m = L->vmad[name];
                    while (c.Left() >= 10 && !c.bad) { const std::uint32_t pt = c.VX(); const std::uint32_t pl = c.VX(); const float u = c.F32(), v = c.F32(); if (c.bad) break; if (++uvCount > kMaxUvs) return Fail("too many UV values"); m[(std::uint64_t)pt << 32 | pl] = Vec2{u, v}; }
                }
            }
            else if (id == "SURF") { if (!Lwo2Surf(c)) return false; }
            else if (id == "CLIP") {
                const std::uint32_t idx = c.U32();
                while (c.Left() >= 6) {
                    const std::string sid = Id4(c.p); c.Skip(4); const unsigned sl = c.U16();
                    if (c.Left() < sl) break;
                    Cur sc{c.p, c.p + sl}; c.Skip(std::min<std::size_t>(c.Left(), (std::size_t)sl + (sl & 1u)));
                    if (sid == "STIL") clips[idx] = sc.S0();
                }
            }
            else if (id == "ENVL") ++envelopes;
            else if (id == "BBOX" || id == "VMPA" || id == "DESC" || id == "TEXT" || id == "ICON" || id == "CMNT" || id == "TAGS") {}
            else { if (unknown.size() < 12) unknown.insert(id); ++unknownChunks; }
        }
        return true;
    }

    // ---- LWO1 (LWOB) ----
    bool LwobSurf(Cur c) {
        Surf s;
        s.name = c.S0();
        float diffPct = 256, lumPct = 0, specPct = 0, tranPct = 0;
        while (c.Left() >= 6 && !c.bad) {
            const std::string id = Id4(c.p); c.Skip(4);
            const unsigned len = c.U16();
            if (c.Left() < len) break;
            Cur sub{c.p, c.p + len};
            c.Skip(len);
            if (id == "COLR") { s.col[0] = sub.U8() / 255.0f; s.col[1] = sub.U8() / 255.0f; s.col[2] = sub.U8() / 255.0f; }
            else if (id == "DIFF") diffPct = (float)sub.I16();
            else if (id == "LUMI") lumPct = (float)sub.I16();
            else if (id == "SPEC") specPct = (float)sub.I16();
            else if (id == "TRAN") tranPct = (float)sub.I16();
            else if (id == "GLOS") s.glossLwob = (float)sub.I16();
            else if (id == "SMAN") s.crease = sub.F32();
            else if (id == "CTEX") {
                const std::string t = sub.S0(); s.projNotUv = true;
                if (t.find("Planar") != std::string::npos) s.proj = 0;
                else if (t.find("Cylindrical") != std::string::npos) s.proj = 1;
                else if (t.find("Spherical") != std::string::npos) s.proj = 2;
                else if (t.find("Cubic") != std::string::npos) s.proj = 3;
                else s.proj = 4;
            }
            else if ((id == "TCTR" || id == "TSIZ") && len >= 12) {   // the map's centre and size, 3 floats each
                float* dst = id == "TCTR" ? s.tmapCenter : s.tmapSize;
                for (int k = 0; k < 3; ++k) dst[k] = sub.F32();
                s.hasTmap = true;
            }
            else if (id == "TFLG") { const unsigned f = sub.U16(); s.axis = (f & 1) ? 0 : (f & 2) ? 1 : (f & 4) ? 2 : 0; }
            else if (id == "TIMG") { if (!s.hasTexture) { s.texture = sub.S0(); s.hasTexture = !s.texture.empty() && s.texture != "(none)"; } }
            else if (id == "DTEX" || id == "STEX" || id == "RTEX" || id == "TTEX" || id == "BTEX") ++otherTex;
        }
        s.diffuse = diffPct / 256.0f; s.lum = lumPct / 256.0f; s.spec = specPct / 256.0f; s.tran = tranPct / 256.0f;
        if (s.hasTexture) {   // LWOB image maps are projected (planar / cylindrical / spherical / cubic / front): UVs are generated for the first three
            s.projNotUv = true;
            if (s.proj > 2) ++nonUvProj; else ++projApplied;
        }
        if (surfs.size() >= kMaxSurfaces) return Fail("too many surfaces");
        surfs.push_back(std::move(s));
        return true;
    }
    bool LwobChunks(const unsigned char* p, const unsigned char* e) {
        Layer* L = CurLayer();
        L->name = "Layer1";
        while ((std::size_t)(e - p) >= 8) {
            if (++chunks > kMaxChunks) return Fail("too many chunks");
            const std::string id = Id4(p);
            const std::uint32_t len = (std::uint32_t)p[4] << 24 | (std::uint32_t)p[5] << 16 | (std::uint32_t)p[6] << 8 | p[7];
            p += 8;
            const std::size_t avail = (std::size_t)(e - p);
            const std::size_t use = std::min<std::size_t>(len, avail);
            Cur c{p, p + use};
            p += std::min<std::size_t>(avail, (std::size_t)len + (len & 1u));
            if (id == "PNTS") {
                const std::size_t n = c.Left() / 12;
                totalPts += n; if (totalPts > kMaxPoints) return Fail("too many points");
                L->pts.clear(); L->pts.reserve(n);
                for (std::size_t i = 0; i < n; ++i) {
                    Vec3 v{c.F32(), c.F32(), c.F32()};
                    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) || std::fabs(v.x) > rc::kMaxCoord || std::fabs(v.y) > rc::kMaxCoord || std::fabs(v.z) > rc::kMaxCoord) return Fail("a point is not finite or out of range");
                    v.z = -v.z;   // left-handed (+Z forward) to right-handed, see Build()
                    L->pts.push_back(v);
                }
            } else if (id == "SRFS") { while (c.Left() > 0 && !c.bad) { std::string s = c.S0(); if (c.bad) break; lwobSurfNames.push_back(std::move(s)); if (lwobSurfNames.size() > kMaxSurfaces) return Fail("too many surfaces"); } }
            else if (id == "POLS") {
                while (c.Left() >= 2) {
                    const unsigned n = c.U16();
                    if (c.Left() < (std::size_t)n * 2 + 2) { c.bad = true; break; }
                    Layer::Poly poly;
                    for (unsigned i = 0; i < n; ++i) poly.v.push_back(c.U16());
                    const int surf = c.I16();
                    if (surf < 0) {   // detail polygons follow: skipped
                        if (c.Left() < 2) break;
                        const unsigned nd = c.U16();
                        for (unsigned d = 0; d < nd; ++d) { if (c.Left() < 2) { c.bad = true; break; } const unsigned dn = c.U16(); c.Skip((std::size_t)dn * 2 + 2); ++detailPolys; }
                    }
                    poly.surf = (surf < 0 ? -surf : surf) - 1;
                    if (++totalPolys > kMaxPolys) return Fail("too many polygons");
                    L->polys.push_back(std::move(poly));
                }
            } else if (id == "SURF") { if (!LwobSurf(c)) return false; }
            else if (id == "CRVS" || id == "PCHS") ++curves;
            else { if (unknown.size() < 12) unknown.insert(id); ++unknownChunks; }
        }
        return true;
    }

    // ---- model ----
    bool Build() {
        ImportedModel& m = *out;
        ImportedNode root; root.name = "RootNode";
        m.nodes.push_back(root);
        std::map<int, int> nodeOfLayer;
        // surfaces by name -> material index (created when a polygon uses it)
        std::map<std::string, int> surfByName;
        for (std::size_t i = 0; i < surfs.size(); ++i) surfByName[surfs[i].name] = (int)i;
        std::map<int, int> matOfSurf;
        auto matFor = [&](int si) {
            auto it = matOfSurf.find(si);
            if (it != matOfSurf.end()) return it->second;
            const Surf& s = surfs[(std::size_t)si];
            ImportedMaterial mat;
            mat.name = s.name;
            auto cl = [](float v) { return std::isfinite(v) ? std::max(0.0f, std::min(1.0f, v)) : 0.0f; };
            mat.hasDiffuse = true;
            mat.diffuse = Color(cl(s.col[0] * s.diffuse), cl(s.col[1] * s.diffuse), cl(s.col[2] * s.diffuse), cl(1.0f - s.tran));
            if (s.lum > 0) { mat.hasEmissive = true; mat.emissive = Color(cl(s.col[0] * s.lum), cl(s.col[1] * s.lum), cl(s.col[2] * s.lum), 1.0f); }
            if (s.spec > 0) {
                if (lwob) { if (s.glossLwob > 0) mat.shininess = std::min(s.glossLwob, 1024.0f); }
                else if (s.gloss >= 0 && std::isfinite(s.gloss)) mat.shininess = (float)std::min(1024.0, std::pow(2.0, 10.0 * std::min((double)s.gloss, 1.0) + 2.0));
            }
            if (s.hasTexture) {   // projected maps list their image too (as other importers do); their UVs are generated below
                std::string f = s.texture;
                if (!lwob) { char* endp = nullptr; const unsigned long ci = std::strtoul(f.c_str(), &endp, 10); auto c = clips.find((std::uint32_t)ci); f = c == clips.end() ? std::string() : c->second; }
                for (char& ch : f) if (ch == '\\') ch = '/';
                if (!f.empty()) {
                    const std::size_t col = f.find(':');   // "Images:foo.tga": a LightWave content directory alias
                    if (col != std::string::npos && col > 1) f = f.substr(col + 1);
                    const bool abs = !f.empty() && (f[0] == '/' || (f.size() > 1 && f[1] == ':'));
                    mat.texture = abs ? f : baseDir + f;
                }
            }
            m.materials.push_back(mat);
            return matOfSurf[si] = (int)m.materials.size() - 1;
        };
        for (std::size_t li = 0; li < layers.size(); ++li) {
            Layer& L = layers[li];
            if (L.hidden) { ++hiddenLayers; continue; }
            if (L.polys.empty() || L.pts.empty()) continue;
            // one soup per surface
            struct SurfSoup { rc::Soup soup; std::map<std::uint32_t, std::uint32_t> pointOf; bool uv = false; };
            std::map<int, SurfSoup> soups;
            std::vector<Vec3> poly3;
            std::vector<std::array<int, 3>> tris;
            for (std::size_t pi = 0; pi < L.polys.size(); ++pi) {
                const Layer::Poly& P = L.polys[pi];
                if (P.v.size() < 3) { ++badPolys; continue; }
                bool ok = true;
                for (std::uint32_t v : P.v) if (v >= L.pts.size()) ok = false;
                if (!ok) { ++badIdx; continue; }
                int si = -1;
                if (lwob) { si = P.surf >= 0 && (std::size_t)P.surf < lwobSurfNames.size() ? (surfByName.count(lwobSurfNames[(std::size_t)P.surf]) ? surfByName[lwobSurfNames[(std::size_t)P.surf]] : -1) : -1; }
                else if (P.surf >= 0 && (std::size_t)P.surf < tags.size()) { auto it = surfByName.find(tags[(std::size_t)P.surf]); if (it != surfByName.end()) si = it->second; }
                if (si < 0) ++noSurf;
                SurfSoup& ss = soups[si];
                // z was negated on reading (left-handed to right-handed), which turns every polygon inside out: the corners
                // are taken in reverse order so faces keep pointing out (checked on real files: positive signed volume)
                const std::vector<std::uint32_t> pv(P.v.rbegin(), P.v.rend());
                poly3.clear();
                for (std::uint32_t v : pv) poly3.push_back(L.pts[v]);
                tris.clear();
                detail::TriangulatePolygon(poly3, tris);
                // UVs for this polygon's corners
                const std::string uvName = si >= 0 && !surfs[(std::size_t)si].uvMap.empty() ? surfs[(std::size_t)si].uvMap : firstUvMap;
                const bool wantUv = si >= 0 ? (surfs[(std::size_t)si].hasTexture ? !surfs[(std::size_t)si].projNotUv : true) : true;
                auto vm = L.vmap.find(uvName);
                auto vd = L.vmad.find(uvName);
                auto uvAt = [&](std::uint32_t pt, bool& have) {
                    if (vd != L.vmad.end()) { auto it = vd->second.find((std::uint64_t)pt << 32 | pi); if (it != vd->second.end()) { have = true; return it->second; } }
                    if (vm != L.vmap.end()) { auto it = vm->second.find(pt); if (it != vm->second.end()) { have = true; return it->second; } }
                    have = false; return Vec2{0, 0};
                };
                for (const auto& t : tris) {
                    if (++totalTris > kMaxTris) return Fail("too many triangles");
                    for (int k = 0; k < 3; ++k) {
                        const std::uint32_t pt = pv[(std::size_t)t[(std::size_t)k]];
                        // positions are per point and per surface (corners welded by the builder through the index)
                        auto pit = ss.pointOf.find(pt);
                        std::uint32_t idx;
                        if (pit == ss.pointOf.end()) { idx = (std::uint32_t)ss.soup.pos.size(); ss.soup.pos.push_back(L.pts[pt]); ss.pointOf[pt] = idx; }
                        else idx = pit->second;
                        ss.soup.ci.push_back(idx);
                        bool have = false;
                        Vec2 uv{0, 0};
                        if (wantUv && (vm != L.vmap.end() || vd != L.vmad.end())) uv = uvAt(pt, have);
                        if (have) ss.uv = true;
                        const float u = std::isfinite(uv.x) ? std::max(-1e15f, std::min(1e15f, uv.x)) : 0.0f, vv = std::isfinite(uv.y) ? std::max(-1e15f, std::min(1e15f, uv.y)) : 0.0f;   // a UV that is not a number is 0
                        ss.soup.cuv.push_back(Vec2{u, 1.0f - vv});
                    }
                }
            }
            // Planar, cylindrical and spherical image maps: UVs from the positions of this surface's mesh, in the layer's space
            // (ProjectUvs: the map's own centre and size when the file has them, else the surface's box; wrap amounts scale U and V
            // of cylindrical and spherical maps; the map's rotation is not applied).
            for (auto& kv : soups) {
                SurfSoup& ss = kv.second;
                if (kv.first < 0 || ss.soup.ci.empty()) continue;
                const Surf& sf = surfs[(std::size_t)kv.first];
                if (!sf.hasTexture || !sf.projNotUv || sf.proj > 2) continue;
                ProjectUvs(ss.soup.pos, ss.soup.ci, sf, ss.soup.cuv);
                ss.uv = true;
            }
            ImportedNode nd; nd.name = L.name; nd.parent = 0;
            if (L.parent >= 0) { auto pn = nodeOfLayer.find(L.parent); if (pn != nodeOfLayer.end()) nd.parent = pn->second; }
            const int ni = (int)m.nodes.size();
            bool any = false;
            for (auto& kv : soups) {
                SurfSoup& ss = kv.second;
                if (ss.soup.ci.empty()) continue;
                if (!ss.uv) ss.soup.cuv.clear();
                ImportedPart part;
                rc::BuildStats bs;
                const double creaseDeg = kv.first >= 0 ? std::max(0.0, std::min(180.0, (double)surfs[(std::size_t)kv.first].crease * 180.0 / 3.14159265358979323846)) : 0.0;
                if (!rc::BuildMesh(ss.soup, creaseDeg, part.mesh, &bs)) return Fail("too many output vertices");
                degenerate += bs.degenerate;
                if (part.mesh.triangles.empty()) continue;
                if (m.parts.size() >= kMaxParts) return Fail("too many mesh parts");
                part.material = kv.first >= 0 ? matFor(kv.first) : -1;
                part.node = ni;
                if (!rc::PartInRange(part)) return Fail("coordinates overflow");
                m.parts.push_back(std::move(part));
                any = true;
            }
            if (any) { m.nodes.push_back(nd); nodeOfLayer[L.number] = ni; }
        }
        if (m.parts.empty()) return Fail("the file has no polygons");
        return true;
    }

    bool Run(const std::string& bytes, ImportedModel& model) {
        out = &model;
        if (bytes.size() > kMaxFileBytes) return Fail("file too large");
        if (bytes.size() < 12) return Fail("the file is too short to be an LWO file");
        const unsigned char* d = (const unsigned char*)bytes.data();
        if (std::memcmp(d, "FORM", 4) != 0) return Fail("not an LWO file (no FORM header)");
        const std::string form = Id4(d + 8);
        const std::uint32_t flen = (std::uint32_t)d[4] << 24 | (std::uint32_t)d[5] << 16 | (std::uint32_t)d[6] << 8 | d[7];
        const std::size_t total = std::min<std::size_t>(bytes.size(), (std::size_t)flen + 8);
        if (total < 12) return Fail("the FORM chunk is shorter than its own header");   // chunks start at byte 12: a smaller end would make the chunk loop read past it
        if (form == "LWO2" || form == "LWO3") { if (form == "LWO3") lwo3 = 1; if (!Lwo2Chunks(d + 12, d + total)) return false; }
        else if (form == "LWOB") { lwob = true; if (!LwobChunks(d + 12, d + total)) return false; }
        else if (form == "LWLO") return Fail("LWLO (layered LWO3 variant) files are not supported");
        else return Fail("not a supported LWO file (form type '" + form + "')");
        if (layers.empty()) return Fail("the file has no points or polygons");
        if (!Build()) return false;
        auto note = [&](const std::string& s) { model.notes.push_back("LWO: " + s); };
        if (lwo3) note("LWO3 file: read with the LWO2 chunk rules; LWO3-only chunks are skipped");
        if (envelopes) note(std::to_string(envelopes) + " animated (envelope) surface value(s) not read; the base values are used");
        if (procBlocks) note(std::to_string(procBlocks) + " shader, procedural or gradient texture block(s) not read");
        if (otherTex) note(std::to_string(otherTex) + " texture block(s) for channels other than colour, or second colour maps, not read");
        if (projApplied) note(std::to_string(projApplied) + " image map(s) with planar, cylindrical or spherical projection: UVs were generated from the map's centre and size (or the mesh's bounding box when the file gives none); the map's rotation is not applied");
        if (nonUvProj) note(std::to_string(nonUvProj) + " image map(s) with cubic or front projection: the texture is kept but no UVs are made");
        if (otherVmaps) note(std::to_string(otherVmaps) + " vertex map(s) that are not UV (weights, colours, morphs) not read");
        if (smgp) note(std::to_string(smgp) + " smoothing group tag block(s) not read; the surface smoothing angle is used");
        if (curves) note(std::to_string(curves) + " curve, bone or metaball polygon block(s) not read");
        if (patches) note(std::to_string(patches) + " subdivision patch block(s) read as plain polygons (not subdivided)");
        if (hiddenLayers) note(std::to_string(hiddenLayers) + " hidden layer(s) skipped");
        if (pivots) note(std::to_string(pivots) + " layer pivot(s) not applied");
        if (detailPolys) note(std::to_string(detailPolys) + " detail polygon(s) of LWO1 not read");
        if (unknownChunks) { std::string l; for (const std::string& s : unknown) { if (!l.empty()) l += ", "; l += s; } note(std::to_string(unknownChunks) + " chunk(s) not read: " + l); }
        if (badPolys) note(std::to_string(badPolys) + " polygon(s) with fewer than 3 points skipped");
        if (badIdx) note(std::to_string(badIdx) + " polygon(s) or tag(s) with an out-of-range index skipped");
        if (noSurf) note(std::to_string(noSurf) + " polygon(s) without a surface have no material");
        if (degenerate) note(std::to_string(degenerate) + " degenerate triangle(s) skipped");
        return true;
    }
};

} // namespace lwo
} // namespace

bool ReadLwoBuffer(const std::string& bytes, const std::string& baseDir, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    lwo::Reader r;
    r.baseDir = baseDir;
    if (!r.Run(bytes, out)) {
        const std::string m = r.err.empty() ? "LWO: could not read file" : r.err;
        out = ImportedModel{};
        if (err) *err = m;
        return false;
    }
    out.backend = "own";
    return true;
}

bool ReadLwoFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::FILE* fp = detail::OpenRead(path);
    if (!fp) { if (err) *err = "cannot open file"; return false; }
    bool big = false;
    std::string bytes = detail::rc::ReadAllBounded(fp, lwo::kMaxFileBytes, &big);
    std::fclose(fp);
    if (big) { if (err) *err = "file too large"; return false; }
    const std::size_t sl = path.find_last_of("/\\");
    return ReadLwoBuffer(bytes, sl == std::string::npos ? std::string() : path.substr(0, sl + 1), out, err);
}

} // namespace okay
