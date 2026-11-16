// The sample files shipped for the own OpenGEX and MDL readers (examples/own_readers: cube_spin.ogex, quake_pyramid.mdl,
// halflife_tri.mdl; they are what docs/try_it.md section 8 tells the owner to import) must really import through the seam
// with the own backend and give what the docs say. The two .mdl files are the in-memory fixtures of tests/e4_fixtures.hpp
// written out, and this program checks that the committed bytes are still exactly those fixtures, so a change to a fixture
// cannot leave a stale sample behind. Run with `--write <dir>` to (re)write the two .mdl files into <dir>.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "e4_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

#ifndef OKAY_SOURCE_DIR
#error "OKAY_SOURCE_DIR is not defined"
#endif

static std::string ReadAll(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
    if (argc == 3 && std::strcmp(argv[1], "--write") == 0) {
        const fs::path out = argv[2];
        for (const auto& kv : {std::make_pair("quake_pyramid.mdl", e4::QuakePyramidMdl()), std::make_pair("halflife_tri.mdl", e4::HalfLifeMdl())}) {
            std::ofstream f(out / kv.first, std::ios::binary);
            f.write(kv.second.data(), (std::streamsize)kv.second.size());
        }
        return 0;
    }
    RUN_SUITE("own_e4_samples");
    const fs::path dir = fs::path(OKAY_SOURCE_DIR) / "examples" / "own_readers";

    // the committed .mdl bytes are the fixtures
    CHECK(ReadAll(dir / "quake_pyramid.mdl") == e4::QuakePyramidMdl());
    CHECK(ReadAll(dir / "halflife_tri.mdl") == e4::HalfLifeMdl());

    SetImportBackend("*", ImportBackend::Own);
    std::string err;
    {   // OpenGEX: a red cube, 24 vertices, 12 triangles, one material, one clip "spin" of 2 seconds
        ImportedModel m;
        const bool ok = ImportModelData((dir / "cube_spin.ogex").string(), m, ImportBackend::Default, false, &err);
        if (!ok) std::fprintf(stderr, "  cube_spin.ogex: %s\n", err.c_str());
        CHECK(ok && m.backend == "own");
        CHECK(m.parts.size() == 1 && fx::TriCount(m) == 12 && m.parts[0].mesh.vertices.size() == 24);
        CHECK(m.materials.size() == 1 && m.materials[0].name == "red" && m.materials[0].hasDiffuse);
        CHECK_NEAR(m.materials[0].diffuse.r, 0.8f, 1e-5f);
        CHECK(m.clips.size() == 1 && m.clips[0].name == "spin");
        CHECK(m.clips[0].channels.size() == 1 && m.clips[0].channels[0].node == "cube" && !m.clips[0].channels[0].rotation.empty());
        CHECK_NEAR(m.clips[0].channels[0].rotation.back().time, 2.0f, 1e-4f);
        for (const Vec3& v : m.parts[0].mesh.vertices) CHECK(std::fabs(v.x) == 0.5f && std::fabs(v.y) == 0.5f && std::fabs(v.z) == 0.5f);
    }
    {   // Quake 1: the first frame as one static part (a pyramid: 4 vertices, 4 triangles), Z-up turned into Y-up, sitting on the ground
        ImportedModel m;
        const bool ok = ImportModelData((dir / "quake_pyramid.mdl").string(), m, ImportBackend::Default, false, &err);
        if (!ok) std::fprintf(stderr, "  quake_pyramid.mdl: %s\n", err.c_str());
        CHECK(ok && m.backend == "own" && m.parts.size() == 1 && fx::TriCount(m) == 4 && m.parts[0].mesh.vertices.size() == 4);
        ImportedModel f;
        CHECK(ImportModelData((dir / "quake_pyramid.mdl").string(), f, ImportBackend::Default, true, &err));   // flatten bakes the root's turn
        float lo[3] = {9, 9, 9}, hi[3] = {-9, -9, -9};
        for (const ImportedPart& p : f.parts) for (const Vec3& v : p.mesh.vertices) {
            const float c[3] = {v.x, v.y, v.z};
            for (int k = 0; k < 3; ++k) { lo[k] = std::fmin(lo[k], c[k]); hi[k] = std::fmax(hi[k], c[k]); }
        }
        CHECK_NEAR(lo[1], 0.0f, 1e-4f);              // on the ground
        CHECK_NEAR(hi[1], 255.0f / 128.0f, 1e-4f);   // the apex, byte 255 * 1/128
        CHECK_NEAR(lo[0], -1.0f, 1e-4f);
        CHECK_NEAR(hi[0], 255.0f / 128.0f - 1.0f, 1e-4f);
    }
    {   // Half-Life 1: two bones, one triangle, one clip "walk" (4 frames at 10 per second)
        ImportedModel m;
        const bool ok = ImportModelData((dir / "halflife_tri.mdl").string(), m, ImportBackend::Default, false, &err);
        if (!ok) std::fprintf(stderr, "  halflife_tri.mdl: %s\n", err.c_str());
        CHECK(ok && m.backend == "own" && fx::TriCount(m) == 1 && m.nodes.size() >= 3);
        CHECK(m.clips.size() == 1 && m.clips[0].name == "walk");
    }
    SetImportBackend("*", ImportBackend::Default);
    TEST_MAIN_RESULT();
}
