// The Xbox UWP build reads every model through OUR OWN readers only (docs/platform/xbox_uwp.md).
// This test compiles ModelImport.cpp and FbxScene.cpp the way okay_engine_uwp does (OKAY_XBOX_UWP, every OKAY_OWN_<FMT>_DEFAULT that is ON in the root
// CMakeLists.txt, no ufbx.h, no Assimp) and loads sample files from tests/data/models and tests/assets through them. It runs on Linux. UNVERIFIED on Windows.
// Written by us; the fixtures are this repository's own (scripts/gen_*_fixtures.py).
#include "test_framework.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Render/FbxReader.hpp"
#include "okay/Render/Mesh.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

using namespace okay;
static const std::string kRoot = std::string(OKAY_SOURCE_DIR) + "/";

static bool LoadsAsMesh(const std::string& rel, const char* what) {
    bool ok = false;
    Mesh m = ImportModel(kRoot + rel, &ok);
    const bool good = ok && !m.vertices.empty() && !m.triangles.empty();
    if (!good) std::fprintf(stderr, "  not loaded by the own reader: %s (%s)\n", rel.c_str(), what);
    return good;
}

int main() {
    // One real fixture per own reader that makes a single mesh. (Animation-only files such as .bvh are not meshes and are left out here.)
    struct Row { const char* file; const char* what; };
    const Row rows[] = {
        {"tests/data/models/cube.obj", "OBJ"}, {"tests/data/models/cube_binary.stl", "STL binary"}, {"tests/data/models/cube_ascii.stl", "STL ascii"},
        {"tests/data/models/cube_le.ply", "PLY binary"}, {"tests/data/models/cube_ascii.ply", "PLY ascii"}, {"tests/data/models/cube.3ds", "3DS"},
        {"tests/data/models/cube.off", "OFF"}, {"tests/data/models/arm.smd", "SMD"}, {"tests/data/models/skel.md5mesh", "MD5"}, {"tests/data/models/rig.ms3d", "MS3D"},
        {"tests/data/models/scene.ase", "ASE"}, {"tests/data/models/skinned_strip.dae", "Collada"}, {"tests/data/models/blend_attr_41.blend", "Blender"},
        {"tests/assets/fbx/cube_zup_cm_bin.fbx", "FBX binary"}, {"tests/assets/fbx/cube_zup_cm_ascii.fbx", "FBX ascii"}, {"tests/assets/fbx/rig_ascii.fbx", "FBX rig"},
        {"tests/assets/rigs/daz_t.glb", "glTF binary"},
    };
    RUN_SUITE("own readers only (UWP defines, no ufbx, no Assimp)");
    for (const Row& r : rows) CHECK(LoadsAsMesh(r.file, r.what));

    // No other reader exists in this build.
    const std::string exts = ImportableExtensions();
    for (const char* e : {".obj", ".gltf", ".glb", ".fbx", ".dae", ".stl", ".ply", ".3ds", ".off", ".smd", ".md5mesh", ".ms3d", ".ase", ".blend"})
        CHECK(exts.find(e) != std::string::npos);

    // The FBX reader is the own one even with a leftover OKAY_OWN_FBX=0 (it used to mean "use ufbx"; ufbx and the switch are deleted).
#if defined(_WIN32)
    _putenv_s("OKAY_OWN_FBX", "0");
#else
    setenv("OKAY_OWN_FBX", "0", 1);
#endif
    CHECK(LoadsAsMesh("tests/assets/fbx/cube_zup_cm_bin.fbx", "FBX with OKAY_OWN_FBX=0"));

    // A file our reader refuses fails with ok=false (never a crash, never a silent fallback): garbage under several extensions.
    const std::string tmp = std::string(OKAY_TEST_TMP_DIR) + "/";
    for (const char* ext : {".fbx", ".dae", ".3ds", ".blend", ".md5mesh", ".glb"}) {
        const std::string p = tmp + "garbage_uwp" + ext;
        { std::ofstream f(p, std::ios::binary); f << "this is not a model file at all, only text " << ext; }
        bool ok = true; Mesh m = ImportModel(p, &ok);
        CHECK(!ok);
        std::remove(p.c_str());
    }
    bool ok = true; Mesh m = ImportModel(kRoot + "tests/data/models/does_not_exist.fbx", &ok); CHECK(!ok && m.vertices.empty());
    TEST_MAIN_RESULT();
}
