// Our own ASE reader: fixtures from scripts/gen_ase_fixtures.py (tests/data/models/scene*.ase, synthetic), values computed by
// hand, the import seam (opt-in switch, scene import, UTF-8 paths, UTF-16 text, locale) and malformed input.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

using namespace okay;

static void TestSetEnv(const char* n, const char* v) {
#ifdef _WIN32
    _putenv_s(n, v);
#else
    setenv(n, v, 1);
#endif
}
static void TestUnsetEnv(const char* n) {
#ifdef _WIN32
    _putenv_s(n, "");
#else
    unsetenv(n);
#endif
}
static const std::string kDir = std::string(OKAY_SOURCE_DIR) + "/tests/data/models/";
static std::string Slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static bool HasNote(const ImportedModel& m, const char* part) {
    for (const std::string& n : m.notes) if (n.find(part) != std::string::npos) return true;
    return false;
}
static bool Near(const Vec3& a, const Vec3& b, float e = 1e-4f) {
    return std::fabs(a.x - b.x) < e && std::fabs(a.y - b.y) < e && std::fabs(a.z - b.z) < e;
}
static const ImportedChannel* Chan(const ImportedModel& m, const char* node) {
    if (m.clips.empty()) return nullptr;
    for (const ImportedChannel& c : m.clips[0].channels) if (c.node == node) return &c;
    return nullptr;
}
static bool HasVert(const Mesh& me, const Vec3& v) {
    for (const Vec3& p : me.vertices) if (Near(p, v)) return true;
    return false;
}

int main() {
    RUN_SUITE("ase_reader");
    std::string err;

    for (const char* file : {"scene.ase", "scene_crlf.ase", "scene_utf16le.ase"}) {
        ImportedModel m;
        CHECK(ReadAseFile(kDir + file, m, &err));
        CHECK(m.backend == "own" && m.nodes.size() == 6 && m.parts.size() == 3 && m.materials.size() == 3 && m.clips.size() == 1);
        if (m.nodes.size() != 6 || m.parts.size() != 3) continue;
        CHECK(m.nodes[0].name == "ASE" && m.nodes[1].name == "Plate" && m.nodes[2].name == "Child" && m.nodes[2].parent == 1);
        CHECK(Near(m.nodes[0].rotation * Vec3{0, 0, 1}, Vec3{0, 1, 0}));                              // Z up becomes Y up
        CHECK(Near(m.nodes[1].position, Vec3{10, 0, 0}));
        CHECK(Near(m.nodes[2].position, Vec3{0, 2, 0}) && Near(m.nodes[2].rotation * Vec3{1, 0, 0}, Vec3{0, 1, 0}));   // relative to Plate, 90 degrees about Z
        CHECK(m.nodes[3].name == "Dummy" && Near(m.nodes[3].position, Vec3{0, 0, 5}));
        CHECK(m.nodes[4].name == "Cam" && Near(m.nodes[4].position, Vec3{0, -20, 3}) && m.nodes[5].name == "Lamp");   // the target transform is not a node
        // materials, made when first used: the two sub-materials of the multi material, then the plain one
        CHECK(m.materials[0].name == "red" && m.materials[1].name == "blue" && m.materials[2].name == "plain");
        CHECK(m.materials[2].texture == kDir + "skin.jpg");                      // an absolute Windows path keeps only its file name
        CHECK(m.materials[1].texture == kDir + "blue.png" && m.materials[0].texture.empty());
        CHECK(m.materials[2].hasDiffuse && std::fabs(m.materials[2].diffuse.g - 0.25f) < 1e-6f && std::fabs(m.materials[2].diffuse.a - 0.5f) < 1e-6f);
        CHECK_NEAR(m.materials[2].shininess, 32.0f, 1e-4);
        // Plate: world vertices become local; one part per sub-material
        const ImportedPart& red = m.parts[0];
        const ImportedPart& blue = m.parts[1];
        const ImportedPart& child = m.parts[2];
        CHECK(red.node == 1 && red.material == 0 && red.mesh.triangles.size() == 3 && red.mesh.vertices.size() == 3);
        CHECK(blue.node == 1 && blue.material == 1 && blue.mesh.triangles.size() == 3 && blue.mesh.vertices.size() == 3);
        CHECK(HasVert(red.mesh, Vec3{0, 0, 0}) && HasVert(red.mesh, Vec3{1, 0, 0}) && HasVert(red.mesh, Vec3{1, 1, 0}));
        CHECK(HasVert(blue.mesh, Vec3{0, 1, 0}));
        CHECK(red.mesh.uvs.size() == 3);
        {   // vertex 2 (1,1,0) has texture coordinate (1,1): v flips to 0
            std::size_t vi = 99;
            for (std::size_t i = 0; i < red.mesh.vertices.size(); ++i) if (Near(red.mesh.vertices[i], Vec3{1, 1, 0})) vi = i;
            CHECK(vi < 3 && std::fabs(red.mesh.uvs[vi].x - 1.0f) < 1e-6f && std::fabs(red.mesh.uvs[vi].y - 0.0f) < 1e-6f);
        }
        // smoothing: faces 0 and 1 share group 1, so a shared corner (vertex 0) gets one averaged normal; all lie in the z = 0 plane: +z
        CHECK(red.mesh.normals.size() == 3 && Near(red.mesh.normals[0], Vec3{0, 0, 1}));
        // Child: vertices relative to its own node (a unit right triangle), normals from the file
        CHECK(child.node == 2 && child.material == 2 && child.mesh.triangles.size() == 3 && child.mesh.vertices.size() == 3);
        CHECK(HasVert(child.mesh, Vec3{0, 0, 0}) && HasVert(child.mesh, Vec3{1, 0, 0}) && HasVert(child.mesh, Vec3{0, 1, 0}));
        CHECK(child.mesh.uvs.empty() && Near(child.mesh.normals[0], Vec3{0, 0, 1}));
        // animation: ticks / 4800 seconds; rotation samples are relative and follow 3ds Max's sign
        const ImportedChannel* plate = Chan(m, "Plate");
        CHECK(plate && m.clips[0].channels.size() == 2);
        {   // the camera's own animation, not its target's
            const ImportedChannel* cam = Chan(m, "Cam");
            CHECK(cam && cam->position.size() == 2 && Near(cam->position[1].value, Vec3{0, -25, 3}));
            CHECK(HasNote(m, "target animation block"));
        }
        if (plate) {
            CHECK(plate->position.size() == 2 && plate->rotation.size() == 2);
            CHECK_NEAR(plate->position[1].time, 1.0f, 1e-6);
            CHECK(Near(plate->position[1].value, Vec3{10, 5, 0}));
            CHECK(Near(plate->rotation[0].value * Vec3{1, 0, 0}, Vec3{1, 0, 0}));
            CHECK(Near(plate->rotation[1].value * Vec3{1, 0, 0}, Vec3{0, -1, 0}));   // axis +z, angle 90: the rotation is the inverse of the right-handed one
        }
        CHECK(HasNote(m, "1 camera(s)") && HasNote(m, "1 light(s)") && HasNote(m, "1 helper object(s)"));
        CHECK(HasNote(m, "second node transform"));
        CHECK(HasNote(m, "vertex color block"));
        CHECK(HasNote(m, "tiling/offset/rotation"));
        CHECK(HasNote(m, "absolute Windows paths"));
        CHECK(HasNote(m, "1 face(s) with an out-of-range"));
        CHECK(HasNote(m, "without texture coordinates"));
        if (std::string(file) == "scene_utf16le.ase") CHECK(HasNote(m, "UTF-16"));
    }
    {   // a file without objects, junk, truncation
        ImportedModel m;
        const std::string good = Slurp(kDir + "scene.ase");
        const char* bad[] = {"", "hello", "*3DSMAX_ASCIIEXPORT 200\n", "*3DSMAX_ASCIIEXPORT 200\n*SCENE {\n}\n", "*GEOMOBJECT {\n}\n", "*3DSMAX_ASCIIEXPORT 200\n}\n", "*3DSMAX_ASCIIEXPORT 200\n*GEOMOBJECT {\n*NODE_NAME \"a\"\n",
                             "*3DSMAX_ASCIIEXPORT 200\n*GEOMOBJECT {\n*MESH {\n*MESH_NUMVERTEX 2000000000\n}\n}\n", "*3DSMAX_ASCIIEXPORT 200\n*GEOMOBJECT {\n*MESH {\n*MESH_NUMVERTEX 1\n*MESH_VERTEX_LIST {\n*MESH_VERTEX 5 0 0 0\n}\n}\n}\n",
                             "*3DSMAX_ASCIIEXPORT 200\n*GEOMOBJECT {\n*MESH {\n*MESH_NUMVERTEX 1\n*MESH_VERTEX_LIST {\n*MESH_VERTEX 0 nan 0 0\n}\n}\n}\n",
                             "*3DSMAX_ASCIIEXPORT 200\n*GEOMOBJECT {\n*NODE_TM {\n*TM_ROW0 1 x 0\n}\n}\n"};
        for (const char* b : bad) { CHECK(!ReadAseBuffer(b, "", m, &err) && !err.empty() && m.nodes.empty() && m.parts.empty()); }
        for (std::size_t cut = 0; cut < good.size(); cut += 7) {
            ImportedModel t; std::string e;
            const bool ok = ReadAseBuffer(good.substr(0, cut), "", t, &e);
            CHECK(ok || (!e.empty() && t.nodes.empty()));
        }
        CHECK(!ReadAseFile(kDir + "does_not_exist.ase", m, &err) && err == "cannot open file");
        // a parent that does not exist, a parent loop and a repeated name do not fail: notes
        const std::string tri = "*MESH {\n*MESH_NUMVERTEX 3\n*MESH_NUMFACES 1\n*MESH_VERTEX_LIST {\n*MESH_VERTEX 0 0 0 0\n*MESH_VERTEX 1 1 0 0\n*MESH_VERTEX 2 0 1 0\n}\n*MESH_FACE_LIST {\n*MESH_FACE 0: A: 0 B: 1 C: 2 *MESH_SMOOTHING 1 *MESH_MTLID 0\n}\n}\n";
        const std::string a = "*GEOMOBJECT {\n*NODE_NAME \"a\"\n*NODE_PARENT \"b\"\n" + tri + "}\n";
        const std::string b = "*GEOMOBJECT {\n*NODE_NAME \"b\"\n*NODE_PARENT \"a\"\n" + tri + "}\n";
        const std::string c = "*GEOMOBJECT {\n*NODE_NAME \"c\"\n*NODE_PARENT \"nobody\"\n" + tri + "}\n";
        const std::string d = "*GEOMOBJECT {\n*NODE_NAME \"c\"\n" + tri + "}\n";
        CHECK(ReadAseBuffer("*3DSMAX_ASCIIEXPORT 200\n" + a + b + c + d, "", m, &err) && m.nodes.size() == 5 && m.parts.size() == 4);
        for (std::size_t i = 0; i < m.nodes.size(); ++i) CHECK(m.nodes[i].parent < (int)i);
        CHECK(HasNote(m, "parent loop") && HasNote(m, "does not exist") && HasNote(m, "repeated") && HasNote(m, "without a node transform"));
    }
    {   // a decimal-comma locale does not change what a '.' number means
        ImportedModel m;
        const char* got = std::setlocale(LC_ALL, "de_DE.UTF-8");
        if (!got) got = std::setlocale(LC_ALL, "de_DE");
        if (got) {
            CHECK(ReadAseBuffer(Slurp(kDir + "scene.ase"), "", m, &err) && m.nodes.size() == 6);
            if (m.nodes.size() == 6) CHECK(Near(m.nodes[2].position, Vec3{0, 2, 0}));
            const ImportedChannel* plate = Chan(m, "Plate");
            if (plate && plate->rotation.size() == 2) CHECK(Near(plate->rotation[1].value * Vec3{1, 0, 0}, Vec3{0, -1, 0}));
            std::setlocale(LC_ALL, "C");
        } else std::printf("  (no German locale installed: locale check skipped)\n");
    }
    {   // a UTF-8 path with characters outside the ANSI code page
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / fs::u8path(u8"okay_ase_Müller_立方");
        std::error_code ec;
        fs::create_directories(dir, ec);
        { std::ofstream f(dir / fs::u8path(u8"立方.ase"), std::ios::binary); f << Slurp(kDir + "scene.ase"); }
        ImportedModel m;
        CHECK(ReadAseFile((dir / fs::u8path(u8"立方.ase")).u8string(), m, &err) && m.parts.size() == 3);
        fs::remove_all(dir, ec);
    }

    {   // the seam
        CHECK(HasOwnReader(".ase") && HasOwnReader(".ASE"));
        TestUnsetEnv("OKAY_OWN_ASE");
        SetImportBackend(".ase", ImportBackend::Default);
        ImportedModel m;
        if (!AssimpAvailable() && EffectiveImportBackend(".ase") == ImportBackend::Default) {
            CHECK(!ImportModelData(kDir + "scene.ase", m, ImportBackend::Default, false, &err));
            CHECK(err.find("OKAY_OWN_ASE") != std::string::npos);
            CHECK(ImportableExtensions().find(".ase") == std::string::npos);
        }
        TestSetEnv("OKAY_OWN_ASE", "1");
        CHECK(EffectiveImportBackend(".ase") == ImportBackend::Own);
        CHECK(ImportableExtensions().find(".ase") != std::string::npos);
        CHECK(ImportModelData(kDir + "scene.ase", m, ImportBackend::Default, false, &err) && m.backend == "own" && m.parts.size() == 3);
        TestSetEnv("OKAY_OWN_ASE", "0");
        CHECK(EffectiveImportBackend(".ase") != ImportBackend::Own);
        CHECK(SetImportBackend(".ase", ImportBackend::Own));
        CHECK(EffectiveImportBackend(".ase") == ImportBackend::Own);
        {
            Scene scene; scene.physicsEnabled = false;
            bool ok = false;
            GameObject* root = ImportModelScene(scene, kDir + "scene.ase", &ok);
            CHECK(ok && root != nullptr);
            if (root) {
                CHECK(root->GetComponentsInChildren<MeshRenderer>(true).size() == 3);
                ModelAnimator* ma = root->GetComponent<ModelAnimator>();
                CHECK(ma && ma->clips.size() == 1 && ma->clips[0].nodes.size() == 2);
            }
        }
        {   // flatten: one static mesh with every node transform and the Z-up turn baked in
            bool mok = false; Mesh merged = ImportModel(kDir + "scene.ase", &mok);
            CHECK(mok && merged.TriangleCount() == 3 && merged.vertices.size() == 9);
            bool found = false;
            for (const Vec3& v : merged.vertices) if (Near(v, Vec3{9, 0, -2})) found = true;   // the Child's (0,1,0) corner: world (9,2,0) in the file
            CHECK(found);
            ImportedModel f;
            CHECK(ImportModelData(kDir + "scene.ase", f, ImportBackend::Default, true, &err) && f.nodes.size() == 1 && f.clips.empty());
        }
        SetImportBackend(".ase", ImportBackend::Default);
        TestUnsetEnv("OKAY_OWN_ASE");
    }

    TEST_MAIN_RESULT();
}
