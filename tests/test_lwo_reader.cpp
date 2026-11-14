// Our own LightWave object reader (engine/src/Render/LwoReader.cpp). SYNTHETIC fixtures (LWO2 and LWOB files built in
// memory), checked against hand-computed numbers; plus the import seam. Real LWO files are checked by model_corpus.
// Builds on its own with -DOKAY_STANDALONE_TEST (reader source only), no engine library.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "okay/Render/LwoReader.hpp"
#ifndef OKAY_STANDALONE_TEST
#include "okay/Render/ModelImport.hpp"
#endif
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;

// ---- big-endian builders ----
static void B16(std::string& s, unsigned v) { s.push_back((char)(v >> 8)); s.push_back((char)v); }
static void B32(std::string& s, std::uint32_t v) { for (int i = 3; i >= 0; --i) s.push_back((char)(v >> (8 * i))); }
static void BF(std::string& s, float f) { std::uint32_t v; std::memcpy(&v, &f, 4); B32(s, v); }
static void BS0(std::string& s, const std::string& x) { s += x; s.push_back(0); if ((x.size() + 1) & 1) s.push_back(0); }
static void BVX(std::string& s, std::uint32_t v) { if (v < 0xFF00) B16(s, v); else { B32(s, v | 0xFF000000u); } }
static std::string Chunk(const char* id, const std::string& body) { std::string s = id; B32(s, (std::uint32_t)body.size()); s += body; if (body.size() & 1) s.push_back(0); return s; }
static std::string Sub(const char* id, const std::string& body) { std::string s = id; B16(s, (unsigned)body.size()); s += body; if (body.size() & 1) s.push_back(0); return s; }
static std::string Form(const char* type, const std::string& chunks) { std::string s = "FORM"; B32(s, (std::uint32_t)(chunks.size() + 4)); s += type; s += chunks; return s; }

static const float kCube[8][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
static const int kQuads[6][4] = {{3, 2, 1, 0}, {4, 5, 6, 7}, {0, 1, 5, 4}, {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};   // outward, counter-clockwise from outside
static std::string Layr(int number, const std::string& name, unsigned flags = 0, int parent = -1, const float* pivot = nullptr) {
    std::string b; B16(b, (unsigned)number); B16(b, flags);
    for (int i = 0; i < 3; ++i) BF(b, pivot ? pivot[i] : 0.0f);
    BS0(b, name); if (parent >= 0) B16(b, (unsigned)parent);
    return Chunk("LAYR", b);
}
static std::string Pnts(const float (*p)[3], int n) { std::string b; for (int i = 0; i < n; ++i) for (int k = 0; k < 3; ++k) BF(b, p[i][k]); return Chunk("PNTS", b); }
static std::string Pols2(const char* type = "FACE") {
    std::string b = type;
    for (auto& q : kQuads) { B16(b, 4); for (int k = 0; k < 4; ++k) BVX(b, (std::uint32_t)q[k]); }
    return Chunk("POLS", b);
}
static std::string PtagSurf(const std::vector<unsigned>& tagOfPoly) {
    std::string b = "SURF"; for (std::size_t i = 0; i < tagOfPoly.size(); ++i) { BVX(b, (std::uint32_t)i); B16(b, tagOfPoly[i]); }
    return Chunk("PTAG", b);
}
static std::string Tags(const std::vector<std::string>& t) { std::string b; for (const std::string& s : t) BS0(b, s); return Chunk("TAGS", b); }
static std::string F3(float a, float b, float c) { std::string s; BF(s, a); BF(s, b); BF(s, c); B16(s, 0); return s; }   // colour + empty envelope
static std::string F1(float a) { std::string s; BF(s, a); B16(s, 0); return s; }
static std::string Surf2(const std::string& name, const std::string& subs) { std::string b; BS0(b, name); BS0(b, ""); b += subs; return Chunk("SURF", b); }

static std::string Box2(bool textured, int proj = 5, int axis = 0, float wrapW = 1.0f, float wrapH = 1.0f) {
    std::string c = Tags({"Red", "Blue"}) + Layr(0, "Box") + Pnts(kCube, 8) + Pols2() + PtagSurf({0, 0, 0, 1, 1, 1});
    c += Surf2("Red", Sub("COLR", F3(1, 0, 0)) + Sub("DIFF", F1(0.5f)) + Sub("LUMI", F1(0.1f)) + Sub("SPEC", F1(0.5f)) + Sub("GLOS", F1(0.2f)) + Sub("TRAN", F1(0.25f)) + Sub("SMAN", [] { std::string s; BF(s, 0); return s; }()));
    std::string blue = Sub("COLR", F3(0, 0, 1));
    if (textured) {
        std::string head; BS0(head, "\x80"); head += Sub("CHAN", "COLR");
        std::string pj; B16(pj, (unsigned)proj);
        std::string blok = Sub("IMAP", head) + Sub("PROJ", pj) + Sub("IMAG", [] { std::string s; BVX(s, 1); return s; }());
        if (proj == 5) blok += Sub("VMAP", [] { std::string s; BS0(s, "uv"); return s; }());
        else {
            std::string ax; B16(ax, (unsigned)axis); blok += Sub("AXIS", ax);
            std::string ww; BF(ww, wrapW); B16(ww, 0); blok += Sub("WRPW", ww);
            std::string wh; BF(wh, wrapH); B16(wh, 0); blok += Sub("WRPH", wh);
        }
        blue += Sub("BLOK", blok);
        std::string clip; B32(clip, 1); clip += Sub("STIL", [] { std::string s; BS0(s, "Images\\blue.tga"); return s; }());
        c += Chunk("CLIP", clip);
        if (proj == 5) {
            std::string vm = "TXUV"; B16(vm, 2); BS0(vm, "uv");
            for (int i = 0; i < 8; ++i) { BVX(vm, (std::uint32_t)i); BF(vm, kCube[i][0]); BF(vm, kCube[i][1]); }
            c += Chunk("VMAP", vm);
        }
    }
    c += Surf2("Blue", blue);
    return Form("LWO2", c);
}
static bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) < e; }
static double Volume(const ImportedModel& m) {
    double v = 0;
    for (const ImportedPart& p : m.parts) for (std::size_t i = 0; i + 2 < p.mesh.triangles.size(); i += 3) {
        const Vec3 a = p.mesh.vertices[(std::size_t)p.mesh.triangles[i]], b = p.mesh.vertices[(std::size_t)p.mesh.triangles[i + 1]], c = p.mesh.vertices[(std::size_t)p.mesh.triangles[i + 2]];
        v += ((double)a.x * ((double)b.y * c.z - (double)b.z * c.y) - (double)a.y * ((double)b.x * c.z - (double)b.z * c.x) + (double)a.z * ((double)b.x * c.y - (double)b.y * c.x)) / 6.0;
    }
    return v;
}
static std::size_t Tris(const ImportedModel& m) { std::size_t n = 0; for (const ImportedPart& p : m.parts) n += p.mesh.triangles.size() / 3; return n; }
static bool HasNote(const ImportedModel& m, const char* part) { for (const std::string& n : m.notes) if (n.find(part) != std::string::npos) return true; return false; }
static bool Read(const std::string& s, ImportedModel& m, std::string& e) { return ReadLwoBuffer(s, "dir/", m, &e); }


// ---- generated UVs of projected maps: single triangles in file space with hand-computed answers ----
struct P3 { float x, y, z; };
static std::string ProjFile(const std::vector<P3>& pts, const std::vector<std::vector<int>>& polys, int proj, int axis, float ww, float wh, const float* cen = nullptr, const float* siz = nullptr) {
    std::string pn; for (const P3& p : pts) { BF(pn, p.x); BF(pn, p.y); BF(pn, p.z); }
    std::string pl = "FACE"; for (const auto& q : polys) { B16(pl, (unsigned)q.size()); for (int i : q) BVX(pl, (std::uint32_t)i); }
    std::string c = Tags({"S"}) + Layr(0, "L") + Chunk("PNTS", pn) + Chunk("POLS", pl) + PtagSurf(std::vector<unsigned>(polys.size(), 0));
    std::string head; BS0(head, "\x80"); head += Sub("CHAN", "COLR");
    std::string pj; B16(pj, (unsigned)proj); std::string ax; B16(ax, (unsigned)axis);
    std::string blok = Sub("IMAP", head) + Sub("PROJ", pj) + Sub("IMAG", [] { std::string s; BVX(s, 1); return s; }()) + Sub("AXIS", ax);
    std::string w1; BF(w1, ww); B16(w1, 0); blok += Sub("WRPW", w1);
    std::string w2; BF(w2, wh); B16(w2, 0); blok += Sub("WRPH", w2);
    if (cen && siz) {
        std::string tm; std::string a; for (int k = 0; k < 3; ++k) BF(a, cen[k]); B16(a, 0); tm += Sub("CNTR", a);
        std::string b; for (int k = 0; k < 3; ++k) BF(b, siz[k]); B16(b, 0); tm += Sub("SIZE", b);
        blok += Sub("TMAP", tm);
    }
    std::string clip; B32(clip, 1); clip += Sub("STIL", [] { std::string s; BS0(s, "a.tga"); return s; }());
    c += Chunk("CLIP", clip) + Surf2("S", Sub("COLR", F3(1, 1, 1)) + Sub("BLOK", blok));
    return Form("LWO2", c);
}
// the (u, v) of the vertex at file-space point p (the reader negates z); false if no such vertex
static bool UvOf(const ImportedModel& m, const P3& p, double& u, double& v) {
    for (const ImportedPart& pt : m.parts) for (std::size_t i = 0; i < pt.mesh.vertices.size() && i < pt.mesh.uvs.size(); ++i) {
        const Vec3& q = pt.mesh.vertices[i];
        if (Near(q.x, p.x, 1e-3f) && Near(q.y, p.y, 1e-3f) && Near(q.z, -p.z, 1e-3f)) { u = pt.mesh.uvs[i].x; v = 1.0 - pt.mesh.uvs[i].y; return true; }
    }
    return false;
}
static bool UvIs(const ImportedModel& m, const P3& p, double eu, double ev, double tol = 2e-4) {
    double u = 0, v = 0; return UvOf(m, p, u, v) && std::fabs(u - eu) < tol && std::fabs(v - ev) < tol;
}
static bool AllUvFinite(const ImportedModel& m) {
    for (const ImportedPart& pt : m.parts) for (const Vec2& t : pt.mesh.uvs) if (!std::isfinite(t.x) || !std::isfinite(t.y) || std::fabs(t.x) > 1.0001e15f || std::fabs(t.y) > 1.0001e15f) return false;
    return true;
}

static void ProjectionTests() {
    const double kPi = 3.14159265358979323846;
    ImportedModel m; std::string err;
    RUN_SUITE("lwo: generated UVs, hand-computed");
    {   // planar, main axis Z with the map's centre (1, 2, 0) and size (4, 2, 1): u = (x - 1) / 4 + 1/2, v = (y - 2) / 2 + 1/2
        const float cen[3] = {1, 2, 0}, siz[3] = {4, 2, 1};
        const std::vector<P3> pts = {{3, 3, 0}, {1, 2, 5}, {-1, 1, 0}};
        CHECK(Read(ProjFile(pts, {{0, 1, 2}}, 0, 2, 1, 1, cen, siz), m, err) && HasNote(m, "UVs were generated"));
        CHECK(UvIs(m, pts[0], 1.0, 1.0) && UvIs(m, pts[1], 0.5, 0.5) && UvIs(m, pts[2], 0.0, 0.0));
    }
    {   // planar, main axis X: u from the file's z, v from y; centre (0, 1, 1), size (1, 4, 2); u = (z - 1) / 2 + 1/2, v = (y - 1) / 4 + 1/2
        const float cen[3] = {0, 1, 1}, siz[3] = {1, 4, 2};
        const std::vector<P3> pts = {{0, 2, 0}, {0, 1, 1}, {5, -1, 3}};
        CHECK(Read(ProjFile(pts, {{0, 1, 2}}, 0, 0, 1, 1, cen, siz), m, err));
        CHECK(UvIs(m, pts[0], 0.0, 0.75) && UvIs(m, pts[1], 0.5, 0.5) && UvIs(m, pts[2], 1.5, 0.0));
    }
    {   // planar, main axis Y: u from x, v from z; no map centre or size: the box of the points (here x in [0, 2], z in [0, 4])
        const std::vector<P3> pts = {{0, 0, 0}, {2, 5, 4}, {0, 1, 0}};
        CHECK(Read(ProjFile(pts, {{0, 1, 2}}, 0, 1, 1, 1), m, err));
        CHECK(UvIs(m, pts[0], 0.0, 0.0) && UvIs(m, pts[1], 1.0, 1.0) && UvIs(m, pts[2], 0.0, 0.0));
    }
    {   // cylindrical round Y, centre 0, size (1, 2, 1), wrap 2 x 3: lon = atan2(z, x) (the planar pair for axis Y: x then z), t = lon / 2pi + 1/2, v = y / 2 + 1/2
        const float cen[3] = {0, 0, 0}, siz[3] = {1, 2, 1};
        const std::vector<P3> pts = {{0, 1, 1}, {1, 0, 0.5f}, {-1, 0, 0.5f}};   // lon atan2(1, 0), atan2(.5, 1), atan2(.5, -1)
        CHECK(Read(ProjFile(pts, {{0, 1, 2}}, 1, 1, 2, 3, cen, siz), m, err));
        const double t0 = std::atan2(1.0, 0.0) / (2 * kPi) + 0.5, t1 = std::atan2(0.5, 1.0) / (2 * kPi) + 0.5, t2 = std::atan2(0.5, -1.0) / (2 * kPi) + 0.5;
        CHECK(UvIs(m, pts[0], t0 * 2, 1.0 * 3) && UvIs(m, pts[1], t1 * 2, 0.5 * 3) && UvIs(m, pts[2], t2 * 2, 0.5 * 3));
    }
    {   // spherical round Y: a pole corner takes the mean longitude of its triangle; latitude = atan2(y, radius), v = lat / pi + 1/2
        const float cen[3] = {0, 0, 0}, siz[3] = {1, 1, 1};
        const std::vector<P3> pts = {{0, 1, 0}, {0, 0, 1}, {1, 1, 1}};
        CHECK(Read(ProjFile(pts, {{0, 1, 2}}, 2, 1, 1, 1, cen, siz), m, err));
        const double t1 = 0.5 + (kPi / 2) / (2 * kPi), t2 = 0.5 + (kPi / 4) / (2 * kPi), lat2 = std::atan2(1.0, std::sqrt(2.0));
        CHECK(UvIs(m, pts[1], t1, 0.5) && UvIs(m, pts[2], t2, lat2 / kPi + 0.5) && UvIs(m, pts[0], 0.5 * (t1 + t2), 1.0));
        // wrap 4 x 2 repeats the texture round the sphere and from pole to pole
        CHECK(Read(ProjFile(pts, {{0, 1, 2}}, 2, 1, 4, 2, cen, siz), m, err) && UvIs(m, pts[1], t1 * 4, 1.0));
    }
    RUN_SUITE("lwo: generated UVs, the wrap seam");
    {   // a triangle across the longitude seam of a cylinder round Y: corners at t = 0.984, 0.016 and 1.0 (x = -1, z = +-0.1 and 0) must not stretch over the texture
        const float cen[3] = {0, 0, 0}, siz[3] = {1, 1, 1};
        const std::vector<P3> pts = {{-1, 0, 0.1f}, {-1, 0, -0.1f}, {-1, 1, 0}};
        CHECK(Read(ProjFile(pts, {{0, 1, 2}}, 1, 1, 1, 1, cen, siz), m, err));
        double u[3], v[3]; bool ok = true;
        for (int i = 0; i < 3; ++i) ok = UvOf(m, pts[(std::size_t)i], u[i], v[i]) && ok;
        CHECK(ok);
        if (ok) {
            CHECK(std::fabs(u[0] - u[1]) < 0.5 && std::fabs(u[0] - u[2]) < 0.5 && std::fabs(u[1] - u[2]) < 0.5);
            const double lon0 = std::atan2(0.1, -1.0) / (2 * kPi) + 0.5;   // 0.984: the corner at z = -0.1 is moved up by one turn to 1.016
            CHECK(std::fabs(u[0] - lon0) < 2e-4 && std::fabs(u[1] - (2.0 - lon0)) < 2e-4 && std::fabs(u[2] - 1.0) < 2e-4);
        }
        // the same triangle on a sphere
        CHECK(Read(ProjFile(pts, {{0, 1, 2}}, 2, 1, 1, 1, cen, siz), m, err));
        ok = true; for (int i = 0; i < 3; ++i) ok = UvOf(m, pts[(std::size_t)i], u[i], v[i]) && ok;
        CHECK(ok && std::fabs(u[0] - u[1]) < 0.5 && std::fabs(u[0] - u[2]) < 0.5 && std::fabs(u[1] - u[2]) < 0.5);
        // a whole cylinder-mapped cube: no triangle spreads over more than 2/3 of a turn (the most any three points can need)
        CHECK(Read(Box2(true, 1, 2), m, err));
        bool tight = true;
        for (const ImportedPart& p : m.parts) if (!p.mesh.uvs.empty()) for (std::size_t i = 0; i + 2 < p.mesh.triangles.size(); i += 3) {
            double lo = 1e9, hi = -1e9;
            for (int k = 0; k < 3; ++k) { const double uu = p.mesh.uvs[p.mesh.triangles[i + (std::size_t)k]].x; lo = std::min(lo, uu); hi = std::max(hi, uu); }
            if (hi - lo > 0.667) tight = false;
        }
        CHECK(tight);
    }
    RUN_SUITE("lwo: generated UVs, degenerate and hostile input");
    {   // corners on the cylinder's axis have no longitude: they take the one the triangle has
        const float cen[3] = {0, 0, 0}, siz[3] = {1, 1, 1};
        const std::vector<P3> pts = {{0, 0, 0}, {0, 1, 0}, {1, 0, 0}};
        CHECK(Read(ProjFile(pts, {{0, 1, 2}}, 1, 1, 1, 1, cen, siz), m, err));
        CHECK(UvIs(m, pts[0], 0.5, 0.5) && UvIs(m, pts[1], 0.5, 1.5) && UvIs(m, pts[2], 0.5, 0.5));   // lon of (1, 0, 0) round Y: atan2(z = 0, x = 1) = 0
        // an axis number outside 0..2 is read as X; a size of 0 falls back to the box's extent (the centre stays)
        const float zs[3] = {0, 0, 0};
        const std::vector<P3> t3 = {{0, 0, 0}, {2, 2, 0}, {0, 2, 3}};
        CHECK(Read(ProjFile(t3, {{0, 1, 2}}, 0, 7, 1, 1, cen, zs), m, err) && AllUvFinite(m) && UvIs(m, t3[0], 0.5, 0.5) && UvIs(m, t3[1], 0.5, 1.5) && UvIs(m, t3[2], 1.5, 1.5));   // axis X; centre 0 kept, size from the box: u = z / 3 + 1/2, v = y / 2 + 1/2
    }
    {   // NaN, infinite, zero, tiny and huge centres, sizes and wrap amounts, on every projection and axis: every UV stays finite
        const float bad[] = {std::nanf(""), INFINITY, -INFINITY, 0.0f, -0.0f, 1e-30f, 1e30f, -1e30f, 1.0f, 3.5f};
        std::uint32_t seed = 12345u;
        auto rnd = [&seed]() { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
        bool fin = true; int ran = 0;
        for (int it = 0; it < 600; ++it) {
            const float cen[3] = {bad[rnd() % 10], bad[rnd() % 10], bad[rnd() % 10]}, siz[3] = {bad[rnd() % 10], bad[rnd() % 10], bad[rnd() % 10]};
            std::vector<P3> pts; for (int k = 0; k < 3; ++k) pts.push_back({(float)(rnd() % 7) - 3, (float)(rnd() % 7) - 3, (float)(rnd() % 7) - 3});
            if (it % 5 == 0) pts[0] = {1e9f, -1e9f, 5e8f};
            ImportedModel mm; std::string e;
            if (!Read(ProjFile(pts, {{0, 1, 2}}, it % 3, (int)(rnd() % 4), bad[rnd() % 10], bad[rnd() % 10], cen, siz), mm, e)) continue;
            ++ran;
            if (!AllUvFinite(mm)) fin = false;
        }
        CHECK(fin && ran > 100);
    }
}

int main() {
    ProjectionTests();
    ImportedModel m; std::string err;
    RUN_SUITE("lwo2: surfaces, parts, materials");
    {
        CHECK(Read(Box2(false), m, err));
        CHECK(m.backend == "own" && m.parts.size() == 2 && m.materials.size() == 2 && m.nodes.size() == 2 && m.nodes[1].name == "Box" && fx::Sane(m));
        CHECK(Tris(m) == 12 && m.parts[0].mesh.vertices.size() == 12 && m.parts[1].mesh.vertices.size() == 12);   // SMAN 0: flat
        CHECK_NEAR(Volume(m), 1.0, 1e-4);
        float zlo = 1e30f, zhi = -1e30f;   // the file's z in [0, 1] comes out in [-1, 0]: left-handed to right-handed
        for (const ImportedPart& p : m.parts) for (const Vec3& v : p.mesh.vertices) { zlo = std::fmin(zlo, v.z); zhi = std::fmax(zhi, v.z); }
        CHECK(Near(zlo, -1) && Near(zhi, 0));
        const ImportedMaterial& r = m.materials[(std::size_t)m.parts[0].material];
        CHECK(r.name == "Red" && Near(r.diffuse.r, 0.5f) && Near(r.diffuse.g, 0) && Near(r.diffuse.a, 0.75f));   // colour times diffuse, alpha = 1 - transparency
        CHECK(r.hasEmissive && Near(r.emissive.r, 0.1f) && Near(r.shininess, 16.0f, 1e-2f));                   // 2^(10 * 0.2 + 2)
        CHECK(m.materials[(std::size_t)m.parts[1].material].name == "Blue" && m.materials[(std::size_t)m.parts[1].material].texture.empty());
        CHECK(m.notes.size() <= 1);
        // a smoothing angle of 100 degrees (1.75 rad) welds the cube corners: 8 vertices per... 3 faces share each corner
        std::string f = Box2(false); const std::size_t at = f.find("SMAN"); CHECK(at != std::string::npos);
        std::uint32_t big; const float ang = 1.75f; std::memcpy(&big, &ang, 4);
        for (int i = 0; i < 4; ++i) f[at + 6 + (std::size_t)i] = (char)(big >> (8 * (3 - i)));
        CHECK(Read(f, m, err) && m.parts[0].mesh.vertices.size() < 12);
    }

    RUN_SUITE("lwo2: UV maps and image maps");
    {
        CHECK(Read(Box2(true), m, err));
        CHECK(m.parts.size() == 2);
        const ImportedPart& blue = m.parts[1];
        CHECK(m.materials[(std::size_t)blue.material].texture == "dir/Images/blue.tga" && blue.mesh.uvs.size() == blue.mesh.vertices.size());
        bool found = false;   // v is flipped: the corner (1, 1, 1) has uv (1, 1) -> (1, 0)
        for (std::size_t i = 0; i < blue.mesh.vertices.size(); ++i) if (Near(blue.mesh.vertices[i].x, 1) && Near(blue.mesh.vertices[i].y, 1)) { found = true; CHECK(Near(blue.mesh.uvs[i].x, 1) && Near(blue.mesh.uvs[i].y, 0)); }
        CHECK(found);
        // a discontinuous UV (VMAD) overrides the per-point one for its polygon corner
        std::string f = Box2(true);
        std::string vmad = "TXUV"; B16(vmad, 2); BS0(vmad, "uv"); BVX(vmad, 1); BVX(vmad, 3); BF(vmad, 0.25f); BF(vmad, 0.75f);   // point 1 in polygon 3
        f.replace(4, 4, [&] { std::string s; B32(s, (std::uint32_t)(f.size() - 8 + Chunk("VMAD", vmad).size())); return s; }());
        f += Chunk("VMAD", vmad);
        CHECK(Read(f, m, err));
        bool over = false;
        for (std::size_t i = 0; i < m.parts[1].mesh.uvs.size(); ++i) if (Near(m.parts[1].mesh.uvs[i].x, 0.25f) && Near(m.parts[1].mesh.uvs[i].y, 0.25f)) over = true;
        CHECK(over);
    }

    RUN_SUITE("lwo2: layers, polygon kinds, notes");
    {
        // two layers, the second a child of the first
        std::string c = Tags({"S"}) + Layr(1, "Parent") + Pnts(kCube, 8) + Pols2() + PtagSurf({0, 0, 0, 0, 0, 0}) + Layr(2, "Child", 0, 1) + Pnts(kCube, 8) + Pols2() + PtagSurf({0, 0, 0, 0, 0, 0}) + Surf2("S", Sub("COLR", F3(1, 1, 1)));
        CHECK(Read(Form("LWO2", c), m, err) && m.parts.size() == 2 && m.nodes.size() == 3 && m.nodes[2].parent == 1 && m.nodes[1].name == "Parent");
        // hidden layer and pivot
        const float pv[3] = {1, 2, 3};
        c = Tags({"S"}) + Layr(0, "Hid", 1) + Pnts(kCube, 8) + Pols2() + Layr(1, "Vis", 0, -1, pv) + Pnts(kCube, 8) + Pols2() + PtagSurf({0, 0, 0, 0, 0, 0}) + Surf2("S", "");
        CHECK(Read(Form("LWO2", c), m, err) && m.parts.size() == 1 && HasNote(m, "hidden") && HasNote(m, "pivot"));
        // patches are polygons with a note; curves are skipped
        c = Layr(0, "L") + Pnts(kCube, 8) + Pols2("PTCH");
        CHECK(Read(Form("LWO2", c), m, err) && HasNote(m, "patch") && Tris(m) == 12);
        std::string curv = "CURV"; B16(curv, 2); BVX(curv, 0); BVX(curv, 1);
        CHECK(Read(Form("LWO2", Layr(0, "L") + Pnts(kCube, 8) + Pols2() + Chunk("POLS", curv)), m, err) && HasNote(m, "curve"));
        // polygon with a concave outline: a 'C' made of 6 points is triangulated by ear clipping
        const float L6[6][3] = {{0, 0, 0}, {2, 0, 0}, {2, 1, 0}, {1, 1, 0}, {1, 2, 0}, {0, 2, 0}};
        std::string lp = "FACE"; B16(lp, 6); for (int i = 0; i < 6; ++i) BVX(lp, (std::uint32_t)i);
        CHECK(Read(Form("LWO2", Layr(0, "L") + Pnts(L6, 6) + Chunk("POLS", lp)), m, err));
        double area = 0; for (std::size_t i = 0; i + 2 < m.parts[0].mesh.triangles.size(); i += 3) { const auto& v = m.parts[0].mesh.vertices; const Vec3 a = v[(std::size_t)m.parts[0].mesh.triangles[i]], b = v[(std::size_t)m.parts[0].mesh.triangles[i + 1]], cc = v[(std::size_t)m.parts[0].mesh.triangles[i + 2]]; area += 0.5 * Vec3::Cross(b - a, cc - a).Magnitude(); }
        CHECK_NEAR(area, 3.0, 1e-5);
        // envelope and a procedural block are reported; the four-byte VX form (index >= 0xFF00) works
        std::string many = "FACE"; B16(many, 3); BVX(many, 0); BVX(many, 1); BVX(many, 2);
        std::string pts; for (int i = 0; i < 70000; ++i) { BF(pts, (float)i); BF(pts, (float)(i % 7)); BF(pts, (float)(i % 3)); }
        std::string big = "FACE"; B16(big, 3); BVX(big, 0); BVX(big, 65000); BVX(big, 69999);
        float tri[3][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}; (void)tri;
        CHECK(Read(Form("LWO2", Layr(0, "L") + Chunk("PNTS", pts) + Chunk("POLS", big)), m, err) && Tris(m) == 1);
        const std::string shd = Sub("BLOK", [] { std::string h = "SHDR"; B16(h, 0); return h; }());
        CHECK(Read(Form("LWO2", Tags({"S"}) + Layr(0, "L") + Pnts(kCube, 8) + Pols2() + PtagSurf({0, 0, 0, 0, 0, 0}) + Surf2("S", shd + Sub("COLR", F3(1, 1, 1)))), m, err) && HasNote(m, "shader"));
    }

    RUN_SUITE("lwob (LWO1)");
    {
        std::string pn; for (auto& p : kCube) for (int k = 0; k < 3; ++k) BF(pn, p[k]);
        std::string pols; for (int i = 0; i < 6; ++i) { B16(pols, 4); for (int k = 0; k < 4; ++k) B16(pols, (unsigned)kQuads[i][k]); B16(pols, i < 3 ? 1u : 2u); }
        std::string srfs; BS0(srfs, "Wall"); BS0(srfs, "Roof");
        auto surf = [](const char* name, int r, int g, int b, int diff, int gloss) {
            std::string s; BS0(s, name);
            s += Sub("COLR", std::string{(char)r, (char)g, (char)b, 0}); std::string d; B16(d, (unsigned)diff); s += Sub("DIFF", d);
            std::string sp; B16(sp, 256); s += Sub("SPEC", sp); std::string gl; B16(gl, (unsigned)gloss); s += Sub("GLOS", gl);
            std::string tr; B16(tr, 64); s += Sub("TRAN", tr);
            return Chunk("SURF", s);
        };
        const std::string f = Form("LWOB", Chunk("PNTS", pn) + Chunk("SRFS", srfs) + Chunk("POLS", pols) + surf("Wall", 255, 128, 0, 256, 16) + surf("Roof", 0, 0, 255, 128, 256));
        CHECK(Read(f, m, err));
        CHECK(m.parts.size() == 2 && m.materials.size() == 2 && Tris(m) == 12 && fx::Sane(m));
        CHECK_NEAR(Volume(m), 1.0, 1e-4);
        const ImportedMaterial& w = m.materials[(std::size_t)m.parts[0].material];
        CHECK(w.name == "Wall" && Near(w.diffuse.r, 1) && Near(w.diffuse.g, 128 / 255.0f) && Near(w.diffuse.a, 0.75f) && Near(w.shininess, 16));   // 256 = 100 %, transparency 64 = 25 %
        const ImportedMaterial& r = m.materials[(std::size_t)m.parts[1].material];
        CHECK(Near(r.diffuse.b, 0.5f) && Near(r.shininess, 256));
        // a texture image in LWOB is projected: the texture is kept and UVs are generated (planar, main axis X: U from Z, V from Y), with a note
        std::string s2; BS0(s2, "Wall"); s2 += Sub("COLR", std::string{(char)255, 0, 0, 0}); s2 += Sub("CTEX", [] { std::string t; BS0(t, "Planar Image Map"); return t; }()); s2 += Sub("TIMG", [] { std::string t; BS0(t, "wall.iff"); return t; }());
        CHECK(Read(Form("LWOB", Chunk("PNTS", pn) + Chunk("SRFS", srfs) + Chunk("POLS", pols) + Chunk("SURF", s2)), m, err) && HasNote(m, "UVs were generated") && !m.materials.empty() && m.materials[0].texture.find("wall.iff") != std::string::npos);
        const ImportedPart* wp = nullptr;
        for (const ImportedPart& p : m.parts) if (p.material >= 0) wp = &p;
        CHECK(wp != nullptr);
        if (wp) {
            const Mesh& pm = wp->mesh;
            bool uvOk = pm.uvs.size() == pm.vertices.size() && !pm.uvs.empty();
            Vec3 lo, hi; pm.Bounds(lo, hi);
            for (std::size_t i = 0; uvOk && i < pm.vertices.size(); ++i) {   // planar, axis X: U from Z, V from Y, each over the part's own box, with U from the file's z (the engine's z negated); stored as (u, 1 - v)
                const Vec3& v = pm.vertices[i]; const Vec2& t = pm.uvs[i];
                if (!Near(t.x, (hi.z - v.z) / (hi.z - lo.z), 1e-4f) || !Near(t.y, 1.0f - (v.y - lo.y) / (hi.y - lo.y), 1e-4f)) uvOk = false;
            }
            CHECK(uvOk);
        }
        // a negative surface number announces detail polygons, which are skipped
        std::string pd; B16(pd, 3); B16(pd, 0); B16(pd, 1); B16(pd, 2); B16(pd, (unsigned)(-1 & 0xFFFF)); B16(pd, 1); B16(pd, 3); B16(pd, 0); B16(pd, 1); B16(pd, 2);
        CHECK(Read(Form("LWOB", Chunk("PNTS", pn) + Chunk("SRFS", srfs) + Chunk("POLS", pd)), m, err) && Tris(m) == 1 && HasNote(m, "detail"));
    }

    RUN_SUITE("lwo: projected image maps");
    {
        ImportedModel m; std::string err;
        // planar, main axis Y: U from X, V from Z (both over the surface's own box); the texture is listed, a note says UVs were made
        CHECK(Read(Box2(true, 0, 1), m, err) && HasNote(m, "UVs were generated"));
        const ImportedPart* bp = nullptr;
        for (const ImportedPart& p : m.parts) if (p.material >= 0 && m.materials[(std::size_t)p.material].name == "Blue") bp = &p;
        CHECK(bp != nullptr);
        if (bp) {
            CHECK(m.materials[(std::size_t)bp->material].texture.find("blue.tga") != std::string::npos);
            const Mesh& pm = bp->mesh;
            bool ok = pm.uvs.size() == pm.vertices.size() && !pm.uvs.empty();
            for (std::size_t i = 0; ok && i < pm.vertices.size(); ++i) {
                // Blue is the three faces at x = 1, y = 1 and z = 1 ... in the file's z (negated on reading): the box of its vertices is the whole cube
                const Vec3& v = pm.vertices[i]; const Vec2& t = pm.uvs[i];
                if (!Near(t.x, v.x, 1e-4f) || !Near(t.y, 1.0f + v.z, 1e-4f)) ok = false;   // v is the file's z = -z: stored (u, 1 - v) = (x, 1 + z)
            }
            CHECK(ok);
        }
        // cylindrical around Z with wrap amount 2 in U: V is the height along Z, U goes round once (twice with the wrap), all finite
        CHECK(Read(Box2(true, 1, 2, 2.0f, 1.0f), m, err) && HasNote(m, "UVs were generated"));
        bool fin = true; float maxU = 0;
        for (const ImportedPart& p : m.parts) for (const Vec2& t : p.mesh.uvs) { if (!std::isfinite(t.x) || !std::isfinite(t.y)) fin = false; maxU = std::max(maxU, t.x); }
        CHECK(fin && maxU > 1.0f && maxU <= 3.4f);   // at most 2 (one turn, wrap 2) plus the up-to-2/3-turn shift of a triangle across the seam
        // spherical: finite UVs, V in 0..1, U in 0..1 or a little above for a triangle moved across the seam
        CHECK(Read(Box2(true, 2, 1), m, err));
        bool in01 = true;
        for (const ImportedPart& p : m.parts) for (const Vec2& t : p.mesh.uvs) if (!(t.x >= -1e-4f && t.x <= 1.5f && t.y >= -1e-4f && t.y <= 1.0001f)) in01 = false;
        CHECK(in01);
        // cubic projection: the texture is kept, no UVs, a note
        CHECK(Read(Box2(true, 3), m, err) && HasNote(m, "cubic or front"));
        bool anyUv = false, tex = false;
        for (const ImportedPart& p : m.parts) { if (!p.mesh.uvs.empty()) anyUv = true; if (p.material >= 0 && !m.materials[(std::size_t)p.material].texture.empty()) tex = true; }
        CHECK(!anyUv && tex);
    }

    RUN_SUITE("lwo: refused files");
    {
        CHECK(!Read("", m, err) && !err.empty());
        CHECK(!Read("FORM", m, err));
        CHECK(!Read("not an lwo file at all", m, err) && err.find("FORM") != std::string::npos);
        CHECK(!Read(Form("LWOX", ""), m, err) && err.find("LWOX") != std::string::npos);
        CHECK(!Read(Form("LWO2", Layr(0, "L")), m, err) && err.find("no polygons") != std::string::npos);
        float nanp[1][3] = {{std::nanf(""), 0, 0}};
        CHECK(!Read(Form("LWO2", Layr(0, "L") + Pnts(nanp, 1) + Chunk("POLS", "FACE")), m, err) && err.find("finite") != std::string::npos);
        float hugep[1][3] = {{1e20f, 0, 0}};
        CHECK(!Read(Form("LWO2", Layr(0, "L") + Pnts(hugep, 1)), m, err));
        // an index past the points drops that polygon only
        std::string bad = "FACE"; B16(bad, 3); BVX(bad, 0); BVX(bad, 1); BVX(bad, 9); B16(bad, 3); BVX(bad, 0); BVX(bad, 1); BVX(bad, 2);
        const float t3[3][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        CHECK(Read(Form("LWO2", Layr(0, "L") + Pnts(t3, 3) + Chunk("POLS", bad)), m, err) && Tris(m) == 1 && HasNote(m, "out-of-range"));
        // a chunk longer than the file is read as far as it goes
        std::string trunc = Form("LWO2", Layr(0, "L") + Pnts(t3, 3) + Chunk("POLS", bad));
        CHECK(Read(trunc.substr(0, trunc.size() - 3), m, err) || !err.empty());
    }

#ifndef OKAY_STANDALONE_TEST
    RUN_SUITE("lwo: the import seam");
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / fs::u8path("okay_lwo_m\xC3\xBCller");
        fs::create_directories(dir);
        const fs::path file = dir / fs::u8path("bo\xC3\xAEte.lwo");
        { std::ofstream f(file, std::ios::binary); const std::string x = Box2(true); f.write(x.data(), (std::streamsize)x.size()); }
        const std::string path = file.u8string();
        CHECK(HasOwnReader(".lwo"));
        CHECK(ImportModelData(path, m, ImportBackend::Own, false, &err) && m.backend == "own" && m.parts.size() == 2);
        SetImportBackend(".lwo", ImportBackend::Default);
        unsetenv("OKAY_OWN_LWO");
        if (EffectiveImportBackend(".lwo") != ImportBackend::Own) CHECK(!ImportModelData(path, m, ImportBackend::Default, false, &err) && err.find("OKAY_OWN_LWO") != std::string::npos);
        setenv("OKAY_OWN_LWO", "1", 1);
        CHECK(EffectiveImportBackend(".lwo") == ImportBackend::Own && ImportableExtensions().find(".lwo") != std::string::npos);
        bool ok = false; std::string tex;
        Mesh merged = ImportModel(path, &ok, &tex);
        CHECK(ok && merged.TriangleCount() == 12 && !tex.empty());
        setenv("OKAY_OWN_LWO", "0", 1);
        CHECK(EffectiveImportBackend(".lwo") != ImportBackend::Own);
        unsetenv("OKAY_OWN_LWO");
        std::error_code ec; fs::remove_all(dir, ec);
    }
#endif

    TEST_MAIN_RESULT();
}
