// World streaming on the job system (feature map 2.6.6, 27.4): the planner with several viewers and square
// rings, scene documents made in a per-frame time budget, the background stream queue, and the World Streamer
// built on them (sources, remembered changes, debug overlay, saved settings).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/StreamingSource.hpp"
#include "okay/Core/Jobs.hpp"
#include "okay/Optimize/StreamingGrid.hpp"
#include "okay/Scene/SceneStreamQueue.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;
using namespace okay::optimize;

namespace {

namespace fs = std::filesystem;

int Live(const Scene& s) {
    int n = 0;
    for (const auto& up : s.Objects()) if (up && !up->IsDestroyed()) ++n;
    return n;
}

int CountNamedPrefix(const Scene& s, const std::string& pre) {
    int n = 0;
    for (const auto& up : s.Objects())
        if (up && !up->IsDestroyed() && up->name.compare(0, pre.size(), pre) == 0) ++n;
    return n;
}

/// A cell file with `extra` child objects under one root named cell_x_z, sitting inside the cell.
void BakeCell(const std::string& dir, int x, int z, float cell, int extra) {
    Scene bake("bake");
    GameObject* root = bake.CreateGameObject("cell_" + std::to_string(x) + "_" + std::to_string(z));
    root->transform->localPosition = Vec3{x * cell + 1.0f, 0.0f, z * cell + 1.0f};
    for (int i = 0; i < extra; ++i) {
        GameObject* k = bake.CreateGameObject("prop_" + std::to_string(x) + "_" + std::to_string(z) + "_" + std::to_string(i));
        k->transform->SetParent(root->transform, false);
        k->transform->localPosition = Vec3{(float)(i % 10), 0.0f, (float)(i / 10)};
    }
    SceneSerializer::SaveObjectToFile(*root, dir + "/cell_" + std::to_string(x) + "_" + std::to_string(z) + ".okayprefab");
}

/// A frame of a running game also takes time: wait about a millisecond (busy, no threads here), so a read on a
/// worker has a chance to finish between frames as it would in a real game.
void FrameTime() {
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() < 1.0) {}
}

void Frames(Scene& s, int n) { for (int i = 0; i < n; ++i) { s.Update(1.0f / 60.0f); FrameTime(); } }

/// Run frames until `done` or the limit; returns the frames used.
template <class F> int Until(Scene& s, F done, int limit = 600) {
    for (int i = 0; i < limit; ++i) { if (done()) return i; s.Update(1.0f / 60.0f); FrameTime(); }
    return limit;
}

}  // namespace

int main() {
    RUN_SUITE("world_streaming");

    // ---- StreamingGrid: square rings and several viewers ---------------------------------------
    {
        StreamingConfig cfg;
        cfg.cellSize = 10.0f;
        cfg.metric = CellDistance::Chebyshev;
        cfg.loadRadius = 10.0f * (1.0f - 1e-5f);     // one ring
        cfg.unloadRadius = 20.0f * (1.0f - 1e-5f);
        cfg.maxLoadsPerFrame = 0; cfg.maxInFlight = 0; cfg.maxUnloadsPerFrame = 0;
        StreamingGrid g(cfg);
        g.RegisterRange(CellCoord{-20, 0, -20}, CellCoord{20, 0, 20});
        // Chebyshev distance: the largest per-axis gap, so a diagonal neighbour is as near as a side one.
        CHECK_NEAR(g.DistanceToCell(Vec3{15, 0, 15}, CellCoord{2, 0, 2}), 5.0, 1e-5);
        CHECK_NEAR(g.DistanceToCell(Vec3{15, 0, 15}, CellCoord{3, 0, 2}), 15.0, 1e-5);
        std::vector<StreamingViewer> two{{Vec3{5, 0, 5}, Vec3{0, 0, 0}}, {Vec3{105, 0, 5}, Vec3{0, 0, 0}}};
        StreamingFrame f = g.Update(two);
        CHECK(f.loadRequested.size() == 18);            // a 3x3 block around each viewer
        for (const CellCoord& c : g.LoadingCells()) g.NotifyLoaded(c);
        CHECK(g.State(CellCoord{1, 0, 1}) == CellState::Loaded);       // diagonal of viewer 1
        CHECK(g.State(CellCoord{11, 0, -1}) == CellState::Loaded);     // diagonal of viewer 2
        CHECK(g.State(CellCoord{5, 0, 0}) == CellState::Unloaded);     // between them
        // Viewer 2 leaves: its cells go once they are past the unload ring of every viewer; viewer 1's stay.
        f = g.Update(std::vector<StreamingViewer>{{Vec3{5, 0, 5}, Vec3{0, 0, 0}}});
        CHECK(f.unloadRequested.size() == 9);
        CHECK(g.State(CellCoord{1, 0, 1}) == CellState::Loaded);
        CHECK(g.State(CellCoord{10, 0, 0}) == CellState::Unloaded);
        // No viewers: nothing is wanted.
        f = g.Update(std::vector<StreamingViewer>{});
        CHECK(f.unloadRequested.size() == 9 && g.LoadedCells().empty());
        // The single-viewer call is the one-element list (Euclidean default unchanged).
        StreamingConfig e; e.cellSize = 10.0f; e.loadRadius = 10.0f; e.maxLoadsPerFrame = 0; e.maxInFlight = 0;
        StreamingGrid a(e), b(e);
        a.RegisterRange(CellCoord{-5, 0, -5}, CellCoord{5, 0, 5});
        b.RegisterRange(CellCoord{-5, 0, -5}, CellCoord{5, 0, 5});
        const StreamingFrame fa = a.Update(Vec3{3, 0, 4}, Vec3{1, 0, 0});
        const StreamingFrame fb = b.Update(std::vector<StreamingViewer>{{Vec3{3, 0, 4}, Vec3{1, 0, 0}}});
        CHECK(fa.loadRequested == fb.loadRequested);
        // A second viewer ranks the cells in front of it first.
        StreamingConfig p = e; p.maxLoadsPerFrame = 1; p.viewDirectionWeight = 2.0f; p.loadRadius = 30.0f;
        StreamingGrid pg(p);
        pg.RegisterCell(CellCoord{-3, 0, 0}); pg.RegisterCell(CellCoord{3, 0, 0});
        const StreamingFrame pf = pg.Update(std::vector<StreamingViewer>{{Vec3{5, 0, 5}, Vec3{1, 0, 0}}});
        CHECK(pf.loadRequested.size() == 1 && pf.loadRequested[0] == (CellCoord{3, 0, 0}));
    }

    // ---- Scene::BeginAsyncLoad with a document read elsewhere, and a time budget -----------------------
    {
        Scene src("src");
        GameObject* root = src.CreateGameObject("BigRoot");
        for (int i = 0; i < 300; ++i) src.CreateGameObject("k" + std::to_string(i))->transform->SetParent(root->transform, false);
        const std::string text = SceneSerializer::SerializeObject(*root);

        Scene s("dst");
        Scene::AsyncLoadOptions o;
        o.objectsPerFrame = 1;               // one object per batch ...
        bool done = false;
        o.onDone = [&](bool ok) { done = ok; };
        const int h = s.BeginAsyncLoad(SceneLoadJob::OpenText(text, true, Vec3{5, 0, 0}), o);
        CHECK(h > 0);
        s.Update(0.016f);
        CHECK(!done && Live(s) < 10);          // ... and no budget: one batch a frame
        CHECK(s.LoadedRoots(h).empty());       // nothing to report while loading
        s.UnloadAdditive(h);
        s.Update(0.016f);

        Scene::AsyncLoadOptions t;
        t.objectsPerFrame = 1;
        t.timeBudgetMs = 10000.0f;           // a budget far larger than the work: everything in one frame
        bool done2 = false;
        t.onDone = [&](bool ok) { done2 = ok; };
        const int h2 = s.BeginAsyncLoad(SceneLoadJob::OpenText(text, true, Vec3{5, 0, 0}), t);
        s.Update(0.016f);
        CHECK(done2);
        const auto roots = s.LoadedRoots(h2);
        CHECK(roots.size() == 1);
        GameObject* r = roots.empty() ? nullptr : GameObject::FromInstanceId(roots[0]);
        CHECK(r && r->name == "BigRoot" && std::fabs(r->transform->Position().x - 5.0f) < 1e-4f);   // the job's offset
        CHECK(Live(s) == 301);
        CHECK(s.BeginAsyncLoad(std::unique_ptr<SceneLoadJob>(), Scene::AsyncLoadOptions()) == 0);
    }

    const std::string dir = (fs::temp_directory_path() / "okay_world_streaming_cells").string();
    fs::remove_all(dir);
    fs::create_directories(dir);
    const float kCell = 50.0f;
    for (int z = 0; z < 8; ++z)
        for (int x = 0; x < 8; ++x)
            if (!(x == 1 && z == 1)) BakeCell(dir, x, z, kCell, 40);   // 41 objects a cell; (1,1) is a hole

    // ---- SceneStreamQueue: read on workers, made over frames, released --------------------------------
    {
        JobSystem pool(2);
        SetEngineJobs(&pool);
        Scene s("q");
        SceneStreamQueue q;
        q.attachBudgetMs = 0.05f;
        q.objectsPerStep = 4;
        int doneCalls = 0;
        const int t1 = q.RequestFile(dir + "/cell_2_2.okayprefab", Vec3{0, 0, 0}, [&](int, bool ok) { if (ok) ++doneCalls; });
        const int bad = q.RequestFile(dir + "/no_such_cell.okayprefab");
        CHECK(q.Busy());
        int maxMade = 0, prev = 0;
        const int frames = Until(s, [&] {
            q.Pump(s);
            const int now = Live(s);
            maxMade = std::max(maxMade, now - prev);
            prev = now;
            return q.State(t1) == StreamLoadState::Loaded && q.State(bad) == StreamLoadState::Failed;
        });
        CHECK(frames < 600);
        if (frames >= 600) std::printf("  queue states: t1 %d bad %d, live %d, reading %d waiting %d attaching %d\n", (int)q.State(t1), (int)q.State(bad), Live(s),
                                       q.Stats().reading, q.Stats().waiting, q.Stats().attaching);
        CHECK(doneCalls == 1);
        CHECK(Live(s) == 41);
        CHECK(maxMade < 41);                    // never the whole cell in one frame
        CHECK(q.Roots(t1).size() == 1);
        CHECK(q.Stats().failed == 1 && q.Stats().finished == 1);
        CHECK(q.Release(s, t1));
        s.Update(0.016f);
        CHECK(Live(s) == 0);
        CHECK(!q.Release(s, t1));
        // Flush: blocking, for the first frame.
        const int t2 = q.RequestFile(dir + "/cell_3_3.okayprefab");
        q.Flush(s);
        CHECK(q.State(t2) == StreamLoadState::Loaded && Live(s) == 41);
        q.Clear(s);
        s.Update(0.016f);
        CHECK(Live(s) == 0 && !q.Busy());
        // Released while reading: nothing arrives later.
        const int t3 = q.RequestFile(dir + "/cell_4_4.okayprefab");
        CHECK(q.Release(s, t3));
        Frames(s, 3); q.Pump(s); Frames(s, 3);
        CHECK(Live(s) == 0);
        SetEngineJobs(nullptr);
    }

    // ---- World Streamer: background loads, budget, sources, remembered changes, overlay ----------------
    {
        Scene scene("world");
        GameObject* player = scene.CreateGameObject("Player");
        player->transform->localPosition = Vec3{kCell * 2 + 5.0f, 0.0f, kCell * 2 + 5.0f};   // cell (2,2)
        GameObject* so = scene.CreateGameObject("Streamer");
        auto* ws = so->AddComponent<WorldStreamer>();
        ws->cellSize = kCell; ws->loadRadius = 1; ws->unloadRadius = 2;
        ws->folder = dir; ws->target = "Player";
        ws->attachBudgetMs = 0.05f;            // a tiny budget: a cell takes several frames
        scene.Start();
        scene.Update(0.016f);                  // Start: the first ring loads at once (blocking, like a level load)
        CHECK(ws->CellFileCount() == 63);
        CHECK(ws->IsCellLoaded(2, 2) && ws->IsCellLoaded(3, 3) && ws->IsCellLoaded(1, 2));
        CHECK(!ws->IsCellLoaded(1, 1) && !ws->HasCellFile(1, 1));      // the hole
        CHECK(ws->LoadedCount() == 8);
        CHECK(scene.Find("cell_2_2") != nullptr && scene.Find("cell_4_4") == nullptr);

        // Walk two cells east: the new column streams in over several frames, never all at once.
        const int before = Live(scene);
        player->transform->localPosition = Vec3{kCell * 4 + 5.0f, 0.0f, kCell * 2 + 5.0f};
        scene.Update(0.016f);
        CHECK(ws->GetStats().loading > 0);     // nearest first: the x = 4 column is asked for before x = 5
        CHECK(!ws->IsCellLoaded(5, 2));
        CHECK(Live(scene) - before < 41);      // the frame that asked made at most part of one cell
        int maxNew = 0, last = Live(scene);
        const int used = Until(scene, [&] {
            const int now = Live(scene);
            maxNew = std::max(maxNew, now - last);
            last = now;
            return ws->IsCellLoaded(5, 1) && ws->IsCellLoaded(5, 2) && ws->IsCellLoaded(5, 3) &&
                   ws->IsCellLoaded(4, 1) && ws->IsCellLoaded(4, 3);
        });
        CHECK(used > 3 && used < 600);
        CHECK(maxNew < 41);
        CHECK(!ws->IsCellLoaded(1, 2));        // past the unload ring
        CHECK(ws->IsCellLoaded(2, 2));         // inside it still (hysteresis)
        CHECK(scene.Find("cell_1_2") == nullptr && scene.Find("cell_5_2") != nullptr);
        std::printf("  [stream] a 3-cell column (123 objects) streamed in over %d frames, at most %d objects in one frame\n", used, maxNew);

        // A second source far away keeps its own cells loaded.
        GameObject* cam2 = scene.CreateGameObject("SecondPlayer");
        cam2->transform->localPosition = Vec3{kCell * 0 + 5.0f, 0.0f, kCell * 6 + 5.0f};    // cell (0,6)
        cam2->AddComponent<StreamingSource>();
        Until(scene, [&] { return ws->IsCellLoaded(0, 6) && ws->IsCellLoaded(1, 7) && ws->IsCellLoaded(0, 5); });
        CHECK(ws->IsCellLoaded(0, 6) && ws->IsCellLoaded(1, 7));
        CHECK(ws->IsCellLoaded(5, 2));          // the first source's cells stay
        CHECK(ws->GetStats().sources == 2);
        cam2->GetComponent<StreamingSource>()->enabled = false;
        Until(scene, [&] { return !ws->IsCellLoaded(0, 6); });
        CHECK(!ws->IsCellLoaded(0, 6));

        // Remembered changes: move a prop and destroy another in cell (5,2), walk away, come back.
        GameObject* moved = scene.Find("prop_5_2_3");
        GameObject* gone = scene.Find("prop_5_2_4");
        CHECK(moved && gone);
        if (moved) moved->transform->localPosition = Vec3{7.5f, 3.0f, -2.0f};
        if (gone) scene.Destroy(gone);
        scene.Update(0.016f);
        player->transform->localPosition = Vec3{kCell * 1 + 5.0f, 0.0f, kCell * 6 + 5.0f};   // far west/north
        Until(scene, [&] { return !ws->IsCellLoaded(5, 2) && ws->IsCellLoaded(1, 6); });
        CHECK(!ws->IsCellLoaded(5, 2) && ws->HasSnapshot(5, 2));
        CHECK(scene.Find("prop_5_2_3") == nullptr);
        player->transform->localPosition = Vec3{kCell * 5 + 5.0f, 0.0f, kCell * 2 + 5.0f};
        Until(scene, [&] { return ws->IsCellLoaded(5, 2); });
        GameObject* back = scene.Find("prop_5_2_3");
        CHECK(back != nullptr);
        if (back) CHECK_NEAR(back->transform->localPosition.y, 3.0f, 1e-4f);
        CHECK(scene.Find("prop_5_2_4") == nullptr);    // destroyed stays destroyed
        CHECK(scene.Find("prop_5_2_5") != nullptr);
        CHECK(!ws->HasSnapshot(5, 2));                 // used up by the reload
        CHECK(scene.Find("prop_4_2_4") != nullptr);   // a neighbour came back whole

        // Debug overlay: one transient mesh object, coloured by state, never saved.
        ws->debugOverlay = true;
        scene.Update(0.016f);
        GameObject* ov = scene.Find("Streaming Debug");
        CHECK(ov != nullptr && ov->transient);
        auto* mr = ov ? ov->GetComponent<MeshRenderer>() : nullptr;
        CHECK(mr && mr->mesh.TriangleCount() > 63 * 8 && mr->mesh.HasFaceColors());
        bool green = false, grey = false;
        if (mr) for (const Color& c : mr->mesh.triColors) {
            if (c.g > 0.7f && c.r < 0.4f) green = true;
            if (std::fabs(c.r - c.g) < 0.05f && c.r < 0.6f && c.r > 0.3f) grey = true;
        }
        CHECK(green && grey);
        CHECK(SceneSerializer::Serialize(scene).find("Streaming Debug") == std::string::npos);
        ws->debugOverlay = false;
        scene.Update(0.016f);
        scene.Update(0.016f);
        CHECK(scene.Find("Streaming Debug") == nullptr);
        CHECK(ws->DebugText().find("loaded") != std::string::npos);

        // Removing the streamer takes its cells with it.
        scene.Destroy(so);
        scene.Update(0.016f);
        scene.Update(0.016f);
        CHECK(CountNamedPrefix(scene, "cell_") == 0 && CountNamedPrefix(scene, "prop_") == 0);
    }

    // ---- Saved settings: the new fields round-trip, an old line still loads ------------------------------
    {
        Scene a("a");
        GameObject* go = a.CreateGameObject("S");
        auto* ws = go->AddComponent<WorldStreamer>();
        ws->backgroundLoading = false; ws->attachBudgetMs = 3.5f; ws->maxLoadsPerFrame = 5; ws->maxInFlight = 7;
        ws->viewDirectionWeight = 1.25f; ws->memoryBudgetMB = 64.0f; ws->rememberChanges = false;
        ws->followSources = false; ws->debugOverlay = true; ws->debugHeight = 2.5f;
        const std::string text = SceneSerializer::SerializeObject(*go);
        Scene b("b");
        GameObject* r = SceneSerializer::InstantiateFromText(b, text);
        auto* w2 = r ? r->GetComponent<WorldStreamer>() : nullptr;
        CHECK(w2 != nullptr);
        if (w2) {
            CHECK(!w2->backgroundLoading && w2->maxLoadsPerFrame == 5 && w2->maxInFlight == 7);
            CHECK_NEAR(w2->attachBudgetMs, 3.5f, 1e-5f); CHECK_NEAR(w2->viewDirectionWeight, 1.25f, 1e-5f);
            CHECK_NEAR(w2->memoryBudgetMB, 64.0f, 1e-5f); CHECK_NEAR(w2->debugHeight, 2.5f, 1e-5f);
            CHECK(!w2->rememberChanges && !w2->followSources && w2->debugOverlay);
        }
        // A line from before the streaming settings (the record cut after its first eight fields): the defaults.
        std::string old = text;
        {
            const std::size_t at = old.find("  worldstreamer ");
            CHECK(at != std::string::npos);
            const std::size_t eol = old.find('\n', at);
            std::string line = old.substr(at, eol - at);
            int quotes = 0; std::size_t cut = line.size();
            for (std::size_t i = 0; i < line.size(); ++i)
                if (line[i] == '"' && ++quotes == 8) { cut = i + 1; break; }
            old.replace(at, eol - at, line.substr(0, cut));
        }
        Scene c("c");
        GameObject* o = SceneSerializer::InstantiateFromText(c, old);
        auto* w3 = o ? o->GetComponent<WorldStreamer>() : nullptr;
        CHECK(w3 != nullptr);
        if (w3) {
            CHECK_NEAR(w3->cellSize, 50.0f, 1e-5f);
            CHECK(w3->backgroundLoading && w3->rememberChanges && w3->followSources && !w3->debugOverlay);
            CHECK_NEAR(w3->attachBudgetMs, 2.0f, 1e-5f);
        }
    }

    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
