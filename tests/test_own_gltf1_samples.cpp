// The glTF 1.0 sample files shipped for our own glTF 1.0 reader (examples/own_readers/box_v1.gltf and box_v1.glb, written by
// scripts/gen_gltf1_samples.py; docs/try_it.md section 8 tells the owner to import them) must import through the seam with the
// own reader switched on and give what the docs say: an orange box of 24 vertices and 12 triangles on a node 0.5 up, one
// material. Both forms (JSON with a data URI, and the binary KHR_binary_glTF container) must agree. With the switch off the
// import must be refused with a message that names the switch (the Windows release has no other glTF 1.0 reader).
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include <Okay.hpp>
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

#ifndef OKAY_SOURCE_DIR
#error "OKAY_SOURCE_DIR is not defined"
#endif

int main() {
#ifdef _WIN32
    _putenv_s("OKAY_OWN_GLTF1", "");
#else
    unsetenv("OKAY_OWN_GLTF1");
#endif
    RUN_SUITE("own_gltf1_samples");
    const fs::path dir = fs::path(OKAY_SOURCE_DIR) / "examples" / "own_readers";
    for (const char* name : {"box_v1.gltf", "box_v1.glb"}) {
        const std::string path = (dir / name).string();
        std::string err;
        {   // switched on at run time (what OKAY_OWN_GLTF1=1 does)
            SetImportBackend(".gltf1", ImportBackend::Own);
            ImportedModel m;
            const bool ok = ImportModelData(path, m, ImportBackend::Default, false, &err);
            if (!ok) std::fprintf(stderr, "  %s: %s\n", name, err.c_str());
            CHECK(ok && m.backend == "own");
            CHECK(m.parts.size() == 1 && fx::TriCount(m) == 12 && m.parts[0].mesh.vertices.size() == 24 && m.parts[0].mesh.normals.size() == 24 && m.parts[0].mesh.uvs.size() == 24);
            CHECK(m.materials.size() == 1 && m.materials[0].name == "orange" && m.materials[0].hasDiffuse);
            CHECK_NEAR(m.materials[0].diffuse.r, 0.95f, 1e-5f);
            CHECK_NEAR(m.materials[0].shininess, 24.0f, 1e-4f);
            // the node tree: the root "glTF", then "box" at (0, 0.5, 0)
            CHECK(m.nodes.size() == 2 && m.nodes[1].name == "box");
            if (m.nodes.size() == 2) CHECK_NEAR(m.nodes[1].position.y, 0.5f, 1e-6f);
            for (const Vec3& v : m.parts[0].mesh.vertices) CHECK(std::fabs(v.x) == 0.5f && std::fabs(v.y) == 0.5f && std::fabs(v.z) == 0.5f);
            // flatten bakes the node's move: the box now spans y 0 to 1
            ImportedModel f;
            CHECK(ImportModelData(path, f, ImportBackend::Default, true, &err));
            float lo = 9, hi = -9;
            for (const ImportedPart& p : f.parts) for (const Vec3& v : p.mesh.vertices) { lo = std::fmin(lo, v.y); hi = std::fmax(hi, v.y); }
            CHECK_NEAR(lo, 0.0f, 1e-5f);
            CHECK_NEAR(hi, 1.0f, 1e-5f);
        }
        {   // not switched on (a build without -DOKAY_OWN_GLTF1=ON): our reader is not used, and the scene import is refused
            SetImportBackend(".gltf1", ImportBackend::Default);
            if (EffectiveImportBackend(".gltf1") != ImportBackend::Own) {
                ImportedModel m;
                CHECK(!ImportModelData(path, m, ImportBackend::Default, false, &err) || m.backend != "own");
                Scene scene; scene.physicsEnabled = false;
                bool ok = true;
                CHECK(ImportModelScene(scene, path, &ok) == nullptr && !ok);
            }
        }
    }
    SetImportBackend(".gltf1", ImportBackend::Default);
    TEST_MAIN_RESULT();
}
