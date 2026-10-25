// Scene-record validation (item 4): bad counts and indices in meshgeo,
// dollypath, charbind, skinmesh, modelanim, tilemap, terrain and voxelterrain
// records are rejected with ok=false and an error, instead of overflowing,
// throwing on a huge allocation, or looping for billions of iterations.
// Well-formed scenes still round-trip exactly.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <string>

using namespace okay;

static std::string Obj(const std::string& body) {
    return "okayscene 1\ngameobject 0 \"O\"\n  transform 0 0 0 0 0 0 1 1 1 1\n" + body + "\n  parent -1\nend\n";
}
static bool Rejects(const std::string& body, const char* what) {
    Scene s("r");
    std::string err;
    const bool ok = SceneSerializer::Deserialize(s, Obj(body), &err);
    if (ok || err.empty()) std::cerr << "  (accepted: " << what << ")\n";
    return !ok && !err.empty();
}

int main() {
    RUN_SUITE("scene_records");

    // ---- bad records ----
    CHECK(Rejects("  meshgeo 3 0 0 0 1 0 0 0 1 0 3 0 1 5", "meshgeo index past vertices"));
    CHECK(Rejects("  meshgeo 3 0 0 0 1 0 0 0 1 0 3 0 -1 2", "meshgeo negative index"));
    CHECK(Rejects("  meshgeo 2000000000 0 0 0", "meshgeo huge vertex count"));
    CHECK(Rejects("  dollypath 0 -1", "dollypath -1"));
    CHECK(Rejects("  dollypath 0 999999999 1 2 3", "dollypath huge"));
    CHECK(Rejects("  charbind -1", "charbind -1"));
    CHECK(Rejects("  charbind 3 0 0 0 1 0 0 0 1 0 3 0 1 9 0", "charbind index past vertices"));
    // skinmesh: 3 verts, 0 normals, 0 uvs, 3 tris, 2 weights (mismatch)
    CHECK(Rejects("  skinmesh 3 0 0 0 1 0 0 0 1 0 0 0 3 0 1 2 2 0 0 0 0 1 0 0 0 0 0 0 0 1 0 0 0"
                  " 1 \"Bone\" 1 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1", "skinmesh weights != vertices"));
    CHECK(Rejects("  skinmesh 3 0 0 0 1 0 0 0 1 0 0 0 3 0 1 7 3 0 0 0 0 1 0 0 0 0 0 0 0 1 0 0 0 0 0 0 0 1 0 0 0"
                  " 1 \"Bone\" 1 1 0 0 0 0 1 0 0 0 0 1 0 0 0 0 1", "skinmesh index past vertices"));
    CHECK(Rejects("  skinmesh -5", "skinmesh negative count"));
    CHECK(Rejects("  modelanim 1 1 1 1 2000000000", "modelanim huge clip count"));
    CHECK(Rejects("  modelanim 1 1 1 1 1 \"c\" 1 \"n\" 1 \"t\" 2000000000 0 0", "modelanim huge key count"));
    CHECK(Rejects("  tilemap 1 100000 100000 0 0", "tilemap huge"));
    CHECK(Rejects("  terrain 100000 50 1 1 1 1 4 0 0 0 0", "terrain huge resolution"));
    CHECK(Rejects("  voxelterrain 4000 4000 4000 1 0 1 0 0", "voxel huge grid"));

    // ---- a well-formed scene round-trips exactly (old scenes load as before) ----
    {
        Scene a("ok");
        GameObject* g = a.CreateGameObject("Geo");
        auto* mr = g->AddComponent<MeshRenderer>();
        mr->mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0}};
        mr->mesh.triangles = {0, 1, 2, 1, 3, 2};
        mr->mesh.uvs = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
        mr->mesh.name = "";
        GameObject* d = a.CreateGameObject("Dolly");
        auto* dp = d->AddComponent<DollyPath>();
        dp->waypoints = {{0, 0, 0}, {1, 2, 3}, {4, 5, 6}};
        GameObject* sk = a.CreateGameObject("Skin");
        auto* sm = sk->AddComponent<SkinnedMesh>();
        sm->bind.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        sm->bind.triangles = {0, 1, 2};
        sm->jointIdx.assign(3, {0, 0, 0, 0});
        sm->jointWt.assign(3, {1, 0, 0, 0});
        sm->jointNames = {"Bone"};
        sm->inverseBind = {Mat4::Identity()};
        GameObject* an = a.CreateGameObject("Anim");
        auto* ma = an->AddComponent<ModelAnimator>();
        ModelAnimator::Clip clip; clip.name = "walk";
        ModelAnimator::NodeClip nc; nc.node = "Hips";
        nc.clip.AddKey("position.x", 0.0f, 1.0f); nc.clip.AddKey("position.x", 1.0f, 2.0f);
        clip.nodes.push_back(nc);
        ma->clips.push_back(clip);
        GameObject* tl = a.CreateGameObject("Tiles");
        auto* tm = tl->AddComponent<Tilemap>();
        tm->Resize(3, 2); tm->SetTile(1, 1, 7);
        GameObject* te = a.CreateGameObject("Terrain");
        auto* tr = te->AddComponent<Terrain>();
        tr->Resize(8); tr->heights[5] = 2.5f;

        const std::string text = SceneSerializer::Serialize(a);
        Scene b("ok2");
        std::string err;
        CHECK(SceneSerializer::Deserialize(b, text, &err));
        CHECK(err.empty());
        if (!err.empty()) std::cerr << "  error: " << err << "\n";
        CHECK(SceneSerializer::Serialize(b) == text);
        GameObject* g2 = b.Find("Geo");
        CHECK(g2 && g2->GetComponent<MeshRenderer>() && g2->GetComponent<MeshRenderer>()->mesh.triangles.size() == 6);
        GameObject* d2 = b.Find("Dolly");
        CHECK(d2 && d2->GetComponent<DollyPath>() && d2->GetComponent<DollyPath>()->waypoints.size() == 3);
        GameObject* s2 = b.Find("Skin");
        CHECK(s2 && s2->GetComponent<SkinnedMesh>() && s2->GetComponent<SkinnedMesh>()->jointWt.size() == 3);
        GameObject* t2 = b.Find("Terrain");
        CHECK(t2 && t2->GetComponent<Terrain>() && t2->GetComponent<Terrain>()->heights.size() == 81);
    }

    // A voxel terrain loads too (exact text isn't compared: Apply() adds its
    // MeshRenderer on load, which is long-standing behaviour).
    {
        Scene a("vox");
        GameObject* vx = a.CreateGameObject("Vox");
        vx->AddComponent<VoxelTerrain>()->Resize(4, 4, 4);
        Scene b("vox2");
        std::string err;
        CHECK(SceneSerializer::Deserialize(b, SceneSerializer::Serialize(a), &err));
        GameObject* v2 = b.Find("Vox");
        CHECK(v2 && v2->GetComponent<VoxelTerrain>() && v2->GetComponent<VoxelTerrain>()->density.size() == 64);
    }

    TEST_MAIN_RESULT();
}
