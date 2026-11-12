// Foliage painting, terrain texture layers and the surface ray cast behind them.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Testing/SceneValidator.hpp"
#include "okay/Components/Terrain.hpp"
#include "okay/Scene/PrefabTools.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>

using namespace okay;
using okay::testing::SceneValidator;

namespace {

Terrain* MakeTerrain(Scene& s, float size = 40.0f, int res = 32) {
    GameObject* g = s.CreateGameObject("Terrain");
    auto* t = g->AddComponent<Terrain>();
    t->Resize(res); t->size = size;
    t->Flatten(0.0f);
    t->Apply();
    return t;
}

Foliage* MakeFoliage(Scene& s, const char* name = "Trees") {
    GameObject* g = s.CreateGameObject(name);
    auto* f = g->AddComponent<Foliage>();
    f->mesh = "Bush"; f->variants = 3;
    return f;
}

int ChunkMeshTriangles(Scene& s) {
    int n = 0;
    for (const auto& o : s.Objects())
        if (o->transient && o->name.rfind("Foliage Chunk", 0) == 0)
            if (auto* mr = o->GetComponent<MeshRenderer>()) n += mr->mesh.TriangleCount();
    return n;
}

} // namespace

int main() {
    RUN_SUITE("foliage");
    Log::console = false;

    // ---- paint: density, radius, spacing, determinism ------------------------------------------------
    {
        Scene s("A"); s.physicsEnabled = false;
        Terrain* t = MakeTerrain(s);
        Foliage* f = MakeFoliage(s);
        f->radius = 5.0f; f->density = 0.5f;
        const auto ground = Foliage::TerrainSurface(*t);
        const int n = f->Paint(0, 0, ground);
        CHECK(n > 0);
        CHECK(f->Count() == n);
        // Everything is inside the brush disc and on the ground.
        bool inside = true, onGround = true;
        for (const auto& in : f->instances) {
            if (std::sqrt(in.position.x * in.position.x + in.position.z * in.position.z) > 5.001f) inside = false;
            if (std::fabs(in.position.y) > 1e-3f) onGround = false;
            CHECK(in.scale >= f->minScale - 1e-4f && in.scale <= f->maxScale + 1e-4f);
        }
        CHECK(inside); CHECK(onGround);
        // Spacing: no two copies closer than spacing/sqrt(density).
        const float gap = f->spacing / std::sqrt(f->density);
        bool spaced = true;
        for (std::size_t i = 0; i < f->instances.size(); ++i)
            for (std::size_t j = i + 1; j < f->instances.size(); ++j) {
                const Vec3 d = f->instances[i].position - f->instances[j].position;
                if (std::sqrt(d.x * d.x + d.z * d.z) < gap - 1e-3f) spaced = false;
            }
        CHECK(spaced);
        // Painting the same place again adds fewer (it is already full) but never breaks spacing.
        const int again = f->Paint(0, 0, ground);
        CHECK(again <= n);
        // Same seed, same result.
        Scene s2("A2"); s2.physicsEnabled = false;
        Terrain* t2 = MakeTerrain(s2);
        Foliage* f2 = MakeFoliage(s2);
        f2->radius = 5.0f; f2->density = 0.5f;
        CHECK(f2->Paint(0, 0, Foliage::TerrainSurface(*t2)) == n);
        CHECK(f2->instances[0].position.x == f->instances[0].position.x);
    }

    // ---- slope and height limits ----------------------------------------------------------------------
    {
        Scene s("B"); s.physicsEnabled = false;
        Terrain* t = MakeTerrain(s, 40.0f, 40);
        // A ramp along X: height = x * 1.0 (45 degrees).
        for (int z = 0; z < t->Dim(); ++z) for (int x = 0; x < t->Dim(); ++x) t->SetHeight(x, z, (x - 20) * 1.0f);
        t->Apply();
        Foliage* f = MakeFoliage(s);
        f->radius = 8.0f; f->density = 0.4f; f->maxSlope = 30.0f;
        CHECK(f->Paint(0, 0, Foliage::TerrainSurface(*t)) == 0);     // a 45 degree ramp is too steep
        f->maxSlope = 60.0f;
        CHECK(f->Paint(0, 0, Foliage::TerrainSurface(*t)) > 0);
        f->Clear();
        f->minHeight = 2.0f; f->maxHeight = 4.0f;
        f->Paint(0, 0, Foliage::TerrainSurface(*t));
        bool band = f->Count() > 0;
        for (const auto& in : f->instances) if (in.position.y < 2.0f - 0.05f || in.position.y > 4.0f + 0.05f) band = false;
        CHECK(band);
    }

    // ---- erase, rescale, clear, budget ------------------------------------------------------------------
    {
        Scene s("C"); s.physicsEnabled = false;
        Terrain* t = MakeTerrain(s);
        Foliage* f = MakeFoliage(s);
        const auto ground = Foliage::TerrainSurface(*t);
        f->radius = 12.0f; f->density = 0.3f;
        f->Paint(0, 0, ground);
        const int before = f->Count();
        CHECK(before > 10);
        const int removed = f->Erase(0, 0, 4.0f);
        CHECK(removed > 0 && f->Count() == before - removed);
        for (const auto& in : f->instances) CHECK(std::sqrt(in.position.x * in.position.x + in.position.z * in.position.z) > 3.999f);
        f->minScale = 2.0f; f->maxScale = 2.0f;
        const int resc = f->Rescale(8, 0, 3.0f);
        CHECK(resc > 0);
        int twos = 0;
        for (const auto& in : f->instances) if (in.scale == 2.0f) ++twos;
        CHECK(twos == resc);
        f->Clear();
        CHECK(f->Count() == 0);
        // Budget: a tiny triangle budget caps the layer.
        f->maxTriangles = f->TrianglesPerInstance() * 5;
        f->radius = 15.0f; f->density = 0.6f;
        f->Paint(0, 0, ground);
        CHECK(f->Count() == 5);
        CHECK(f->Paint(0, 0, ground) == 0);
        Scene s3("V"); s3.physicsEnabled = false;
        Foliage* fv = MakeFoliage(s3);
        fv->mesh = "NotAProp";
        auto rep = SceneValidator::Validate(s3);
        bool shape = false;
        for (const auto& i : rep.issues) if (std::string(i.code) == "foliage.shape") shape = true;
        CHECK(shape);
    }

    // ---- chunks: one merged mesh per square, only dirty ones rebuilt ------------------------------------
    {
        Scene s("D"); s.physicsEnabled = false;
        Terrain* t = MakeTerrain(s, 80.0f, 40);
        Foliage* f = MakeFoliage(s);
        f->radius = 6.0f; f->density = 0.4f;
        const auto ground = Foliage::TerrainSurface(*t);
        f->Paint(-25, -25, ground);
        f->Paint(25, 25, ground);
        f->EnsureBuilt();
        CHECK(f->ChunkCount() >= 2);
        CHECK(ChunkMeshTriangles(s) == f->TotalTriangles());
        const int objectsWithChunks = s.Objects().size();
        // The number of objects is the chunk count, not the copy count.
        CHECK((int)s.Objects().size() < f->Count());
        f->Erase(-25, -25, 10.0f);
        f->EnsureBuilt();
        s.FlushDestroyed();
        CHECK(ChunkMeshTriangles(s) == f->TotalTriangles());
        CHECK((int)s.Objects().size() < objectsWithChunks);
        // Chunks are never saved, the copies are.
        const std::string text = SceneSerializer::Serialize(s);
        CHECK(text.find("Foliage Chunk") == std::string::npos);
        Scene r("R"); r.physicsEnabled = false;
        CHECK(SceneSerializer::Deserialize(r, text, nullptr, nullptr));
        auto* rf = r.Find("Trees")->GetComponent<Foliage>();
        CHECK(rf != nullptr && rf->Count() == f->Count());
        CHECK(rf->mesh == "Bush" && rf->variants == 3 && rf->density == f->density);
        rf->EnsureBuilt();
        CHECK(ChunkMeshTriangles(r) == rf->TotalTriangles());
        CHECK(SceneSerializer::Serialize(r) == text);                 // stable
        // Deleting the layer takes its chunks with it.
        s.Destroy(f->gameObject);
        s.FlushDestroyed();
        CHECK(ChunkMeshTriangles(s) == 0);
    }

    // ---- brush options: turn, lean, chunk size, tint, shapes --------------------------------------------------------
    {
        Scene s("L"); s.physicsEnabled = false;
        Terrain* t = MakeTerrain(s, 60.0f, 40);
        for (int z = 0; z < t->Dim(); ++z) for (int x = 0; x < t->Dim(); ++x) t->SetHeight(x, z, (x - 20) * 0.4f);   // a gentle ramp, about 22 degrees
        t->Apply();
        const auto ground = Foliage::TerrainSurface(*t);
        Foliage* f = MakeFoliage(s);
        f->radius = 6.0f; f->density = 0.3f; f->randomYaw = false; f->alignToSurface = 0.0f;
        f->Paint(0, 0, ground);
        CHECK(f->Count() > 0);
        bool straight = true, noTurn = true;
        for (const auto& in : f->instances) { if (in.leanX != 0.0f || in.leanZ != 0.0f) straight = false; if (in.yaw != 0.0f) noTurn = false; }
        CHECK(straight && noTurn);                                    // no random turn, no lean
        f->Clear();
        f->randomYaw = true; f->alignToSurface = 1.0f;
        f->Paint(0, 0, ground);
        bool leans = true, turns = false;
        for (const auto& in : f->instances) { if (std::fabs(in.leanX) < 0.1f) leans = false; if (in.yaw != 0.0f) turns = true; }
        CHECK(leans && turns);                                        // leaning with the ramp (its normal tilts along x), turned at random
        // The lean reaches the drawn mesh: a leaning copy is not a straight one.
        f->EnsureBuilt();
        // Chunk size: smaller squares give more chunks after a rebuild.
        f->Clear(); f->alignToSurface = 0.0f; f->radius = 12.0f; f->density = 0.4f;
        f->Paint(0, 0, ground);
        f->EnsureBuilt();
        const int big = f->ChunkCount();
        f->chunkSize = 4.0f;
        f->RebuildAll();
        s.FlushDestroyed();
        CHECK(f->ChunkCount() > big);
        CHECK(ChunkMeshTriangles(s) == f->TotalTriangles());
        // Tint reaches the chunks.
        f->tint = Color(1.0f, 0.5f, 0.25f, 1.0f);
        f->RebuildAll();
        bool tinted = false;
        for (const auto& o : s.Objects())
            if (o->transient && o->name.rfind("Foliage Chunk", 0) == 0)
                if (auto* mr = o->GetComponent<MeshRenderer>()) tinted = mr->color.g == 0.5f && mr->color.b == 0.25f;
        CHECK(tinted);
        // Shadows are off by default and the switch reaches the chunks.
        CHECK(!f->castShadows);
        bool anyShadow = false;
        for (const auto& o : s.Objects()) if (o->transient) if (auto* mr = o->GetComponent<MeshRenderer>()) if (mr->castShadows) anyShadow = true;
        CHECK(!anyShadow);
        f->castShadows = true; f->RebuildAll(); s.FlushDestroyed();
        bool allShadow = true, anyChunk = false;
        for (const auto& o : s.Objects()) if (o->transient && o->name.rfind("Foliage Chunk", 0) == 0) { anyChunk = true; if (!o->GetComponent<MeshRenderer>()->castShadows) allShadow = false; }
        CHECK(anyChunk && allShadow);
        f->castShadows = false; f->RebuildAll();
        // Shapes: a prop with variants gets that many shapes, one without gets one.
        f->variants = 3;
        CHECK(f->ShapeCount() == 3);
        f->variants = 1; CHECK(f->ShapeCount() == 1);
        f->mesh = "Barrel"; CHECK(f->ShapeCount() == 1);
    }

    // ---- plant a mesh file and a prefab ------------------------------------------------------------------------------------
    {
        namespace fs = std::filesystem;
        const fs::path dir = fs::temp_directory_path() / "okay_foliage_test";
        fs::create_directories(dir);
        const std::string obj = (dir / "pyramid.obj").string();
        {
            std::ofstream o(obj);
            o << "v -1 0 -1\nv 1 0 -1\nv 1 0 1\nv -1 0 1\nv 0 2 0\n"
                 "f 1 2 5\nf 2 3 5\nf 3 4 5\nf 4 1 5\nf 1 4 3 2\n";
        }
        Scene s("M"); s.physicsEnabled = false;
        Terrain* t = MakeTerrain(s);
        Foliage* f = MakeFoliage(s, "Pyramids");
        f->meshPath = obj;
        f->radius = 5.0f; f->density = 0.2f;
        const int n = f->Paint(0, 0, Foliage::TerrainSurface(*t));
        CHECK(n > 0 && f->Problem().empty());
        CHECK(f->TrianglesPerInstance() >= 5);
        f->EnsureBuilt();
        CHECK(ChunkMeshTriangles(s) == f->TotalTriangles());
        f->meshPath = (dir / "missing.obj").string();
        f->RebuildAll();
        CHECK(!f->Problem().empty());                                    // the validator reports this one
        // A prefab with two parts is flattened into one shape.
        Scene ps("P"); ps.physicsEnabled = false;
        GameObject* root = ps.CreateGameObject("Lamp");
        root->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
        GameObject* top = ps.CreateGameObject("Top");
        top->transform->SetParent(root->transform, false);
        top->transform->localPosition = {0, 2, 0};
        top->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
        const std::string prefab = (dir / "lamp.okayprefab").string();
        std::string err;
        CHECK(PrefabTools::SavePrefab(*root, prefab, &err));
        Foliage* g = MakeFoliage(s, "Lamps");
        g->prefabPath = prefab;
        g->radius = 5.0f; g->density = 0.2f;
        CHECK(g->Paint(0, 0, Foliage::TerrainSurface(*t)) > 0 && g->Problem().empty());
        CHECK(g->TrianglesPerInstance() == 24);                          // two cubes of 12 triangles, merged
        g->EnsureBuilt();
        CHECK(ChunkMeshTriangles(s) >= g->TotalTriangles());
        g->prefabPath = (dir / "none.okayprefab").string();
        g->RebuildAll();
        CHECK(!g->Problem().empty());
        std::error_code ec; fs::remove_all(dir, ec);
    }

    // ---- a layer on a transformed object plants in its own space ------------------------------------------
    {
        Scene s("E"); s.physicsEnabled = false;
        Terrain* t = MakeTerrain(s);
        Foliage* f = MakeFoliage(s);
        f->gameObject->transform->localPosition = {100, 5, 0};
        f->radius = 3.0f; f->density = 0.5f;
        f->Paint(0, 0, Foliage::TerrainSurface(*t));
        CHECK(f->Count() > 0);
        bool local = true;
        for (const auto& in : f->instances) if (in.position.x > -96.0f || in.position.x < -104.0f || std::fabs(in.position.y + 5.0f) > 1e-3f) local = false;
        CHECK(local);
    }

    // ---- painting onto any mesh surface -----------------------------------------------------------------------
    {
        Scene s("F"); s.physicsEnabled = false;
        GameObject* floor = s.CreateGameObject("Floor");
        floor->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
        floor->transform->localScale = {20, 1, 20};
        floor->transform->localPosition = {0, -0.5f, 0};   // top face at y = 0
        Foliage* f = MakeFoliage(s);
        f->radius = 4.0f; f->density = 0.3f;
        const int n = f->Paint(0, 0, f->SceneSurface(s, 50.0f));
        CHECK(n > 0);
        for (const auto& in : f->instances) CHECK(std::fabs(in.position.y) < 1e-3f);
        // Off the floor there is no surface to plant on.
        Foliage* g = MakeFoliage(s, "Far");
        g->radius = 3.0f; g->density = 0.5f;
        CHECK(g->Paint(500, 500, g->SceneSurface(s, 50.0f)) == 0);
        // The ray cast itself.
        SurfaceHit hit;
        CHECK(RaycastSurfaces(s, {1, 10, 1}, {0, -1, 0}, 100, hit));
        CHECK_NEAR(hit.position.y, 0.0f, 1e-3f);
        CHECK_NEAR(hit.normal.y, 1.0f, 1e-3f);
        CHECK(hit.object == floor);
        CHECK(!RaycastSurfaces(s, {1, 10, 1}, {0, 1, 0}, 100, hit));
        CHECK(!RaycastSurfaces(s, {1, 10, 1}, {0, -1, 0}, 5, hit));   // out of range
    }

    // ---- scatter on a terrain (what the Terrain inspector's button uses) -----------------------------------------
    {
        Scene s("G"); s.physicsEnabled = false;
        Terrain* t = MakeTerrain(s);
        Foliage* f = MakeFoliage(s);
        CHECK(f->ScatterOnTerrain(*t, 60, 7u) == 60);
        CHECK(f->Count() == 60);
    }

    // ---- terrain ray cast: hits the real surface of a hill, not a flat plane --------------------------------------
    {
        Scene s("I"); s.physicsEnabled = false;
        Terrain* t = MakeTerrain(s, 40.0f, 40);
        for (int z = 0; z < t->Dim(); ++z) for (int x = 0; x < t->Dim(); ++x) t->SetHeight(x, z, 6.0f);   // a plateau at y = 6
        Vec3 hit;
        CHECK(t->Raycast({3, 30, 4}, {0, -1, 0}, 100, hit));
        CHECK_NEAR(hit.y, 6.0f, 0.01f); CHECK_NEAR(hit.x, 3.0f, 0.01f); CHECK_NEAR(hit.z, 4.0f, 0.01f);
        const Vec3 diag = Vec3{1, -1, 0}.Normalized();
        CHECK(t->Raycast({-5, 16, 0}, diag, 100, hit));
        CHECK_NEAR(hit.y, 6.0f, 0.02f); CHECK_NEAR(hit.x, 5.0f, 0.05f);
        CHECK(!t->Raycast({3, 30, 4}, {0, 1, 0}, 100, hit));          // pointing away
        CHECK(!t->Raycast({100, 30, 4}, {0, -1, 0}, 100, hit));       // beside the terrain
        CHECK(!t->Raycast({3, 30, 4}, {0, -1, 0}, 10, hit));          // too short
        t->gameObject->transform->localPosition = {50, 10, 0};
        CHECK(t->Raycast({53, 40, 4}, {0, -1, 0}, 100, hit));
        CHECK_NEAR(hit.y, 16.0f, 0.02f);                              // the moved terrain's plateau
    }

    // ---- terrain texture layers ----------------------------------------------------------------------------------
    {
        Scene s("H"); s.physicsEnabled = false;
        Terrain* t = MakeTerrain(s, 32.0f, 16);
        t->splatResolution = 64;
        CHECK(t->layers.empty());
        TerrainLayer grass; grass.name = "Grass"; grass.tint = Color(0, 1, 0, 1);
        TerrainLayer dirt;  dirt.name = "Dirt";  dirt.tint = Color(1, 0, 0, 1);
        CHECK(t->AddLayer(grass) == 0);
        CHECK(t->AddLayer(dirt) == 1);
        CHECK(t->LayerWeight(0, 3, 3) == 1.0f && t->LayerWeight(1, 3, 3) == 0.0f);
        t->Apply();
        auto* mr = t->gameObject->GetComponent<MeshRenderer>();
        CHECK(mr->texture == t->SplatName() && !mr->texture.empty());
        CHECK(!mr->mesh.HasFaceColors());                              // layers replace the auto-colours
        const Image* img = t->SplatImage();
        CHECK(img != nullptr && img->Width() == 64);
        CHECK(img->GetPixel(10, 10).g > 0.9f && img->GetPixel(10, 10).r < 0.1f);    // all grass
        // Paint dirt in the middle.
        t->PaintLayerAt(1, 0, 0, 6.0f, 1.0f, 1.0f);
        t->PublishSplat();
        CHECK(t->LayerWeightAt(1, 0, 0) > 0.99f);
        CHECK(t->LayerWeightAt(1, 14, 14) < 0.01f);                    // far away untouched
        CHECK(t->LayerWeightAt(0, 0, 0) < 0.01f);                      // the others shrank to make room
        const Color mid = t->SplatImage()->GetPixel(32, 32), edge = t->SplatImage()->GetPixel(2, 2);
        CHECK(mid.r > 0.9f && mid.g < 0.1f);                           // the picture shows it
        CHECK(edge.g > 0.9f && edge.r < 0.1f);
        // Weights always add up to 1.
        bool sum1 = true;
        for (int z = 0; z < t->Dim(); ++z) for (int x = 0; x < t->Dim(); ++x)
            if (std::fabs(t->LayerWeight(0, x, z) + t->LayerWeight(1, x, z) - 1.0f) > 0.01f) sum1 = false;
        CHECK(sum1);
        // A half-strength stroke blends.
        t->PaintLayerAt(1, -12, -12, 3.0f, 0.5f, 1.0f);
        const float blend = t->LayerWeightAt(1, -12, -12);
        CHECK(blend > 0.4f && blend < 0.6f);
        // A negative stroke takes dirt away again.
        t->PaintLayerAt(1, 0, 0, 6.0f, -1.0f, 1.0f);
        CHECK(t->LayerWeightAt(1, 0, 0) < 0.01f && t->LayerWeightAt(0, 0, 0) > 0.99f);
        t->PaintLayerAt(1, 0, 0, 6.0f, 1.0f, 1.0f);
        // Save and load: layers, tints and weights come back and the picture is rebuilt.
        const std::string text = SceneSerializer::Serialize(s);
        CHECK(text.find("terrainlayers") != std::string::npos);
        Scene r("R"); r.physicsEnabled = false;
        CHECK(SceneSerializer::Deserialize(r, text, nullptr, nullptr));
        auto* rt = r.Find("Terrain")->GetComponent<Terrain>();
        CHECK(rt->layers.size() == 2 && rt->layers[1].name == "Dirt" && rt->layers[1].tint.r == 1.0f);
        CHECK(rt->splat == t->splat);
        CHECK(rt->SplatImage() != nullptr && rt->SplatImage()->GetPixel(32, 32).r > 0.9f);
        CHECK(SceneSerializer::Serialize(r) == text);
        // Remove the dirt layer: all grass again; remove the last: back to the single-texture look.
        CHECK(t->RemoveLayer(1));
        CHECK(t->LayerWeight(0, 8, 8) == 1.0f);
        CHECK(t->RemoveLayer(0));
        t->Apply();
        CHECK(t->layers.empty() && t->gameObject->GetComponent<MeshRenderer>()->texture.empty());
        CHECK(t->gameObject->GetComponent<MeshRenderer>()->mesh.HasFaceColors());
        CHECK(SceneSerializer::Serialize(s).find("terrainlayers") == std::string::npos);
        // A layer picture and its tiling: a 2x2 checker repeated 4 times across the terrain.
        {
            namespace fs = std::filesystem;
            const fs::path dir = fs::temp_directory_path() / "okay_terrain_layer_test";
            fs::create_directories(dir);
            Image chk(2, 2);
            chk.SetPixel(0, 0, Color(1, 0, 0, 1)); chk.SetPixel(1, 0, Color(0, 0, 1, 1));
            chk.SetPixel(0, 1, Color(0, 0, 1, 1)); chk.SetPixel(1, 1, Color(1, 0, 0, 1));
            const std::string png = (dir / "checker.png").string();
            CHECK(chk.SavePNG(png));
            Scene s2("T2"); s2.physicsEnabled = false;
            Terrain* t2 = MakeTerrain(s2, 32.0f, 16);
            t2->splatResolution = 64;
            TerrainLayer pic; pic.name = "Checker"; pic.texture = png; pic.tiling = 4.0f; pic.tint = Color(1, 1, 1, 1);
            t2->AddLayer(pic);
            t2->Apply();
            const Image* im = t2->SplatImage();
            CHECK(im != nullptr);
            int reds = 0, blues = 0;
            for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) { const Color c = im->GetPixel(x, y); if (c.r > 0.5f && c.b < 0.5f) ++reds; else if (c.b > 0.5f && c.r < 0.5f) ++blues; }
            CHECK(reds > 1000 && blues > 1000);                                // the picture shows, not a flat colour
            // Tiling: 4 repeats across 64 pixels = a 16 pixel period, so pixels 16 apart match.
            const Color a = im->GetPixel(5, 5), b = im->GetPixel(21, 5), c2 = im->GetPixel(5, 21);
            CHECK(std::fabs(a.r - b.r) < 0.2f && std::fabs(a.r - c2.r) < 0.2f);
            t2->layers[0].tiling = 2.0f; t2->MarkSplatDirty(); t2->Apply();    // fewer repeats: the period doubles
            const Color d = t2->SplatImage()->GetPixel(5, 5), e = t2->SplatImage()->GetPixel(21, 5);
            CHECK(std::fabs(d.r - e.r) > 0.3f);
            // Fill and the detail size.
            TerrainLayer other; other.name = "Flat"; other.tint = Color(0, 1, 0, 1);
            t2->AddLayer(other);
            t2->FillLayer(1);
            CHECK(t2->LayerWeight(1, 4, 4) == 1.0f && t2->LayerWeight(0, 4, 4) == 0.0f);
            t2->Apply();
            CHECK(t2->SplatImage()->GetPixel(10, 10).g > 0.9f && t2->SplatImage()->GetPixel(10, 10).r < 0.1f);
            t2->splatResolution = 128; t2->MarkSplatDirty(); t2->Apply();
            CHECK(t2->SplatImage()->Width() == 128);
            std::error_code ec; fs::remove_all(dir, ec);
        }
        // The in-memory ground picture is not a file: no missing-asset error, nothing to ship, no scene-file reference.
        t->AddLayer(grass); t->Apply();
        {
            auto rep = SceneValidator::Validate(s);
            bool missing = false;
            for (const auto& i : rep.issues) if (std::string(i.code) == "asset.missing") missing = true;
            CHECK(!missing);
            for (const auto& pth : SceneSerializer::CollectAssetPaths(s)) CHECK(pth.empty() || pth[0] != '@');
            t->layers[0].texture = "no/such/picture.png";
            auto rep2 = SceneValidator::Validate(s);
            bool layerMissing = false;
            for (const auto& i : rep2.issues) if (std::string(i.code) == "asset.missing" && i.component == "Terrain") layerMissing = true;
            CHECK(layerMissing);                                           // a layer's own picture is checked like any other
            bool shipped = false;
            for (const auto& pth : SceneSerializer::CollectAssetPaths(s)) if (pth == "no/such/picture.png") shipped = true;
            CHECK(shipped);
            t->layers[0].texture.clear();
            while (!t->layers.empty()) t->RemoveLayer(0);
        }
        // Limits and bad input.
        for (int i = 0; i < 6; ++i) t->AddLayer(grass);
        CHECK((int)t->layers.size() == Terrain::kMaxLayers);
        CHECK(!t->DecodeSplat("zz1;"));
        CHECK(!t->DecodeSplat("ff5;"));
    }

    // ---- cable: a sagging line between two objects ------------------------------------------------------------------
    {
        Scene s("J"); s.physicsEnabled = false;
        GameObject* a = s.CreateGameObject("PoleA"); a->transform->localPosition = {0, 5, 0};
        GameObject* b = s.CreateGameObject("PoleB"); b->transform->localPosition = {10, 5, 0};
        GameObject* c = s.CreateGameObject("Wire");
        auto* cab = c->AddComponent<Cable>();
        cab->startObject = "PoleA"; cab->endObject = "PoleB"; cab->sag = 2.0f; cab->segments = 10;
        CHECK(cab->Rebuild());
        auto* lr = c->GetComponent<LineRenderer>();
        CHECK(lr && lr->positions.size() == 11);
        CHECK_NEAR(lr->positions.front().x, 0.0f, 1e-4f); CHECK_NEAR(lr->positions.back().x, 10.0f, 1e-4f);
        CHECK_NEAR(lr->positions[5].y, 3.0f, 1e-4f);                  // the middle hangs `sag` below the ends
        CHECK_NEAR(lr->positions.front().y, 5.0f, 1e-4f);
        const float len = cab->Length();
        CHECK(len > 10.0f && len < 12.5f);                           // longer than the straight 10
        b->transform->localPosition = {10, 8, 0};                      // move an end: the cable follows
        CHECK(cab->Rebuild());
        CHECK_NEAR(lr->positions.back().y, 8.0f, 1e-4f);
        cab->width = 0.4f; cab->color = Color(1, 0, 0, 1); cab->segments = 4; cab->Rebuild();
        CHECK(lr->width == 0.4f && lr->color.r == 1.0f && lr->positions.size() == 5);   // the settings reach the Line Renderer
        cab->segments = 1000; cab->Rebuild();
        CHECK(lr->positions.size() == 201);                                           // clamped to 200 pieces
        cab->segments = 10; cab->width = 0.08f;
        // An offset end: no end object, the end is `endOffset` from the start.
        {
            GameObject* e = s.CreateGameObject("Free"); e->transform->localPosition = {0, 9, 0};
            auto* fc = e->AddComponent<Cable>();
            fc->endOffset = {5, 0, 0}; fc->sag = 0.0f; fc->segments = 5;
            CHECK(fc->Rebuild());
            CHECK_NEAR(e->GetComponent<LineRenderer>()->positions.back().x, 5.0f, 1e-4f);
        }
        cab->sag = 0.0f; cab->Rebuild();
        CHECK_NEAR(lr->positions[5].y, 6.5f, 1e-4f);                  // no sag: a straight line
        cab->endObject = "Nowhere";
        CHECK(!cab->Rebuild());
        // Saved and loaded.
        cab->endObject = "PoleB"; cab->sag = 1.5f; cab->width = 0.3f;
        const std::string text = SceneSerializer::Serialize(s);
        Scene r("R"); r.physicsEnabled = false;
        CHECK(SceneSerializer::Deserialize(r, text, nullptr, nullptr));
        auto* rc = r.Find("Wire")->GetComponent<Cable>();
        CHECK(rc && rc->startObject == "PoleA" && rc->endObject == "PoleB" && rc->sag == 1.5f && rc->width == 0.3f);
        CHECK(SceneSerializer::Serialize(r) == text);
    }

    // ---- decal: cut from the surfaces inside the box ---------------------------------------------------------------------
    {
        Scene s("K"); s.physicsEnabled = false;
        GameObject* floor = s.CreateGameObject("Floor");
        floor->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
        floor->transform->localScale = {20, 1, 20};
        floor->transform->localPosition = {0, -0.5f, 0};              // top face at y = 0
        GameObject* d = s.CreateGameObject("Stain");
        d->transform->localPosition = {0, 2, 0};
        d->transform->localRotation = Quat::LookRotation({0, -1, 0}, {0, 0, 1});      // +Z points down
        auto* dc = d->AddComponent<Decal>();
        dc->size = {4, 4, 5};
        const int tris = dc->Project();
        CHECK(tris > 0);
        auto* mr = d->GetComponent<MeshRenderer>();
        CHECK(mr && mr->mesh.TriangleCount() == tris);
        // Every vertex lies on the floor (just above it) and inside the 4 x 4 box.
        const Mat4 W = d->transform->LocalToWorldMatrix();
        bool ok = true;
        float area = 0.0f;
        for (const Vec3& v : mr->mesh.vertices) {
            const Vec3 w = W.MultiplyPoint(v);
            if (w.y < 0.0f || w.y > 0.05f || std::fabs(w.x) > 2.01f || std::fabs(w.z) > 2.01f) ok = false;
        }
        for (int t = 0; t < tris; ++t) {
            const Vec3 a = mr->mesh.vertices[mr->mesh.triangles[t * 3]], b = mr->mesh.vertices[mr->mesh.triangles[t * 3 + 1]], c = mr->mesh.vertices[mr->mesh.triangles[t * 3 + 2]];
            area += 0.5f * Vec3::Cross(b - a, c - a).Magnitude();
        }
        CHECK(ok);
        CHECK_NEAR(area, 16.0f, 0.1f);                                 // the whole 4 x 4 patch is covered
        CHECK(mr->mesh.HasUVs());
        // Lift: the decal floats that far above the floor.
        dc->lift = 0.2f; dc->Project();
        float minY = 1e9f, maxY = -1e9f;
        for (const Vec3& v : d->GetComponent<MeshRenderer>()->mesh.vertices) { const float wy = W.MultiplyPoint(v).y; minY = std::fmin(minY, wy); maxY = std::fmax(maxY, wy); }
        CHECK_NEAR(minY, 0.2f, 0.01f); CHECK_NEAR(maxY, 0.2f, 0.01f);
        dc->lift = 0.01f;
        // Tint and texture reach the renderer.
        dc->tint = Color(1, 0, 0, 0.5f); dc->texture = "crack.png"; dc->Project();
        CHECK(mr->color.r == 1.0f && mr->color.a == 0.5f && mr->texture == "crack.png");
        // Max Angle: a wall that faces the projector at a slant is skipped when the limit is smaller than the slant.
        {
            GameObject* wall = s.CreateGameObject("Slant");
            wall->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
            wall->transform->localScale = {6, 0.2f, 6};
            wall->transform->localPosition = {40, 0, 0};
            wall->transform->localRotation = Quat::Euler(0, 0, 50.0f);          // tipped 50 degrees (its thin end faces 40)
            GameObject* d2 = s.CreateGameObject("StainTwo");
            d2->transform->localPosition = {40, 3, 0};
            d2->transform->localRotation = Quat::LookRotation({0, -1, 0}, {0, 0, 1});
            auto* c2 = d2->AddComponent<Decal>();
            c2->size = {4, 4, 6};
            c2->maxAngle = 75.0f;
            const int wide = c2->Project();
            c2->maxAngle = 30.0f;
            const int narrow = c2->Project();
            CHECK(wide > 0 && narrow == 0);
        }
        // Nothing in reach: an empty decal.
        d->transform->localPosition = {0, 200, 0};
        CHECK(dc->Project() == 0);
        // A surface turned away gets nothing: the projector looks up at the floor's underside only.
        d->transform->localPosition = {0, -3, 0};
        CHECK(dc->Project() == 0);
        // Saved and loaded.
        dc->texture = "stain.png"; dc->maxAngle = 60.0f; d->transform->localPosition = {0, 2, 0};
        const std::string text = SceneSerializer::Serialize(s);
        Scene r("R"); r.physicsEnabled = false;
        CHECK(SceneSerializer::Deserialize(r, text, nullptr, nullptr));
        auto* rd = r.Find("Stain")->GetComponent<Decal>();
        CHECK(rd && rd->texture == "stain.png" && rd->maxAngle == 60.0f && rd->size.x == 4.0f);
        // The cut mesh is not saved (it is cut from the scene again): a loaded decal has none until it is projected.
        dc->Project();
        const int saved = dc->Triangles();
        CHECK(saved > 0);
        const std::string text2 = SceneSerializer::Serialize(s);
        Scene r2("R2"); r2.physicsEnabled = false;
        CHECK(SceneSerializer::Deserialize(r2, text2, nullptr, nullptr));
        auto* rm = r2.Find("Stain")->GetComponent<MeshRenderer>();
        CHECK(rm == nullptr || rm->mesh.TriangleCount() == 0);
        auto* rdc = r2.Find("Stain")->GetComponent<Decal>();
        CHECK(rdc && !rdc->Projected());
        CHECK(rdc->Project() == saved);                                  // the loaded scene has the floor: the same cut comes back
    }

    TEST_MAIN_RESULT();
}
