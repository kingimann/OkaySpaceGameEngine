// Chunk streaming for voxel and procedural worlds (feature map 27.4): the generic ChunkStreamer (endless
// registration, background generation, budgeted attach, kept edits) and the endless Voxel World built on it.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/Jobs.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <string>

using namespace okay;
using namespace okay::optimize;

namespace {
struct Data { int x = 0, z = 0; int edits = 0; };
/// One game frame: the scene update plus about 3 ms of waiting (a busy wait, no threads here), so chunks generated on
/// the job system's workers arrive between frames as they would in a game running at a normal frame rate.
void Frame(Scene& s) {
    s.Update(1.0f / 60.0f);
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() < 3.0) {}
}
}

int main() {
    RUN_SUITE("chunk_streaming");

    // ---- ChunkStreamer: an endless line of chunks around a walking viewer ---------------------------
    {
        JobSystem pool(2);
        SetEngineJobs(&pool);
        ChunkStreamerConfig cfg;
        cfg.chunkSize = 10.0f;
        cfg.loadRadius = 20.0f;
        cfg.unloadRadius = 30.0f;
        cfg.maxRequestsPerFrame = 4;
        cfg.maxAttachPerFrame = 2;
        cfg.attachBudgetMs = 1000.0f;
        ChunkStreamer cs(cfg);
        std::atomic<int> generatedOnWorker{0};
        std::map<CellCoord, std::shared_ptr<Data>> world;   // what is "in the scene"
        int attachedThisFrame = 0, maxAttached = 0;
        ChunkHooks h;
        h.generate = [&](const CellCoord& c) -> ChunkPayload {
            ++generatedOnWorker;
            auto d = std::make_shared<Data>();
            d->x = c.x; d->z = c.z;
            return d;
        };
        h.attach = [&](const CellCoord& c, ChunkPayload& p) {
            world[c] = std::static_pointer_cast<Data>(p);
            ++attachedThisFrame;
            return true;
        };
        h.detach = [&](const CellCoord& c) -> ChunkPayload {
            auto d = world[c];
            world.erase(c);
            return d && d->edits > 0 ? d : nullptr;     // only edited chunks are kept
        };
        cs.SetHooks(h);
        auto frame = [&](Vec3 p) {
            attachedThisFrame = 0;
            cs.Update({StreamingViewer{p, Vec3{1, 0, 0}}});
            maxAttached = std::max(maxAttached, attachedThisFrame);
            pool.WaitIdle();
        };
        for (int i = 0; i < 40; ++i) frame(Vec3{5, 0, 5});
        // Chunks whose box is within 20 of (5,5): x and z from -2 to 2, minus the corners farther than 20.
        CHECK(cs.LoadedCount() == 21);
        CHECK(maxAttached <= 2);
        CHECK(generatedOnWorker.load() == 21);
        CHECK(world.count(CellCoord{0, 0, 0}) && world.count(CellCoord{2, 0, 0}) && !world.count(CellCoord{2, 0, 2}));
        // Dig chunk (1,0), walk far east (an endless world: chunks are registered as the viewer comes).
        world[CellCoord{1, 0, 0}]->edits = 3;
        for (int step = 0; step < 100; ++step) frame(Vec3{5.0f + step * 10.0f, 0, 5});
        for (int i = 0; i < 20; ++i) frame(Vec3{1005, 0, 5});
        CHECK(cs.IsLoaded(CellCoord{100, 0, 0}) && !cs.IsLoaded(CellCoord{0, 0, 0}));
        CHECK(cs.HasSaved(CellCoord{1, 0, 0}) && cs.Stats().saved == 1);    // only the dug one was kept
        CHECK(cs.Stats().registered < 200);                                // far-behind chunks were forgotten
        // The 21 chunks within 20 of the viewer are in; a few just walked past stay until they are 30 away (hysteresis).
        int near = 0;
        for (int x = 98; x <= 102; ++x) for (int z = -2; z <= 2; ++z) if (cs.IsLoaded(CellCoord{x, 0, z})) ++near;
        CHECK(near == 21 && cs.LoadedCount() >= 21 && cs.LoadedCount() <= 25 && maxAttached <= 2);
        // Walk back: the dug chunk returns as it was left, not generated again.
        const int genBefore = generatedOnWorker.load();
        for (int i = 0; i < 40; ++i) frame(Vec3{5, 0, 5});
        const CellCoord dugChunk{1, 0, 0};
        CHECK(world.count(dugChunk) && world[dugChunk]->edits == 3);
        CHECK(!cs.HasSaved(CellCoord{1, 0, 0}) && cs.Stats().restored == 1);
        CHECK(generatedOnWorker.load() - genBefore == 20);                 // 21 chunks, one restored
        // Flush: everything at once.
        cs.Clear();
        CHECK(world.empty() && cs.LoadedCount() == 0);
        cs.Flush({StreamingViewer{Vec3{-500, 0, -500}, Vec3{}}});
        CHECK(cs.LoadedCount() == 21);
        if (cs.LoadedCount() != 21) std::printf("  flush: loaded %d registered %d ready %d generating %d\n", cs.LoadedCount(),
                                                cs.Stats().registered, cs.Stats().ready, cs.Stats().generating);
        // A generator that throws is retried later, not a crash.
        ChunkStreamer bad(cfg);
        ChunkHooks bh = h;
        int tries = 0;
        bh.generate = [&](const CellCoord&) -> ChunkPayload { ++tries; throw std::runtime_error("broken"); };
        bad.SetHooks(bh);
        for (int i = 0; i < 5; ++i) { bad.Update({StreamingViewer{Vec3{5000, 0, 5000}, Vec3{}}}); pool.WaitIdle(); }
        CHECK(bad.LoadedCount() == 0 && tries > 0);
        SetEngineJobs(nullptr);
    }

    // ---- VoxelWorld: endless voxel chunks that join without cracks --------------------------------
    {
        Scene s("voxels");
        GameObject* player = s.CreateGameObject("Player");
        player->transform->localPosition = Vec3{8, 20, 8};
        GameObject* w = s.CreateGameObject("World");
        auto* vw = w->AddComponent<VoxelWorld>();
        vw->chunkVoxels = 16; vw->heightVoxels = 24; vw->voxelSize = 1.0f;
        vw->viewDistance = 24.0f; vw->unloadDistance = 40.0f;
        vw->groundHeight = 10.0f; vw->hills = 5.0f; vw->featureSize = 40.0f; vw->seed = 7;
        vw->maxChunksPerFrame = 1; vw->meshBudgetMs = 0.0f;
        vw->target = "Player";
        s.Start();                    // the first ring is made at once
        s.Update(0.016f);
        const int first = vw->LoadedChunkCount();
        CHECK(first >= 9);
        CHECK(vw->IsChunkLoaded(0, 0) && vw->IsChunkLoaded(-1, -1) && vw->IsChunkLoaded(1, 1));
        // The chunks are ordinary, never-saved VoxelTerrains under the world object.
        VoxelTerrain* a = vw->ChunkAt(Vec3{8, 0, 8});
        VoxelTerrain* b = vw->ChunkAt(Vec3{24, 0, 8});
        CHECK(a && b && a != b);
        CHECK(a && a->gameObject->transient && a->gameObject->transform->Parent() == w->transform);
        CHECK(a && a->nx == 17 && a->ny == 25 && a->nz == 17);
        CHECK(SceneSerializer::Serialize(s).find("Voxel Chunk") == std::string::npos);
        // Neighbours share their border samples exactly (no cracks): a's last column is b's first.
        if (a && b) {
            int same = 0, total = 0;
            for (int z = 0; z < a->nz; ++z)
                for (int y = 0; y < a->ny; ++y) {
                    ++total;
                    if (a->Get(a->nx - 1, y, z) == b->Get(0, y, z)) ++same;
                }
            CHECK(same == total);
            // And the surface is where the world function says.
            float ya = 0.0f;
            CHECK(a->SurfaceY(0.0f, 0.0f, ya));
            CHECK(std::fabs(ya - vw->GroundAt(8.0f, 8.0f)) < 1.01f);
            CHECK(a->BuildMesh().TriangleCount() > 100);
        }
        // The ground function is the same every time (worker and main thread agree) and varies.
        CHECK(vw->DensityAt(Vec3{3, 2, 4}) == vw->DensityAt(Vec3{3, 2, 4}));
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < 200; ++i) { const float g = vw->GroundAt(i * 7.0f, i * 3.0f); lo = std::min(lo, g); hi = std::max(hi, g); }
        CHECK(hi - lo > 2.0f && lo > 10.0f - 5.01f && hi < 10.0f + 5.01f);

        // Dig chunk (0,0), walk 300 units away, come back: the hole is still there.
        if (a) {
            a->Dig(Vec3{0, 10, 0}, 3.0f, 10.0f);
            a->Apply();
        }
        const float dug = a ? a->SampleDensity(Vec3{0, 10, 0}) : 0.0f;
        CHECK(dug < 0.0f);
        int maxNew = 0;
        for (int step = 0; step <= 30; ++step) {
            player->transform->localPosition = Vec3{8.0f + step * 10.0f, 20, 8};
            const int before = (int)vw->GetStats().attached;
            Frame(s);
            maxNew = std::max(maxNew, (int)vw->GetStats().attached - before);
        }
        for (int i = 0; i < 60; ++i) Frame(s);
        CHECK(!vw->IsChunkLoaded(0, 0) && vw->IsChunkLoaded(19, 0));
        CHECK(vw->RememberedChunkCount() == 1);
        CHECK(maxNew <= 1);                               // one chunk meshed per frame at most
        player->transform->localPosition = Vec3{8, 20, 8};
        for (int i = 0; i < 120 && !vw->IsChunkLoaded(0, 0); ++i) Frame(s);
        for (int i = 0; i < 30; ++i) Frame(s);
        VoxelTerrain* back = vw->ChunkAt(Vec3{8, 0, 8});
        CHECK(back != nullptr);
        if (back) CHECK_NEAR(back->SampleDensity(Vec3{0, 10, 0}), dug, 1e-4f);
        CHECK(vw->RememberedChunkCount() == 0);
        const VoxelWorld::Stats st = vw->GetStats();
        std::printf("  [voxel world] %llu chunks generated on workers (%.1f ms worker time), the slowest frame meshed for %.2f ms\n",
                    (unsigned long long)st.generated, st.generateMs, st.maxMeshMs);
        CHECK(vw->DebugText().find("chunks") != std::string::npos);
        // Saved settings round-trip.
        const std::string text = SceneSerializer::SerializeObject(*w);
        CHECK(text.find("voxelworld") != std::string::npos);
        Scene c("c");
        GameObject* r = SceneSerializer::InstantiateFromText(c, text);
        auto* vw2 = r ? r->GetComponent<VoxelWorld>() : nullptr;
        CHECK(vw2 && vw2->seed == 7 && vw2->heightVoxels == 24 && vw2->target == "Player" && std::fabs(vw2->hills - 5.0f) < 1e-5f);
        // Taking the world away removes its chunks.
        s.Destroy(w);
        s.Update(0.016f); s.Update(0.016f);
        int chunks = 0;
        for (const auto& up : s.Objects()) if (up && !up->IsDestroyed() && up->name.rfind("Voxel Chunk", 0) == 0) ++chunks;
        CHECK(chunks == 0);
    }

    TEST_MAIN_RESULT();
}
