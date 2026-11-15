// Budgets, strips and fans, and where a scene may point, for the batch-D own readers (AC3D, OGRE XML, XGL, Irrlicht scene, NFF).
//  1. Small hostile files (a few hundred KB at most) that used to ask for gigabytes or minutes must now finish in under 2 seconds,
//     be refused (or cut, for AC3D subdivision) with a reason that names the budget, and never hold more than kCapBytes of
//     memory at once (a counting operator new measures the live peak).
//  2. OGRE triangle strips and fans, checked against hand-computed triangles (alternating winding for strips, the shared first
//     vertex for fans) in both ways the face elements are written.
//  3. A scene may only name files inside its own folder: Irrlicht mesh files and the NFF material table are refused when the
//     path is absolute, climbs out with "..", or goes through a link that leaves the folder.
// All fixtures are SYNTHETIC. Builds on its own with the reader sources, like test_batchd_hostile.cpp.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "batchd_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <new>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

// ---- a counting allocator: bytes live now and the peak since the last reset ------------------------------------------
static std::atomic<std::size_t> g_live{0}, g_peak{0};
static void* CountedAlloc(std::size_t n) {
    void* raw = std::malloc(n + 16);
    if (!raw) throw std::bad_alloc();
    *static_cast<std::size_t*>(raw) = n;
    const std::size_t now = g_live.fetch_add(n) + n;
    std::size_t p = g_peak.load();
    while (now > p && !g_peak.compare_exchange_weak(p, now)) {}
    return static_cast<char*>(raw) + 16;
}
static void CountedFree(void* q) noexcept {
    if (!q) return;
    void* raw = static_cast<char*>(q) - 16;
    g_live.fetch_sub(*static_cast<std::size_t*>(raw));
    std::free(raw);
}
void* operator new(std::size_t n) { return CountedAlloc(n); }
void* operator new[](std::size_t n) { return CountedAlloc(n); }
void operator delete(void* p) noexcept { CountedFree(p); }
void operator delete[](void* p) noexcept { CountedFree(p); }
void operator delete(void* p, std::size_t) noexcept { CountedFree(p); }
void operator delete[](void* p, std::size_t) noexcept { CountedFree(p); }

// The most memory any hostile case below may hold at once. The budgets are 4,000,000 triangles / vertices per file; a refusal
// comes before that is reached by much, so a few hundred MB is the stated limit (the old code went to 2 GB and beyond).
static constexpr std::size_t kCapBytes = 640u * 1024u * 1024u;
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_UNDEFINED__)
static constexpr double kMaxSeconds = 30.0;   // a sanitizer build is several times slower. A hang guard, not a speed test (see docs/automated_testing.md)
#else
static constexpr double kMaxSeconds = 10.0;   // the slowest case takes well under 1 s here; the old code took minutes. 10x+ headroom for a loaded runner
#endif

static double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
static bool HasNote(const ImportedModel& m, const char* what) { for (const std::string& n : m.notes) if (n.find(what) != std::string::npos) return true; return false; }

struct Run { bool ok; double secs; std::size_t peakMB; };
template <class F> static Run Measure(const char* what, F&& f) {
    const std::size_t base = g_live.load();
    g_peak.store(base);
    const double t = Now();
    const bool ok = f();
    const double s = Now() - t;
    const std::size_t extra = g_peak.load() - base;
    std::fprintf(stderr, "  %s: %s in %.2f s, peak %zu MB\n", what, ok ? "read" : "refused", s, extra >> 20);
    CHECK(s < kMaxSeconds);
    CHECK(extra < kCapBytes);
    return Run{ok, s, extra >> 20};
}

static ImportedPart OnePart(std::size_t tris) {
    ImportedPart p;
    for (std::size_t i = 0; i < tris; ++i) {
        const float x = (float)(i % 1000), y = (float)(i / 1000);
        p.mesh.vertices.push_back(Vec3{x, y, 0}); p.mesh.vertices.push_back(Vec3{x + 1, y, 0}); p.mesh.vertices.push_back(Vec3{x, y + 1, 0});
        p.mesh.triangles.push_back((int)(3 * i)); p.mesh.triangles.push_back((int)(3 * i + 1)); p.mesh.triangles.push_back((int)(3 * i + 2));
    }
    p.node = 0; p.material = -1;
    return p;
}

// ---- hostile files ------------------------------------------------------------------------------------------------
static void XglHostile() {
    // One mesh of 2000 triangles named by 6000 objects: 12 million placed triangles from a file of about 0.5 MB. The old reader
    // built the mesh again for every object (and checked only the output vertices).
    const int kFaces = 2000, kObjs = 6000;
    std::string s = "<WORLD>\n<MESH ID=\"0\">\n";
    for (int i = 0; i < kFaces + 2; ++i) s += "<P ID=\"" + std::to_string(i) + "\">" + std::to_string(i) + "," + std::to_string((i * i) % 97) + "," + std::to_string((i * 37) % 101) + "</P>";
    for (int i = 0; i < kFaces; ++i) s += "<F><FV1><PREF>" + std::to_string(i) + "</PREF></FV1><FV2><PREF>" + std::to_string(i + 1) + "</PREF></FV2><FV3><PREF>" + std::to_string(i + 2) + "</PREF></FV3></F>\n";
    s += "</MESH>\n";
    for (int i = 0; i < kObjs; ++i) s += "<OBJECT><MESHREF>0</MESHREF></OBJECT>";
    s += "</WORLD>\n";
    std::fprintf(stderr, "  xgl hostile file: %zu bytes\n", s.size());
    ImportedModel m; std::string err;
    const Run r = Measure("xgl mesh named 6000 times", [&] { return ReadXglBuffer(s, "", m, &err); });
    CHECK(!r.ok && err.find("budget") != std::string::npos && err.find("placed") != std::string::npos && m.parts.empty() && m.nodes.empty());
    // below the budget the same shape still reads, and the mesh is built once (the degenerate-triangle note counts once)
    std::string t = s.substr(0, s.find("<OBJECT>"));
    for (int i = 0; i < 50; ++i) t += "<OBJECT><MESHREF>0</MESHREF></OBJECT>";
    t += "</WORLD>\n";
    ImportedModel n; std::string e2;
    CHECK(ReadXglBuffer(t, "", n, &e2) && n.parts.size() == 50 && fx::TriCount(n) == 50u * (std::size_t)kFaces && fx::Sane(n));
}

static void IrrHostile() {
    const std::string tri = "a";
    {   // 100 spheres of 512 x 512 segments: 52 million triangles from a file of a few KB
        std::string s = "<irr_scene>";
        for (int i = 0; i < 100; ++i) s += "<node type=\"sphere\"><attributes><float name=\"Radius\" value=\"1\"/><int name=\"PolyCountX\" value=\"512\"/><int name=\"PolyCountY\" value=\"512\"/></attributes></node>";
        s += "</irr_scene>";
        ImportedModel m; std::string err;
        const Run r = Measure("irr 100 big spheres", [&] { return ReadIrrSceneBuffer(s, "", m, &err, nullptr); });
        CHECK(!r.ok && err.find("budget") != std::string::npos && m.parts.empty());
    }
    {   // one mesh file of 3000 triangles, named by 3000 nodes: 9 million copied triangles
        int loads = 0;
        IrrMeshLoader loader = [&](const std::string&, ImportedModel& m, std::string*) { ++loads; m.parts.push_back(OnePart(3000)); m.nodes.push_back(ImportedNode{}); return true; };
        std::string s = "<irr_scene>";
        for (int i = 0; i < 3000; ++i) s += "<node type=\"mesh\"><attributes><string name=\"Mesh\" value=\"a.obj\"/></attributes></node>";
        s += "</irr_scene>";
        ImportedModel m; std::string err;
        const Run r = Measure("irr mesh copied 3000 times", [&] { return ReadIrrSceneBuffer(s, "", m, &err, &loader); });
        CHECK(!r.ok && err.find("budget") != std::string::npos && loads == 1 && m.parts.empty());
    }
    {   // 1000 different mesh files of 20000 triangles each: they are all kept until the end
        IrrMeshLoader loader = [&](const std::string&, ImportedModel& m, std::string*) { m.parts.push_back(OnePart(20000)); m.nodes.push_back(ImportedNode{}); return true; };
        std::string s = "<irr_scene>";
        for (int i = 0; i < 400; ++i) s += "<node type=\"mesh\"><attributes><string name=\"Mesh\" value=\"f" + std::to_string(i) + ".obj\"/></attributes></node>";
        s += "</irr_scene>";
        ImportedModel m; std::string err;
        const Run r = Measure("irr 400 different 20000-triangle files", [&] { return ReadIrrSceneBuffer(s, "", m, &err, &loader); });
        CHECK(!r.ok && err.find("in all") != std::string::npos && m.parts.empty());
    }
    {   // a sphere with an absurd count is clamped, and a double that is not a number does not break the cast
        ImportedModel m; std::string err;
        CHECK(ReadIrrSceneBuffer("<irr_scene><node type=\"sphere\"><attributes><int name=\"PolyCountX\" value=\"1e300\"/><int name=\"PolyCountY\" value=\"3000000000\"/></attributes></node></irr_scene>", "", m, &err, nullptr) && fx::Sane(m));
    }
    (void)tri;
}

static void Ac3dHostile() {
    // 60 objects, each one quad with `subdiv 8` (65,536 times the faces): about 15 KB. Every object alone is allowed up to its own
    // cap, so only a budget for the whole file keeps the time and memory down.
    std::string s = "AC3Db\nMATERIAL \"m\" rgb 1 1 1 amb 0 0 0 emis 0 0 0 spec 0 0 0 shi 0 trans 0\nOBJECT world\nkids 60\n";
    for (int i = 0; i < 60; ++i)
        s += "OBJECT poly\nname \"o" + std::to_string(i) + "\"\nsubdiv 8\nnumvert 4\n0 0 0\n1 0 0\n1 1 0\n0 1 0\nnumsurf 1\nSURF 0x10\nmat 0\nrefs 4\n0 0 0\n1 0 0\n2 0 0\n3 0 0\nkids 0\n";
    std::fprintf(stderr, "  ac3d hostile file: %zu bytes\n", s.size());
    ImportedModel m; std::string err;
    const Run r = Measure("ac3d 60 objects with subdiv 8", [&] { return ReadAc3dBuffer(s, "", m, &err); });
    CHECK(r.ok && fx::Sane(m) && HasNote(m, "budget of 2,000,000 subdivided corners"));
    CHECK(fx::TriCount(m) <= 2u * 2000000u);   // never more than the budget corners, as two triangles per quad
}

// ---- OGRE strips and fans ------------------------------------------------------------------------------------------
// The triangles of a model as (centroid x, centroid y, normal z sign), found from the positions alone.
struct Tri { double cx, cy; int nz; };
static std::vector<Tri> Triangles(const ImportedModel& m) {
    std::vector<Tri> r;
    for (const ImportedPart& p : m.parts)
        for (std::size_t i = 0; i + 2 < p.mesh.triangles.size(); i += 3) {
            const Vec3 a = p.mesh.vertices[(std::size_t)p.mesh.triangles[i]], b = p.mesh.vertices[(std::size_t)p.mesh.triangles[i + 1]], c = p.mesh.vertices[(std::size_t)p.mesh.triangles[i + 2]];
            const double z = (double)(b.x - a.x) * (c.y - a.y) - (double)(b.y - a.y) * (c.x - a.x);
            r.push_back(Tri{(a.x + b.x + c.x) / 3.0, (a.y + b.y + c.y) / 3.0, z > 0 ? 1 : -1});
        }
    return r;
}
static bool Has(const std::vector<Tri>& v, double x, double y, int nz) {
    for (const Tri& t : v) if (std::fabs(t.cx - x) < 1e-5 && std::fabs(t.cy - y) < 1e-5 && t.nz == nz) return true;
    return false;
}
static std::string OgreMesh(const char* op, const char* verts, const std::string& faces, int count) {
    return std::string("<mesh><submeshes><submesh material=\"m\" usesharedvertices=\"false\" use32bitindexes=\"false\" operationtype=\"") + op + "\">"
           "<faces count=\"" + std::to_string(count) + "\">" + faces + "</faces><geometry vertexcount=\"5\"><vertexbuffer positions=\"true\">" + verts + "</vertexbuffer></geometry></submesh></submeshes></mesh>";
}
static std::string V(double x, double y) { return "<vertex><position x=\"" + std::to_string(x) + "\" y=\"" + std::to_string(y) + "\" z=\"0\"/></vertex>"; }
static std::string F3(int a, int b, int c) { return "<face v1=\"" + std::to_string(a) + "\" v2=\"" + std::to_string(b) + "\" v3=\"" + std::to_string(c) + "\"/>"; }
static std::string F1(int a) { return "<face v1=\"" + std::to_string(a) + "\"/>"; }

static void OgreStripsAndFans() {
    // Strip vertices 0..4 = (0,0) (0,1) (1,0) (1,1) (2,0). The strip's triangles: (0,1,2), (2,1,3) turned over, (2,3,4): all three
    // wind the same way, clockwise seen from +z (normal -z); centroids (1/3,1/3), (2/3,2/3), (4/3,1/3).
    const std::string sv = V(0, 0) + V(0, 1) + V(1, 0) + V(1, 1) + V(2, 0);
    for (int form = 0; form < 2; ++form) {
        // form 0: every face element carries the triangle of the strip; form 1: the first face has three indices, the others the new one
        const std::string faces = form == 0 ? F3(0, 1, 2) + F3(1, 2, 3) + F3(2, 3, 4) : F3(0, 1, 2) + F1(3) + F1(4);
        ImportedModel m; std::string err;
        CHECK(ReadOgreXmlBuffer(OgreMesh("triangle_strip", sv.c_str(), faces, 3), "", m, &err));
        const std::vector<Tri> t = Triangles(m);
        CHECK(t.size() == 3);
        CHECK(Has(t, 1.0 / 3, 1.0 / 3, -1) && Has(t, 2.0 / 3, 2.0 / 3, -1) && Has(t, 4.0 / 3, 1.0 / 3, -1));
    }
    {   // a repeated index in a strip is a degenerate triangle: dropped
        ImportedModel m; std::string err;
        CHECK(ReadOgreXmlBuffer(OgreMesh("triangle_strip", sv.c_str(), F3(0, 1, 2) + F1(2) + F1(3), 3), "", m, &err));
        CHECK(Triangles(m).size() == 1);   // the sequence 0 1 2 2 3 gives (0,1,2), then (2,1,2) and (2,2,3), which are degenerate
    }
    // Fan around vertex 0: vertices (0,0) (1,0) (1,1) (0,1) (-1,1): triangles (0,1,2), (0,2,3), (0,3,4), all counter-clockwise (normal +z).
    const std::string fv = V(0, 0) + V(1, 0) + V(1, 1) + V(0, 1) + V(-1, 1);
    for (int form = 0; form < 2; ++form) {
        const std::string faces = form == 0 ? F3(0, 1, 2) + F3(0, 2, 3) + F3(0, 3, 4) : F3(0, 1, 2) + F1(3) + F1(4);
        ImportedModel m; std::string err;
        CHECK(ReadOgreXmlBuffer(OgreMesh("triangle_fan", fv.c_str(), faces, 3), "", m, &err));
        const std::vector<Tri> t = Triangles(m);
        CHECK(t.size() == 3);
        CHECK(Has(t, 2.0 / 3, 1.0 / 3, 1) && Has(t, 1.0 / 3, 2.0 / 3, 1) && Has(t, -1.0 / 3, 2.0 / 3, 1));
    }
    {   // a list is still a list
        ImportedModel m; std::string err;
        CHECK(ReadOgreXmlBuffer(OgreMesh("triangle_list", fv.c_str(), F3(0, 1, 2) + F3(0, 2, 3), 2), "", m, &err) && Triangles(m).size() == 2);
        CHECK(!ReadOgreXmlBuffer(OgreMesh("triangle_list", fv.c_str(), F1(0), 1), "", m, &err));
    }
}

// ---- where a scene may point ----------------------------------------------------------------------------------------
static void WriteFile(const fs::path& p, const std::string& s) { std::ofstream f(p, std::ios::binary); f.write(s.data(), (std::streamsize)s.size()); }

static void ScenePaths() {
    const fs::path root = fs::temp_directory_path() / "okay_batchd_paths";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path scene = root / "scene", outside = root / "outside";
    fs::create_directories(scene / "sub");
    fs::create_directories(outside);
    WriteFile(outside / "x.obj", "x");
    WriteFile(outside / "m.mat", fx::BuildNffMat());
    WriteFile(scene / "sub" / "ok.obj", "x");
    WriteFile(scene / "m.mat", fx::BuildNffMat());
    bool link = true;
    fs::create_directory_symlink(outside, scene / "link", ec);
    if (ec) link = false;   // no symbolic links here (Windows without the right): that one case is skipped
    const std::string base = scene.string() + "/";

    // Irrlicht: the loader is told only about allowed paths
    std::vector<std::string> asked;
    IrrMeshLoader loader = [&](const std::string& p, ImportedModel& m, std::string*) { asked.push_back(p); m.parts.push_back(OnePart(1)); m.nodes.push_back(ImportedNode{}); return true; };
    auto readOne = [&](const std::string& ref, ImportedModel& m, std::string& err) {
        asked.clear();
        return ReadIrrSceneBuffer("<irr_scene><node type=\"cube\"/><node type=\"mesh\"><attributes><string name=\"Mesh\" value=\"" + ref + "\"/></attributes></node></irr_scene>", base, m, &err, &loader);
    };
    {
        ImportedModel m; std::string err;
        CHECK(readOne("sub/ok.obj", m, err) && asked.size() == 1 && m.parts.size() == 2 && !HasNote(m, "refused"));
        CHECK(readOne("sub\\ok.obj", m, err) && asked.size() == 1);
        CHECK(readOne("sub/../sub/ok.obj", m, err) && asked.size() == 1);
        const std::string refs[] = {(outside / "x.obj").string(), "../outside/x.obj", "sub/../../outside/x.obj", "..\\outside\\x.obj", "/etc/passwd", "C:/x.obj", "C:\\x.obj", "\\\\host\\share\\x.obj", "//host/x.obj", "../scene/sub/ok.obj"};
        for (const std::string& ref : refs) {
            ImportedModel a; std::string e;
            const bool ok = readOne(ref, a, e);
            CHECK(ok && a.parts.size() == 1 && asked.empty() && HasNote(a, "refused"));   // the cube still reads; the mesh node does not
        }
        if (link) { ImportedModel a; std::string e; CHECK(readOne("link/x.obj", a, e) && asked.empty() && a.parts.size() == 1 && HasNote(a, "refused")); }
        // with only the refused node the whole scene says why
        ImportedModel a; std::string e;
        CHECK(!ReadIrrSceneBuffer("<irr_scene><node type=\"mesh\"><attributes><string name=\"Mesh\" value=\"../outside/x.obj\"/></attributes></node></irr_scene>", base, a, &e, &loader) && e.find("refused") != std::string::npos && asked.empty());
    }
    // NFF: the material table named by "mtable"
    auto nffWith = [&](const std::string& table) {
        std::string s = fx::BuildNffSense8();
        s.replace(s.find("m.mat"), 5, table);
        return s;
    };
    {
        ImportedModel m; std::string err;
        CHECK(ReadNffBuffer(nffWith("m.mat"), base, m, &err) && m.parts.size() == 2 && !HasNote(m, "refused") && !HasNote(m, "mtable"));
        const std::string refs[] = {(outside / "m.mat").string(), "../outside/m.mat", "..\\outside\\m.mat", "/etc/hostname", "C:/m.mat"};
        for (const std::string& ref : refs) {
            ImportedModel a; std::string e;
            CHECK(ReadNffBuffer(nffWith(ref), base, a, &e) && a.parts.size() == 2 && HasNote(a, "refused"));   // the polygons still read, the table does not
        }
        if (link) { ImportedModel a; std::string e; CHECK(ReadNffBuffer(nffWith("link/m.mat"), base, a, &e) && HasNote(a, "refused")); }
    }
    fs::remove_all(root, ec);
}

int main() {
    RUN_SUITE("batchd_budget");
    XglHostile();
    IrrHostile();
    Ac3dHostile();
    OgreStripsAndFans();
    ScenePaths();
    TEST_MAIN_RESULT();
}
