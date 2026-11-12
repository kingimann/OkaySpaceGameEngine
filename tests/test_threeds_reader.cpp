// Our own Autodesk 3DS (.3ds) reader and its place in the import seam: fixtures written by hand
// (tests/data/models/*.3ds, made by make_3ds_fixtures.py), chunk-level malformed input, units and axes,
// materials and texture tiling, smoothing groups, the object matrix, the "say what was not read" notes,
// the opt-in backend switch, and (with OKAY_USE_ASSIMP) Assimp's reading of the same files compared with ours.
// The reader is NOT READY and off by default; this suite switches it on for itself only.
#include "test_framework.hpp"
// Braced lists inside a CHECK contain commas: take them all as the condition.
#undef CHECK
#define CHECK(...)                                                                                     \
    do {                                                                                               \
        ++::okaytest::g_checks;                                                                        \
        if (!(__VA_ARGS__)) {                                                                          \
            ++::okaytest::g_failures;                                                                  \
            std::cerr << "  FAIL: " << #__VA_ARGS__ << "  (" << __FILE__ << ':' << __LINE__ << ")\n";   \
        }                                                                                              \
    } while (0)
#include <Okay.hpp>
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace okay;

static const std::string kDir = std::string(OKAY_SOURCE_DIR) + "/tests/data/models/";

static std::string Slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static bool HasNote(const ImportedModel& m, const char* needle) {
    for (const std::string& n : m.notes) if (n.find(needle) != std::string::npos) return true;
    return false;
}
static bool Near3(const Vec3& a, float x, float y, float z, float eps = 1e-4f) {
    return std::fabs(a.x - x) < eps && std::fabs(a.y - y) < eps && std::fabs(a.z - z) < eps;
}
static Vec3 Apply(const Mat4& m, const Vec3& p) {
    return Vec3{m.at(0, 0) * p.x + m.at(1, 0) * p.y + m.at(2, 0) * p.z + m.at(3, 0),
                m.at(0, 1) * p.x + m.at(1, 1) * p.y + m.at(2, 1) * p.z + m.at(3, 1),
                m.at(0, 2) * p.x + m.at(1, 2) * p.y + m.at(2, 2) * p.z + m.at(3, 2)};
}
static Mat4 WorldOf(const ImportedModel& m, int node) {
    Mat4 w;
    std::vector<int> chain;
    for (int n = node; n >= 0; n = m.nodes[(std::size_t)n].parent) chain.push_back(n);
    for (std::size_t i = chain.size(); i-- > 0;) {
        const ImportedNode& nd = m.nodes[(std::size_t)chain[i]];
        w = w * Mat4::TRS(nd.position, nd.rotation, nd.scale);
    }
    return w;
}
static bool HasWorldPoint(const ImportedModel& m, const Vec3& p, float eps = 1e-4f) {
    for (const ImportedPart& pt : m.parts) {
        const Mat4 w = WorldOf(m, pt.node);
        for (const Vec3& v : pt.mesh.vertices) { const Vec3 q = Apply(w, v); if (Near3(q, p.x, p.y, p.z, eps)) return true; }
    }
    return false;
}

// ---- A tiny chunk writer for the malformed-input cases ----
static std::string U16(unsigned v) { std::string s(2, 0); s[0] = (char)(v & 255); s[1] = (char)(v >> 8); return s; }
static std::string U32(std::uint32_t v) { std::string s(4, 0); for (int i = 0; i < 4; ++i) s[(std::size_t)i] = (char)(v >> (8 * i)); return s; }
static std::string F32(float f) { std::uint32_t u; std::memcpy(&u, &f, 4); return U32(u); }
static std::string Chunk(unsigned id, const std::string& payload) { return U16(id) + U32((std::uint32_t)(6 + payload.size())) + payload; }
static std::string Verts(const std::vector<Vec3>& v) { std::string d = U16((unsigned)v.size()); for (const Vec3& p : v) d += F32(p.x) + F32(p.y) + F32(p.z); return Chunk(0x4110, d); }
static std::string Faces(const std::vector<std::array<unsigned, 3>>& f, const std::string& sub = "") {
    std::string d = U16((unsigned)f.size());
    for (const auto& t : f) d += U16(t[0]) + U16(t[1]) + U16(t[2]) + U16(7);
    return Chunk(0x4120, d + sub);
}
static std::string Object(const std::string& name, const std::string& meshBody) { return Chunk(0x4000, name + std::string(1, '\0') + Chunk(0x4100, meshBody)); }
static std::string Main(const std::string& editorBody) { return Chunk(0x4D4D, Chunk(0x3D3D, editorBody)); }
static const std::vector<Vec3> kTri = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
static std::string GoodTri() { return Main(Object("T", Verts(kTri) + Faces({{0, 1, 2}}))); }

static bool Reads(const std::string& bytes, ImportedModel& m, std::string* err = nullptr, bool flatten = false) {
    std::string e; const bool ok = Read3dsBuffer(bytes, "", m, err ? err : &e, flatten);
    if (ok) { if (m.parts.empty()) return false; }
    else if (!m.parts.empty() || !m.nodes.empty()) return false;   // a failure leaves nothing behind
    return ok;
}

int main() {
    RUN_SUITE("threeds_reader");

    // ---------------- one_triangle.3ds: units and axes ----------------
    {
        ImportedModel m; std::string err;
        CHECK(Read3dsFile(kDir + "one_triangle.3ds", m, &err));
        CHECK(m.backend == "own");
        CHECK(m.notes.empty());
        CHECK(m.parts.size() == 1 && m.nodes.size() == 2);
        if (m.parts.size() == 1) {
            const Mesh& me = m.parts[0].mesh;
            CHECK(me.vertices.size() == 3 && me.TriangleCount() == 1 && me.HasNormals());
            CHECK(m.parts[0].material == -1);
            CHECK(me.name == "Tri");
            // Z-up file, Y-up engine: file (x, y, z) -> (x, z, -y). The triangle lies on the ground and faces up.
            CHECK(HasWorldPoint(m, Vec3{0, 0, 0}) && HasWorldPoint(m, Vec3{2, 0, 0}) && HasWorldPoint(m, Vec3{0, 0, -2}));
            const Mat4 w = WorldOf(m, m.parts[0].node);
            const Vec3 up = Vec3{w.at(0, 0) * me.normals[0].x + w.at(1, 0) * me.normals[0].y + w.at(2, 0) * me.normals[0].z,
                                 w.at(0, 1) * me.normals[0].x + w.at(1, 1) * me.normals[0].y + w.at(2, 1) * me.normals[0].z,
                                 w.at(0, 2) * me.normals[0].x + w.at(1, 2) * me.normals[0].y + w.at(2, 2) * me.normals[0].z};
            CHECK_NEAR(up.y, 1.0, 1e-4);
        }
        // Flattened: one root node, transforms baked (the same world points), a face that still faces up.
        ImportedModel f;
        CHECK(Read3dsFile(kDir + "one_triangle.3ds", f, &err, true));
        CHECK(f.nodes.size() == 1 && f.parts.size() == 1 && f.parts[0].node == 0);
        CHECK(Near3(f.parts[0].mesh.vertices[2], 0, 0, -2));
        CHECK_NEAR(f.parts[0].mesh.normals[0].y, 1.0, 1e-4);
        const Mesh& fm = f.parts[0].mesh;
        const Vec3 fnrm = Vec3::Cross(fm.vertices[(std::size_t)fm.triangles[1]] - fm.vertices[(std::size_t)fm.triangles[0]],
                                      fm.vertices[(std::size_t)fm.triangles[2]] - fm.vertices[(std::size_t)fm.triangles[0]]);
        CHECK(fnrm.y > 0.0f);   // winding is kept: counter-clockwise from above, as in the file
    }

    // ---------------- two_materials.3ds: colours, shininess, transparency, texture and tiling, UVs ----------------
    {
        ImportedModel m; std::string err;
        CHECK(Read3dsFile(kDir + "two_materials.3ds", m, &err));
        CHECK(m.parts.size() == 2 && m.materials.size() == 2);
        if (m.parts.size() == 2 && m.materials.size() == 2) {
            const ImportedMaterial& red = m.materials[(std::size_t)m.parts[0].material];
            const ImportedMaterial& wood = m.materials[(std::size_t)m.parts[1].material];
            CHECK(red.name == "Red" && wood.name == "Wood");
            CHECK(red.hasDiffuse && red.diffuse.r == 1.0f && red.diffuse.g == 0.0f && red.diffuse.a == 1.0f);   // float colour 0x0010
            CHECK_NEAR(red.shininess, 32.0, 1e-3);                                                              // 25 percent
            CHECK(red.texture.empty());
            CHECK(wood.hasDiffuse && std::fabs(wood.diffuse.r - 0.2f) < 1e-4f && std::fabs(wood.diffuse.b - 0.8f) < 1e-4f);   // byte colour 0x0011
            CHECK_NEAR(wood.diffuse.a, 0.75, 1e-5);                                                             // 25 percent transparent
            CHECK_NEAR(wood.shininess, 64.0, 1e-3);
            CHECK(wood.texture == kDir + "tex/WOOD.BMP");                                                       // backslash path, next to the file
            // One face per material: 3 vertices and one triangle each.
            CHECK(m.parts[0].mesh.vertices.size() == 3 && m.parts[0].mesh.TriangleCount() == 1);
            CHECK(m.parts[1].mesh.vertices.size() == 3 && m.parts[1].mesh.TriangleCount() == 1);
            // UVs: the Red part is plain (v flipped); the Wood part has scale (2,3) and offset (0.25,0.5) baked in.
            bool r = false, w = false;
            for (std::size_t v = 0; v < 3; ++v) {
                const Vec3& p = m.parts[0].mesh.vertices[v];
                if (Near3(p, 1, 0, 0)) { r = true; CHECK_NEAR(m.parts[0].mesh.uvs[v].x, 1.0, 1e-5); CHECK_NEAR(m.parts[0].mesh.uvs[v].y, 1.0, 1e-5); }
                const Vec3& q = m.parts[1].mesh.vertices[v];
                if (Near3(q, 1, 1, 0)) { w = true; CHECK_NEAR(m.parts[1].mesh.uvs[v].x, 2.25, 1e-5); CHECK_NEAR(m.parts[1].mesh.uvs[v].y, -2.5, 1e-5); }
            }
            CHECK(r && w);
        }
        CHECK(HasNote(m, "ambient / specular"));   // read, but the imported material has nowhere to keep them
    }

    // ---------------- transformed.3ds: the object's local-axes matrix and the master scale ----------------
    {
        ImportedModel m; std::string err;
        CHECK(Read3dsFile(kDir + "transformed.3ds", m, &err));
        CHECK(m.nodes.size() == 2 && m.parts.size() == 1);
        if (m.nodes.size() == 2 && m.parts.size() == 1) {
            const ImportedNode& n = m.nodes[1];
            CHECK(n.name == "Moved" && n.parent == 0);
            CHECK(Near3(n.position, 10, 0, 0, 1e-4f));
            CHECK_NEAR(std::fabs(n.rotation.z), std::sqrt(0.5), 1e-4);   // 90 degrees about the file's Z
            CHECK_NEAR(std::fabs(n.rotation.w), std::sqrt(0.5), 1e-4);
            for (const Vec3& v : m.parts[0].mesh.vertices) CHECK(std::fabs(v.x) < 1.001f && std::fabs(v.y) < 1.001f && std::fabs(v.z) < 0.001f);   // local space
            // World positions = the file's world vertices, scaled by 0.5, then Z-up -> Y-up.
            const float lx[4] = {0, 1, 1, 0}, ly[4] = {0, 0, 1, 1};
            for (int i = 0; i < 4; ++i) CHECK(HasWorldPoint(m, Vec3{0.5f * (10 - ly[i]), 0.0f, -0.5f * lx[i]}));
        }
        ImportedModel f;
        CHECK(Read3dsFile(kDir + "transformed.3ds", f, &err, true));
        CHECK(f.nodes.size() == 1);
        const float lx[4] = {0, 1, 1, 0}, ly[4] = {0, 0, 1, 1};
        for (int i = 0; i < 4; ++i) { bool found = false; for (const Vec3& v : f.parts[0].mesh.vertices) if (Near3(v, 0.5f * (10 - ly[i]), 0.0f, -0.5f * lx[i])) found = true; CHECK(found); }
        // The same object with a singular matrix keeps its file-space vertices and says so.
        std::string bytes = Slurp(kDir + "transformed.3ds");
        const std::size_t at = bytes.find(std::string("\x60\x41", 2));
        CHECK(at != std::string::npos);
        if (at != std::string::npos) {
            for (std::size_t k = 0; k < 36; ++k) bytes[at + 6 + k] = 0;   // zero rotation part
            ImportedModel s;
            CHECK(Reads(bytes, s));
            CHECK(HasNote(s, "singular"));
            CHECK(HasWorldPoint(s, Vec3{0.5f * 10, 0.0f, 0.0f}));
        }
    }

    // ---------------- smoothing.3ds ----------------
    {
        ImportedModel m; std::string err;
        CHECK(Read3dsFile(kDir + "smoothing.3ds", m, &err));
        CHECK(m.parts.size() == 2);
        if (m.parts.size() == 2) {
            const Mesh& soft = m.parts[0].mesh; const Mesh& hard = m.parts[1].mesh;
            CHECK(soft.vertices.size() == 4 && soft.TriangleCount() == 2);   // shared corners welded
            CHECK(hard.vertices.size() == 6 && hard.TriangleCount() == 2);   // different groups: every corner keeps its face normal
            const float s = (float)std::sqrt(0.5);
            for (std::size_t v = 0; v < soft.vertices.size(); ++v) {
                if (Near3(soft.vertices[v], 0, 0, 0) || Near3(soft.vertices[v], 1, 0, 0)) CHECK(Near3(soft.normals[v], 0, s, s, 1e-4f));   // averaged across the fold
                if (Near3(soft.vertices[v], 0, 1, 0)) CHECK(Near3(soft.normals[v], 0, 0, 1, 1e-4f));
                if (Near3(soft.vertices[v], 0, 0, 1)) CHECK(Near3(soft.normals[v], 0, 1, 0, 1e-4f));
            }
            for (const Vec3& n : hard.normals) CHECK(Near3(n, 0, 0, 1, 1e-4f) || Near3(n, 0, 1, 0, 1e-4f));
        }
        // Group 0 is flat; no smoothing chunk at all means smooth per position (like the other own readers).
        std::string flat = Main(Object("F", Verts({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}) +
                                              Faces({{0, 1, 2}, {1, 0, 3}}, Chunk(0x4150, U32(0) + U32(0)))));
        std::string soft = Main(Object("F", Verts({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}}) + Faces({{0, 1, 2}, {1, 0, 3}})));
        ImportedModel a, b;
        CHECK(Reads(flat, a) && Reads(soft, b));
        if (!a.parts.empty() && !b.parts.empty()) { CHECK(a.parts[0].mesh.vertices.size() == 6); CHECK(b.parts[0].mesh.vertices.size() == 4); }
    }

    // ---------------- unsupported.3ds: everything not read is said ----------------
    {
        ImportedModel m; std::string err;
        CHECK(Read3dsFile(kDir + "unsupported.3ds", m, &err));
        CHECK(m.parts.size() == 1 && m.nodes.size() == 2);   // the empty object makes no node
        CHECK(HasNote(m, "keyframe"));
        CHECK(HasNote(m, "1 light(s)"));
        CHECK(HasNote(m, "1 camera(s)"));
        CHECK(HasNote(m, "without faces"));
        CHECK(HasNote(m, "ambient / specular"));
        CHECK(HasNote(m, "opacity / reflection"));
        CHECK(HasNote(m, "0x1100") && HasNote(m, "0x4165") && HasNote(m, "0x7777"));   // unknown chunks listed by id
        CHECK(m.materials.size() == 1 && m.materials[0].name == "Glossy");
        // The seam logs every note as a warning (checked by running it through ImportModelData below).
    }

    // ---------------- built in code: material groups, order independence, edge cases ----------------
    {
        ImportedModel m;
        // Materials defined AFTER the object that uses them, an undefined name, a face in two groups.
        const std::string grp = Chunk(0x4130, std::string("B\0", 2) + U16(2) + U16(0) + U16(1)) +
                                Chunk(0x4130, std::string("Nope\0", 5) + U16(1) + U16(1)) +
                                Chunk(0x4130, std::string("A\0", 2) + U16(1) + U16(0));
        const std::string mat = Chunk(0xAFFF, Chunk(0xA000, std::string("A\0", 2))) + Chunk(0xAFFF, Chunk(0xA000, std::string("B\0", 2)));
        const std::string body = Object("T", Verts({{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}}) + Faces({{0, 1, 2}, {1, 3, 2}, {0, 2, 3}}, grp));
        CHECK(Reads(Main(body + mat), m));
        CHECK(m.materials.size() == 2);
        CHECK(m.parts.size() == 2);   // faces 0,1 -> B; face 2 unassigned (A's claim on face 0 is ignored, Nope is undefined)
        CHECK(HasNote(m, "not defined") && HasNote(m, "more than one material"));
        // UV list that does not match the vertex count is dropped, said.
        ImportedModel u;
        CHECK(Reads(Main(Object("T", Verts(kTri) + Faces({{0, 1, 2}}) + Chunk(0x4140, U16(1) + F32(0) + F32(0)))), u));
        CHECK(u.parts[0].mesh.uvs.empty() && HasNote(u, "UV list"));
        // Trailing bytes after the main chunk are said.
        ImportedModel t;
        CHECK(Reads(GoodTri() + std::string(9, 'x'), t) && HasNote(t, "9 byte(s)"));
        // Colour chunk forms: 0x0013 float (linear) and 0x0012 bytes.
        ImportedModel c;
        CHECK(Reads(Main(Chunk(0xAFFF, Chunk(0xA000, std::string("C\0", 2)) + Chunk(0xA020, Chunk(0x0013, F32(0.5f) + F32(0.25f) + F32(1.0f)))) + Object("T", Verts(kTri) + Faces({{0, 1, 2}}))), c));
        CHECK(c.materials.size() == 1 && c.materials[0].hasDiffuse && std::fabs(c.materials[0].diffuse.g - 0.25f) < 1e-5f);
        // Shininess / transparency as float percentages.
        ImportedModel p;
        CHECK(Reads(Main(Chunk(0xAFFF, Chunk(0xA000, std::string("P\0", 2)) + Chunk(0xA040, Chunk(0x0031, F32(50.0f))) + Chunk(0xA050, Chunk(0x0031, F32(100.0f)))) + Object("T", Verts(kTri) + Faces({{0, 1, 2}}))), p));
        CHECK(std::fabs(p.materials[0].shininess - 64.0f) < 1e-3f && std::fabs(p.materials[0].diffuse.a) < 1e-5f);
        // Master scale of 0 or NaN is ignored and said.
        ImportedModel ms;
        CHECK(Reads(Main(Chunk(0x0100, F32(0.0f)) + Object("T", Verts(kTri) + Faces({{0, 1, 2}}))), ms) && HasNote(ms, "master scale"));
        // A mesh with faces but unordered: faces before vertices is fine.
        ImportedModel o;
        CHECK(Reads(Main(Object("T", Faces({{0, 1, 2}}) + Verts(kTri))), o));
        // An object with no faces only: no mesh in the file is an error.
        CHECK(!Reads(Main(Object("T", Verts(kTri))), o));
        CHECK(!Reads(Main(""), o));
        // Two objects, same name, one material each.
        ImportedModel two;
        CHECK(Reads(Main(Object("T", Verts(kTri) + Faces({{0, 1, 2}})) + Object("T", Verts(kTri) + Faces({{0, 2, 1}}))), two) && two.parts.size() == 2 && two.nodes.size() == 3);
    }

    // ---------------- malformed input ----------------
    {
        ImportedModel m; std::string err;
        CHECK(Reads(GoodTri(), m));
        CHECK(!Reads("", m, &err) && err.find("3DS") == 0);
        CHECK(!Reads("MM", m));
        CHECK(!Reads(std::string(6, '\0'), m));
        CHECK(!Reads(std::string("\x4D\x4E", 2) + U32(6), m));                              // wrong magic
        CHECK(!Reads(Chunk(0x4D4D, ""), m, &err) && err.find("editor") != std::string::npos);
        // Main chunk length: below 6, zero, past the end, one past the end.
        auto withMainLen = [&](std::uint32_t len) { std::string s = GoodTri(); s.replace(2, 4, U32(len)); return s; };
        CHECK(!Reads(withMainLen(0), m)); CHECK(!Reads(withMainLen(5), m));
        CHECK(!Reads(withMainLen((std::uint32_t)GoodTri().size() + 1), m));
        CHECK(!Reads(withMainLen(0xFFFFFFFFu), m));
        CHECK(!Reads(withMainLen(6), m));                                                   // an empty main chunk
        // A child chunk whose length is zero, short, or overlong, in every container we descend into.
        const std::string good = GoodTri();
        for (std::size_t at = 0; at + 6 <= good.size(); ++at) {   // every offset: whatever sits there, a length edit never crashes
            for (std::uint32_t len : {0u, 5u, 6u, 7u, 0xFFFFu, 0xFFFFFFFFu}) {
                std::string s = good; s.replace(at + 2, 4, U32(len));
                ImportedModel r; std::string e; Read3dsBuffer(s, "", r, &e);   // result unconstrained; ASan/UBSan police it
            }
        }
        // Specific ones that must fail: editor chunk length 0 / 5 / overlong; object; mesh; vertex list.
        auto setLen = [&](std::size_t chunkAt, std::uint32_t len) { std::string s = good; s.replace(chunkAt + 2, 4, U32(len)); return s; };
        const std::size_t ed = 6;                     // GoodTri has no version chunk: the editor chunk follows the main header
        CHECK(good.compare(ed, 2, std::string("\x3D\x3D", 2)) == 0);
        CHECK(!Reads(setLen(ed, 0), m)); CHECK(!Reads(setLen(ed, 5), m)); CHECK(!Reads(setLen(ed, 0x7FFFFFFFu), m));
        const std::size_t ob = ed + 6;
        CHECK(good.compare(ob, 2, std::string("\x00\x40", 2)) == 0);
        CHECK(!Reads(setLen(ob, 0), m)); CHECK(!Reads(setLen(ob, 5), m)); CHECK(!Reads(setLen(ob, 999999), m));
        const std::size_t me = ob + 6 + 2;           // after "T\0"
        CHECK(good.compare(me, 2, std::string("\x00\x41", 2)) == 0);
        CHECK(!Reads(setLen(me, 0), m)); CHECK(!Reads(setLen(me, 5), m)); CHECK(!Reads(setLen(me, 999999), m));
        // A chunk with 1..5 bytes left in its parent is a truncated header.
        CHECK(!Reads(Chunk(0x4D4D, Chunk(0x3D3D, Object("T", Verts(kTri) + Faces({{0, 1, 2}}))) + "abc"), m));
        // Counts that do not fit their chunk, or the limits.
        CHECK(!Reads(Main(Object("T", Chunk(0x4110, U16(65535) + F32(0)))), m));                                    // vertex count > data
        CHECK(!Reads(Main(Object("T", Verts(kTri) + Chunk(0x4120, U16(65535)))), m));                               // face count > data
        CHECK(!Reads(Main(Object("T", Verts(kTri) + Chunk(0x4120, U16(2) + U16(0) + U16(1) + U16(2) + U16(0)))), m));   // second face record missing
        CHECK(!Reads(Main(Object("T", Chunk(0x4110, ""))), m));                                                     // no count
        CHECK(!Reads(Main(Object("T", Verts(kTri) + Chunk(0x4120, ""))), m));
        CHECK(!Reads(Main(Object("T", Verts(kTri) + Faces({{0, 1, 3}}))), m));                                      // vertex index out of range
        CHECK(!Reads(Main(Object("T", Verts(kTri) + Faces({{65535, 0, 1}}))), m));
        CHECK(!Reads(Main(Object("T", Verts(kTri) + Faces({{0, 1, 2}}) + Verts(kTri))), m));                        // two vertex lists
        CHECK(!Reads(Main(Object("T", Verts(kTri) + Faces({{0, 1, 2}}) + Faces({{0, 1, 2}}))), m));                 // two face lists
        CHECK(!Reads(Main(Object("T", Verts(kTri) + Faces({{0, 1, 2}}) + Chunk(0x4140, U16(1) + F32(0)))), m));    // UV list too short
        CHECK(!Reads(Main(Object("T", Verts(kTri) + Faces({{0, 1, 2}}, Chunk(0x4130, std::string("A\0", 2) + U16(1) + U16(7))))), m));   // group names face 7
        CHECK(!Reads(Main(Object("T", Verts(kTri) + Faces({{0, 1, 2}}, Chunk(0x4130, std::string("A\0", 2) + U16(5) + U16(0))))), m));   // group count > data
        CHECK(!Reads(Main(Object("T", Verts(kTri) + Faces({{0, 1, 2}}, Chunk(0x4130, std::string("A", 1))))), m));  // unterminated name
        CHECK(!Reads(Main(Object("T", Verts(kTri) + Faces({{0, 1, 2}, {0, 1, 2}}, Chunk(0x4150, U32(1))))), m));    // smoothing list shorter than faces
        CHECK(!Reads(Main(Object("T", Verts(kTri) + Faces({{0, 1, 2}}) + Chunk(0x4160, std::string(47, '\0')))), m));   // matrix too short
        CHECK(!Reads(Main(Object("T", Verts({{0, 0, std::nanf("")}, {1, 0, 0}, {0, 1, 0}}) + Faces({{0, 1, 2}}))), m));   // NaN vertex
        CHECK(!Reads(Main(Object("T", Verts({{0, 0, INFINITY}, {1, 0, 0}, {0, 1, 0}}) + Faces({{0, 1, 2}}))), m));
        CHECK(!Reads(Main(Object("T", Verts(kTri) + Faces({{0, 1, 2}}) + Chunk(0x4160, std::string(36, '\0') + F32(std::nanf("")) + F32(0) + F32(0)))), m));   // NaN matrix
        CHECK(!Reads(Main(Object("T", Verts(kTri) + Faces({{0, 1, 2}}) + Chunk(0x4140, U16(3) + F32(0) + F32(0) + F32(0) + F32(0) + F32(std::nanf("")) + F32(0)))), m));   // NaN UV
        CHECK(!Reads(Main(Chunk(0xAFFF, Chunk(0xA020, Chunk(0x0010, F32(1) + F32(1))) ) + Object("T", Verts(kTri) + Faces({{0, 1, 2}}))), m));   // colour too short
        CHECK(!Reads(Main(Chunk(0xAFFF, Chunk(0xA040, Chunk(0x0030, "x"))) + Object("T", Verts(kTri) + Faces({{0, 1, 2}}))), m));   // percentage too short
        CHECK(!Reads(Main(Chunk(0xAFFF, Chunk(0xA200, Chunk(0xA300, "nozero"))) + Object("T", Verts(kTri) + Faces({{0, 1, 2}}))), m));   // file name unterminated
        CHECK(!Reads(Main(Chunk(0x0100, "ab") + Object("T", Verts(kTri) + Faces({{0, 1, 2}}))), m));                 // master scale too short
        CHECK(!Reads(Chunk(0x4D4D, Chunk(0x3D3D, Chunk(0x4000, std::string(2000, 'n')))), m));                      // object name too long
        CHECK(!Reads(Main(Chunk(0x4000, std::string("T") + Chunk(0x4100, "")) ), m));                                // name not terminated before the chunk end
        // A mesh with faces but no vertices.
        CHECK(!Reads(Main(Object("T", Faces({{0, 1, 2}}))), m));
        // After every failure the result is empty.
        CHECK(!Read3dsBuffer("MM", "", m, &err) && m.parts.empty() && m.nodes.empty() && m.materials.empty() && !err.empty());
        CHECK(!Read3dsFile(kDir + "does_not_exist.3ds", m, &err) && !err.empty());
    }

    // ---------------- truncation at every length ----------------
    {
        int cut = 0;
        for (const char* f : {"one_triangle.3ds", "two_materials.3ds", "transformed.3ds", "smoothing.3ds", "unsupported.3ds"}) {
            const std::string s = Slurp(kDir + f);
            CHECK(!s.empty());
            ImportedModel m;
            CHECK(Read3dsBuffer(s, "", m, nullptr));
            for (std::size_t n = 0; n < s.size(); ++n) {
                ImportedModel t; std::string e;
                ++cut;
                if (Read3dsBuffer(s.substr(0, n), "", t, &e)) { CHECK(false); std::printf("  accepted a %zu byte prefix of %s\n", n, f); }
                else if (!t.parts.empty() || e.empty()) CHECK(false);
            }
        }
        std::printf("  truncation: %d prefixes, none accepted\n", cut);
    }

    // ---------------- huge counts and caps ----------------
    {
        ImportedModel m; std::string err;
        // 62 objects of 65535 vertices each is past the 4 million vertex cap (the per-chunk count is 16 bits).
        std::string big;
        {
            std::string v = U16(65535); v.append((std::size_t)65535 * 12, '\0');
            const std::string obj = Chunk(0x4000, std::string("O\0", 2) + Chunk(0x4100, Chunk(0x4110, v)));
            for (int i = 0; i < 62; ++i) big += obj;
        }
        CHECK(!Reads(Main(big), m, &err) && err.find("too many vertices") != std::string::npos);
        std::string bigF;
        {
            std::string f = U16(65535); f.append((std::size_t)65535 * 8, '\0');
            const std::string obj = Chunk(0x4000, std::string("O\0", 2) + Chunk(0x4100, Verts(kTri) + Chunk(0x4120, f)));
            for (int i = 0; i < 62; ++i) bigF += obj;
        }
        CHECK(!Reads(Main(bigF), m, &err) && err.find("too many faces") != std::string::npos);
        std::string many;
        for (int i = 0; i < 100001; ++i) many += Chunk(0x4000, std::string("\0", 1));
        CHECK(!Reads(Main(many), m, &err) && err.find("too many objects") != std::string::npos);
        // Deeply nested container chunks that we do not descend into are skipped, not recursed into.
        std::string deep = Chunk(0x7000, "");
        for (int i = 0; i < 20000; ++i) deep = Chunk(0x7000, deep);
        ImportedModel d;
        CHECK(Reads(Main(Object("T", Verts(kTri) + Faces({{0, 1, 2}})) + deep), d) && HasNote(d, "0x7000"));
        // The largest legal mesh (65535 vertices, 21845 faces) reads, and a long smoothing fan stays fast.
        {
            std::vector<Vec3> v; for (int i = 0; i < 65535; ++i) v.push_back(Vec3{(float)(i % 256), (float)(i / 256), 0});
            std::vector<std::array<unsigned, 3>> f; for (int i = 0; i + 2 < 65535 && f.size() < 65535; i += 1) f.push_back({0u, (unsigned)(i + 1), (unsigned)(i + 2)});
            f.resize(std::min<std::size_t>(f.size(), 65535));
            std::string sg; for (std::size_t i = 0; i < f.size(); ++i) sg += U32((std::uint32_t)(1u << (i % 31)) | 1u);
            ImportedModel big2;
            CHECK(Reads(Main(Object("Fan", Verts(v) + Faces(f, Chunk(0x4150, sg)))), big2));   // all faces share vertex 0: the smoothing budget kicks in
            CHECK(!big2.parts.empty());
        }
    }

    // ---------------- the seam: opt-in switch, merged mesh, scene import, notes logged ----------------
    {
        CHECK(HasOwnReader(".3ds") && HasOwnReader(".3DS"));
        unsetenv("OKAY_OWN_3DS");
        SetImportBackend(".3ds", ImportBackend::Default);
        ImportedModel m; std::string err;
#if defined(OKAY_OWN_3DS_ON)
        CHECK(EffectiveImportBackend(".3ds") == ImportBackend::Own);   // built with -DOKAY_OWN_3DS=ON
        CHECK(ImportModelData(kDir + "two_materials.3ds", m, ImportBackend::Default, false, &err) && m.backend == "own");
        setenv("OKAY_OWN_3DS", "0", 1);
        CHECK(EffectiveImportBackend(".3ds") == ImportBackend::Own);   // the CMake option is a compile-time default; the variable can still switch it off
        unsetenv("OKAY_OWN_3DS");
#else
#ifdef OKAY_HAVE_ASSIMP
        CHECK(EffectiveImportBackend(".3ds") == ImportBackend::Assimp);   // never ours unless asked
#else
        // Not built with the option: with no Assimp nothing reads .3ds until it is switched on, and it says how.
        CHECK(EffectiveImportBackend(".3ds") == ImportBackend::Default);
        CHECK(!ImportModelData(kDir + "two_materials.3ds", m, ImportBackend::Default, false, &err));
        CHECK(err.find("OKAY_OWN_3DS") != std::string::npos);
        CHECK(ImportableExtensions().find(".3ds") == std::string::npos);
        bool ok = true; ImportModel(kDir + "two_materials.3ds", &ok); CHECK(!ok);
        Scene s0; s0.physicsEnabled = false; bool sok0 = true;
        CHECK(ImportModelScene(s0, kDir + "two_materials.3ds", &sok0) == nullptr || !sok0);
#endif
        // Environment switch.
        setenv("OKAY_OWN_3DS", "1", 1);
        CHECK(EffectiveImportBackend(".3ds") == ImportBackend::Own);
        CHECK(ImportableExtensions().find(".3ds") != std::string::npos);
        CHECK(ImportModelData(kDir + "two_materials.3ds", m, ImportBackend::Default, false, &err));
        CHECK(m.backend == "own" && m.parts.size() == 2);
        setenv("OKAY_OWN_3DS", "0", 1);
        CHECK(EffectiveImportBackend(".3ds") != ImportBackend::Own);   // =0 forces it off
        unsetenv("OKAY_OWN_3DS");
        // The Collada switches do not turn it on, and its switch does not turn Collada on.
        setenv("OKAY_OWN_COLLADA", "1", 1);
        CHECK(EffectiveImportBackend(".3ds") != ImportBackend::Own);
        unsetenv("OKAY_OWN_COLLADA");
        SetImportBackend(".dae", ImportBackend::Own);
        CHECK(EffectiveImportBackend(".3ds") != ImportBackend::Own);
        SetImportBackend(".dae", ImportBackend::Default);
        setenv("OKAY_OWN_3DS", "1", 1);
        CHECK(EffectiveImportBackend(".dae") != ImportBackend::Own);
        unsetenv("OKAY_OWN_3DS");
#endif
        // Run-time switch beats the environment, both ways.
        CHECK(SetImportBackend(".3ds", ImportBackend::Own));
        setenv("OKAY_OWN_3DS", "0", 1);
        CHECK(EffectiveImportBackend(".3ds") == ImportBackend::Own);
        unsetenv("OKAY_OWN_3DS");
        CHECK(ImportModelData(kDir + "two_materials.3ds", m, ImportBackend::Default, false, &err) && m.backend == "own");
        // Explicit Own always runs ours, whatever the default.
        CHECK(ImportModelData(kDir + "one_triangle.3ds", m, ImportBackend::Own, false, &err) && m.backend == "own");
        // Merged one-mesh path (ImportModel): transforms baked, Y-up.
        bool ok2 = false; std::string tex;
        Mesh merged = ImportModel(kDir + "two_materials.3ds", &ok2, &tex);
        CHECK(ok2 && merged.TriangleCount() == 2 && merged.vertices.size() == 6);
        CHECK(tex == kDir + "tex/WOOD.BMP");
        for (const Vec3& v : merged.vertices) CHECK(std::fabs(v.y) < 1e-4f && v.z <= 1e-4f && v.z >= -1.0001f);   // the quad lies flat
        // Scene import: both materials, the textured one keeps its file.
        Scene scene; scene.physicsEnabled = false;
        bool sok = false;
        GameObject* root = ImportModelScene(scene, kDir + "two_materials.3ds", &sok);
        CHECK(sok && root != nullptr);
        if (root) {
            int meshes = 0; bool tinted = false, textured = false;
            for (MeshRenderer* mr : root->GetComponentsInChildren<MeshRenderer>(true)) {
                ++meshes;
                if (mr->texture.empty() && mr->color.r > 0.9f && mr->color.g < 0.1f) tinted = true;
                if (mr->texture == kDir + "tex/WOOD.BMP") textured = true;
            }
            CHECK(meshes == 2 && tinted && textured);
        }
        // Through the seam the notes are carried and the failing path reports.
        CHECK(ImportModelData(kDir + "unsupported.3ds", m, ImportBackend::Default, false, &err) && !m.notes.empty());
        CHECK(!ImportModelData(kDir + "does_not_exist.3ds", m, ImportBackend::Own, false, &err) && !err.empty());
        CHECK(!ImportModelData(kDir + "tex/missing.3ds", m, ImportBackend::Default, false, &err));
        SetImportBackend(".3ds", ImportBackend::Default);
        unsetenv("OKAY_OWN_3DS");
        // "*" switches every own reader, .3ds included, and "*" Default puts it back.
        SetImportBackend("*", ImportBackend::Own);
        CHECK(EffectiveImportBackend(".3ds") == ImportBackend::Own);
        SetImportBackend("*", ImportBackend::Default);
#if !defined(OKAY_OWN_3DS_ON) && !defined(OKAY_HAVE_ASSIMP)
        CHECK(EffectiveImportBackend(".3ds") == ImportBackend::Default);
#endif
    }

#ifdef OKAY_HAVE_ASSIMP
    // ---------------- Assimp reads the same files: compare ----------------
    for (const char* f : {"one_triangle.3ds", "two_materials.3ds", "transformed.3ds", "smoothing.3ds"}) {
        ImportedModel own, ai; std::string e1, e2;
        const bool a = ImportModelData(kDir + f, own, ImportBackend::Own, false, &e1);
        const bool b = ImportModelData(kDir + f, ai, ImportBackend::Assimp, false, &e2);
        std::printf("  %-20s own: %s | assimp: %s\n", f, a ? "ok" : e1.c_str(), b ? "ok" : e2.c_str());
        CHECK(a && b);
        if (!(a && b)) continue;
        const ModelCounts co = CountModel(own), ca = CountModel(ai);
        std::printf("    own: parts %d verts %d tris %d | assimp: parts %d verts %d tris %d\n", co.parts, co.vertices, co.triangles, ca.parts, ca.vertices, ca.triangles);
        CHECK(co.triangles == ca.triangles);
        CHECK(co.parts == ca.parts);
        auto bounds = [](const ImportedModel& m, Vec3& lo, Vec3& hi) {
            lo = Vec3{1e30f, 1e30f, 1e30f}; hi = Vec3{-1e30f, -1e30f, -1e30f};
            for (const ImportedPart& p : m.parts) {
                const Mat4 w = WorldOf(m, p.node);
                for (const Vec3& v : p.mesh.vertices) {
                    const Vec3 q = Apply(w, v);
                    lo = Vec3{std::fmin(lo.x, q.x), std::fmin(lo.y, q.y), std::fmin(lo.z, q.z)};
                    hi = Vec3{std::fmax(hi.x, q.x), std::fmax(hi.y, q.y), std::fmax(hi.z, q.z)};
                }
            }
        };
        Vec3 lo1, hi1, lo2, hi2;
        bounds(own, lo1, hi1); bounds(ai, lo2, hi2);
        std::printf("    bounds own (%.3f %.3f %.3f)-(%.3f %.3f %.3f) assimp (%.3f %.3f %.3f)-(%.3f %.3f %.3f)\n", lo1.x, lo1.y, lo1.z, hi1.x, hi1.y, hi1.z, lo2.x, lo2.y, lo2.z, hi2.x, hi2.y, hi2.z);
        CHECK(Near3(lo1, lo2.x, lo2.y, lo2.z, 1e-3f) && Near3(hi1, hi2.x, hi2.y, hi2.z, 1e-3f));
    }
#else
    std::printf("  Assimp not built: backend comparison skipped\n");
#endif

    TEST_MAIN_RESULT();
}
