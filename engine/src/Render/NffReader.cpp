// Our own NFF reader: Eric Haines' "Neutral File Format" (the ray-tracing test scene format: viewpoint, lights, fill colours,
// polygons, polygonal patches, spheres, cones and cylinders) with the common extensions (ellipsoids, the platonic solids
// tet / hex / oct / dod / ico, `tess`), and the "nff" object files of the Sense8 WorldToolKit (a vertex list, polygons by
// index with `matid`, a `.mat` material table). Written from the public descriptions of both; no code from any other
// importer. Spheres are subdivided icosahedra, cones and cylinders are tubes with caps; `tess` sets how fine. Bounds-checked,
// capped, no recursion. See okay/Render/ModelData.hpp and docs/own_nff_reader.md.
#include "PolygonTriangulate.hpp"
#include "ReaderCommon.hpp"
#include "TextReaderUtil.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace okay {

namespace {
namespace nff {

constexpr std::size_t kMaxFileBytes = 256u * 1024u * 1024u;
constexpr long kMaxPolyVerts = 1000000, kMaxVertexList = 10000000, kMaxPolys = 10000000;
constexpr std::size_t kMaxPrims = 2000000, kMaxOutVerts = 40000000;
constexpr int kMaxTess = 6;
constexpr double kPi = 3.14159265358979323846;

bool Fail(std::string* err, const std::string& m) { if (err) *err = "NFF: " + m; return false; }

// A triangle mesh with explicit per-corner data, before it becomes a part.
struct Prim {
    detail::rc::Soup soup;
    bool explicitNormals = false;
    bool anyUv = false;
    double crease = 0;   // used when the normals are made here
    int mat = -1;
    std::string name;
};

struct V3 { double x, y, z; };

// Icosahedron: 12 vertices on the unit sphere, 20 outward triangles (found from the edge length, not typed in).
void Icosahedron(std::vector<V3>& v, std::vector<std::array<int, 3>>& f) {
    const double phi = (1.0 + std::sqrt(5.0)) / 2.0, n = std::sqrt(1.0 + phi * phi);
    v.clear(); f.clear();
    for (int a : {-1, 1}) for (int b : {-1, 1}) {
        v.push_back({0, a / n, b * phi / n});
        v.push_back({a / n, b * phi / n, 0});
        v.push_back({b * phi / n, 0, a / n});
    }
    const double edge2 = 4.0 / (n * n);
    auto d2 = [&](int i, int j) { const double dx = v[(std::size_t)i].x - v[(std::size_t)j].x, dy = v[(std::size_t)i].y - v[(std::size_t)j].y, dz = v[(std::size_t)i].z - v[(std::size_t)j].z; return dx * dx + dy * dy + dz * dz; };
    for (int i = 0; i < 12; ++i) for (int j = i + 1; j < 12; ++j) for (int k = j + 1; k < 12; ++k)
        if (std::fabs(d2(i, j) - edge2) < 1e-9 && std::fabs(d2(j, k) - edge2) < 1e-9 && std::fabs(d2(i, k) - edge2) < 1e-9) {
            const V3 &a = v[(std::size_t)i], &b = v[(std::size_t)j], &c = v[(std::size_t)k];
            const V3 u{b.x - a.x, b.y - a.y, b.z - a.z}, w{c.x - a.x, c.y - a.y, c.z - a.z};
            const V3 nn{u.y * w.z - u.z * w.y, u.z * w.x - u.x * w.z, u.x * w.y - u.y * w.x};
            const double out = nn.x * (a.x + b.x + c.x) + nn.y * (a.y + b.y + c.y) + nn.z * (a.z + b.z + c.z);
            if (out > 0) f.push_back({i, j, k}); else f.push_back({i, k, j});
        }
}

// Unit-circumradius solids with polygon faces (outward winding).
void Solid(const std::string& kind, std::vector<V3>& v, std::vector<std::vector<int>>& f) {
    v.clear(); f.clear();
    if (kind == "tet") {
        v = {{0, 0, 1}, {std::sqrt(8.0 / 9.0), 0, -1.0 / 3.0}, {-std::sqrt(2.0 / 9.0), std::sqrt(2.0 / 3.0), -1.0 / 3.0}, {-std::sqrt(2.0 / 9.0), -std::sqrt(2.0 / 3.0), -1.0 / 3.0}};
        f = {{0, 1, 2}, {0, 2, 3}, {0, 3, 1}, {1, 3, 2}};
    } else if (kind == "hex") {
        const double s = 1.0 / std::sqrt(3.0);
        for (int i = 0; i < 8; ++i) v.push_back({(i & 1) ? s : -s, (i & 2) ? s : -s, (i & 4) ? s : -s});
        f = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
    } else if (kind == "oct") {
        v = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (int sx = 0; sx < 2; ++sx) for (int sy = 2; sy < 4; ++sy) for (int sz = 4; sz < 6; ++sz) f.push_back({sx, sy, sz});
    } else if (kind == "dod") {
        const double phi = (1.0 + std::sqrt(5.0)) / 2.0, n = std::sqrt(3.0);
        for (int i = 0; i < 8; ++i) v.push_back({((i & 1) ? 1 : -1) / n, ((i & 2) ? 1 : -1) / n, ((i & 4) ? 1 : -1) / n});
        for (int a : {-1, 1}) for (int b : {-1, 1}) {
            v.push_back({0, a / phi / n, b * phi / n});
            v.push_back({a / phi / n, b * phi / n, 0});
            v.push_back({b * phi / n, 0, a / phi / n});
        }
        // the 12 face directions are the cyclic permutations of (0, +-phi, +-1); each face holds the 5 vertices nearest to it
        std::vector<V3> dirs;
        for (int a : {-1, 1}) for (int b : {-1, 1}) { dirs.push_back({0, a * phi, (double)b}); dirs.push_back({(double)b, 0, a * phi}); dirs.push_back({a * phi, (double)b, 0}); }
        for (const V3& d : dirs) {
            double best = -1e30;
            for (const V3& p : v) best = std::max(best, p.x * d.x + p.y * d.y + p.z * d.z);
            std::vector<int> idx;
            for (std::size_t i = 0; i < v.size(); ++i) if (v[i].x * d.x + v[i].y * d.y + v[i].z * d.z > best - 1e-6) idx.push_back((int)i);
            if (idx.size() != 5) continue;
            // order around the direction
            const double dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            const V3 nrm{d.x / dl, d.y / dl, d.z / dl};
            V3 ax = std::fabs(nrm.x) < 0.9 ? V3{1, 0, 0} : V3{0, 1, 0};
            V3 e1{nrm.y * ax.z - nrm.z * ax.y, nrm.z * ax.x - nrm.x * ax.z, nrm.x * ax.y - nrm.y * ax.x};
            const double l1 = std::sqrt(e1.x * e1.x + e1.y * e1.y + e1.z * e1.z);
            e1 = {e1.x / l1, e1.y / l1, e1.z / l1};
            const V3 e2{nrm.y * e1.z - nrm.z * e1.y, nrm.z * e1.x - nrm.x * e1.z, nrm.x * e1.y - nrm.y * e1.x};
            std::sort(idx.begin(), idx.end(), [&](int a, int b) {
                const V3 &p = v[(std::size_t)a], &q = v[(std::size_t)b];
                return std::atan2(p.x * e2.x + p.y * e2.y + p.z * e2.z, p.x * e1.x + p.y * e1.y + p.z * e1.z) < std::atan2(q.x * e2.x + q.y * e2.y + q.z * e2.z, q.x * e1.x + q.y * e1.y + q.z * e1.z);
            });
            f.push_back(idx);
        }
    } else {   // "ico"
        std::vector<std::array<int, 3>> t;
        Icosahedron(v, t);
        for (const auto& a : t) f.push_back({a[0], a[1], a[2]});
    }
    // make every face wind outward (the solids are convex and centred on the origin)
    for (auto& face : f) {
        double nx = 0, ny = 0, nz = 0, cx = 0, cy = 0, cz = 0;
        for (std::size_t i = 0; i < face.size(); ++i) {
            const V3 &a = v[(std::size_t)face[i]], &b = v[(std::size_t)face[(i + 1) % face.size()]];
            nx += (a.y - b.y) * (a.z + b.z); ny += (a.z - b.z) * (a.x + b.x); nz += (a.x - b.x) * (a.y + b.y);
            cx += a.x; cy += a.y; cz += a.z;
        }
        if (nx * cx + ny * cy + nz * cz < 0) std::reverse(face.begin(), face.end());
    }
}

struct Material { std::string name; Color diffuse{1, 1, 1, 1}; bool hasDiffuse = false; float shininess = -1; std::string texture; Color emissive{0, 0, 0, 1}; bool hasEmissive = false; };

bool SameMat(const Material& a, const Material& b) {
    return a.diffuse.r == b.diffuse.r && a.diffuse.g == b.diffuse.g && a.diffuse.b == b.diffuse.b && a.diffuse.a == b.diffuse.a && a.hasDiffuse == b.hasDiffuse &&
           a.shininess == b.shininess && a.texture == b.texture && a.hasEmissive == b.hasEmissive;
}

std::string FixTexture(std::string t, const std::string& baseDir) {
    for (char& c : t) if (c == '\\') c = '/';
    if (t.compare(0, 2, "./") == 0) t.erase(0, 2);
    if (!t.empty() && !baseDir.empty() && t[0] != '/' && t.find(':') == std::string::npos) t = baseDir + t;
    return t;
}

// Cuts "//" comments (Sense8 files) and a whole-line '#' (classic NFF).
std::string StripComment(const std::string& s, bool sense8) {
    const std::size_t a = s.find("//");
    std::string r = a == std::string::npos ? s : s.substr(0, a);
    if (!sense8) {
        std::size_t i = 0;
        while (i < r.size() && detail::txt::IsWs(r[i])) ++i;
        if (i < r.size() && r[i] == '#') r.clear();
    }
    return r;
}

bool IsNumber(const std::string& s) { double d; return detail::txt::ToDouble(s, d); }

} // namespace nff
} // namespace

static bool ReadNffImpl(const std::string& input, const std::string& baseDir, ImportedModel& out, std::string* err) {
    using namespace nff;
    std::string text = input;
    bool converted = false;
    std::string why;
    if (!detail::txt::NormalizeEncoding(text, converted, why, kMaxFileBytes)) return Fail(err, why);
    detail::txt::Lines lines(text);
    std::string raw;
    // ---- which flavour ----
    bool sense8 = false;
    {
        detail::txt::Lines peek(text);
        while (peek.Next(raw)) {
            const std::string c = StripComment(raw, true);
            const auto t = detail::txt::Split(c);
            if (t.empty()) continue;
            sense8 = t.size() == 1 && detail::txt::Lower(t[0]) == "nff";
            break;
        }
    }
    std::vector<Prim> prims;
    std::vector<Material> mats;
    Material cur;
    int curMat = -1;
    int tess = 4;
    std::size_t outVerts = 0;
    int skipped = 0, lights = 0, viewpoint = 0, badPolys = 0, twoSided = 0, colorWords = 0, unknownKeys = 0, extraNums = 0, noMtable = 0;
    std::string firstUnknown;

    auto matIndex = [&]() -> int {
        if (curMat >= 0 && SameMat(mats[(std::size_t)curMat], cur)) return curMat;
        mats.push_back(cur);
        curMat = (int)mats.size() - 1;
        return curMat;
    };
    auto addPrim = [&](Prim&& p) -> bool {
        if (prims.size() >= kMaxPrims) { Fail(err, "too many primitives"); return false; }
        prims.push_back(std::move(p));
        return true;
    };
    auto tokens = [&](std::vector<std::string>& t) -> bool {
        while (lines.Next(raw)) {
            t = detail::txt::Split(StripComment(raw, sense8));
            if (!t.empty()) return true;
        }
        return false;
    };
    auto numAt = [&](const std::vector<std::string>& t, std::size_t i, double& v) { return i < t.size() && detail::txt::ToDouble(t[i], v); };

    std::vector<std::string> t;
    if (!sense8) {
        Prim* openPolys = nullptr;   // the polygon group being filled (same material, same normals flag)
        int openMat = -2;
        bool openN = false;
        auto closePolys = [&]() { openPolys = nullptr; openMat = -2; };
        while (tokens(t)) {
            const std::string k = detail::txt::Lower(t[0]);
            if (k == "v" || k == "from" || k == "at" || k == "up" || k == "angle" || k == "hither" || k == "resolution" || k == "yon") { ++viewpoint; continue; }
            if (k == "b") { continue; }
            if (k == "l") { ++lights; continue; }
            if (k == "tess") { double a; if (numAt(t, 1, a)) tess = (int)std::max(0.0, std::min<double>(a, kMaxTess)); continue; }
            if (k == "f") {
                double n[8];
                std::size_t cnt = 0;
                std::string texName;
                for (std::size_t i = 1; i < t.size(); ++i) {
                    double d;
                    if (detail::txt::ToDouble(t[i], d)) { if (cnt < 8) n[cnt] = d; ++cnt; }
                    else texName = t[i];
                }
                if (cnt < 3) return Fail(err, "bad fill colour (f) on line " + std::to_string(lines.number));
                cur = Material{};
                cur.hasDiffuse = true;
                cur.diffuse = Color{(float)std::max(0.0, std::min(1.0, n[0])), (float)std::max(0.0, std::min(1.0, n[1])), (float)std::max(0.0, std::min(1.0, n[2])), 1.0f};
                if (cnt >= 6 && n[5] >= 0) cur.shininess = (float)n[5];
                if (cnt > 3) extraNums = 1;
                cur.texture = texName;
                closePolys();
                continue;
            }
            if (k == "s" || k == "tet" || k == "hex" || k == "oct" || k == "dod" || k == "ico") {
                double a[6];
                for (std::size_t i = 0; i < 4; ++i) if (!numAt(t, i + 1, a[i])) return Fail(err, "bad " + k + " on line " + std::to_string(lines.number));
                double ry = a[3], rz = a[3];
                if (numAt(t, 5, a[4])) { ry = a[4]; if (numAt(t, 6, a[5])) rz = a[5]; else rz = a[4]; }
                if (!(a[3] > 0) || !(ry > 0) || !(rz > 0)) { ++skipped; continue; }   // a flat or negative radius makes no solid
                Prim p;
                p.mat = matIndex();
                p.name = k;
                std::vector<V3> v;
                std::vector<std::vector<int>> faces;
                if (k == "s") {
                    std::vector<std::array<int, 3>> tr;
                    Icosahedron(v, tr);
                    // subdivide `tess` times; vertices are shared by the edge midpoint cache
                    for (int lvl = 0; lvl < tess; ++lvl) {
                        std::map<std::pair<int, int>, int> mid;
                        std::vector<std::array<int, 3>> nt;
                        nt.reserve(tr.size() * 4);
                        auto mp = [&](int x, int y) {
                            const auto key = std::make_pair(std::min(x, y), std::max(x, y));
                            auto it = mid.find(key);
                            if (it != mid.end()) return it->second;
                            V3 m{(v[(std::size_t)x].x + v[(std::size_t)y].x) / 2, (v[(std::size_t)x].y + v[(std::size_t)y].y) / 2, (v[(std::size_t)x].z + v[(std::size_t)y].z) / 2};
                            const double l = std::sqrt(m.x * m.x + m.y * m.y + m.z * m.z);
                            m = {m.x / l, m.y / l, m.z / l};
                            v.push_back(m);
                            return mid[key] = (int)v.size() - 1;
                        };
                        for (const auto& f : tr) {
                            const int ab = mp(f[0], f[1]), bc = mp(f[1], f[2]), ca = mp(f[2], f[0]);
                            nt.push_back({f[0], ab, ca}); nt.push_back({f[1], bc, ab}); nt.push_back({f[2], ca, bc}); nt.push_back({ab, bc, ca});
                        }
                        tr.swap(nt);
                    }
                    for (const auto& f : tr) faces.push_back({f[0], f[1], f[2]});
                    p.explicitNormals = true;
                } else {
                    Solid(k, v, faces);
                }
                for (const V3& q : v) p.soup.pos.push_back(Vec3{(float)(a[0] + q.x * a[3]), (float)(a[1] + q.y * ry), (float)(a[2] + q.z * rz)});
                for (const auto& f : faces) {
                    std::vector<Vec3> poly;
                    for (int ix : f) poly.push_back(p.soup.pos[(std::size_t)ix]);
                    std::vector<std::array<int, 3>> tri;
                    detail::TriangulatePolygon(poly, tri);
                    for (const auto& tt : tri) for (int c = 0; c < 3; ++c) {
                        const int vi = f[(std::size_t)tt[(std::size_t)c]];
                        p.soup.ci.push_back((std::uint32_t)vi);
                        if (p.explicitNormals) {   // normal of an ellipsoid: the unit direction divided by the radii
                            const V3& q = v[(std::size_t)vi];
                            const double nx = q.x / a[3], ny = q.y / ry, nz = q.z / rz, l = std::sqrt(nx * nx + ny * ny + nz * nz);
                            p.soup.cnrm.push_back(Vec3{(float)(nx / l), (float)(ny / l), (float)(nz / l)});
                        }
                    }
                }
                outVerts += p.soup.ci.size();
                if (outVerts > kMaxOutVerts) return Fail(err, "too many vertices");
                if (!addPrim(std::move(p))) return false;
                closePolys();
                continue;
            }
            if (k == "c") {
                std::vector<std::string> b, e;
                if (!tokens(b) || !tokens(e)) return Fail(err, "a cone (c) needs two lines (base and apex)");
                double bb[4], ee[4];
                for (std::size_t i = 0; i < 4; ++i) if (!numAt(b, i, bb[i]) || !numAt(e, i, ee[i])) return Fail(err, "bad cone line near line " + std::to_string(lines.number));
                const double ax = ee[0] - bb[0], ay = ee[1] - bb[1], az = ee[2] - bb[2], len = std::sqrt(ax * ax + ay * ay + az * az);
                if (!(len > 1e-12) || bb[3] < 0 || ee[3] < 0 || (bb[3] == 0 && ee[3] == 0)) { ++skipped; continue; }
                const int segs = 1 << (2 * std::max(tess, 1));   // 4^tess: 4 .. 4096 (tess 4, the default, gives 256)
                const V3 u{ax / len, ay / len, az / len};
                const V3 helper = std::fabs(u.x) < 0.9 ? V3{1, 0, 0} : V3{0, 1, 0};
                V3 e1{u.y * helper.z - u.z * helper.y, u.z * helper.x - u.x * helper.z, u.x * helper.y - u.y * helper.x};
                const double l1 = std::sqrt(e1.x * e1.x + e1.y * e1.y + e1.z * e1.z);
                e1 = {e1.x / l1, e1.y / l1, e1.z / l1};
                const V3 e2{u.y * e1.z - u.z * e1.y, u.z * e1.x - u.x * e1.z, u.x * e1.y - u.y * e1.x};
                Prim p;
                p.mat = matIndex();
                p.name = "cone";
                p.crease = 60;
                // positions: base ring, apex ring, base centre, apex centre
                for (int ring = 0; ring < 2; ++ring) for (int i = 0; i < segs; ++i) {
                    const double ang = 2 * kPi * i / segs, r = ring ? ee[3] : bb[3];
                    const double* c = ring ? ee : bb;
                    p.soup.pos.push_back(Vec3{(float)(c[0] + r * (std::cos(ang) * e1.x + std::sin(ang) * e2.x)), (float)(c[1] + r * (std::cos(ang) * e1.y + std::sin(ang) * e2.y)),
                                              (float)(c[2] + r * (std::cos(ang) * e1.z + std::sin(ang) * e2.z))});
                }
                const std::uint32_t cb = (std::uint32_t)p.soup.pos.size();
                p.soup.pos.push_back(Vec3{(float)bb[0], (float)bb[1], (float)bb[2]});
                p.soup.pos.push_back(Vec3{(float)ee[0], (float)ee[1], (float)ee[2]});
                auto tri = [&](std::uint32_t x, std::uint32_t y, std::uint32_t z) { p.soup.ci.push_back(x); p.soup.ci.push_back(y); p.soup.ci.push_back(z); };
                for (int i = 0; i < segs; ++i) {
                    const std::uint32_t b0 = (std::uint32_t)i, b1 = (std::uint32_t)((i + 1) % segs), a0 = (std::uint32_t)(segs + i), a1 = (std::uint32_t)(segs + (i + 1) % segs);
                    tri(b0, b1, a1); tri(b0, a1, a0);          // side, outward for a counter-clockwise ring seen from the apex
                    if (bb[3] > 0) tri(cb, b1, b0);            // base cap, facing away from the apex
                    if (ee[3] > 0) tri(cb + 1, a0, a1);        // apex cap
                }
                outVerts += p.soup.ci.size();
                if (outVerts > kMaxOutVerts) return Fail(err, "too many vertices");
                if (!addPrim(std::move(p))) return false;
                closePolys();
                continue;
            }
            if (k == "p" || k == "pp") {
                long long n;
                if (t.size() < 2 || !detail::txt::ToInt(t[1], n) || n < 0 || n > kMaxPolyVerts) return Fail(err, "bad polygon vertex count on line " + std::to_string(lines.number));
                const bool withN = k == "pp";
                std::vector<Vec3> pts, nrm;
                for (long long i = 0; i < n; ++i) {
                    std::vector<std::string> vt;
                    if (!tokens(vt)) return Fail(err, "the file ends after " + std::to_string(i) + " of " + std::to_string(n) + " polygon vertices");
                    double f[6];
                    for (std::size_t j = 0; j < 3; ++j) if (!numAt(vt, j, f[j])) return Fail(err, "bad polygon vertex on line " + std::to_string(lines.number));
                    pts.push_back(Vec3{(float)f[0], (float)f[1], (float)f[2]});
                    if (withN) {
                        for (std::size_t j = 3; j < 6; ++j) if (!numAt(vt, j, f[j])) return Fail(err, "bad patch normal on line " + std::to_string(lines.number));
                        nrm.push_back(Vec3{(float)f[3], (float)f[4], (float)f[5]});
                    }
                }
                if (n < 3) { ++badPolys; continue; }
                const int m = matIndex();
                if (!openPolys || openMat != m || openN != withN) {
                    Prim p; p.mat = m; p.name = "polygons"; p.explicitNormals = withN;
                    if (!addPrim(std::move(p))) return false;
                    openPolys = &prims.back(); openMat = m; openN = withN;
                }
                Prim& p = *openPolys;
                std::vector<std::array<int, 3>> tr;
                detail::TriangulatePolygon(pts, tr);
                const std::uint32_t base = (std::uint32_t)p.soup.pos.size();
                for (const Vec3& q : pts) p.soup.pos.push_back(q);
                for (const auto& tt : tr) for (int c = 0; c < 3; ++c) {
                    p.soup.ci.push_back(base + (std::uint32_t)tt[(std::size_t)c]);
                    if (withN) p.soup.cnrm.push_back(nrm[(std::size_t)tt[(std::size_t)c]]);
                }
                outVerts += tr.size() * 3;
                if (outVerts > kMaxOutVerts) return Fail(err, "too many vertices");
                continue;
            }
            if (IsNumber(t[0])) { ++extraNums; continue; }
            ++unknownKeys;
            if (firstUnknown.empty()) firstUnknown = t[0];
        }
    } else {
        // ---- Sense8 object file ----
        std::vector<Material> table;
        std::string mtable;
        std::string objName;
        bool header = true;
        long vcount = -1;
        std::vector<std::string> nt;
        bool first = true;
        while (tokens(t)) {
            if (first) { first = false; continue; }   // the "nff" line
            const std::string k = detail::txt::Lower(t[0]);
            if (header && (k == "version" || k == "viewpos" || k == "viewdir" || k == "projection" || k == "hither" || k == "yon" || k == "resolution" || k == "angle" || k == "from" || k == "at" || k == "up")) { ++viewpoint; continue; }
            if (header && k == "mtable") { if (t.size() >= 2) mtable = t[1]; continue; }
            // object name (a line that is not a count), then the vertex count
            if (!(t.size() == 1 && IsNumber(t[0]))) {
                objName = t[0];
                for (;;) {   // header lines may follow the name (mtable, ...) before the vertex count
                    if (!tokens(nt)) return Fail(err, "the file ends after the object name");
                    const std::string nk = detail::txt::Lower(nt[0]);
                    if (nk == "mtable") { if (nt.size() >= 2) mtable = nt[1]; continue; }
                    if (nk == "version" || nk == "viewpos" || nk == "viewdir" || nk == "projection" || nk == "hither" || nk == "yon" || nk == "resolution" || nk == "angle" || nk == "from" || nk == "at" || nk == "up") { ++viewpoint; continue; }
                    break;
                }
                t = nt;
            }
            long long n;
            if (t.size() != 1 || !detail::txt::ToInt(t[0], n) || n < 0 || n > kMaxVertexList) return Fail(err, "bad vertex count on line " + std::to_string(lines.number));
            vcount = (long)n;
            header = false;
            std::vector<Vec3> pos;
            std::vector<Vec2> uv;
            std::vector<char> hasUv;
            pos.reserve((std::size_t)std::min<long>(vcount, (long)(text.size() / 8 + 1)));
            for (long i = 0; i < vcount; ++i) {
                std::vector<std::string> vt;
                if (!tokens(vt)) return Fail(err, "the file ends after " + std::to_string(i) + " of " + std::to_string(vcount) + " vertices");
                double f[3];
                for (std::size_t j = 0; j < 3; ++j) if (!numAt(vt, j, f[j])) return Fail(err, "bad vertex on line " + std::to_string(lines.number));
                pos.push_back(Vec3{(float)f[0], (float)f[1], (float)f[2]});
                Vec2 q{0, 0};
                char hu = 0;
                for (std::size_t j = 3; j + 2 < vt.size(); ++j) {
                    if (detail::txt::Lower(vt[j]) == "uv") { double a, b; if (numAt(vt, j + 1, a) && numAt(vt, j + 2, b)) { q = Vec2{(float)a, (float)b}; hu = 1; } break; }
                }
                uv.push_back(q); hasUv.push_back(hu);
            }
            if (!tokens(nt)) return Fail(err, "the file ends before the polygon count");
            if (nt.size() != 1 || !detail::txt::ToInt(nt[0], n) || n < 0 || n > kMaxPolys) return Fail(err, "bad polygon count on line " + std::to_string(lines.number));
            const long pcount = (long)n;
            // load the material table when there is one
            if (!mtable.empty() && table.empty()) {
                std::string mt, e2;
                std::string path = mtable;
                for (char& c : path) if (c == '\\') c = '/';
                if (!baseDir.empty() && path[0] != '/' && path.find(':') == std::string::npos) path = baseDir + path;
                if (detail::txt::ReadFileCapped(path, 16u * 1024u * 1024u, mt, &e2)) {
                    bool dummy; std::string w2;
                    detail::txt::NormalizeEncoding(mt, dummy, w2, 64u * 1024u * 1024u);
                    detail::txt::Lines ml(mt);
                    std::string r2;
                    int idx = -1;
                    while (ml.Next(r2)) {
                        const auto mtk = detail::txt::Split(StripComment(r2, true));
                        if (mtk.empty()) continue;
                        const std::string mk = detail::txt::Lower(mtk[0]);
                        if (mk == "matdef") { table.push_back(Material{}); idx = (int)table.size() - 1; table.back().name = "mat" + std::to_string(idx); if (table.size() > 100000) break; continue; }
                        if (idx < 0) continue;
                        double a, b, c;
                        Material& m = table[(std::size_t)idx];
                        if ((mk == "ambientdiffuse" || mk == "diffuse") && mtk.size() >= 4 && detail::txt::ToDouble(mtk[1], a) && detail::txt::ToDouble(mtk[2], b) && detail::txt::ToDouble(mtk[3], c)) {
                            m.hasDiffuse = true; m.diffuse.r = (float)std::max(0.0, std::min(1.0, a)); m.diffuse.g = (float)std::max(0.0, std::min(1.0, b)); m.diffuse.b = (float)std::max(0.0, std::min(1.0, c));
                        } else if (mk == "emission" && mtk.size() >= 4 && detail::txt::ToDouble(mtk[1], a) && detail::txt::ToDouble(mtk[2], b) && detail::txt::ToDouble(mtk[3], c)) {
                            m.hasEmissive = a + b + c > 0; m.emissive = Color{(float)a, (float)b, (float)c, 1};
                        } else if (mk == "shininess" && mtk.size() >= 2 && detail::txt::ToDouble(mtk[1], a) && a >= 0) m.shininess = (float)a;
                        else if (mk == "opacity" && mtk.size() >= 2 && detail::txt::ToDouble(mtk[1], a)) { m.hasDiffuse = true; m.diffuse.a = (float)std::max(0.0, std::min(1.0, a)); }
                    }
                } else ++noMtable;
            }
            std::map<std::pair<int, std::string>, std::size_t> groups;
            for (long pi = 0; pi < pcount; ++pi) {
                std::vector<std::string> pt;
                if (!tokens(pt)) return Fail(err, "the file ends after " + std::to_string(pi) + " of " + std::to_string(pcount) + " polygons");
                long long nv;
                if (!detail::txt::ToInt(pt[0], nv) || nv < 0 || nv > kMaxPolyVerts || (std::size_t)nv + 1 > pt.size()) return Fail(err, "bad polygon on line " + std::to_string(lines.number));
                std::vector<std::uint32_t> ix;
                for (long long j = 0; j < nv; ++j) {
                    long long v;
                    if (!detail::txt::ToInt(pt[(std::size_t)j + 1], v) || v < 0 || v >= (long long)pos.size()) return Fail(err, "polygon vertex index out of range on line " + std::to_string(lines.number));
                    ix.push_back((std::uint32_t)v);
                }
                int matid = -1;
                std::string tex;
                for (std::size_t j = (std::size_t)nv + 1; j < pt.size(); ++j) {
                    const std::string w = detail::txt::Lower(pt[j]);
                    if (w == "matid") { long long m; if (j + 1 < pt.size() && detail::txt::ToInt(pt[j + 1], m)) matid = (int)std::max<long long>(-1, std::min<long long>(m, 1000000)); ++j; }
                    else if (w == "both") ++twoSided;
                    else if (w.compare(0, 2, "0x") == 0) ++colorWords;
                    else tex = pt[j];
                }
                if (nv < 3) { ++badPolys; continue; }
                const auto key = std::make_pair(matid, tex);
                auto it = groups.find(key);
                if (it == groups.end()) {
                    Prim p; p.crease = 0;
                    p.mat = matid; p.name = tex;   // reused as: material id and texture name, resolved below
                    prims.push_back(std::move(p));
                    if (prims.size() > kMaxPrims) return Fail(err, "too many primitives");
                    it = groups.emplace(key, prims.size() - 1).first;
                }
                Prim& p = prims[it->second];
                std::vector<Vec3> poly;
                for (std::uint32_t v : ix) poly.push_back(pos[v]);
                std::vector<std::array<int, 3>> tr;
                detail::TriangulatePolygon(poly, tr);
                const std::uint32_t base = (std::uint32_t)p.soup.pos.size();
                for (std::uint32_t v : ix) { p.soup.pos.push_back(pos[v]); }
                const bool anyUv = [&] { for (std::uint32_t v : ix) if (hasUv[v]) return true; return false; }();
                if (anyUv) p.anyUv = true;
                for (const auto& tt : tr) for (int c = 0; c < 3; ++c) {
                    p.soup.ci.push_back(base + (std::uint32_t)tt[(std::size_t)c]);
                    p.soup.cuv.push_back(anyUv ? uv[ix[(std::size_t)tt[(std::size_t)c]]] : Vec2{0, 0});
                }
                outVerts += tr.size() * 3;
                if (outVerts > kMaxOutVerts) return Fail(err, "too many vertices");
            }
            // turn this object's groups into materials (the prims hold matid in `mat` and the texture in `name`)
            for (auto& g : groups) {
                Prim& p = prims[g.second];
                Material m;
                const int matid = p.mat;
                if (matid >= 0 && (std::size_t)matid < table.size()) m = table[(std::size_t)matid];
                m.texture = p.name;
                p.name = objName;
                mats.push_back(m);
                p.mat = (int)mats.size() - 1;
            }
            // a second object in the same file: loop again (the next name line)
        }
        if (vcount < 0) return Fail(err, "the file has no vertex list");
    }

    // ---- parts ----
    ImportedNode root;
    root.name = "NFF";
    out.nodes.push_back(root);
    std::map<int, int> matOut;
    int degenerate = 0;
    outVerts = 0;
    for (Prim& p : prims) {
        if (p.soup.ci.empty()) continue;
        ImportedPart part;
        detail::rc::BuildStats st;
        if (p.explicitNormals && p.soup.cnrm.size() != p.soup.ci.size()) p.soup.cnrm.clear();
        if (!p.anyUv) p.soup.cuv.clear();
        if (!detail::rc::BuildMesh(p.soup, p.explicitNormals ? 0.0 : p.crease, part.mesh, &st, kMaxOutVerts - std::min(outVerts, kMaxOutVerts))) return Fail(err, "too many output vertices");
        degenerate += st.degenerate;
        outVerts += part.mesh.vertices.size();
        if (part.mesh.triangles.empty()) continue;
        if (!detail::rc::PartInRange(part)) return Fail(err, "a vertex is out of range");
        part.mesh.name = p.name;
        part.node = 0;
        if (p.mat >= 0) {
            auto it = matOut.find(p.mat);
            if (it == matOut.end()) {
                const Material& m = mats[(std::size_t)p.mat];
                ImportedMaterial im;
                im.name = m.name;
                im.hasDiffuse = m.hasDiffuse; im.diffuse = m.diffuse;
                im.hasEmissive = m.hasEmissive; im.emissive = m.emissive;
                im.shininess = m.shininess;
                im.texture = m.texture.empty() ? std::string() : FixTexture(m.texture, baseDir);
                out.materials.push_back(im);
                it = matOut.emplace(p.mat, (int)out.materials.size() - 1).first;
            }
            part.material = it->second;
        }
        out.parts.push_back(std::move(part));
    }
    if (out.parts.empty()) return Fail(err, "the file has no geometry");
    out.backend = "own";

    auto note = [&](const std::string& s) { out.notes.push_back("NFF: " + s); };
    if (!sense8) note("spheres are subdivided icosahedra and cones and cylinders are capped tubes whose fineness follows `tess` (default 4); the exact tessellation of other programs is not reproduced");
    if (skipped) note(std::to_string(skipped) + " solid(s) with a zero or negative size skipped");
    if (lights) note(std::to_string(lights) + " light(s) (l) not imported");
    if (viewpoint) note("viewpoint, background and resolution lines are not imported");
    if (extraNums) note("the diffuse, specular, transmission and refraction values of the fill colours are not applied; only the colour, the shininess and a texture name are");
    if (badPolys) note(std::to_string(badPolys) + " polygon(s) with fewer than 3 vertices skipped");
    if (twoSided) note(std::to_string(twoSided) + " polygon(s) marked 'both' (two-sided): the engine's meshes are one-sided");
    if (colorWords) note(std::to_string(colorWords) + " polygon colour word(s) (0x...) not imported");
    if (noMtable) note("the material table file (mtable) could not be read");
    if (unknownKeys) note(std::to_string(unknownKeys) + " unknown keyword line(s) skipped (first: " + firstUnknown + ")");
    if (degenerate) note(std::to_string(degenerate) + " degenerate triangle(s) skipped");
    return true;
}

bool ReadNffBuffer(const std::string& text, const std::string& baseDir, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    const bool ok = ReadNffImpl(text, baseDir, out, err);
    if (!ok) out = ImportedModel{};
    return ok;
}

bool ReadNffFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, nff::kMaxFileBytes, bytes, err)) return false;
    std::string dir;
    const std::size_t sl = path.find_last_of("/\\");
    if (sl != std::string::npos) dir = path.substr(0, sl + 1);
    return ReadNffBuffer(bytes, dir, out, err);
}

} // namespace okay
