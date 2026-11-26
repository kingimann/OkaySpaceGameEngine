// Asset streaming core (docs/engine/asset_streaming.md): AssetManager::Stream priorities, cancel, the RAM budget
// cache, FindFresh; streamed scene loads (Scene::BeginAsyncLoad stream) under the frame budget, cancel mid-load,
// spread unloads, quick unload/reload, 0 worker threads; mesh decodes through the streamer; WorldStreamer cells.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Core/AssetManager.hpp"
#include "okay/Core/Jobs.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/WorldStreamer.hpp"
#include "okay/Modeling/MeshAsset.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace okay;
namespace fs = std::filesystem;

namespace {
struct Blob { std::string text; };

std::atomic<bool> g_gateOpen{false};
std::atomic<bool> g_gateEntered{false};
std::vector<std::string> g_order;   // decode order (written by the one decode job at a time)

void RegisterBlob(AssetManager& m) {
    m.RegisterLoader<Blob>("Blob",
        [](const std::string& p, std::string& err) -> std::shared_ptr<Blob> {
            const std::string name = fs::path(p).filename().string();
            if (name == "gate.blob") {
                g_gateEntered = true;
                const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                while (!g_gateOpen.load() && std::chrono::steady_clock::now() < end) {}
            }
            g_order.push_back(name);
            std::ifstream f(p, std::ios::binary);
            if (!f) { err = "cannot open " + p; return nullptr; }
            auto b = std::make_shared<Blob>();
            b->text.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
            return b;
        },
        [](const Blob&) { return std::size_t(40); });
}

void WriteText(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << s;
}

int CountNamed(const Scene& s, const std::string& prefix) {
    int n = 0;
    for (const auto& go : s.Objects())
        if (go && !go->IsDestroyed() && go->name.compare(0, prefix.size(), prefix) == 0) ++n;
    return n;
}

// A cell prefab: one root with `n` children, each with one grandchild (2n + 1 objects).
std::string MakeCell(const fs::path& dir, const std::string& name, int n, const std::string& meshFile = "") {
    Scene bake("bake");
    GameObject* root = bake.CreateGameObject(name);
    for (int i = 0; i < n; ++i) {
        GameObject* c = bake.CreateGameObject(name + "_obj" + std::to_string(i));
        c->transform->SetParent(root->transform);
        c->transform->localPosition = Vec3{float(i), 0.0f, 0.0f};
        GameObject* g = bake.CreateGameObject(name + "_leaf" + std::to_string(i));
        g->transform->SetParent(c->transform);
        if (!meshFile.empty() && i == 0) {
            auto* mr = c->AddComponent<MeshRenderer>();
            std::string err;
            CHECK(modeling::LoadMeshAsset(meshFile, mr->mesh, nullptr, &err));
            mr->meshPath = meshFile;
        }
    }
    const std::string path = (dir / (name + ".okayprefab")).string();
    CHECK(SceneSerializer::SaveObjectToFile(*root, path));
    return path;
}

// maxFrames < 0: until done or 10 s of wall time. A frame here does almost no work, so a fixed 5000 frames passed in a few
// milliseconds and a busy CI machine had not finished the background read by then (main's full run, 2026-11-26).
template <class F> int RunFrames(Scene& s, F done, int maxFrames = -1) {
    int frames = 0;
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!done() && (maxFrames >= 0 ? frames < maxFrames : std::chrono::steady_clock::now() < until)) {
        s.Update(0.016f);
        AssetManager::Instance().Pump();
        ++frames;
    }
    return frames;
}
} // namespace

int main() {
    RUN_SUITE("asset_streaming");
    const fs::path dir = fs::temp_directory_path() / "okay_asset_streaming";
    fs::remove_all(dir);
    fs::create_directories(dir);
    AssetManager& am = AssetManager::Instance();
    RegisterBlob(am);

    // --- Budgets come from the machine (Automatic mode); a renderer's report sets the video budget -------------
    {
        am.ResetStreamingForTests();
        CHECK(AssetManager::SystemMemoryBytes() > 0);   // Linux: sysconf
        CHECK(am.RamBudget() >= 256u * 1024u * 1024u);
        CHECK(am.VramBudget() >= 512u * 1024u * 1024u);
        am.ReportVideoMemory(1000ull * 1024ull * 1024ull);
        CHECK(am.VramBudget() == 800u * 1024u * 1024u);
        StreamSettings s; s.automatic = false; s.ramBudgetBytes = 12345; s.vramBudgetBytes = 678;
        am.SetStreamSettings(s);
        CHECK(am.RamBudget() == 12345u);
        CHECK(am.VramBudget() == 678u);
        am.ResetStreamingForTests();
        CHECK(am.GetStreamSettings().automatic);
        CHECK(am.GetStreamStats().systemMemory > 0);
    }

    // --- Priority order and cancel: one worker, the first decode held at a gate --------------------------------
    {
        am.ResetStreamingForTests();
        JobSystem js(1);
        SetEngineJobs(&js);
        for (const char* n : {"gate", "low", "high", "mid", "dropped"}) WriteText(dir / (std::string(n) + ".blob"), n);
        g_order.clear();
        AssetHandle<Blob> gate = am.Stream<Blob>((dir / "gate.blob").string(), 0.0f);
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!g_gateEntered.load() && std::chrono::steady_clock::now() < end) {}
        CHECK(g_gateEntered.load());
        AssetHandle<Blob> low = am.Stream<Blob>((dir / "low.blob").string(), 1.0f);
        AssetHandle<Blob> high = am.Stream<Blob>((dir / "high.blob").string(), 5.0f);
        AssetHandle<Blob> mid = am.Stream<Blob>((dir / "mid.blob").string(), 3.0f);
        am.Stream<Blob>((dir / "dropped.blob").string(), 9.0f);          // nobody keeps a handle ...
        CHECK(am.CancelStream<Blob>((dir / "dropped.blob").string()));    // ... and it is no longer wanted
        CHECK(am.GetStreamStats().queued == 4);
        g_gateOpen = true;
        js.WaitIdle();
        am.Pump();
        const std::vector<std::string> want{"gate.blob", "high.blob", "mid.blob", "low.blob"};
        CHECK(g_order == want);   // highest priority first; the cancelled one never decoded
        CHECK(am.GetStreamStats().cancelled == 1);
        CHECK(high.Ready() && mid.Ready() && low.Ready());
        CHECK(!am.CancelStream<Blob>((dir / "high.blob").string()));     // decoded: stays cached
        SetEngineJobs(nullptr);
    }

    // --- The RAM budget: cached assets nobody holds go lowest priority first ----------------------------------
    {
        am.ResetStreamingForTests();
        JobSystem js(0);
        SetEngineJobs(&js);
        StreamSettings s; s.automatic = false; s.ramBudgetBytes = 100;   // each Blob counts 40 bytes
        am.SetStreamSettings(s);
        for (int i = 1; i <= 5; ++i) {
            WriteText(dir / ("p" + std::to_string(i) + ".blob"), "x");
            CHECK(am.Stream<Blob>((dir / ("p" + std::to_string(i) + ".blob")).string(), float(i)).Ready());
        }
        AssetHandle<Blob> held = am.Find<Blob>((dir / "p1.blob").string());   // in use: never evicted
        am.Pump();
        StreamStats st = am.GetStreamStats();
        CHECK(st.evicted == 3);                 // p2, p3, p4 go (lowest first); p1 is held and still counts
        CHECK(st.residentBytes == 80);          // p1 and p5
        CHECK(am.Find<Blob>((dir / "p5.blob").string()).Ready());
        CHECK(!am.Find<Blob>((dir / "p4.blob").string()).Ready());
        CHECK(!am.Find<Blob>((dir / "p2.blob").string()).Ready());
        CHECK(held.Ready());
        // FindFresh: only while the file is unchanged.
        CHECK(am.FindFresh<Blob>((dir / "p5.blob").string()).Ready());
        WriteText(dir / "p5.blob", "a longer text, so the size differs");
        CHECK(!am.FindFresh<Blob>((dir / "p5.blob").string()).Ready());
        am.ResetStreamingForTests();
        SetEngineJobs(nullptr);
    }

    const int kChildren = 400;   // 801 objects per cell
    const std::string cellA = MakeCell(dir, "cellA", kChildren);

    // --- A streamed load never spends more than the frame budget (fake clock, 0 workers: inline) -------------
    for (int workers : {0, 2}) {
        am.ResetStreamingForTests();
        JobSystem js(workers);
        SetEngineJobs(&js);
        StreamSettings s; s.frameBudgetMs = 2.0;
        am.SetStreamSettings(s);
        Scene scene("w");
        double now = 0.0;
        scene.SetStreamClock([&now] { now += 0.05; return now; });   // every clock read costs 50 us
        Scene::AsyncLoadOptions o; o.additive = true; o.stream = true;
        int doneCalls = 0; bool doneOk = false;
        o.onDone = [&](bool ok) { ++doneCalls; doneOk = ok; };
        const int h = scene.BeginAsyncLoad(cellA, o);
        CHECK(h > 0);
        int frames = 0, maxCreated = 0, framesCreating = 0;
        double maxMs = 0.0;
        bool sawReading = false, sawCreating = false;
        while (!scene.IsLoaded(h) && frames < 5000) {
            const Scene::LoadPhase ph = scene.GetLoadPhase(h);
            sawReading |= ph == Scene::LoadPhase::Reading;
            sawCreating |= ph == Scene::LoadPhase::Creating;
            scene.Update(0.016f);
            const auto& f = scene.LastStreamFrame();
            maxCreated = std::max(maxCreated, f.created);
            maxMs = std::max(maxMs, f.ms);
            if (f.created > 0) ++framesCreating;
            ++frames;
        }
        CHECK(scene.IsLoaded(h));
        CHECK(doneCalls == 1 && doneOk);
        CHECK(sawReading);
        CHECK(sawCreating || framesCreating > 1);
        CHECK(framesCreating > 4);                   // spread over frames, not one
        CHECK(maxMs <= 2.0 + 0.2);                   // the budget, plus the last object's clock reads
        CHECK(maxCreated <= 2.0 / 0.05 + 1);
        CHECK(maxCreated < 2 * kChildren + 1);
        CHECK(CountNamed(scene, "cellA") == 2 * kChildren + 1);
        CHECK(scene.LoadedRoots(h).size() == 1);
        std::cout << "  workers " << workers << ": " << framesCreating << " frames creating, at most " << maxCreated
                  << " objects and " << maxMs << " ms (fake clock) per frame\n";

        // Spread unload: also under the budget, children before parents, nothing left. (A small budget: the fake
        // clock does not see what removing an object costs, only the clock reads.)
        s.frameBudgetMs = 0.2;
        am.SetStreamSettings(s);
        CHECK(scene.UnloadAdditiveSpread(h));
        CHECK(scene.GetLoadPhase(h) == Scene::LoadPhase::Unloading);
        int uframes = 0; double umax = 0.0;
        while (scene.GetLoadPhase(h) == Scene::LoadPhase::Unloading && uframes < 5000) {
            scene.Update(0.016f);
            umax = std::max(umax, scene.LastStreamFrame().ms);
            ++uframes;
        }
        CHECK(scene.GetLoadPhase(h) == Scene::LoadPhase::None);
        CHECK(uframes > 1);
        CHECK(umax <= 0.2 + 0.3);
        CHECK(CountNamed(scene, "cellA") == 0);
        SetEngineJobs(nullptr);
    }

    // --- Cancel mid-load leaves nothing behind; unload then reload at once is safe ----------------------------
    {
        am.ResetStreamingForTests();
        JobSystem js(2);
        SetEngineJobs(&js);
        StreamSettings s; s.frameBudgetMs = 0.5;
        am.SetStreamSettings(s);
        Scene scene("c");
        double now = 0.0;
        scene.SetStreamClock([&now] { now += 0.01; return now; });
        Scene::AsyncLoadOptions o; o.additive = true; o.stream = true;
        bool cancelledHeard = false;
        o.onDone = [&](bool ok) { cancelledHeard = !ok; };
        int h = scene.BeginAsyncLoad(cellA, o);
        RunFrames(scene, [&] { return CountNamed(scene, "cellA") > 0; });
        CHECK(scene.GetLoadPhase(h) == Scene::LoadPhase::Creating);
        CHECK(CountNamed(scene, "cellA") < 2 * kChildren + 1);
        CHECK(scene.UnloadAdditiveSpread(h));         // the player turned back
        CHECK(cancelledHeard);
        RunFrames(scene, [&] { return scene.GetLoadPhase(h) == Scene::LoadPhase::None; });
        CHECK(CountNamed(scene, "cellA") == 0);

        // Cancel before the read finished (a read still queued is dropped too).
        h = scene.BeginAsyncLoad(cellA, o);
        CHECK(scene.UnloadAdditiveSpread(h));
        RunFrames(scene, [&] { return scene.GetLoadPhase(h) == Scene::LoadPhase::None; }, 50);
        js.WaitIdle();
        scene.Update(0.016f);
        CHECK(CountNamed(scene, "cellA") == 0);

        // Load, unload and at once load again: the old copy leaves while the new one comes in; one copy at the end.
        Scene::AsyncLoadOptions o2; o2.additive = true; o2.stream = true;
        h = scene.BeginAsyncLoad(cellA, o2);
        RunFrames(scene, [&] { return scene.IsLoaded(h); });
        CHECK(scene.IsLoaded(h));
        CHECK(scene.UnloadAdditiveSpread(h));
        const int h2 = scene.BeginAsyncLoad(cellA, o2);
        CHECK(scene.UnloadAdditiveSpread(h2));          // and once more, the other way round
        const int h3 = scene.BeginAsyncLoad(cellA, o2);
        RunFrames(scene, [&] {
            return scene.IsLoaded(h3) && scene.GetLoadPhase(h) == Scene::LoadPhase::None &&
                   scene.GetLoadPhase(h2) == Scene::LoadPhase::None;
        });
        CHECK(scene.IsLoaded(h3));
        CHECK(CountNamed(scene, "cellA") == 2 * kChildren + 1);
        // CompleteLoad finishes a load in one call (a teleport).
        CHECK(scene.UnloadAdditiveSpread(h3));
        const int h4 = scene.BeginAsyncLoad(cellA, o2);
        CHECK(scene.CompleteLoad(h4));
        CHECK(scene.IsLoaded(h4));
        RunFrames(scene, [&] { return scene.GetLoadPhase(h3) == Scene::LoadPhase::None; });
        CHECK(CountNamed(scene, "cellA") == 2 * kChildren + 1);
        // A missing file reports a failure a frame later (the read is on a job thread).
        bool failed = false;
        Scene::AsyncLoadOptions o3; o3.additive = true; o3.stream = true; o3.onDone = [&](bool ok) { failed = !ok; };
        const int hm = scene.BeginAsyncLoad((dir / "nope.okayprefab").string(), o3);
        CHECK(hm > 0);
        RunFrames(scene, [&] { return failed; }, 200);
        CHECK(failed);
        CHECK(scene.GetLoadPhase(hm) == Scene::LoadPhase::Failed);
        SetEngineJobs(nullptr);
    }

    // --- Meshes a cell names are decoded on a job thread through the streamer, then copied, not read again ----
    {
        am.ResetStreamingForTests();
        JobSystem js(2);
        SetEngineJobs(&js);
        const std::string meshFile = (dir / "rock.okaymesh").string();
        std::string err;
        CHECK(modeling::SaveMeshAsset(meshFile, Mesh::FromName("Rock"), &err));
        const std::string cellM = MakeCell(dir, "cellM", 3, meshFile);
        Scene scene("m");
        Scene::AsyncLoadOptions o; o.additive = true; o.stream = true;
        const int h = scene.BeginAsyncLoad(cellM, o);
        RunFrames(scene, [&] { return scene.IsLoaded(h); });
        CHECK(scene.IsLoaded(h));
        CHECK(am.FindFresh<modeling::MeshFile>(meshFile).Ready());   // decoded by the streamer, kept cached
        GameObject* obj = scene.Find("cellM_obj0");
        MeshRenderer* mr = obj ? obj->GetComponent<MeshRenderer>() : nullptr;
        CHECK(mr && !mr->mesh.vertices.empty());
        CHECK(am.GetStreamStats().resident >= 1);
        SetEngineJobs(nullptr);
        am.ResetStreamingForTests();
    }

    // --- WorldStreamer: cells stream in over frames; turning back cancels; nothing is left behind -------------
    {
        am.ResetStreamingForTests();
        JobSystem js(2);
        SetEngineJobs(&js);
        StreamSettings s; s.frameBudgetMs = 1.0;
        am.SetStreamSettings(s);
        const fs::path cells = dir / "cells";
        fs::create_directories(cells);
        for (int z = 0; z < 6; ++z)
            for (int x = 0; x < 6; ++x) {
                const std::string name = "cell_" + std::to_string(x) + "_" + std::to_string(z);
                MakeCell(cells, name, 60);
            }
        Scene scene("ws");
        GameObject* player = scene.CreateGameObject("Player");
        player->transform->localPosition = Vec3{25.0f, 0.0f, 25.0f};   // cell (0,0)
        auto* ws = scene.CreateGameObject("Streamer")->AddComponent<WorldStreamer>();
        ws->cellSize = 50.0f; ws->loadRadius = 1; ws->unloadRadius = 2;
        ws->folder = cells.string(); ws->prefix = "cell"; ws->target = "Player";
        scene.Update(0.016f);   // Start: lists the folder and asks for the 2 x 2 block around (0,0)
        CHECK(ws->KnownCellCount() == 36);
        CHECK(ws->IsCellWanted(1, 1));
        RunFrames(scene, [&] { return ws->LoadingCount() == 0; });
        CHECK(ws->IsCellLoaded(0, 0) && ws->IsCellLoaded(1, 1));
        CHECK(!ws->IsCellWanted(2, 2));
        // Fly to (4,4), then straight back before those cells finish: their loads are cancelled.
        player->transform->localPosition = Vec3{225.0f, 0.0f, 225.0f};
        scene.Update(0.016f);
        CHECK(ws->IsCellWanted(4, 4));
        CHECK(ws->GetCellState(0, 0) == WorldStreamer::CellState::Unloading ||
              ws->GetCellState(0, 0) == WorldStreamer::CellState::Unloaded);
        player->transform->localPosition = Vec3{25.0f, 0.0f, 25.0f};
        scene.Update(0.016f);
        CHECK(!ws->IsCellWanted(4, 4));
        RunFrames(scene, [&] { return ws->LoadingCount() == 0 && !scene.AnyLoading(); });
        for (int i = 0; i < 200; ++i) scene.Update(0.016f);   // spread unloads finish
        CHECK(CountNamed(scene, "cell_4_4") == 0);
        CHECK(CountNamed(scene, "cell_3_3") == 0);
        CHECK(CountNamed(scene, "cell_0_0") == 121);
        CHECK(CountNamed(scene, "cell_1_1") == 121);
        // SyncNow (teleport): everything wanted is in when it returns.
        player->transform->localPosition = Vec3{175.0f, 0.0f, 175.0f};   // (3,3)
        ws->SyncNow();
        CHECK(ws->IsCellLoaded(3, 3) && ws->IsCellLoaded(4, 4) && ws->IsCellLoaded(2, 2));
        CHECK(CountNamed(scene, "cell_3_3") == 121);
        SetEngineJobs(nullptr);
    }

    // --- Predictive loading: StreamingGrid look-ahead, WorldStreamer velocity -------------------------------
    {
        optimize::StreamingConfig cfg;
        cfg.cellSize = 10.0f; cfg.loadRadius = 10.0f; cfg.unloadRadius = 20.0f; cfg.ringDistance = true;
        cfg.maxLoadsPerFrame = 0; cfg.maxInFlight = 0;
        optimize::StreamingGrid g(cfg);
        g.RegisterRange(optimize::CellCoord{-5, 0, -5}, optimize::CellCoord{10, 0, 5});
        optimize::StreamingFrame f = g.Update(Vec3{5, 0, 5}, Vec3{1, 0, 0});
        CHECK(g.State(optimize::CellCoord{4, 0, 0}) == optimize::CellState::Unloaded);
        f = g.Update(Vec3{5, 0, 5}, Vec3{1, 0, 0}, Vec3{40, 0, 0});    // heading 4 cells east
        CHECK(g.State(optimize::CellCoord{4, 0, 0}) == optimize::CellState::Loading);
        CHECK(g.State(optimize::CellCoord{5, 0, 0}) == optimize::CellState::Loading);
        CHECK(g.State(optimize::CellCoord{2, 0, 0}) == optimize::CellState::Loading);    // on the way
        CHECK(g.State(optimize::CellCoord{6, 0, 0}) == optimize::CellState::Unloaded);
        CHECK(g.State(optimize::CellCoord{-1, 0, 0}) == optimize::CellState::Loading);   // still around the viewer
        CHECK(g.State(optimize::CellCoord{-4, 0, 0}) == optimize::CellState::Unloaded);
        (void)f;

        am.ResetStreamingForTests();
        JobSystem js(2);
        SetEngineJobs(&js);
        const fs::path cells = dir / "cells";   // the 6 x 6 world made above
        Scene scene("predict");
        GameObject* player = scene.CreateGameObject("Player");
        player->transform->localPosition = Vec3{10.0f, 0.0f, 25.0f};
        auto* ws = scene.CreateGameObject("Streamer")->AddComponent<WorldStreamer>();
        ws->cellSize = 50.0f; ws->loadRadius = 0; ws->unloadRadius = 3;
        ws->folder = cells.string(); ws->prefix = "cell"; ws->target = "Player";
        ws->predictSeconds = 1.5f;
        for (int i = 0; i < 30; ++i) {   // east at 100 units per second (2 cells per second)
            player->transform->localPosition = player->transform->localPosition + Vec3{100.0f / 60.0f, 0.0f, 0.0f};
            scene.Update(1.0f / 60.0f);
        }
        CHECK(ws->MeasuredVelocity().x > 80.0f);
        int cx = -1, cz = -1;
        CHECK(ws->CurrentCell(cx, cz) && cx == 1 && cz == 0);
        CHECK(ws->IsCellWanted(2, 0));   // ahead of the player (about 130 units), asked for before it gets there
        CHECK(ws->IsCellWanted(3, 0));
        CHECK(!ws->IsCellWanted(4, 0));
        ws->SyncNow();                   // and SyncNow finishes what is wanted, the cells ahead included
        CHECK(ws->IsCellLoaded(1, 0) && ws->IsCellLoaded(3, 0));
        SetEngineJobs(nullptr);
    }

    am.ResetStreamingForTests();
    fs::remove_all(dir);
    TEST_MAIN_RESULT();
}
