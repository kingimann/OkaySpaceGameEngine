// The import seam routes every own-reader format to ITS reader. All own backends are switched ON at run time
// (SetImportBackend), then one tiny fixture of each opt-in slot kind (.dae .3ds .bvh .off .smd .md5mesh .md5anim .ms3d
// .ase .3mf .amf .x3d .dxf .lwo .blend) goes through ImportModelData. A model is accepted only if it says backend "own"
// and has geometry (or clips for the animation-only files). For the batch B formats and Blender the result must also equal
// what the format's own Read*File gives directly, so a wrong arm in the dispatch chain (a format absorbed by the last arm)
// fails here. Fixtures: tests/data/models, or built in memory.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Render/ThreeMfReader.hpp"
#include "okay/Render/AmfReader.hpp"
#include "okay/Render/X3dReader.hpp"
#include "okay/Render/DxfReader.hpp"
#include "okay/Render/LwoReader.hpp"
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

static const std::string kDir = std::string(OKAY_SOURCE_DIR) + "/tests/data/models/";

static void WriteFile(const fs::path& p, const std::string& s) { std::ofstream f(p, std::ios::binary); f.write(s.data(), (std::streamsize)s.size()); }
static std::size_t Verts(const ImportedModel& m) { std::size_t n = 0; for (const ImportedPart& p : m.parts) n += p.mesh.vertices.size(); return n; }
static std::size_t Tris(const ImportedModel& m) { std::size_t n = 0; for (const ImportedPart& p : m.parts) n += p.mesh.triangles.size() / 3; return n; }

static void B16(std::string& s, unsigned v) { s.push_back((char)(v >> 8)); s.push_back((char)v); }
static void B32(std::string& s, std::uint32_t v) { for (int i = 3; i >= 0; --i) s.push_back((char)(v >> (8 * i))); }
static void BF(std::string& s, float f) { std::uint32_t v; std::memcpy(&v, &f, 4); B32(s, v); }
static std::string Chunk(const char* id, const std::string& body) { std::string s = id; B32(s, (std::uint32_t)body.size()); s += body; if (body.size() & 1) s.push_back(0); return s; }

// One triangle in every in-memory format.
static std::string Lwo() {
    std::string layr; B16(layr, 0); B16(layr, 0); for (int i = 0; i < 3; ++i) BF(layr, 0.0f); layr += std::string("L\0", 2);
    std::string pnts; const float p[3][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}; for (auto& v : p) for (float c : v) BF(pnts, c);
    std::string pols = "FACE"; B16(pols, 3); B16(pols, 0); B16(pols, 1); B16(pols, 2);
    std::string body = "LWO2" + Chunk("LAYR", layr) + Chunk("PNTS", pnts) + Chunk("POLS", pols);
    std::string s = "FORM"; B32(s, (std::uint32_t)body.size()); return s + body;
}
static std::string ThreeMf() {
    const std::string model = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><model unit=\"millimeter\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\"><resources>"
        "<object id=\"1\" type=\"model\"><mesh><vertices><vertex x=\"0\" y=\"0\" z=\"0\"/><vertex x=\"1\" y=\"0\" z=\"0\"/><vertex x=\"0\" y=\"1\" z=\"0\"/></vertices>"
        "<triangles><triangle v1=\"0\" v2=\"1\" v3=\"2\"/></triangles></mesh></object></resources><build><item objectid=\"1\"/></build></model>";
    fx::ZipEntry a; a.name = "[Content_Types].xml"; a.data = "<Types/>";
    fx::ZipEntry r; r.name = "_rels/.rels";
    r.data = "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\"><Relationship Target=\"/3D/3dmodel.model\" Id=\"rel0\" Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/></Relationships>";
    fx::ZipEntry m; m.name = "3D/3dmodel.model"; m.data = model;
    return fx::MakeZip({a, r, m});
}
static const char* kAmf = "<?xml version=\"1.0\" encoding=\"utf-8\"?><amf unit=\"millimeter\"><object id=\"0\"><mesh><vertices>"
    "<vertex><coordinates><x>0</x><y>0</y><z>0</z></coordinates></vertex><vertex><coordinates><x>1</x><y>0</y><z>0</z></coordinates></vertex>"
    "<vertex><coordinates><x>0</x><y>1</y><z>0</z></coordinates></vertex></vertices><volume><triangle><v1>0</v1><v2>1</v2><v3>2</v3></triangle></volume></mesh></object></amf>";
static const char* kX3d = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><X3D version=\"3.2\"><Scene><Shape><IndexedFaceSet coordIndex=\"0 1 2 -1\">"
    "<Coordinate point=\"0 0 0 1 0 0 0 1 0\"/></IndexedFaceSet></Shape></Scene></X3D>";
static std::string G(int code, const std::string& v) { char b[16]; std::snprintf(b, sizeof b, "%3d\n", code); return b + v + "\n"; }
static std::string Dxf() {
    std::string e = G(0, "3DFACE") + G(8, "L");
    const char* c[4][3] = {{"0", "0", "0"}, {"1", "0", "0"}, {"0", "1", "0"}, {"0", "1", "0"}};
    for (int i = 0; i < 4; ++i) for (int k = 0; k < 3; ++k) e += G(10 + i + 10 * k, c[i][k]);
    return G(0, "SECTION") + G(2, "ENTITIES") + e + G(0, "ENDSEC") + G(0, "EOF");
}

int main() {
    RUN_SUITE("own_dispatch");
    SetImportBackend("*", ImportBackend::Own);

    const fs::path dir = fs::temp_directory_path() / "okay_own_dispatch";
    fs::create_directories(dir);
    WriteFile(dir / "t.lwo", Lwo());
    WriteFile(dir / "t.3mf", ThreeMf());
    WriteFile(dir / "t.amf", kAmf);
    WriteFile(dir / "t.x3d", kX3d);
    WriteFile(dir / "t.dxf", Dxf());

    // Every opt-in slot kind with a fixture: must import through the seam with the own reader.
    struct Case { std::string path; bool geometry; };
    const Case cases[] = {
        {kDir + "two_materials.dae", true}, {kDir + "cube.3ds", true}, {kDir + "walk.bvh", false}, {kDir + "cube.off", true},
        {kDir + "arm.smd", true}, {kDir + "skel.md5mesh", true}, {kDir + "skel.md5anim", false}, {kDir + "rig.ms3d", true},
        {kDir + "scene.ase", true}, {(dir / "t.3mf").string(), true}, {(dir / "t.amf").string(), true}, {(dir / "t.x3d").string(), true},
        {(dir / "t.dxf").string(), true}, {(dir / "t.lwo").string(), true}, {kDir + "blend_attr_41.blend", true},
    };
    for (const Case& c : cases) {
        ImportedModel m; std::string err;
        const bool ok = ImportModelData(c.path, m, ImportBackend::Default, false, &err);
        if (!ok) std::fprintf(stderr, "  %s: %s\n", c.path.c_str(), err.c_str());
        CHECK(ok);
        CHECK(m.backend == "own");
        if (c.geometry) CHECK(!m.parts.empty() && Tris(m) > 0);
        else CHECK(!m.parts.empty() || !m.clips.empty());
    }

    // The last arms of the chain: the result must be the one the format's own reader gives directly.
    {
        ImportedModel a, b; std::string err;
        CHECK(ImportModelData((dir / "t.lwo").string(), a, ImportBackend::Own, false, &err) && ReadLwoFile((dir / "t.lwo").string(), b, &err));
        CHECK(Verts(a) == Verts(b) && Verts(a) == 3 && Tris(a) == 1 && Tris(b) == 1);
    }
    {
        ImportedModel a, b; std::string err;
        CHECK(ImportModelData((dir / "t.3mf").string(), a, ImportBackend::Own, false, &err) && Read3mfFile((dir / "t.3mf").string(), b, &err, false));
        CHECK(Verts(a) == Verts(b) && Tris(a) == 1 && Tris(b) == 1);
    }
    {
        ImportedModel a, b; std::string err;
        CHECK(ImportModelData((dir / "t.amf").string(), a, ImportBackend::Own, false, &err) && ReadAmfFile((dir / "t.amf").string(), b, &err, false));
        CHECK(Verts(a) == Verts(b) && Tris(a) == 1);
    }
    {
        ImportedModel a, b; std::string err;
        CHECK(ImportModelData((dir / "t.x3d").string(), a, ImportBackend::Own, false, &err) && ReadX3dFile((dir / "t.x3d").string(), b, &err, false));
        CHECK(Verts(a) == Verts(b) && Tris(a) == 1);
    }
    {
        ImportedModel a, b; std::string err;
        CHECK(ImportModelData((dir / "t.dxf").string(), a, ImportBackend::Own, false, &err) && ReadDxfFile((dir / "t.dxf").string(), b, &err, false));
        CHECK(Verts(a) == Verts(b) && Tris(a) >= 1);
    }
    {
        ImportedModel a, b; std::string err;
        CHECK(ImportModelData(kDir + "blend_attr_41.blend", a, ImportBackend::Own, false, &err) && ReadBlendFile(kDir + "blend_attr_41.blend", b, &err, false));
        CHECK(a.parts.size() == 2 && a.parts.size() == b.parts.size() && Verts(a) == Verts(b) && a.backend == "own");
        // flattening collapses the node tree to one root and keeps the two parts
        ImportedModel f; CHECK(ImportModelData(kDir + "blend_attr_41.blend", f, ImportBackend::Own, true, &err) && f.nodes.size() == 1 && f.parts.size() == 2);
    }

    SetImportBackend("*", ImportBackend::Default);
    std::error_code ec; fs::remove_all(dir, ec);
    TEST_MAIN_RESULT();
}
