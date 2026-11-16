// own_reader_fallback: when one of our own model readers refuses a file, the fallback reader must read it.
//
// Step 0.2 of the own-code flip proposal. The dispatch tests check that a reader that is switched off is not used;
// this test checks the other half: our reader is switched ON, it says no to a file, and the import still works
// because the fallback runs.
//
//   * Formats that go through ImportModelData (okay/Render/ModelData.hpp): our reader is forced on with
//     SetImportBackend(ext, Own), and the fallback is Assimp (the line in ImportModelData after "Our reader said no").
//   * FBX: our reader is forced on with fbx::SetOwnReaderEnabled(true), and the fallback is ufbx (in ImportFbxMerged
//     and ImportFbxSceneGraph of ModelImport.cpp).
//
// Every file is written by this test (no downloads, nothing from another project). Each one is a small, valid file in
// a variant that the reader's own document says it refuses. For every case the test first proves the two premises
// (our reader alone refuses the file; the fallback alone reads it), then checks the import through the seam.
//
// Built only with -DOKAY_USE_ASSIMP=ON (tests/CMakeLists.txt): without Assimp there is no fallback for these formats.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Render/FbxReader.hpp"
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

static const std::string kDir = "own_reader_fallback_files/";

static std::string WriteFile(const std::string& name, const std::string& bytes) {
    fs::create_directories(kDir);
    const std::string p = kDir + name;
    std::ofstream f(p, std::ios::binary);
    f.write(bytes.data(), (std::streamsize)bytes.size());
    return p;
}

static int Vertices(const ImportedModel& m) { int n = 0; for (const ImportedPart& p : m.parts) n += (int)p.mesh.vertices.size(); return n; }
static int Triangles(const ImportedModel& m) { int n = 0; for (const ImportedPart& p : m.parts) n += (int)p.mesh.triangles.size() / 3; return n; }

using OwnRead = bool (*)(const std::string&, ImportedModel&, std::string*);

struct Case {
    const char* what;        // the variant, and where our reader's document says it is refused
    const char* file;        // file name (its extension picks the reader)
    std::string bytes;
    OwnRead own;             // our reader, called directly, to prove it refuses the file
    bool needTriangles;      // false only where the fallback reads the file as lines (nothing to fill)
};

// ---- The files ---------------------------------------------------------------------------------------------------

// OFF with homogeneous coordinates ("4OFF": x y z w per vertex). docs/own_off_reader.md, "Not handled":
// "4OFF (homogeneous coordinates) ... fail the read with a message". Assimp divides by w.
static std::string Off4() {
    return "4OFF\n"
           "4 2 0\n"
           "0 0 0 1\n"
           "2 0 0 2\n"      // (1, 0, 0) once divided by w
           "3 3 0 3\n"      // (1, 1, 0)
           "0 4 0 4\n"      // (0, 1, 0)
           "3 0 1 2\n"
           "3 0 2 3\n";
}

// SMD with "version 2". docs/own_smd_reader.md: "a version other than 1 ... fail the read". Assimp only warns.
static std::string SmdVersion2() {
    return "version 2\n"
           "nodes\n"
           "0 \"root\" -1\n"
           "end\n"
           "skeleton\n"
           "time 0\n"
           "0 0 0 0 0 0 0\n"
           "end\n"
           "triangles\n"
           "skin.bmp\n"
           "0 0 0 0 0 0 1 0 0\n"
           "0 1 0 0 0 0 1 1 0\n"
           "0 0 1 0 0 0 1 0 1\n"
           "end\n";
}

// ASCII STL whose facet is a quad (four `vertex` lines). Our STL reader (ModelReaders.cpp) refuses
// "facet with more than 3 vertices"; Assimp logs it and keeps the first three corners.
static std::string StlQuadFacet() {
    return "solid quad\n"
           "facet normal 0 0 1\n"
           " outer loop\n"
           "  vertex 0 0 0\n"
           "  vertex 1 0 0\n"
           "  vertex 1 1 0\n"
           "  vertex 0 1 0\n"
           " endloop\n"
           "endfacet\n"
           "endsolid quad\n";
}

// Irrlicht mesh whose vertexCount says 4 but the text holds 3. docs/own_irrmesh_reader.md: "vertexCount ... the text
// must really hold that many values, or the file is refused". Assimp reads the vertices that are there.
static std::string IrrMeshShortVertices() {
    return "<?xml version=\"1.0\"?>\n"
           "<mesh xmlns=\"http://irrlicht.sourceforge.net/IRRMESH_09_2007\" version=\"1.0\">\n"
           "<buffer>\n"
           "<material>\n"
           "<color name=\"Diffuse\" value=\"ffffffff\" />\n"
           "</material>\n"
           "<vertices type=\"standard\" vertexCount=\"4\">\n"
           "0 0 0 0 0 1 ffffffff 0 0\n"
           "1 0 0 0 0 1 ffffffff 1 0\n"
           "0 1 0 0 0 1 ffffffff 0 1\n"
           "</vertices>\n"
           "<indices indexCount=\"3\">\n"
           "0 1 2\n"
           "</indices>\n"
           "<primitives type=\"triangles\" primitiveCount=\"1\" />\n"
           "</buffer>\n"
           "</mesh>\n";
}

// AC3D whose only surface is an open line. docs/own_ac3d_reader.md: "files that have only lines or no surfaces (the
// file is refused: the engine's meshes hold triangles)". Assimp reads it as a mesh of line primitives (vertices, no
// triangles), so this case checks the fallback ran and found the vertices, not triangles.
static std::string Ac3dOnlyLines() {
    return "AC3Db\n"
           "MATERIAL \"white\" rgb 1 1 1  amb 0.2 0.2 0.2  emis 0 0 0  spec 0.5 0.5 0.5  shi 10  trans 0\n"
           "OBJECT world\n"
           "kids 1\n"
           "OBJECT poly\n"
           "name \"wire\"\n"
           "numvert 3\n"
           "0 0 0\n"
           "1 0 0\n"
           "1 1 0\n"
           "numsurf 1\n"
           "SURF 0x02\n"
           "mat 0\n"
           "refs 3\n"
           "0 0 0\n"
           "1 0 0\n"
           "2 0 0\n"
           "kids 0\n";
}

// FBX 6.1 in ASCII: the geometry sits inside the Model (no separate Geometry object). docs/own_fbx_reader.md:
// "FBX 6.x and older ... not read". Assimp refuses every FBX below 7100 too, so a working import here is ufbx's.
static std::string Fbx61Ascii() {
    return "; FBX 6.1.0 project file\n"
           "; ----------------------------------------------------\n"
           "\n"
           "FBXHeaderExtension:  {\n"
           "\tFBXHeaderVersion: 1003\n"
           "\tFBXVersion: 6100\n"
           "}\n"
           "\n"
           "Definitions:  {\n"
           "\tVersion: 100\n"
           "\tCount: 1\n"
           "\tObjectType: \"Model\" {\n"
           "\t\tCount: 1\n"
           "\t}\n"
           "}\n"
           "\n"
           "Objects:  {\n"
           "\tModel: \"Model::Tri\", \"Mesh\" {\n"
           "\t\tVersion: 232\n"
           "\t\tProperties60:  {\n"
           "\t\t\tProperty: \"Lcl Translation\", \"Lcl Translation\", \"A+\",0,0,0\n"
           "\t\t\tProperty: \"Lcl Rotation\", \"Lcl Rotation\", \"A+\",0,0,0\n"
           "\t\t\tProperty: \"Lcl Scaling\", \"Lcl Scaling\", \"A+\",1,1,1\n"
           "\t\t}\n"
           "\t\tVertices: 0,0,0,1,0,0,1,1,0,0,1,0\n"
           "\t\tPolygonVertexIndex: 0,1,2,-4\n"
           "\t\tGeometryVersion: 124\n"
           "\t}\n"
           "}\n"
           "\n"
           "Connections:  {\n"
           "\tConnect: \"OO\", \"Model::Tri\", \"Model::Scene\"\n"
           "}\n";
}

// ---- The checks --------------------------------------------------------------------------------------------------

static void SeamCase(const Case& c) {
    RUN_SUITE(std::string("fallback: ") + c.what);
    const std::string path = WriteFile(c.file, c.bytes);
    const std::string ext = fs::path(path).extension().string();

    // Premise 1: our reader, called directly, refuses this file.
    {
        ImportedModel m; std::string err;
        const bool ownOk = c.own(path, m, &err);
        CHECK(!ownOk);
        if (ownOk) std::cerr << "  our reader read " << c.file << " (the case no longer tests a refusal)\n";
        else std::cout << "  our reader refuses it: " << err << "\n";
    }
    // Premise 2: Assimp, asked directly, reads it with triangles (or vertices, for a lines-only file).
    {
        ImportedModel m; std::string err;
        const bool aOk = ImportModelData(path, m, ImportBackend::Assimp, false, &err);
        CHECK(aOk);
        if (!aOk) std::cerr << "  Assimp refused " << c.file << ": " << err << "\n";
        CHECK(m.backend == "assimp");
        CHECK(!m.parts.empty());
        CHECK(Vertices(m) > 0);
        if (c.needTriangles) CHECK(Triangles(m) > 0);
    }

    // Our reader is forced on for this extension.
    CHECK(SetImportBackend(ext, ImportBackend::Own));
    CHECK(EffectiveImportBackend(ext) == ImportBackend::Own);

    // Asking for our reader by name: no fallback, the refusal stands (the fallback is only for the default choice).
    {
        ImportedModel m; std::string err;
        CHECK(!ImportModelData(path, m, ImportBackend::Own, false, &err));
        CHECK(!err.empty());
        CHECK(m.parts.empty());
    }
    // The default choice (what the editor and the player use): our reader runs first, refuses, Assimp reads the file.
    for (bool flatten : {false, true}) {
        ImportedModel m; std::string err;
        const bool ok = ImportModelData(path, m, ImportBackend::Default, flatten, &err);
        CHECK(ok);
        if (!ok) std::cerr << "  no fallback for " << c.file << " (flatten " << flatten << "): " << err << "\n";
        CHECK(m.backend == "assimp");
        CHECK(!m.parts.empty());
        CHECK(Vertices(m) > 0);
        if (c.needTriangles) CHECK(Triangles(m) > 0);
        if (ok) std::cout << "  read by " << m.backend << " (flatten " << flatten << "): " << m.parts.size() << " part(s), "
                          << Vertices(m) << " vertices, " << Triangles(m) << " triangles\n";
    }
    // The single-mesh path (ImportModel) and the scene path (ImportModelScene) go through the same seam.
    if (c.needTriangles) {
        bool ok = false;
        Mesh mesh = ImportModel(path, &ok);
        CHECK(ok);
        CHECK(!mesh.vertices.empty());
        CHECK(!mesh.triangles.empty());
        Scene scene;
        bool sok = false;
        GameObject* root = ImportModelScene(scene, path, &sok);
        CHECK(root != nullptr);
        CHECK(sok);
        if (root) CHECK(!root->GetComponentsInChildren<MeshRenderer>(true).empty());
    }
    SetImportBackend(ext, ImportBackend::Default);
}

static void FbxCase() {
    RUN_SUITE("fallback: FBX 6.1 ASCII (our FBX reader refuses, ufbx reads)");
    const std::string path = WriteFile("fallback_fbx61.fbx", Fbx61Ascii());

    // Premise 1: our FBX reader refuses the file.
    {
        fbx::Scene sc; std::string err;
        const bool ownOk = fbx::LoadFile(path, sc, &err);
        bool hasMesh = false;
        if (ownOk) for (const fbx::SceneNode& n : sc.nodes) if (n.mesh >= 0) hasMesh = true;
        CHECK(!ownOk);
        if (ownOk) std::cerr << "  our FBX reader accepted the FBX 6.1 file (" << (hasMesh ? "with" : "without") << " a mesh)\n";
        else std::cout << "  our FBX reader refuses it: " << err << "\n";
    }
    // Premise 2: Assimp refuses it as well (FBX below 7100), so an import that works can only be ufbx's.
    {
        ImportedModel m; std::string err;
        CHECK(!ImportModelData(path, m, ImportBackend::Assimp, false, &err));
    }

    const bool before = fbx::OwnReaderEnabled();
    fbx::SetOwnReaderEnabled(true);
    CHECK(fbx::OwnReaderEnabled());

    // The single-mesh path: ImportFbxMerged tries ours, then ufbx.
    {
        bool ok = false;
        Mesh mesh = ImportModel(path, &ok);
        CHECK(ok);
        CHECK(mesh.vertices.size() >= 4);
        CHECK(mesh.triangles.size() >= 6);   // the quad, as two triangles
        std::cout << "  ImportModel: " << mesh.vertices.size() << " vertices, " << mesh.triangles.size() / 3 << " triangles\n";
    }
    // The scene path: ImportFbxSceneGraph tries ours, then ufbx. ufbx keeps the node hierarchy, so the model's node
    // "Tri" is a child of the root with the mesh on it. (Had the scene-graph fallback not run, the import would end
    // in the single-mesh path, which puts the mesh on the root and makes no "Tri" child.)
    {
        Scene scene;
        bool ok = false;
        GameObject* root = ImportModelScene(scene, path, &ok);
        CHECK(root != nullptr);
        CHECK(ok);
        GameObject* tri = nullptr;
        if (root) root->ForEachDescendant([&](GameObject& g) { if (g.name == "Tri") tri = &g; });
        CHECK(tri != nullptr);
        if (tri) {
            MeshRenderer* mr = tri->GetComponent<MeshRenderer>();
            CHECK(mr != nullptr);
            if (mr) { CHECK(mr->mesh.vertices.size() >= 4); CHECK(mr->mesh.triangles.size() >= 6); }
        }
    }
    fbx::SetOwnReaderEnabled(before);
}

int main() {
    const std::vector<Case> cases = {
        {"OFF, 4OFF header (homogeneous coordinates)", "fallback_4off.off", Off4(), &ReadOffFile, true},
        {"SMD, version 2", "fallback_v2.smd", SmdVersion2(), &ReadSmdFile, true},
        {"STL, ASCII facet with four corners", "fallback_quad.stl", StlQuadFacet(), &ReadStlFile, true},
        {"Irrlicht mesh, vertexCount larger than the vertices given", "fallback_short.irrmesh", IrrMeshShortVertices(), &ReadIrrMeshFile, true},
        {"AC3D, only a line surface", "fallback_lines.ac", Ac3dOnlyLines(), &ReadAc3dFile, false},
    };
    for (const Case& c : cases) SeamCase(c);

    // The 4OFF vertices were divided by w: the fallback's numbers, not garbage.
    {
        SetImportBackend(".off", ImportBackend::Own);
        ImportedModel m;
        if (ImportModelData(kDir + "fallback_4off.off", m, ImportBackend::Default, false, nullptr) && !m.parts.empty()) {
            float maxX = -1e9f, maxY = -1e9f;
            for (const Vec3& v : m.parts[0].mesh.vertices) { maxX = std::max(maxX, v.x); maxY = std::max(maxY, v.y); }
            CHECK_NEAR(maxX, 1.0, 1e-5);
            CHECK_NEAR(maxY, 1.0, 1e-5);
        } else CHECK(false);
        SetImportBackend(".off", ImportBackend::Default);
    }

    FbxCase();

    std::error_code ec;
    fs::remove_all(kDir, ec);
    TEST_MAIN_RESULT();
}
