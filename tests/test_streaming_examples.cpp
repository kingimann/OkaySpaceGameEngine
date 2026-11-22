// The streaming example scenes in examples/streaming (shipped in the Windows zip): they load and work, and the
// frame-time numbers of the open world with the old main-thread streamer and the new background one.
//
// Regenerate the examples: OKAY_REGEN_STREAMING=1 ./test_streaming_examples (writes into examples/streaming,
// found from this source file's folder or OKAY_EXAMPLES_DIR).
#include "test_framework.hpp"
#include <Okay.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

namespace {

fs::path ExamplesDir() {
    if (const char* e = std::getenv("OKAY_EXAMPLES_DIR")) return fs::path(e) / "streaming";
    return fs::path(__FILE__).parent_path().parent_path() / "examples" / "streaming";
}

GameObject* Prim(Scene& s, const std::string& name, Mesh mesh, Vec3 pos, Vec3 scale, Color c) {
    GameObject* g = s.CreateGameObject(name);
    g->transform->localPosition = pos;
    g->transform->localScale = scale;
    auto* mr = g->AddComponent<MeshRenderer>();
    mr->mesh = std::move(mesh);
    mr->color = c;
    return g;
}

GameObject* FlyCamera(Scene& s, Vec3 pos, float yaw, float pitch, float speed) {
    GameObject* cam = s.CreateGameObject("Fly Camera");
    cam->transform->localPosition = pos;
    cam->AddComponent<Camera>();
    auto* fr = cam->AddComponent<FreeRoamController>();
    fr->moveSpeed = speed;
    fr->yaw = yaw; fr->pitch = pitch;
    s.mainCamera = cam->GetComponent<Camera>();
    return cam;
}

void Sun(Scene& s) {
    GameObject* l = s.CreateGameObject("Sun");
    l->AddComponent<Light>();
    l->transform->localRotation = Quat::Euler(50.0f, -30.0f, 0.0f);
}

/// A deterministic pseudo-random number in [0, 1) for building the town.
float R(int a, int b, int c) {
    std::uint32_t h = (std::uint32_t)a * 0x3A8F05C5u ^ (std::uint32_t)b * 0x6F4D2B19u ^ (std::uint32_t)c * 0x2C1B3C6Du;
    h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 13;
    return (float)(h & 0xFFFFFF) / 16777216.0f;
}

const float kBlock = 60.0f;
const int kBlocks = 12;

/// The open world before partitioning: 12 x 12 blocks of 60 m, each with houses, a tower or trees.
void BuildOpenWorldSource(Scene& s) {
    Sun(s);
    Prim(s, "Ground", Mesh::Cube(), Vec3{kBlocks * kBlock * 0.5f, -0.5f, kBlocks * kBlock * 0.5f},
         Vec3{kBlocks * kBlock, 1.0f, kBlocks * kBlock}, Color::FromBytes(92, 140, 78));
    for (int bx = 0; bx < kBlocks; ++bx)
        for (int bz = 0; bz < kBlocks; ++bz) {
            const float x0 = bx * kBlock, z0 = bz * kBlock;
            const std::string tag = std::to_string(bx) + "-" + std::to_string(bz);
            Prim(s, "Street " + tag, Mesh::Cube(), Vec3{x0 + kBlock * 0.5f, 0.02f, z0 + 3.0f}, Vec3{kBlock, 0.05f, 6.0f},
                 Color::FromBytes(70, 70, 76));
            const int houses = 2 + (int)(R(bx, bz, 1) * 4.0f);
            for (int h = 0; h < houses; ++h) {
                const float hx = x0 + 10.0f + R(bx, bz, 10 + h) * (kBlock - 20.0f), hz = z0 + 14.0f + R(bx, bz, 20 + h) * (kBlock - 24.0f);
                const float height = 4.0f + R(bx, bz, 30 + h) * (bx == 6 && bz == 6 ? 40.0f : 10.0f);
                GameObject* house = Prim(s, "House " + tag + " " + std::to_string(h), Mesh::Cube(), Vec3{hx, height * 0.5f, hz},
                                         Vec3{7.0f, height, 7.0f},
                                         Color::FromBytes((std::uint8_t)(150 + R(bx, bz, 40 + h) * 90), (std::uint8_t)(120 + R(bx, bz, 50 + h) * 80), 110));
                GameObject* roof = Prim(s, "Roof", Mesh::Pyramid(), Vec3{0.0f, 0.5f + 1.5f / height, 0.0f}, Vec3{1.15f, 3.0f / height, 1.15f},
                                        Color::FromBytes(150, 60, 50));
                roof->transform->SetParent(house->transform, false);
            }
            const int trees = (int)(R(bx, bz, 2) * 5.0f);
            for (int t = 0; t < trees; ++t) {
                const float tx = x0 + 5.0f + R(bx, bz, 60 + t) * (kBlock - 10.0f), tz = z0 + 10.0f + R(bx, bz, 70 + t) * (kBlock - 15.0f);
                GameObject* trunk = Prim(s, "Tree " + tag + " " + std::to_string(t), Mesh::Cylinder(), Vec3{tx, 1.5f, tz}, Vec3{0.6f, 3.0f, 0.6f},
                                         Color::FromBytes(110, 80, 50));
                GameObject* crown = Prim(s, "Crown", Mesh::Cone(), Vec3{0.0f, 1.4f, 0.0f}, Vec3{6.0f, 1.8f, 6.0f}, Color::FromBytes(50, 120, 60));
                crown->transform->SetParent(trunk->transform, false);
            }
        }
    FlyCamera(s, Vec3{30.0f, 22.0f, -20.0f}, 0.0f, 15.0f, 25.0f);
}

/// Level streaming corridor: section A here; B and C are streamed in by volumes as the camera flies along +Z.
void Section(Scene& s, const std::string& name, float z0, Color wall) {
    Prim(s, name + " Floor", Mesh::Cube(), Vec3{0, -0.25f, z0 + 20.0f}, Vec3{8, 0.5f, 40}, Color::FromBytes(90, 90, 96));
    Prim(s, name + " Wall L", Mesh::Cube(), Vec3{-4.25f, 2.5f, z0 + 20.0f}, Vec3{0.5f, 5, 40}, wall);
    Prim(s, name + " Wall R", Mesh::Cube(), Vec3{4.25f, 2.5f, z0 + 20.0f}, Vec3{0.5f, 5, 40}, wall);
    for (int i = 0; i < 8; ++i) {
        GameObject* l = Prim(s, name + " Lamp " + std::to_string(i), Mesh::Sphere(), Vec3{(i % 2) ? 3.5f : -3.5f, 4.0f, z0 + 3.0f + i * 5.0f},
                             Vec3{0.6f, 0.6f, 0.6f}, Color::FromBytes(255, 230, 160));
        auto* pl = l->AddComponent<Light>();
        pl->type = Light::Type::Point;
        pl->range = 8.0f;
    }
    for (int i = 0; i < 12; ++i)
        Prim(s, name + " Crate " + std::to_string(i), Mesh::Cube(), Vec3{(i % 3 - 1) * 2.0f, 0.5f, z0 + 4.0f + i * 3.0f}, Vec3{0.9f, 0.9f, 0.9f},
             Color::FromBytes(160, 120, 70));
}

void BuildCorridor(Scene& s) {
    Sun(s);
    Section(s, "A", 0.0f, Color::FromBytes(180, 180, 190));
    struct V { const char* name; const char* level; float z; };
    for (const V& v : {V{"Load Section B", "corridor_b", 40.0f}, V{"Load Section C", "corridor_c", 80.0f}}) {
        GameObject* g = s.CreateGameObject(v.name);
        g->transform->localPosition = Vec3{0, 2.5f, v.z + 20.0f};
        auto* vol = g->AddComponent<LevelStreamingVolume>();
        vol->level = v.level;
        vol->size = Vec3{8, 5, 40};
        vol->preloadDistance = 25.0f;
        vol->unloadMargin = 15.0f;
    }
    FlyCamera(s, Vec3{0.0f, 2.0f, 2.0f}, 0.0f, 0.0f, 8.0f);
}

void BuildVoxelWorld(Scene& s) {
    Sun(s);
    GameObject* w = s.CreateGameObject("Endless Voxel World");
    auto* vw = w->AddComponent<VoxelWorld>();
    vw->viewDistance = 80.0f; vw->unloadDistance = 112.0f;
    vw->groundHeight = 14.0f; vw->hills = 9.0f; vw->featureSize = 60.0f; vw->caves = 0.3f; vw->seed = 4;
    FlyCamera(s, Vec3{8.0f, 30.0f, 8.0f}, 0.0f, 20.0f, 20.0f);
    GameObject* cam = s.Find("Fly Camera");
    if (cam) cam->AddComponent<StreamingSource>();
}

double Ms(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

struct Run { double maxMs = 0, p99 = 0, meanMs = 0; int frames = 0, maxObjectsMade = 0; };

Run Summarize(std::vector<double> t, int maxMade) {
    Run r;
    r.frames = (int)t.size();
    if (t.empty()) return r;
    double sum = 0;
    for (double v : t) { sum += v; r.maxMs = std::max(r.maxMs, v); }
    r.meanMs = sum / t.size();
    std::sort(t.begin(), t.end());
    r.p99 = t[std::min(t.size() - 1, (std::size_t)(t.size() * 0.99))];
    r.maxObjectsMade = maxMade;
    return r;
}

int Live(const Scene& s) {
    int n = 0;
    for (const auto& up : s.Objects()) if (up && !up->IsDestroyed()) ++n;
    return n;
}

}  // namespace

int main() {
    RUN_SUITE("streaming_examples");
    const fs::path dir = ExamplesDir();

    if (std::getenv("OKAY_REGEN_STREAMING")) {
        fs::create_directories(dir);
        {
            Scene src("Open World (source)");
            BuildOpenWorldSource(src);
            CHECK(SceneSerializer::SaveToFile(src, (dir / "open_world_source.okayscene").string()));
            PartitionSettings st;
            st.cellSize = kBlock;
            st.prefix = "cell";
            st.loadRadius = 2; st.unloadRadius = 3;
            const PartitionResult r = WritePartition(src, st, (dir / "open_world.okayscene").string(), (dir / "open_world_cells").string());
            CHECK(r.ok);
            // The example shows its cells while it plays.
            Scene out("o");
            CHECK(SceneSerializer::LoadFromFile(out, (dir / "open_world.okayscene").string()));
            if (GameObject* g = out.Find("World Streamer")) g->GetComponent<WorldStreamer>()->debugOverlay = true;
            CHECK(SceneSerializer::SaveToFile(out, (dir / "open_world.okayscene").string()));
            std::printf("  wrote open_world: %d cells, %d objects streamed, %d always loaded\n", r.cellsWritten, r.objectsStreamed, r.objectsAlwaysLoaded);
        }
        {
            Scene c("Level Streaming Corridor");
            BuildCorridor(c);
            CHECK(SceneSerializer::SaveToFile(c, (dir / "level_corridor.okayscene").string()));
            Scene b("corridor_b");
            Section(b, "B", 40.0f, Color::FromBytes(120, 160, 210));
            CHECK(SceneSerializer::SaveToFile(b, (dir / "corridor_b.okayscene").string()));
            Scene cc("corridor_c");
            Section(cc, "C", 80.0f, Color::FromBytes(210, 140, 120));
            CHECK(SceneSerializer::SaveToFile(cc, (dir / "corridor_c.okayscene").string()));
        }
        {
            Scene v("Endless Voxel World");
            BuildVoxelWorld(v);
            CHECK(SceneSerializer::SaveToFile(v, (dir / "infinite_voxels.okayscene").string()));
        }
    }

    // ---- The shipped open world streams, and the frame times before and after --------------------------
    const std::string openWorld = (dir / "open_world.okayscene").string();
    CHECK(fs::exists(openWorld) && fs::exists(dir / "open_world_source.okayscene"));
    CHECK(fs::is_directory(dir / "open_world_cells"));
    Run oldRun, newRun;
    for (int pass = 0; pass < 2; ++pass) {
        const bool legacy = pass == 0;
        Scene s("ow");
        CHECK(SceneSerializer::LoadFromFile(s, openWorld));
        GameObject* cam = s.Find("Fly Camera");
        GameObject* so = s.Find("World Streamer");
        auto* ws = so ? so->GetComponent<WorldStreamer>() : nullptr;
        CHECK(cam && ws);
        if (!cam || !ws) break;
        if (auto* fr = cam->GetComponent<FreeRoamController>()) fr->enabled = false;   // the test drives the camera
        ws->debugOverlay = false;
        const fs::path cellDir = dir / "open_world_cells";
        // "Before": what the World Streamer did until now, run here as it was: every cell that comes into range
        // is read and made on the main thread in the frame the camera crosses into a new cell.
        std::map<std::pair<int, int>, std::uint64_t> legacyCells;
        int lastX = 1 << 30, lastZ = 1 << 30;
        auto legacyUpdate = [&]() {
            const Vec3 p = cam->transform->Position();
            const int cx = (int)std::floor(p.x / ws->cellSize), cz = (int)std::floor(p.z / ws->cellSize);
            if (cx == lastX && cz == lastZ) return;
            lastX = cx; lastZ = cz;
            for (auto it = legacyCells.begin(); it != legacyCells.end();) {
                if (std::max(std::abs(it->first.first - cx), std::abs(it->first.second - cz)) > ws->unloadRadius) {
                    if (GameObject* g = GameObject::FromInstanceId(it->second)) s.Destroy(g);
                    it = legacyCells.erase(it);
                } else ++it;
            }
            for (int z = cz - ws->loadRadius; z <= cz + ws->loadRadius; ++z)
                for (int x = cx - ws->loadRadius; x <= cx + ws->loadRadius; ++x) {
                    if (legacyCells.count({x, z})) continue;
                    GameObject* r = SceneSerializer::InstantiateFromFile(s, (cellDir / ("cell_" + std::to_string(x) + "_" + std::to_string(z) + ".okayprefab")).string());
                    legacyCells[{x, z}] = r ? r->InstanceId() : 0;
                }
        };
        if (legacy) ws->enabled = false;
        s.Start();
        if (legacy) legacyUpdate();
        s.Update(0.016f);
        // Fly diagonally across the town at 30 m/s for 15 seconds of game time (900 frames at 60 Hz).
        std::vector<double> times;
        int maxMade = 0;
        for (int f = 0; f < 900; ++f) {
            const float t = f / 60.0f;
            cam->transform->localPosition = Vec3{30.0f + t * 30.0f, 22.0f, 30.0f + t * 30.0f * 0.8f};
            const int before = Live(s);
            const auto t0 = std::chrono::steady_clock::now();
            if (legacy) legacyUpdate();
            s.Update(1.0f / 60.0f);
            times.push_back(Ms(t0, std::chrono::steady_clock::now()));
            maxMade = std::max(maxMade, Live(s) - before);
        }
        (legacy ? oldRun : newRun) = Summarize(times, maxMade);
        if (!legacy) {
            CHECK(ws->LoadedCount() >= 9);
            CHECK(ws->GetStats().loadsFinished > 20);
        }
    }
    std::printf("  [open world, 900 frames flying at 30 m/s, main-thread time of each frame]\n");
    std::printf("    before (cells made on the main thread when needed): max %.2f ms, 99th percentile %.2f ms, mean %.3f ms, most objects made in one frame %d\n",
                oldRun.maxMs, oldRun.p99, oldRun.meanMs, oldRun.maxObjectsMade);
    std::printf("    after  (read on workers, made in a 2 ms budget):     max %.2f ms, 99th percentile %.2f ms, mean %.3f ms, most objects made in one frame %d\n",
                newRun.maxMs, newRun.p99, newRun.meanMs, newRun.maxObjectsMade);
    CHECK(newRun.maxObjectsMade < oldRun.maxObjectsMade);   // the work is spread over frames

    // ---- The corridor: the sections stream in ahead of the camera and out behind it ---------------------
    {
        Scene s("corridor");
        CHECK(SceneSerializer::LoadFromFile(s, (dir / "level_corridor.okayscene").string()));
        GameObject* cam = s.Find("Fly Camera");
        CHECK(cam != nullptr);
        if (cam) {
            if (auto* fr = cam->GetComponent<FreeRoamController>()) fr->enabled = false;
            s.Start();
            for (int i = 0; i < 10; ++i) s.Update(1.0f / 60.0f);
            CHECK(s.Find("B Floor") == nullptr);                 // 40 m ahead is farther than the preload distance
            bool sawB = false, sawC = false, bGone = false;
            for (int f = 0; f < 60 * 28; ++f) {                  // fly along at 4 m/s, to z = 114
                cam->transform->localPosition = Vec3{0, 2, 2.0f + f * (4.0f / 60.0f)};
                s.Update(1.0f / 60.0f);
                if (s.Find("B Floor")) sawB = true;
                if (s.Find("C Floor")) sawC = true;
                if (sawB && sawC && !s.Find("B Floor")) bGone = true;
            }
            CHECK(sawB && sawC);
            CHECK(!bGone);                                       // still within B's unload margin (34 m past it, margin 40)
            cam->transform->localPosition = Vec3{0, 2, 400};
            for (int i = 0; i < 5; ++i) s.Update(1.0f / 60.0f);
            CHECK(s.Find("B Floor") == nullptr && s.Find("C Floor") == nullptr);   // far away: both out
        }
    }

    // ---- The endless voxel world generates around the camera -----------------------------------------------
    {
        Scene s("voxels");
        CHECK(SceneSerializer::LoadFromFile(s, (dir / "infinite_voxels.okayscene").string()));
        GameObject* w = s.Find("Endless Voxel World");
        auto* vw = w ? w->GetComponent<VoxelWorld>() : nullptr;
        GameObject* cam = s.Find("Fly Camera");
        CHECK(vw && cam);
        if (vw && cam) {
            if (auto* fr = cam->GetComponent<FreeRoamController>()) fr->enabled = false;
            s.Start();
            s.Update(1.0f / 60.0f);
            CHECK(vw->LoadedChunkCount() >= 9);
            double worst = 0;
            for (int f = 0; f < 600; ++f) {
                cam->transform->localPosition = Vec3{8.0f + f * 0.5f, 30, 8};
                const auto t0 = std::chrono::steady_clock::now();
                s.Update(1.0f / 60.0f);
                worst = std::max(worst, Ms(t0, std::chrono::steady_clock::now()));
            }
            CHECK(vw->IsChunkLoaded(19, 0));
            const VoxelWorld::Stats st = vw->GetStats();
            std::printf("  [voxel world, 600 frames flying at 30 m/s] %llu chunks generated on workers (%.0f ms of worker time), "
                        "worst frame %.2f ms, worst meshing in one frame %.2f ms\n",
                        (unsigned long long)st.generated, st.generateMs, worst, st.maxMeshMs);
        }
    }
    TEST_MAIN_RESULT();
}
