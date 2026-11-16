#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/SceneTemplates.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace okay;
namespace fs = std::filesystem;

// examples/voxel: the scenes the owner opens to try digging (docs/try_it.md 7s). They are made by the scene writer from the
// Sandbox template, never by hand:
//     OKAY_WRITE_VOXEL_EXAMPLES=1 ./test_voxel_examples        (rewrites examples/voxel/*.okayscene in the source tree)
// A normal run checks that the shipped files open, hold the terrain and the digger they promise, and that digging in them works.
namespace {

void AddHelp(Scene& s, const char* text, float y) {
    GameObject* g = s.CreateGameObject("Voxel Perf Help");
    auto* t = g->AddComponent<TextRenderer>();
    t->text = text;
    t->screenSpace = true;
    t->anchor = UIAnchor::StretchHorizontal;
    t->screenPos = {16, Templates::kHudTop + y}; t->size = {16, 72};
    t->vcenter = false; t->wrap = true; t->pixelSize = 2.0f; t->outline = true;
}

// The owner's Sandbox, with a line about where to read the cost of a dig.
void BuildSandbox(Scene& s) {
    Templates::VoxelSandbox(s);
    AddHelp(s, "Hold LEFT mouse to dig, RIGHT mouse to build. F3 shows the frame rate. In the editor, View > Panels > Profiler shows Voxel.Edit, Voxel.Mesh and Voxel.Upload.", 74.0f);
}

// The same scene on a bigger hill (918 thousand voxels instead of 228 thousand): where the old edit path really stutters.
void BuildBigWorld(Scene& s) {
    Templates::VoxelSandbox(s, 128, 56, 128, 12.0f, 77u);
    AddHelp(s, "Big world: 128 x 56 x 128 voxels. Hold LEFT mouse to dig, RIGHT mouse to build. F3 shows the frame rate. Start with OKAY_VOXEL_OLD_REMESH=1 to feel the old behaviour.", 74.0f);
}

// Real-life digging (docs/try_it.md 7t): the same hill with dirt, sand, stone and ore in layers, a digger that keeps what it
// digs (debris you can pick up, a count on screen), building that spends it, loose ground that slumps and pieces that fall.
void BuildRealistic(Scene& s) {
    Templates::VoxelSandbox(s);
    GameObject* g = s.Find("Terrain");
    auto* v = g->GetComponent<VoxelTerrain>();
    v->GenerateMaterials(2024u);
    v->Apply();
    auto* d = g->GetComponent<VoxelDigger>();
    d->ApplyPreset(VoxelDigger::Preset::Realistic);
    d->addMaterial = v->MaterialIndex("Dirt");
    d->hudTarget = "Dig Count";
    d->giveItems = false;
    GameObject* c = s.CreateGameObject("Dig Count");
    auto* t = c->AddComponent<TextRenderer>();
    t->text = "Nothing collected";
    t->screenSpace = true;
    t->anchor = UIAnchor::TopLeft;
    t->screenPos = {16, Templates::kHudTop + 104.0f}; t->size = {420, 28};
    t->vcenter = false; t->pixelSize = 2.0f; t->outline = true;
    AddHelp(s, "Real-life digging: hold LEFT mouse to dig - the ground falls as debris and the count below rises when you pick it up. RIGHT mouse builds with what you hold. Ctrl+Z takes back the last stroke. Stone is hard: dig it slowly.", 74.0f);
}

struct Example { const char* file; void (*build)(Scene&); int nx, ny, nz; };
const Example kExamples[] = {
    {"voxel_sandbox.okayscene", &BuildSandbox, 72, 44, 72},
    {"voxel_big_world.okayscene", &BuildBigWorld, 128, 56, 128},
    {"voxel_realistic.okayscene", &BuildRealistic, 72, 44, 72},
};

} // namespace

int main() {
    RUN_SUITE("voxel_examples");
    const fs::path dir = fs::path(OKAY_SOURCE_DIR) / "examples" / "voxel";

    if (const char* w = std::getenv("OKAY_WRITE_VOXEL_EXAMPLES"); w && *w && *w != '0') {
        std::error_code ec;
        fs::create_directories(dir, ec);
        for (const Example& e : kExamples) {
            Scene s("example");
            e.build(s);
            std::string err;
            const bool ok = SceneSerializer::SaveToFile(s, (dir / e.file).string(), &err);
            std::cout << (ok ? "wrote " : "FAILED ") << (dir / e.file).string() << " " << err << "\n";
            CHECK(ok);
        }
        TEST_MAIN_RESULT();
    }

    for (const Example& e : kExamples) {
        const fs::path path = dir / e.file;
        CHECK(fs::exists(path));
        if (!fs::exists(path)) continue;
        std::ifstream in(path, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        CHECK(text.size() > 1000);
        Scene s("loaded");
        std::string err;
        const bool ok = SceneSerializer::Deserialize(s, text, &err);
        if (!ok) std::cout << "  " << e.file << ": " << err << "\n";
        CHECK(ok);

        GameObject* ground = s.Find("Terrain");
        CHECK(ground != nullptr);
        auto* v = ground ? ground->GetComponent<VoxelTerrain>() : nullptr;
        CHECK(v != nullptr);
        if (!v) continue;
        CHECK(v->nx == e.nx && v->ny == e.ny && v->nz == e.nz);
        CHECK(ground->GetComponent<VoxelDigger>() != nullptr);
        CHECK(s.Find("Player") != nullptr && s.Find("FPS Camera") != nullptr);
        CHECK(s.Find("Water") != nullptr);
        CHECK(s.Find("Voxel Perf Help") != nullptr);
        if (std::string(e.file) == "voxel_realistic.okayscene") {
            auto* vd = ground->GetComponent<VoxelDigger>();
            CHECK(v->UseMaterials() && v->materials.size() == 7 && v->material.size() == v->density.size());
            CHECK(vd && vd->collect == VoxelDigger::Collect::Both && vd->needsMaterial && vd->slump && vd->collapse && vd->hudTarget == "Dig Count");
            CHECK(s.Find("Dig Count") && s.Find("Dig Count")->GetComponent<TextRenderer>());
            int kinds = 0, seen[7] = {0};
            for (std::size_t i = 0; i < v->material.size(); ++i) if (v->density[i] > v->iso && v->material[i] < 7 && !seen[v->material[i]]++) ++kinds;
            CHECK(kinds >= 4);                                 // grass, dirt, stone and ore at least
        }
        // the density really came through: solid ground and open air, and a surface to walk on
        int solid = 0, air = 0;
        for (float d : v->density) { if (d > v->iso) ++solid; else ++air; }
        CHECK(solid > v->Count() / 10 && air > v->Count() / 10);
        float sy = 0.0f;
        CHECK(v->SurfaceY(0, 0, sy));
        CHECK(ground->GetComponent<MeshRenderer>() && ground->GetComponent<MeshRenderer>()->mesh.vertices.size() > 1000);

        // Play for a short while: every frame runs, and a dig at the surface changes the field and gets re-drawn
        s.Update(1.0f / 60.0f);
        for (int i = 0; i < 20; ++i) s.Update(1.0f / 60.0f);
        const std::vector<float> before = v->density;
        CHECK(v->Dig({0.0f, sy, 0.0f}, 3.0f, 3.0f));
        v->Apply();
        CHECK(v->density != before);
        CHECK(v->lastStats.chunksRemeshed >= 1);
        CHECK(v->UsesChunkObjects());                      // the running game draws the dug terrain with chunk objects
        CHECK(v->ChunkObjectCount() > 4);
    }

    TEST_MAIN_RESULT();
}
