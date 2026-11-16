// The sample maps shipped for the own Q3BSP reader (examples/own_readers/tiny_arena.bsp and tiny_arena.pk3, written by
// scripts/gen_q3bsp_samples.py, which is an independent second writer of the format) must import through the seam with the own
// backend and give what docs/try_it.md says: 2 nodes, 4 parts, 3 materials, 86 triangles, a bump whose top is at z = 40, a platform
// on its own node. The .pk3 holds the same map deflated, so it also exercises the engine's inflate.
#include "test_framework.hpp"
#include "model_fixture_util.hpp"
#include "okay/Render/ModelData.hpp"
#include "okay/Render/ModelImport.hpp"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace okay;
namespace fs = std::filesystem;

#ifndef OKAY_SOURCE_DIR
#error "OKAY_SOURCE_DIR is not defined"
#endif

int main() {
    RUN_SUITE("own_q3bsp_samples");
    const fs::path dir = fs::path(OKAY_SOURCE_DIR) / "examples" / "own_readers";
    SetImportBackend("*", ImportBackend::Own);
    for (const char* f : {"tiny_arena.bsp", "tiny_arena.pk3"}) {
        ImportedModel m; std::string err;
        const bool ok = ImportModelData((dir / f).string(), m, ImportBackend::Default, false, &err);
        if (!ok) std::fprintf(stderr, "  %s: %s\n", f, err.c_str());
        CHECK(ok && m.backend == "own" && fx::Sane(m));
        CHECK(m.nodes.size() == 2 && m.nodes[1].name == "model_1" && m.parts.size() == 4 && m.materials.size() == 3 && fx::TriCount(m) == 86);
        CHECK(m.materials[0].name == "textures/own/floor" && m.materials[1].name == "textures/own/wall" && m.materials[2].name == "textures/own/bump");
        std::size_t verts = 0; for (const ImportedPart& p : m.parts) verts += p.mesh.vertices.size();
        CHECK(verts == 77);
        float top = -1e9f, lo = 1e9f, hi = -1e9f;
        for (const ImportedPart& p : m.parts) if (p.material == 2) for (const Vec3& v : p.mesh.vertices) top = std::max(top, v.z);
        for (const ImportedPart& p : m.parts) for (const Vec3& v : p.mesh.vertices) { lo = std::min(lo, v.z); hi = std::max(hi, v.z); }
        CHECK(std::fabs(top - 40.f) < 1e-3f && lo == 0.f && hi == 256.f);
        // the platform is the only part on node 1: 4 vertices at z = 64
        int onPlatform = 0;
        for (const ImportedPart& p : m.parts) if (p.node == 1) { ++onPlatform; CHECK(p.mesh.vertices.size() == 4); for (const Vec3& v : p.mesh.vertices) CHECK(v.z == 64.f); }
        CHECK(onPlatform == 1);
        // the floor faces up (+z) and the walls face into the room: every triangle's geometric normal agrees with its stored vertex normal
        int disagree = 0;
        for (const ImportedPart& p : m.parts) {
            const Mesh& me = p.mesh;
            for (std::size_t i = 0; i + 2 < me.triangles.size(); i += 3) {
                const Vec3 &a = me.vertices[(std::size_t)me.triangles[i]], &b = me.vertices[(std::size_t)me.triangles[i + 1]], &c = me.vertices[(std::size_t)me.triangles[i + 2]];
                const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z, vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
                const Vec3 n = me.normals[(std::size_t)me.triangles[i]];
                if ((uy * vz - uz * vy) * n.x + (uz * vx - ux * vz) * n.y + (ux * vy - uy * vx) * n.z <= 0) ++disagree;
            }
        }
        CHECK(disagree == 0);
    }
    SetImportBackend("*", ImportBackend::Default);
    TEST_MAIN_RESULT();
}
