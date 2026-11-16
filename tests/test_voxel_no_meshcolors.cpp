#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Modeling/MeshAsset.hpp"
#include <iostream>
#include <sstream>
#include <string>

using namespace okay;

// A VoxelTerrain builds its own MeshRenderer mesh (and its per-face colours)
// from the voxels every time it loads. The scene file must not also carry that
// mesh's `meshcolors`: it is derived data that more than doubled the file
// (#888 finding 24). A normal mesh with painted colours still saves them, and an
// old file that does carry the voxel colours still loads to the same terrain.

namespace {

// The `meshcolors` line the serializer writes for a mesh, taken from a plain
// (non-voxel) object holding the same mesh, so its text is exactly what an old
// save wrote for the terrain.
std::string MeshColorsLineFor(const Mesh& mesh) {
    Scene tmp;
    GameObject* o = tmp.CreateGameObject("Copy");
    o->AddComponent<MeshRenderer>()->mesh = mesh;
    const std::string text = SceneSerializer::Serialize(tmp);
    const auto at = text.find("  meshcolors ");
    if (at == std::string::npos) return {};
    const auto end = text.find('\n', at);
    return text.substr(at, end - at + 1);
}

std::size_t Count(const std::string& s, const std::string& what) {
    std::size_t n = 0;
    for (auto p = s.find(what); p != std::string::npos; p = s.find(what, p + 1)) ++n;
    return n;
}

bool SameColors(const Mesh& a, const Mesh& b) {
    if (a.triColors.size() != b.triColors.size()) return false;
    for (std::size_t i = 0; i < a.triColors.size(); ++i) {
        const Color& x = a.triColors[i];
        const Color& y = b.triColors[i];
        if (x.r != y.r || x.g != y.g || x.b != y.b || x.a != y.a) return false;
    }
    return true;
}

const Mesh* TerrainMesh(const Scene& scene, const char* name) {
    GameObject* go = scene.Find(name);
    if (!go || !go->GetComponent<VoxelTerrain>()) return nullptr;
    auto* mr = go->GetComponent<MeshRenderer>();
    return mr ? &mr->mesh : nullptr;
}

// Save, reload and compare one scene whose terrain object is called `name`.
void CheckTerrainScene(Scene& scene, const char* name, const char* label) {
    const Mesh* orig = TerrainMesh(scene, name);
    CHECK(orig != nullptr);
    if (!orig) return;
    CHECK(orig->HasFaceColors());   // the terrain really has colours to (not) save
    const std::uint64_t origHash = modeling::GeometryHash(*orig);

    const std::string after = SceneSerializer::Serialize(scene);
    // Only the terrain carries a mesh with face colours in these scenes.
    CHECK(Count(after, "meshcolors") == 0);
    CHECK(Count(after, "voxelterrain ") == 1);

    // What the file was before the fix: the same text plus the terrain's
    // meshcolors line right after its `mesh` line.
    const std::string colorsLine = MeshColorsLineFor(*orig);
    CHECK(!colorsLine.empty());
    const std::size_t before = after.size() + colorsLine.size();
    std::cout << "  " << label << ": with meshcolors " << before << " bytes, now " << after.size()
              << " bytes (" << (100.0 * after.size() / before) << "%)\n";
    CHECK(after.size() < before * 6 / 10);

    // Reload: the rebuilt terrain matches the original exactly.
    Scene loaded;
    std::string err;
    CHECK(SceneSerializer::Deserialize(loaded, after, &err));
    const Mesh* back = TerrainMesh(loaded, name);
    CHECK(back != nullptr);
    if (back) {
        CHECK(modeling::GeometryHash(*back) == origHash);
        CHECK(back->vertices.size() == orig->vertices.size());
        CHECK(SameColors(*back, *orig));
    }
    // Saving again is stable.
    CHECK(SceneSerializer::Serialize(loaded) == after);

    // An old file with the voxel meshcolors still loads to the same result.
    std::string old = after;
    const std::string meshKey = "  mesh \"VoxelTerrain\"";
    const auto m = old.find(meshKey);
    CHECK(m != std::string::npos);
    if (m != std::string::npos) {
        const auto eol = old.find('\n', m);
        old.insert(eol + 1, colorsLine);
        CHECK(Count(old, "meshcolors") == 1);
        Scene oldLoaded;
        std::string err2;
        CHECK(SceneSerializer::Deserialize(oldLoaded, old, &err2));
        const Mesh* ob = TerrainMesh(oldLoaded, name);
        CHECK(ob != nullptr);
        if (ob) {
            CHECK(modeling::GeometryHash(*ob) == origHash);
            CHECK(SameColors(*ob, *orig));
        }
        // ...and the next save drops the old colours.
        CHECK(SceneSerializer::Serialize(oldLoaded) == after);
    }
}

}  // namespace

int main() {
    RUN_SUITE("voxel_no_meshcolors");

    // --- The default VoxelTerrain (64 x 36 x 64), generated and applied ------
    {
        Scene scene;
        GameObject* go = scene.CreateGameObject("Voxels");
        auto* v = go->AddComponent<VoxelTerrain>();
        v->Generate(0.5f, 6.0f, 0.6f, 7u);
        v->DecodeDensity(v->EncodeDensity());   // the file stores one byte per voxel
        v->Apply();
        CheckTerrainScene(scene, "Voxels", "default 64x36x64 terrain");
    }

    // --- The Sandbox template ------------------------------------------------
    {
        Scene scene;
        Templates::VoxelSandbox(scene);
        CheckTerrainScene(scene, "Terrain", "Sandbox template");
    }

    // --- A normal mesh with painted colours still saves them -----------------
    {
        Scene scene;
        GameObject* go = scene.CreateGameObject("Painted");
        auto* mr = go->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Cube();
        mr->mesh.triColors.assign((std::size_t)mr->mesh.TriangleCount(), Color{0.25f, 0.5f, 0.75f, 1.0f});
        mr->mesh.triColors[0] = Color{1.0f, 0.0f, 0.0f, 1.0f};
        CHECK(mr->mesh.HasFaceColors());
        const std::string text = SceneSerializer::Serialize(scene);
        CHECK(Count(text, "meshcolors") == 1);
        Scene loaded;
        CHECK(SceneSerializer::Deserialize(loaded, text));
        GameObject* lg = loaded.Find("Painted");
        auto* lmr = lg ? lg->GetComponent<MeshRenderer>() : nullptr;
        CHECK(lmr != nullptr);
        if (lmr) CHECK(SameColors(lmr->mesh, mr->mesh));
    }

    TEST_MAIN_RESULT();
}
