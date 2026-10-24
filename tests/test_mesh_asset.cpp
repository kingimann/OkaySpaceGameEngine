// Mesh files on a MeshRenderer (meshPath): .okaymesh assets load with the scene,
// edits to a file-backed mesh survive save + reload, and an exported game finds
// the file next to its scene.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Modeling/MeshAsset.hpp"
#include "okay/Modeling/PolyMesh.hpp"
#include <cstdio>
#include <filesystem>

using namespace okay;
using namespace okay::modeling;
namespace fs = std::filesystem;

static float TopY(const Mesh& m) { Vec3 lo, hi; m.Bounds(lo, hi); return hi.y; }

int main() {
    RUN_SUITE("mesh_asset");
    const fs::path dir = fs::temp_directory_path() / "okay_test_mesh_asset";
    fs::remove_all(dir);
    fs::create_directories(dir / "elsewhere");

    // ---- Save a mesh as .okaymesh and load it back through LoadMeshAsset ----
    const std::string asset = (dir / "Crate.okaymesh").string();
    {
        std::string err;
        CHECK(SaveMeshAsset(asset, Mesh::Cube(1.0f), &err));
        PolyMesh pm;
        CHECK(PolyMesh::Load(asset, pm, &err) && pm.CountFaces(4) == 6);
        Mesh m;
        CHECK(LoadMeshAsset(asset, m, nullptr, &err));
        CHECK(m.TriangleCount() == 12 && m.Topology().Closed());
        CHECK(!LoadMeshAsset((dir / "missing.okaymesh").string(), m, nullptr, &err) && err.find("can't find") != std::string::npos);
        CHECK(!SaveMeshAsset((dir / "empty.okaymesh").string(), Mesh{}, &err));
    }
    // ---- A scene whose object points at the asset loads its geometry; unedited, no copy is stored ----
    const std::string scenePath = (dir / "a.okayscene").string();
    {
        Scene s("A");
        auto* mr = s.CreateGameObject("Crate")->AddComponent<MeshRenderer>();
        mr->meshPath = asset;
        LoadMeshAsset(asset, mr->mesh);
        mr->mesh.name = "";
        CHECK(SceneSerializer::SaveToFile(s, scenePath));
        std::ifstream f(scenePath); std::stringstream ss; ss << f.rdbuf();
        CHECK(ss.str().find("meshgeo") == std::string::npos);   // the file is the source; nothing duplicated
        Scene r("x");
        CHECK(SceneSerializer::LoadFromFile(r, scenePath));
        auto* lmr = r.Find("Crate")->GetComponent<MeshRenderer>();
        CHECK(lmr->meshPath == asset && lmr->mesh.TriangleCount() == 12 && lmr->meshPathHash != 0);
        CHECK(lmr->meshPathHash == GeometryHash(lmr->mesh));
    }
    // ---- Editing a file-backed mesh: the edit is kept in the scene (it used to be lost) ----
    {
        const std::string obj = (dir / "box.obj").string();
        CHECK(Mesh::Cube(1.0f).SaveOBJ(obj));
        Scene s("B");
        auto* mr = s.CreateGameObject("Box")->AddComponent<MeshRenderer>();
        mr->meshPath = obj;
        mr->mesh = Mesh::LoadOBJ(obj);
        for (Vec3& v : mr->mesh.vertices) if (v.y > 0) v.y += 1.0f;
        mr->mesh.name = "";
        const std::string p = (dir / "b.okayscene").string();
        CHECK(SceneSerializer::SaveToFile(s, p));
        Scene r("x");
        CHECK(SceneSerializer::LoadFromFile(r, p));
        auto* lmr = r.Find("Box")->GetComponent<MeshRenderer>();
        CHECK(std::fabs(TopY(lmr->mesh) - 1.5f) < 1e-4f);
        CHECK(lmr->meshPath == obj);   // still linked to its file
        // Saved again unchanged, it still keeps the edit (and only one copy of it).
        CHECK(SceneSerializer::SaveToFile(r, p));
        Scene r2("y");
        CHECK(SceneSerializer::LoadFromFile(r2, p));
        CHECK(std::fabs(TopY(r2.Find("Box")->GetComponent<MeshRenderer>()->mesh) - 1.5f) < 1e-4f);
    }
    // ---- An exported game: the scene's path to the asset is gone, the file sits next to the scene ----
    {
        const fs::path game = dir / "elsewhere";
        fs::copy_file(asset, game / "Crate.okaymesh", fs::copy_options::overwrite_existing);
        std::string text;
        { std::ifstream f(scenePath); std::stringstream ss; ss << f.rdbuf(); text = ss.str(); }
        const std::string bogus = "/no/such/machine/Assets/Crate.okaymesh";
        for (std::size_t at; (at = text.find(asset)) != std::string::npos;) text.replace(at, asset.size(), bogus);
        { std::ofstream f(game / "game.okayscene"); f << text; }
        Scene r("x");
        CHECK(SceneSerializer::LoadFromFile(r, (game / "game.okayscene").string()));
        auto* lmr = r.Find("Crate")->GetComponent<MeshRenderer>();
        CHECK(lmr && lmr->mesh.TriangleCount() == 12);
    }
    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
