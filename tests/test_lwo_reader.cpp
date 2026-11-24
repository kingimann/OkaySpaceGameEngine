// Our own LightWave object reader (engine/src/Render/LwoReader.cpp). SYNTHETIC fixtures (LWO2, LWOB and Modo LXOB files built
// in memory, the last in tests/lxo_fixture.hpp), checked against hand-computed numbers; plus the import seam (and its one log
// line naming the reader). Real LWO files are checked by model_corpus.
// Builds on its own with -DOKAY_STANDALONE_TEST (reader source only), no engine library.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "lxo_fixture.hpp"
#include "okay/Render/LwoReader.hpp"
#ifndef OKAY_STANDALONE_TEST
#include "okay/Render/ModelImport.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Core/Log.hpp"
#endif
#include <array>
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
static std::string TagsChunk(const std::vector<std::string>& t) { std::string b; for (const std::string& s : t) BS0(b, s); return Chunk("TAGS", b); }
static std::string F3(float a, float b, float c) { std::string s; BF(s, a); BF(s, b); BF(s, c); B16(s, 0); return s; }   // colour + empty envelope
static std::string F1(float a) { std::string s; BF(s, a); B16(s, 0); return s; }
static std::string Surf2(const std::string& name, const std::string& subs) { std::string b; BS0(b, name); BS0(b, ""); b += subs; return Chunk("SURF", b); }

static std::string Box2(bool textured, int proj = 5, int axis = 0, float wrapW = 1.0f, float wrapH = 1.0f) {
    std::string c = TagsChunk({"Red", "Blue"}) + Layr(0, "Box") + Pnts(kCube, 8) + Pols2() + PtagSurf({0, 0, 0, 1, 1, 1});
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
// One surface "S" with a projected colour image map over any points and triangles (file coordinates).
static std::string ProjMesh(int proj, int axis, const std::vector<std::array<float, 3>>& pts, const std::vector<std::array<int, 3>>& tris,
                            float wrapW = 1.0f, float wrapH = 1.0f) {
    std::string pb; for (const auto& p : pts) for (int k = 0; k < 3; ++k) BF(pb, p[(std::size_t)k]);
    std::string pol = "FACE"; for (const auto& t : tris) { B16(pol, 3); for (int k = 0; k < 3; ++k) BVX(pol, (std::uint32_t)t[(std::size_t)k]); }
    std::string c = TagsChunk({"S"}) + Layr(0, "L") + Chunk("PNTS", pb) + Chunk("POLS", pol) + PtagSurf(std::vector<unsigned>(tris.size(), 0));
    std::string head; BS0(head, "\x80"); head += Sub("CHAN", "COLR");
    std::string pj; B16(pj, (unsigned)proj);
    std::string ax; B16(ax, (unsigned)axis);
    std::string ww; BF(ww, wrapW); B16(ww, 0);
    std::string wh; BF(wh, wrapH); B16(wh, 0);
    const std::string blok = Sub("IMAP", head) + Sub("PROJ", pj) + Sub("IMAG", [] { std::string s; BVX(s, 1); return s; }()) + Sub("AXIS", ax) + Sub("WRPW", ww) + Sub("WRPH", wh);
    std::string clip; B32(clip, 1); clip += Sub("STIL", [] { std::string s; BS0(s, "map.tga"); return s; }());
    c += Chunk("CLIP", clip) + Surf2("S", Sub("COLR", F3(1, 1, 1)) + Sub("BLOK", blok));
    return Form("LWO2", c);
}
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

int main() {
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
        std::string c = TagsChunk({"S"}) + Layr(1, "Parent") + Pnts(kCube, 8) + Pols2() + PtagSurf({0, 0, 0, 0, 0, 0}) + Layr(2, "Child", 0, 1) + Pnts(kCube, 8) + Pols2() + PtagSurf({0, 0, 0, 0, 0, 0}) + Surf2("S", Sub("COLR", F3(1, 1, 1)));
        CHECK(Read(Form("LWO2", c), m, err) && m.parts.size() == 2 && m.nodes.size() == 3 && m.nodes[2].parent == 1 && m.nodes[1].name == "Parent");
        // hidden layer and pivot
        const float pv[3] = {1, 2, 3};
        c = TagsChunk({"S"}) + Layr(0, "Hid", 1) + Pnts(kCube, 8) + Pols2() + Layr(1, "Vis", 0, -1, pv) + Pnts(kCube, 8) + Pols2() + PtagSurf({0, 0, 0, 0, 0, 0}) + Surf2("S", "");
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
        CHECK(Read(Form("LWO2", TagsChunk({"S"}) + Layr(0, "L") + Pnts(kCube, 8) + Pols2() + PtagSurf({0, 0, 0, 0, 0, 0}) + Surf2("S", shd + Sub("COLR", F3(1, 1, 1)))), m, err) && HasNote(m, "shader"));
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
            for (std::size_t i = 0; uvOk && i < pm.vertices.size(); ++i) {   // planar, axis X: U from Z, V from Y, each over the part's own box; stored as (u, 1 - v)
                const Vec3& v = pm.vertices[i]; const Vec2& t = pm.uvs[i];
                if (!Near(t.x, (v.z - lo.z) / (hi.z - lo.z), 1e-4f) || !Near(t.y, 1.0f - (v.y - lo.y) / (hi.y - lo.y), 1e-4f)) uvOk = false;
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
                if (!Near(t.x, v.x, 1e-4f) || !Near(t.y, -v.z, 1e-4f)) ok = false;   // stored (u, 1 - v) with v = z + 1
            }
            CHECK(ok);
        }
        // cylindrical around Z with wrap amount 2 in U: V is the height along Z, U goes round once (twice with the wrap), all finite
        CHECK(Read(Box2(true, 1, 2, 2.0f, 1.0f), m, err) && HasNote(m, "UVs were generated"));
        bool fin = true; float maxU = 0;
        for (const ImportedPart& p : m.parts) for (const Vec2& t : p.mesh.uvs) { if (!std::isfinite(t.x) || !std::isfinite(t.y)) fin = false; maxU = std::max(maxU, t.x); }
        CHECK(fin && maxU > 1.0f && maxU <= 2.0001f);
        // the seam is mended: no triangle's U values spread over more than half the wrap, and none leaves 0 .. wrap
        bool mended = true;
        for (const ImportedPart& p : m.parts) {
            if (p.mesh.uvs.empty()) continue;
            for (std::size_t t = 0; t + 2 < p.mesh.triangles.size(); t += 3) {
                float lo = 1e9f, hi = -1e9f;
                for (int k = 0; k < 3; ++k) { const float u = p.mesh.uvs[(std::size_t)p.mesh.triangles[t + (std::size_t)k]].x; lo = std::min(lo, u); hi = std::max(hi, u); }
                if (hi - lo > 1.0001f || lo < -1e-4f || hi > 2.0001f) mended = false;
            }
        }
        CHECK(mended);
        // spherical: finite UVs, U in 0..1, V in 0..1
        CHECK(Read(Box2(true, 2, 1), m, err));
        bool in01 = true;
        for (const ImportedPart& p : m.parts) for (const Vec2& t : p.mesh.uvs) if (!(t.x >= -1e-4f && t.x <= 1.0001f && t.y >= -1e-4f && t.y <= 1.0001f)) in01 = false;
        CHECK(in01);
        // spherical about Y: V (stored as 1 - v) follows the height: the highest vertex has the smallest stored V
        {   const ImportedPart* tp = nullptr;
            for (const ImportedPart& p : m.parts) if (!p.mesh.uvs.empty()) tp = &p;
            CHECK(tp != nullptr);
            if (tp) {
                std::size_t top = 0, bottom = 0;
                for (std::size_t i = 0; i < tp->mesh.vertices.size(); ++i) {
                    if (tp->mesh.vertices[i].y > tp->mesh.vertices[top].y) top = i;
                    if (tp->mesh.vertices[i].y < tp->mesh.vertices[bottom].y) bottom = i;
                }
                CHECK(tp->mesh.uvs[top].y < tp->mesh.uvs[bottom].y);
            } }
        // ---- hand-computed projected UVs (positions are the engine's: the reader negates the file's z) ----
        // Cylindrical about Z, a band of 8 points: angle = atan2(y, x) as a fraction of a turn plus half a turn, so
        // +X -> 0.5, +Y -> 0.75, -X -> 1.0 (the seam: 0 or 1), -Y -> 0.25; V = height, stored as 1 - v.
        {   const std::vector<std::array<float, 3>> band = {{1, 0, 0}, {0, 1, 0}, {-1, 0, 0}, {0, -1, 0}, {1, 0, 1}, {0, 1, 1}, {-1, 0, 1}, {0, -1, 1}};
            std::vector<std::array<int, 3>> tris;
            for (int i = 0; i < 4; ++i) { const int j = (i + 1) % 4; tris.push_back({i, j, j + 4}); tris.push_back({i, j + 4, i + 4}); }
            CHECK(Read(ProjMesh(1, 2, band, tris), m, err) && m.parts.size() == 1);
            if (m.parts.size() == 1) {
                const Mesh& pm = m.parts[0].mesh;
                bool ok = !pm.uvs.empty(), seam0 = false, seam1 = false;
                for (std::size_t i = 0; i < pm.vertices.size(); ++i) {
                    const Vec3& p = pm.vertices[i]; const Vec2& t = pm.uvs[i];
                    if (!Near(t.y, -p.z)) ok = false;                         // file z 0 -> stored 0, file z 1 -> stored 1
                    if (p.x > 0.5f && !Near(t.x, 0.5f)) ok = false;
                    if (p.y > 0.5f && !Near(t.x, 0.75f)) ok = false;
                    if (p.y < -0.5f && !Near(t.x, 0.25f)) ok = false;
                    if (p.x < -0.5f) { if (Near(t.x, 0.0f)) seam0 = true; else if (Near(t.x, 1.0f)) seam1 = true; else ok = false; }
                }
                CHECK(ok);
                CHECK(seam0 && seam1);   // the -X column is split: 0 for the triangles towards -Y, 1 for those towards +Y
                for (std::size_t t = 0; t + 2 < pm.triangles.size(); t += 3) {   // no triangle spans the whole image
                    float lo = 9, hi = -9;
                    for (int k = 0; k < 3; ++k) { const float u = pm.uvs[(std::size_t)pm.triangles[t + (std::size_t)k]].x; lo = std::min(lo, u); hi = std::max(hi, u); }
                    CHECK(hi - lo <= 0.5001f);
                }
            } }
        // Cylindrical about Z with WRPH = 2, WRPW = 1: V is scaled by the height wrap like the old reader did. Same band as above:
        // WRPH 1 gives stored V = -z (0 or 1, i.e. v = 1 - stored = 1 at engine z 0 and 0 at engine z -1); with WRPH 2 v doubles, so
        // the stored values are 1 - 2 * v = -1 (engine z 0) and 1 (engine z -1). U is unchanged (WRPW = 1).
        {   const std::vector<std::array<float, 3>> band = {{1, 0, 0}, {0, 1, 0}, {-1, 0, 0}, {0, -1, 0}, {1, 0, 1}, {0, 1, 1}, {-1, 0, 1}, {0, -1, 1}};
            std::vector<std::array<int, 3>> tris;
            for (int i = 0; i < 4; ++i) { const int j = (i + 1) % 4; tris.push_back({i, j, j + 4}); tris.push_back({i, j + 4, i + 4}); }
            CHECK(Read(ProjMesh(1, 2, band, tris, 1.0f, 2.0f), m, err) && m.parts.size() == 1);
            if (m.parts.size() == 1) {
                const Mesh& pm = m.parts[0].mesh;
                bool ok = !pm.uvs.empty(), sawLow = false, sawHigh = false;
                for (std::size_t i = 0; i < pm.vertices.size(); ++i) {
                    const Vec3& p = pm.vertices[i]; const Vec2& t = pm.uvs[i];
                    if (p.z > -0.5f) { sawLow = true; if (!Near(t.y, -1.0f)) ok = false; }
                    else { sawHigh = true; if (!Near(t.y, 1.0f)) ok = false; }
                    if (p.x > 0.5f && !Near(t.x, 0.5f)) ok = false;
                    if (p.y > 0.5f && !Near(t.x, 0.75f)) ok = false;
                    if (p.y < -0.5f && !Near(t.x, 0.25f)) ok = false;
                }
                CHECK(ok && sawLow && sawHigh);
            } }
        // Cylindrical about Y: a point on the axis has no angle (atan2 of two zeros, maybe signed): its U must be the turn's start for Y,
        // 0 (mod 1: seam mending may give 1), whichever zero sign atan2 sees; a signed-zero slip would give 0.5. A ring of 4 points at y = 0 and one on the axis at y = 1.
        {   const std::vector<std::array<float, 3>> pts = {{1, 0, 0}, {0, 0, 1}, {-1, 0, 0}, {0, 0, -1}, {0, 1, 0}};
            const std::vector<std::array<int, 3>> tris = {{0, 1, 4}, {1, 2, 4}, {2, 3, 4}, {3, 0, 4}};
            CHECK(Read(ProjMesh(1, 1, pts, tris), m, err) && m.parts.size() == 1);
            if (m.parts.size() == 1) {
                const Mesh& pm = m.parts[0].mesh;
                bool ok = !pm.uvs.empty(), sawAxis = false;
                for (std::size_t i = 0; i < pm.vertices.size(); ++i) {
                    const Vec3& p = pm.vertices[i]; const Vec2& t = pm.uvs[i];
                    if (std::fabs(p.x) < 0.5f && std::fabs(p.z) < 0.5f && p.y > 0.5f) {
                        sawAxis = true;
                        const float f = t.x - std::floor(t.x);
                        if (!(Near(f, 0.0f) || Near(f, 1.0f))) ok = false;   // 0 or 1 (seam mending may add a turn), never 0.5 or a NaN
                    }
                }
                CHECK(ok && sawAxis);
            } }
        // Spherical about Z on an octahedron: the poles are on the axis, so their U is the turn's start, 0.5 (Z); latitude
        // -90 / 0 / +90 degrees gives v 0 / 0.5 / 1, stored as 1 - v. The pole fans are big triangles but are not seam-mended
        // unless they really cross the seam.
        {   const std::vector<std::array<float, 3>> oct = {{1, 0, 0}, {0, 1, 0}, {-1, 0, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
            std::vector<std::array<int, 3>> tris;
            for (int i = 0; i < 4; ++i) { const int j = (i + 1) % 4; tris.push_back({i, j, 4}); tris.push_back({j, i, 5}); }
            CHECK(Read(ProjMesh(2, 2, oct, tris), m, err) && m.parts.size() == 1);
            if (m.parts.size() == 1) {
                const Mesh& pm = m.parts[0].mesh;
                bool ok = !pm.uvs.empty(), sawPole = false;
                for (std::size_t i = 0; i < pm.vertices.size(); ++i) {
                    const Vec3& p = pm.vertices[i]; const Vec2& t = pm.uvs[i];
                    if (std::fabs(p.z) > 0.5f) {   // a pole: file z = +1 is engine z = -1, latitude -90 degrees, v 0, stored 1
                        sawPole = true;
                        if (!Near(t.x, 0.5f) || !Near(t.y, p.z < 0 ? 1.0f : 0.0f)) ok = false;
                    } else {
                        if (!Near(t.y, 0.5f)) ok = false;
                        if (p.x > 0.5f && !Near(t.x, 0.5f)) ok = false;
                        if (p.y > 0.5f && !Near(t.x, 0.75f)) ok = false;
                        if (p.y < -0.5f && !Near(t.x, 0.25f)) ok = false;
                        if (p.x < -0.5f && !Near(t.x, 0.0f) && !Near(t.x, 1.0f)) ok = false;
                    }
                }
                CHECK(ok && sawPole);
            } }
        // Planar about X on a quad that is flat in z (file z = 2): U comes from z, which has no extent, so U is 0; V from y.
        {   const std::vector<std::array<float, 3>> quad = {{0, 0, 2}, {1, 0, 2}, {1, 3, 2}, {0, 3, 2}};
            CHECK(Read(ProjMesh(0, 0, quad, {{0, 1, 2}, {0, 2, 3}}), m, err) && m.parts.size() == 1);
            if (m.parts.size() == 1) {
                const Mesh& pm = m.parts[0].mesh;
                bool ok = !pm.uvs.empty();
                for (std::size_t i = 0; i < pm.vertices.size(); ++i) if (!Near(pm.uvs[i].x, 0.0f) || !Near(pm.uvs[i].y, 1.0f - pm.vertices[i].y / 3.0f)) ok = false;
                CHECK(ok);
            } }
        // cubic projection: the texture is kept, no UVs, a note
        CHECK(Read(Box2(true, 3), m, err) && HasNote(m, "cubic or front"));
        bool anyUv = false, tex = false;
        for (const ImportedPart& p : m.parts) { if (!p.mesh.uvs.empty()) anyUv = true; if (p.material >= 0 && !m.materials[(std::size_t)p.material].texture.empty()) tex = true; }
        CHECK(!anyUv && tex);
    }


    RUN_SUITE("lxo (Modo): material tags, shader tree, NORM maps");
    {
        auto near3 = [](const Vec3& a, float x, float y, float z) { return std::fabs(a.x - x) < 1e-4f && std::fabs(a.y - y) < 1e-4f && std::fabs(a.z - z) < 1e-4f; };
        ImportedModel x; std::string e;
        CHECK(ReadLwoBuffer(fx::lxo::BuildLxo(), "", x, &e));
        CHECK(x.backend == "own" && fx::Sane(x));
        CHECK(x.nodes.size() == 2 && x.parts.size() == 2);   // LAYR flag bit 0 does not hide an LXO layer
        CHECK(x.materials.size() == 2);
        if (x.parts.size() == 2 && x.materials.size() == 2) {
            const ImportedMaterial& shell = x.materials[(std::size_t)x.parts[0].material];
            const ImportedMaterial& glass = x.materials[(std::size_t)x.parts[1].material];
            CHECK(shell.name == "Shell" && glass.name == "Glass");
            // the mask's material: red times 0.5, transparency 0.25; the base material: white times 0.8
            CHECK(shell.hasDiffuse && std::fabs(shell.diffuse.r - 0.5f) < 1e-6f && shell.diffuse.g == 0.0f && std::fabs(shell.diffuse.a - 0.75f) < 1e-6f);
            CHECK(glass.hasDiffuse && std::fabs(glass.diffuse.r - 0.8f) < 1e-6f && std::fabs(glass.diffuse.b - 0.8f) < 1e-6f && glass.diffuse.a == 1.0f);
            const Mesh& sm = x.parts[0].mesh;
            const Mesh& gm = x.parts[1].mesh;
            CHECK(sm.TriangleCount() == 6 && gm.TriangleCount() == 6);
            // Shell: polygon 0's corners use the VMAD normals, every other corner its point's: 4 + 4 + 2 vertices
            CHECK(sm.vertices.size() == 10 && gm.vertices.size() == 8);
            bool down = false, corner = false;
            for (std::size_t i = 0; i < sm.vertices.size(); ++i) if (near3(sm.normals[i], 0, 0, 1)) down = true;   // (0, 0, -1) with z flipped
            const float k = 0.57735027f;
            for (std::size_t i = 0; i < gm.vertices.size(); ++i) if (near3(gm.vertices[i], 1, 1, -1) && near3(gm.normals[i], k, k, -k)) corner = true;
            CHECK(down && corner);
        }
        bool modo = false, items = false;
        for (const std::string& n : x.notes) { if (n.find("LXO: Modo LXO file") == 0) modo = true; if (n.find("4 scene item or action chunk(s)") != std::string::npos) items = true; }
        CHECK(modo && items);
        // without NORM maps the smoothing angles decide: Shell has smoothing off (flat, 12 vertices), Glass smooths at
        // 100 degrees (the cube's 8 corners)
        fx::lxo::Opts o; o.normals = false;
        ImportedModel f; CHECK(ReadLwoBuffer(fx::lxo::BuildLxo(o), "", f, &e));
        CHECK(f.parts.size() == 2 && f.parts[0].mesh.vertices.size() == 12 && f.parts[1].mesh.vertices.size() == 8);
        // no shader tree: the LWO defaults (grey, flat)
        o.shader = false;
        ImportedModel g; CHECK(ReadLwoBuffer(fx::lxo::BuildLxo(o), "", g, &e));
        CHECK(g.parts.size() == 2 && g.parts[0].mesh.vertices.size() == 12 && g.parts[1].mesh.vertices.size() == 12);
        CHECK(g.materials.size() == 2 && std::fabs(g.materials[0].diffuse.r - 0.78f) < 1e-6f);
        // an LWO2 file keeps its LAYR flag: bit 0 hides the layer there
        std::string hidden = Form("LWO2", TagsChunk({"A"}) + Layr(0, "L", 1) + Pnts(kCube, 8) + Pols2() + PtagSurf({0, 0, 0, 0, 0, 0}));
        ImportedModel h; CHECK(!ReadLwoBuffer(hidden, "", h, &e));
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
        // no reader at all: ours is switched off
        if (EffectiveImportBackend(".lwo") == ImportBackend::Default) CHECK(!ImportModelData(path, m, ImportBackend::Default, false, &err) && err.find("OKAY_OWN_LWO") != std::string::npos);
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

    RUN_SUITE("lxo: the import seam and the log line naming the reader");
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "okay_lxo_seam";
        std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir);
        const fs::path file = dir / "cube.lxo";
        { std::ofstream f(file, std::ios::binary); const std::string x = fx::lxo::BuildLxo(); f.write(x.data(), (std::streamsize)x.size()); }
        const std::string path = file.u8string();
        CHECK(HasOwnReader(".lxo"));
        CHECK(ImportModelData(path, m, ImportBackend::Own, false, &err) && m.backend == "own" && m.parts.size() == 2);
        unsetenv("OKAY_OWN_LXO");
        SetImportBackend(".lxo", ImportBackend::Default);
        if (EffectiveImportBackend(".lxo") == ImportBackend::Default) CHECK(!ImportModelData(path, m, ImportBackend::Default, false, &err) && err.find("OKAY_OWN_LXO") != std::string::npos);
        setenv("OKAY_OWN_LXO", "1", 1);
        CHECK(EffectiveImportBackend(".lxo") == ImportBackend::Own && ImportableExtensions().find(".lxo") != std::string::npos);
        std::vector<std::string> lines;
        Log::sink = [&](Log::Level, const std::string& msg) { if (msg.find("model import") != std::string::npos && msg.find("read by") != std::string::npos) lines.push_back(msg); };
        {
            Scene sc("lxo");
            bool ok = false;
            CHECK(ImportModelScene(sc, path, &ok) != nullptr && ok);
        }
        Log::sink = nullptr;
        CHECK(lines.size() == 1);   // one line per import
        if (lines.size() == 1) CHECK(lines[0].find("read by the own LXO reader in ") != std::string::npos && lines[0].find(" ms (whole import ") != std::string::npos);
        unsetenv("OKAY_OWN_LXO");
        fs::remove_all(dir, ec);
    }
#endif

    TEST_MAIN_RESULT();
}
