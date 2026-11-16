// Our own LightWave object reader (LWO2 and LWO1 / LWOB). Written from the public LWO2 / LWOB format description,
// no code from any other importer. See docs/own_lwo_reader.md.
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

// Projected texture coordinates for a surface whose image map is planar (0), cylindrical (1) or spherical (2) about its
// main axis X, Y or Z: one UV per triangle corner (`ci` holds each corner's position index), stored as (u, 1 - v) like
// every other UV this reader makes.
//
// Reimplemented (rule 19a): the three projections are standard maths, but the turn start and pole rules were fitted to the old output, not derived from geometry alone (see docs/provenance_audit.md). The projection is placed on the
// surface's own bounding box, which is how this reader has always placed it (the map's own TMAP centre, size and
// rotation are not read yet; see docs/own_lwo_reader.md):
//   - each main axis A has a fixed pair of other axes (P, Q): X -> (Z, Y), Y -> (X, Z), Z -> (X, Y);
//   - planar: u and v are where the point sits across the box along P and along Q (0 at the low side, 1 at the high side);
//   - cylindrical: u is the angle around A, seen from the box centre, as a fraction of a turn (where the turn starts is
//     set per axis, see TurnRuleFor) and times the width wrap amount; v is the position along A across the box;
//   - spherical: u is the same angle (times the width wrap amount); v is the latitude, from -90 degrees (0) to +90
//     degrees (1), times the height wrap amount.
// A triangle that straddles the seam where the angle jumps from a full turn back to 0 would smear the whole image across
// itself. Whether it straddles is decided from the three gaps between its U values around the circle: if the gap that
// runs through the seam is the largest, the triangle sits on one side and is left alone (a big triangle, such as a pole
// fan, is not touched); otherwise the corners on one side of the largest gap are moved onto the seam (the low ones up to
// `wrap` or the high ones down to 0, whichever moves them less), so the values never leave 0 .. wrap.
namespace proj {

struct Axes { int a, p, q; };
inline Axes AxesFor(int mainAxis) {
    switch (mainAxis) {
    case 0:  return {0, 2, 1};
    case 1:  return {1, 0, 2};
    default: return {2, 0, 1};
    }
}
inline double Coord(const Vec3& v, int i) { return i == 0 ? v.x : i == 1 ? v.y : v.z; }

struct Box {
    double low[3] = {0, 0, 0}, high[3] = {0, 0, 0};
    double Mid(int i) const { return 0.5 * (low[i] + high[i]); }
    // Where `x` sits across the box along axis i, 0 .. 1; a flat box (no extent) gives 0, as this reader always did.
    double Across(double x, int i) const {
        const double extent = high[i] - low[i];
        return extent > 1e-20 ? (x - low[i]) / extent : 0.0;
    }
};

inline Box BoxOf(const std::vector<Vec3>& pos) {
    Box b;
    if (pos.empty()) return b;
    for (int i = 0; i < 3; ++i) b.low[i] = b.high[i] = Coord(pos[0], i);
    for (const Vec3& v : pos)
        for (int i = 0; i < 3; ++i) { const double c = Coord(v, i); if (c < b.low[i]) b.low[i] = c; if (c > b.high[i]) b.high[i] = c; }
    return b;
}

constexpr double kTurn = 6.283185307179586476925;   // 2 pi

// The angle around the main axis as a fraction of a turn, 0 .. 1. A point on the axis itself (a sphere's pole) has angle
// 0, so it gets the turn's starting value (0.5 for X and Z, 0 for Y), as this reader always did. Where the turn
// starts and which way it runs is fixed per axis so that images land where this reader has always put them (measured on
// the real cylindrical and spherical files in docs/model_corpus.md, every corner the same):
// (positions here are the engine's, the file's Z already mirrored):
//   X: from +Y towards +Z, starting half a turn round;  Y: from -Z towards -X;  Z: from +X towards +Y, half a turn round.
struct TurnRule { int across, up; double acrossSign, upSign, start; };   // angle = atan2(upSign * up, acrossSign * across) + start
inline TurnRule TurnRuleFor(int mainAxis) {
    switch (mainAxis) {
    case 0:  return {1, 2, 1.0, 1.0, 0.5};
    case 1:  return {2, 0, -1.0, -1.0, 0.0};
    default: return {0, 1, 1.0, 1.0, 0.5};
    }
}
inline double TurnFraction(const Vec3& v, const Box& b, const Axes& ax) {
    const TurnRule rule = TurnRuleFor(ax.a);
    const double across = Coord(v, rule.across) - b.Mid(rule.across), up = Coord(v, rule.up) - b.Mid(rule.up);
    double f = std::atan2(rule.upSign * up, rule.acrossSign * across) / kTurn + rule.start;   // -0.5 .. 1.0
    f -= std::floor(f);
    return f >= 1.0 ? 0.0 : f;
}

// Latitude of the point seen from the box centre, -90 .. +90 degrees, as 0 .. 1 (the centre itself gets 0.5).
inline double LatitudeFraction(const Vec3& v, const Box& b, const Axes& ax) {
    const double da = Coord(v, ax.a) - b.Mid(ax.a), dp = Coord(v, ax.p) - b.Mid(ax.p), dq = Coord(v, ax.q) - b.Mid(ax.q);
    const double r = std::sqrt(da * da + dp * dp + dq * dq);
    if (!(r > 0.0)) return 0.5;
    const double s = std::max(-1.0, std::min(1.0, da / r));
    return 0.5 + std::asin(s) / (kTurn / 2.0);
}

// Keeps a seam-straddling triangle on one side of the seam (see above). `u` holds the triangle's three values, 0 .. wrap.
inline void MendSeam(double u[3], double wrap) {
    int lo = 0, mid = 1, hi = 2;   // corner numbers sorted by U
    if (u[lo] > u[mid]) std::swap(lo, mid);
    if (u[mid] > u[hi]) std::swap(mid, hi);
    if (u[lo] > u[mid]) std::swap(lo, mid);
    const double gapLow = u[mid] - u[lo], gapHigh = u[hi] - u[mid], gapSeam = u[lo] + wrap - u[hi];
    if (gapSeam >= gapLow && gapSeam >= gapHigh) return;   // the triangle does not cross the seam
    // The largest gap splits the corners into a low group (just past 0) and a high group (just before a full turn). Move
    // one group onto the seam: the low group up to `wrap`, or the high group down to 0, whichever moves the values less
    // (a corner that lies on the seam moves by nothing).
    const bool splitLow = gapLow >= gapHigh;                  // low group: {lo}; else {lo, mid}
    const double costUp = splitLow ? u[lo] : u[lo] + u[mid];
    const double costDown = splitLow ? (wrap - u[mid]) + (wrap - u[hi]) : wrap - u[hi];
    if (costUp <= costDown) { u[lo] = wrap; if (!splitLow) u[mid] = wrap; }
    else { u[hi] = 0.0; if (splitLow) u[mid] = 0.0; }
}

} // namespace proj

void ProjectUvs(const std::vector<Vec3>& pos, const std::vector<std::uint32_t>& ci, const Surf& sf, std::vector<Vec2>& cuv) {
    using namespace proj;
    const Box box = BoxOf(pos);
    const Axes ax = AxesFor(sf.axis);
    const double wrapW = std::isfinite(sf.wrapW) && sf.wrapW > 0.0f ? (double)sf.wrapW : 1.0;
    const double wrapH = std::isfinite(sf.wrapH) && sf.wrapH > 0.0f ? (double)sf.wrapH : 1.0;
    cuv.assign(ci.size(), Vec2{0.0f, 0.0f});
    for (std::size_t t = 0; t + 2 < ci.size(); t += 3) {
        double u[3], v[3];
        for (int k = 0; k < 3; ++k) {
            const std::uint32_t idx = ci[t + (std::size_t)k];
            const Vec3 p = idx < pos.size() ? pos[idx] : Vec3{0, 0, 0};
            if (sf.proj == 0) {
                u[k] = box.Across(Coord(p, ax.p), ax.p);
                v[k] = box.Across(Coord(p, ax.q), ax.q);
            } else if (sf.proj == 1) {
                u[k] = TurnFraction(p, box, ax) * wrapW;
                v[k] = box.Across(Coord(p, ax.a), ax.a) * wrapH;
            } else {
                u[k] = TurnFraction(p, box, ax) * wrapW;
                v[k] = LatitudeFraction(p, box, ax) * wrapH;
            }
        }
        if (sf.proj != 0) MendSeam(u, wrapW);
        for (int k = 0; k < 3; ++k) cuv[t + (std::size_t)k] = Vec2{(float)u[k], (float)(1.0 - v[k])};
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
                int proj = 5, axis = 0; float wrapW = 1.0f, wrapH = 1.0f; std::uint32_t clip = 0xFFFFFFFFu; std::string vm;
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
                    else if (bid == "VMAP") vm = b.S0();
                }
                if (chan != "COLR") { ++otherTex; continue; }
                if (s.hasTexture) { ++otherTex; continue; }   // only the first colour image map
                if (clip == 0xFFFFFFFFu) continue;
                s.hasTexture = true; s.uvMap = vm; s.texture = std::to_string(clip);
                s.proj = proj; s.axis = axis < 0 || axis > 2 ? 0 : axis; s.wrapW = wrapW; s.wrapH = wrapH;
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
        {   // count the polygons first (a copy of the cursor, the same walk without storing) and make room once
            Cur k = c;
            std::size_t count = 0;
            while (k.Left() >= 2) {
                const unsigned n = k.U16() & 0x3FFu;
                for (unsigned i = 0; i < n; ++i) { if (k.Left() < 2) { k.bad = true; break; } k.VX(); }
                if (k.bad) break;
                ++count;
            }
            L.polys.reserve(L.polys.size() + std::min<std::size_t>(count, kMaxPolys));
        }
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
            if (s.hasTexture) {   // projected maps list their image too (Assimp does); their UVs are generated below
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
            struct SurfSoup { rc::Soup soup; detail::WeldMap<1> pointOf; bool uv = false; };   // pointOf: layer point -> soup position
            std::map<int, SurfSoup> soups;
            std::vector<Vec3> poly3;
            std::vector<std::array<int, 3>> tris;
            std::vector<std::uint32_t> pv;   // a polygon's corners, reversed (reused)
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
                pv.assign(P.v.rbegin(), P.v.rend());
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
                        bool added = false;
                        const std::uint32_t idx = (std::uint32_t)ss.pointOf.Get(&pt, (int)ss.soup.pos.size(), added);
                        if (added) ss.soup.pos.push_back(L.pts[pt]);
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
            // The layer's points, polygons and maps are all in the soups now: freed before the meshes are built (a lower peak)
            for (auto& kv : soups) kv.second.pointOf.clear();
            std::vector<Vec3>().swap(L.pts);
            std::vector<Layer::Poly>().swap(L.polys);
            L.vmap.clear();
            L.vmad.clear();
            // Planar, cylindrical and spherical image maps: UVs from the positions of this surface's mesh, in the layer's space,
            // placed on the mesh's own bounding box (ProjectUvs above; the map's TMAP centre, size and rotation are not read yet).
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
                ss.soup = rc::Soup{};   // built: freed before the next surface's mesh
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

    // `release`: the caller's buffer behind `bytes`, freed once the chunks are read (nothing read points into it), so the
    // file is not held while the meshes are built. ReadLwoFile passes its own bytes; ReadLwoBuffer passes none.
    bool Run(const std::string& bytes, ImportedModel& model, std::string* release = nullptr) {
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
        if (release) std::string().swap(*release);   // `bytes` and `d` are not used below
        if (layers.empty()) return Fail("the file has no points or polygons");
        if (!Build()) return false;
        auto note = [&](const std::string& s) { model.notes.push_back("LWO: " + s); };
        if (lwo3) note("LWO3 file: read with the LWO2 chunk rules; LWO3-only chunks are skipped");
        if (envelopes) note(std::to_string(envelopes) + " animated (envelope) surface value(s) not read; the base values are used");
        if (procBlocks) note(std::to_string(procBlocks) + " shader, procedural or gradient texture block(s) not read");
        if (otherTex) note(std::to_string(otherTex) + " texture block(s) for channels other than colour, or second colour maps, not read");
        if (projApplied) note(std::to_string(projApplied) + " image map(s) with planar, cylindrical or spherical projection: UVs were generated from the mesh's bounding box, the map's own centre, size and rotation are not applied");
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
    // as ReadLwoBuffer, but the file's bytes are freed once they are read
    lwo::Reader r;
    r.baseDir = sl == std::string::npos ? std::string() : path.substr(0, sl + 1);
    if (!r.Run(bytes, out, &bytes)) {
        const std::string m = r.err.empty() ? "LWO: could not read file" : r.err;
        out = ImportedModel{};
        if (err) *err = m;
        return false;
    }
    out.backend = "own";
    return true;
}

} // namespace okay
