// World streaming foundation: sources, decisions, hysteresis, loaders with budgets, stable ids,
// round trips and saves (docs/foundations/world_streaming.md).
#include "test_framework.hpp"
#include "scene_text_testkit.hpp"
#include "okay/Scene/PartitionStreamer.hpp"
#include <algorithm>
#include <cmath>
#include <set>

using namespace okay;
using namespace okay::worldpart;

namespace {

const float kCell = 16.0f;

// A 5x5 block of cells around the origin (cells -2..2 on x and z), two trees in each:
// a "Crate" and a "Hut" with a child "Door". Positions are inside the cell.
Scene& FillWorld(Scene& s) {
    for (int cx = -2; cx <= 2; ++cx)
        for (int cz = -2; cz <= 2; ++cz) {
            const std::string tag = std::to_string(cx) + "_" + std::to_string(cz);
            GameObject* crate = s.CreateGameObject("Crate" + tag);
            crate->transform->localPosition = {cx * kCell + 3.0f, 0.5f, cz * kCell + 4.0f};
            crate->AddComponent<SpriteRenderer>()->glyph = 'C';
            GameObject* hut = s.CreateGameObject("Hut" + tag);
            hut->transform->localPosition = {cx * kCell + 10.0f, 0.0f, cz * kCell + 9.0f};
            GameObject* door = s.CreateGameObject("Door" + tag);
            door->transform->SetParent(hut->transform, false);
            door->transform->localPosition = {1.0f, 0.0f, 0.0f};
            door->tag = "door";
        }
    s.FlushDestroyed();
    return s;
}

std::map<std::string, std::string> Snapshot(const Scene& s) {
    std::map<std::string, std::string> m;
    for (GameObject* r : s.Roots()) m[r->Guid()] = SceneSerializer::SerializeObject(*r);
    return m;
}

struct Fake { double now = 0.0; };

SourceSample Src(float x, float z, float load = 20.0f, float unload = 30.0f, float prio = 1.0f) {
    SourceSample s; s.position = Vec3{x, 0, z}; s.loadRadius = load; s.unloadRadius = unload; s.priority = prio; return s;
}

} // namespace

int main() {
    RUN_SUITE("world_streamer");
    RegisterStreamingSerializers();
    const WorldGrid grid(kCell, GridPlane::XZ);

    Scene world("world");
    FillWorld(world);
    const auto original = Snapshot(world);
    SplitResult split;
    std::string err;
    CHECK(SplitScene(world, grid, SplitOptions{}, split, &err));
    CHECK(split.cells.size() == 25 && split.persistentRoots == 0 && split.cellRoots == 50);
    MemoryChunkStore baseline;
    baseline.Add(split);
    const std::map<std::string, std::string> baselineCopy = [&] {
        std::map<std::string, std::string> m;
        for (const std::string& n : baseline.List()) { std::string t; baseline.Read(n, t); m[n] = t; }
        return m;
    }();
    auto storeUntouched = [&](const MemoryChunkStore& st) {
        for (const auto& kv : baselineCopy) { std::string t; if (!st.Read(kv.first, t) || t != kv.second) return false; }
        return st.List().size() == baselineCopy.size();
    };

    // ---- planner: wanted, unload, cancel, scores ----
    {
        std::vector<CellCoord> known = split.cells;
        std::sort(known.begin(), known.end());
        // Source in cell (0,0) with a load radius of 1 cell: its own cell and the ones touching.
        CellPlan p = PlanCells(grid, {Src(8, 8, 1.0f, 1.0f)}, known, {}, {});
        CHECK(p.wanted.size() == 1 && p.wanted[0].first == CellCoord({0, 0, 0}) && p.wanted[0].second == 0.0f);
        p = PlanCells(grid, {Src(8, 8, 12.0f, 12.0f)}, known, {}, {});
        CHECK(p.wanted.size() == 9);
        CHECK(p.wanted[0].first == CellCoord({0, 0, 0}));                 // nearest first
        for (std::size_t i = 1; i < p.wanted.size(); ++i) CHECK(p.wanted[i - 1].second <= p.wanted[i].second);
        // Loaded far away goes, loaded near stays.
        p = PlanCells(grid, {Src(8, 8, 9.0f, 20.0f)}, known, {{0, 0, 0}, {2, 0, 2}, {1, 0, 0}}, {{-2, 0, -2}, {0, 0, 1}});
        CHECK(p.unload == std::vector<CellCoord>({CellCoord{2, 0, 2}}));
        CHECK(p.cancel == std::vector<CellCoord>({CellCoord{-2, 0, -2}}));
        // No usable source: nothing wanted, NOTHING unloaded.
        const float nan = std::numeric_limits<float>::quiet_NaN();
        p = PlanCells(grid, {}, known, {{2, 0, 2}}, {{1, 0, 1}});
        CHECK(p.wanted.empty() && p.unload.empty() && p.cancel.empty());
        SourceSample bad1 = Src(nan, 0), bad2 = Src(0, 0, -1.0f), bad3 = Src(0, 0, 10, 5), bad4 = Src(0, 0, 10, 20, 0.0f), bad5 = Src(0, 0, nan);
        p = PlanCells(grid, {bad1, bad2, bad3, bad4, bad5}, known, {{2, 0, 2}}, {});
        CHECK(p.wanted.empty() && p.unload.empty());
        p = PlanCells(WorldGrid(0.0f), {Src(0, 0)}, known, {{2, 0, 2}}, {});
        CHECK(p.wanted.empty() && p.unload.empty());
        // Priority: the same distance scores lower (sooner) for the higher priority source.
        p = PlanCells(grid, {Src(8, 8, 12.0f, 12.0f, 1.0f), Src(-24, 8, 12.0f, 12.0f, 4.0f)}, known, {}, {});
        CHECK(p.wanted.size() == 12);
        double sumHigh = 0, sumLow = 0;
        for (auto& w : p.wanted) (w.first.x < 0 ? sumHigh : sumLow) += w.second;
        CHECK(sumHigh < sumLow);
        // Two sources wanting the same cell score it by the better one.
        p = PlanCells(grid, {Src(8, 8, 9.0f, 9.0f), Src(40, 8, 40.0f, 40.0f, 1.0f)}, known, {}, {});
        for (auto& w : p.wanted) if (w.first == CellCoord({0, 0, 0})) CHECK(w.second == 0.0f);
    }

    // ---- planner: a big catalogue (looked up by radius) gives the same answer as a scan ----
    {
        std::vector<CellCoord> big;
        for (int x = -30; x <= 30; ++x) for (int z = -30; z <= 30; ++z) if ((x * 7 + z * 13) % 3 != 0) big.push_back({x, 0, z});
        std::sort(big.begin(), big.end());
        CHECK(big.size() > 2048);
        tk::TkRng rng(42);
        for (int i = 0; i < 40; ++i) {
            const SourceSample s = Src(rng.real(-300, 300), rng.real(-300, 300), rng.real(0, 90), 100.0f, 0.5f + rng.real(0, 3));
            const CellPlan fast = PlanCells(grid, {s}, big, {}, {});
            std::vector<std::pair<CellCoord, float>> brute;
            for (const CellCoord& c : big) {
                const float d = grid.DistanceToCell(s.position, c);
                if (d <= s.loadRadius) brute.push_back({c, d / s.priority});
            }
            std::sort(brute.begin(), brute.end(), [](const auto& a, const auto& b) { return a.second != b.second ? a.second < b.second : a.first < b.first; });
            CHECK(fast.wanted == brute);
        }
    }

    // ---- hysteresis: no thrash at a boundary ----
    {
        std::vector<CellCoord> known = split.cells;
        std::sort(known.begin(), known.end());
        // Cell (1,0,0) spans x 16..32. A source on the x axis wiggles around its load edge (x = 16 - 20 = -4)
        // and unload edge (x = 16 - 30 = -14).
        std::set<CellCoord> loaded;
        int transitions = 0;
        for (int i = 0; i < 2000; ++i) {
            const float x = -4.0f + 3.0f * std::sin(i * 0.9f);        // swings across the load edge, never near the unload edge
            const CellPlan p = PlanCells(grid, {Src(x, 8, 20.0f, 30.0f)}, known, std::vector<CellCoord>(loaded.begin(), loaded.end()), {});
            for (auto& w : p.wanted) if (loaded.insert(w.first).second) ++transitions;
            for (auto& u : p.unload) { loaded.erase(u); ++transitions; }
        }
        CHECK(loaded.count({1, 0, 0}) == 1);
        const int firstLoadTransitions = transitions;
        // Without hysteresis (unload == load) the same walk would flip the cell many times.
        loaded.clear(); transitions = 0;
        for (int i = 0; i < 2000; ++i) {
            const float x = -4.0f + 3.0f * std::sin(i * 0.9f);
            const CellPlan p = PlanCells(grid, {Src(x, 8, 20.0f, 20.0f)}, known, std::vector<CellCoord>(loaded.begin(), loaded.end()), {});
            for (auto& w : p.wanted) if (loaded.insert(w.first).second) ++transitions;
            for (auto& u : p.unload) { loaded.erase(u); ++transitions; }
        }
        CHECK(transitions > firstLoadTransitions + 100);
        CHECK(firstLoadTransitions < 20);        // only the loads of the cells that ever came near: no unload at all
    }

    // ---- streamer with sources in the scene: load, keep, unload, multi source ----
    {
        Scene scene("play");
        MemoryChunkStore store = baseline;
        SyncCellLoader loader;
        PartitionStreamer st(scene, grid, store, loader);
        CHECK(st.KnownCells().size() == 25);
        TickReport r = st.Tick();
        CHECK(r.sources == 0 && r.loaded == 0 && scene.Objects().empty());      // no source, nothing happens

        GameObject* player = scene.CreateGameObject("Player");
        auto* a = player->AddComponent<StreamingSource>();
        a->loadRadius = 7.0f; a->unloadRadius = 20.0f;
        player->transform->localPosition = {8, 0, 8};                            // middle of cell (0,0)
        r = st.Tick();
        CHECK(r.sources == 1 && r.loaded == 1 && r.wanted == 1);
        CHECK(st.State({0, 0, 0}) == CellState::Loaded && st.LoadedCount() == 1);
        CHECK(scene.Find("Crate0_0") && scene.Find("Door0_0") && !scene.Find("Crate1_0"));

        // Walk east; hysteresis keeps cell 0 until the player is 20 from its edge.
        player->transform->localPosition = {30, 0, 8};
        r = st.Tick();
        CHECK(st.State({1, 0, 0}) == CellState::Loaded);
        CHECK(st.State({0, 0, 0}) == CellState::Loaded);                         // 14 away: inside the unload radius
        player->transform->localPosition = {40, 0, 8};                          // 24 from cell 0's edge (x = 16): past 20
        r = st.Tick();
        CHECK(r.unloaded >= 1 && st.State({0, 0, 0}) == CellState::Unloaded);
        CHECK(!scene.Find("Crate0_0"));

        // A second source far to the west loads its own cells; both areas stay.
        GameObject* scout = scene.CreateGameObject("Scout");
        auto* b = scout->AddComponent<StreamingSource>();
        b->loadRadius = 7.0f; b->unloadRadius = 20.0f;
        scout->transform->localPosition = {-24, 0, 8};                           // cell (-2, 0)
        st.Tick();
        CHECK(st.State({-2, 0, 0}) == CellState::Loaded && st.State({2, 0, 0}) == CellState::Loaded);
        // Disabling the scout's source lets its cells go (not "no source": the player's is still there).
        b->enabled = false;
        st.Tick();
        CHECK(st.State({-2, 0, 0}) == CellState::Unloaded && st.State({2, 0, 0}) == CellState::Loaded);
        b->enabled = true;
        scout->active = false;                                                    // inactive object: its source is ignored too
        scene.Update(0.0f);
        st.Tick();
        CHECK(st.State({-2, 0, 0}) == CellState::Unloaded);
        // Extra (non-component) source.
        st.SetExtraSources({Src(-24, 8, 7, 20)});
        st.Tick();
        CHECK(st.State({-2, 0, 0}) == CellState::Loaded);
        st.SetExtraSources({});
        st.Tick();
        CHECK(st.State({-2, 0, 0}) == CellState::Unloaded);
        // Non-finite source position: ignored, nothing is unloaded.
        const std::size_t before = st.LoadedCount();
        player->transform->localPosition = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
        r = st.Tick();
        CHECK(r.sources == 0 && st.LoadedCount() == before);
        CHECK(storeUntouched(store));
    }

    // ---- budgets with a fake clock ----
    {
        Fake clock;
        auto run = [&](double budget, double costPerCell, int maxPerPump, int cellsWanted, std::vector<int>& perFrame, int& frames) {
            clock.now = 0.0;
            MemoryChunkStore store = baseline;
            // Each chunk read costs `costPerCell` of fake time.
            struct SlowStore : IChunkStore {
                MemoryChunkStore& in; Fake& c; double cost;
                SlowStore(MemoryChunkStore& i, Fake& cc, double k) : in(i), c(cc), cost(k) {}
                bool Read(const std::string& n, std::string& o) const override { c.now += cost; return in.Read(n, o); }
                bool Write(const std::string& n, const std::string& t) override { return in.Write(n, t); }
                std::vector<std::string> List() const override { return in.List(); }
            };
            SlowStore slow(store, clock, costPerCell);
            Scene scene2("b2");
            BudgetedCellLoader loader2(budget, [&] { return clock.now; }, maxPerPump);
            PartitionStreamer st2(scene2, grid, slow, loader2);
            GameObject* p2 = scene2.CreateGameObject("P");
            auto* s2 = p2->AddComponent<StreamingSource>();
            s2->loadRadius = 1000.0f; s2->unloadRadius = 1000.0f;
            p2->transform->localPosition = {8, 0, 8};
            perFrame.clear();
            frames = 0;
            while (st2.LoadedCount() < static_cast<std::size_t>(cellsWanted) && frames < 100) {
                const TickReport r = st2.Tick();
                perFrame.push_back(r.loaded + r.failures);
                ++frames;
            }
        };
        std::vector<int> per; int frames = 0;
        // 1 ms a cell, 2.5 ms budget: starts at t=0, 1, 2 (elapsed < 2.5), not at 3: three cells per frame.
        run(0.0025, 0.001, 0, 25, per, frames);
        CHECK(frames == 9);                                   // 8 frames of 3, then 1
        for (std::size_t i = 0; i + 1 < per.size(); ++i) CHECK(per[i] == 3);
        CHECK(per.back() == 1);
        // A budget smaller than one cell still makes progress: one cell a frame.
        run(0.0001, 0.001, 0, 25, per, frames);
        CHECK(frames == 25);
        for (int n : per) CHECK(n == 1);
        run(0.0, 0.001, 0, 25, per, frames);
        CHECK(frames == 25);
        // A huge budget loads all at once ... unless capped per frame.
        run(10.0, 0.001, 0, 25, per, frames);
        CHECK(frames == 1 && per[0] == 25);
        run(10.0, 0.001, 4, 25, per, frames);
        CHECK(frames == 7);
        for (std::size_t i = 0; i + 1 < per.size(); ++i) CHECK(per[i] == 4);
        // Bad budgets read as 0.
        run(std::numeric_limits<double>::quiet_NaN(), 0.001, 0, 25, per, frames);
        CHECK(frames == 25);
        run(-5.0, 0.001, 0, 25, per, frames);
        CHECK(frames == 25);
    }

    // ---- priority order, ties, cancel ----
    {
        BudgetedCellLoader loader(0.0, [] { return 0.0; }, 0);          // one a pump
        loader.Enqueue({3, 0, 0}, 5.0f);
        loader.Enqueue({1, 0, 0}, 1.0f);
        loader.Enqueue({2, 0, 0}, 1.0f);                                 // ties by coordinate
        loader.Enqueue({0, 0, 9}, std::numeric_limits<float>::quiet_NaN());   // NaN last
        loader.Enqueue({4, 0, 0}, 0.5f);
        loader.Enqueue({3, 0, 0}, 0.1f);                                 // re-queue updates the score
        CHECK(loader.Pending() == 5 && loader.Queued({3, 0, 0}) && !loader.Queued({9, 9, 9}));
        CHECK((loader.Order() == std::vector<CellCoord>({{3, 0, 0}, {4, 0, 0}, {1, 0, 0}, {2, 0, 0}, {0, 0, 9}})));
        std::vector<CellCoord> seen;
        while (loader.Pending()) loader.Pump([&](const CellCoord& c) { seen.push_back(c); return LoadStatus::Done; });
        CHECK((seen == std::vector<CellCoord>({{3, 0, 0}, {4, 0, 0}, {1, 0, 0}, {2, 0, 0}, {0, 0, 9}})));
        // Retry stays queued and is not called twice in one pump; Failed leaves the queue.
        SyncCellLoader sync;
        sync.Enqueue({1, 0, 0}, 1); sync.Enqueue({2, 0, 0}, 2); sync.Enqueue({3, 0, 0}, 3);
        int calls = 0;
        int n = sync.Pump([&](const CellCoord& c) { ++calls; return c.x == 1 ? LoadStatus::Retry : (c.x == 2 ? LoadStatus::Failed : LoadStatus::Done); });
        CHECK(n == 3 && calls == 3 && sync.Pending() == 1 && sync.Queued({1, 0, 0}));
        CHECK(sync.Cancel({1, 0, 0}) && !sync.Cancel({1, 0, 0}) && sync.Pending() == 0);
        // A load that cancels another queued cell: that one is skipped.
        sync.Enqueue({1, 0, 0}, 1); sync.Enqueue({2, 0, 0}, 2);
        calls = 0;
        sync.Pump([&](const CellCoord& c) { ++calls; sync.Cancel({2, 0, 0}); return LoadStatus::Done; });
        CHECK(calls == 1);
        CHECK(sync.Pump(CellLoadFn()) == 0);

        // End to end: nearest cells load first.
        Scene scene("order");
        MemoryChunkStore store = baseline;
        BudgetedCellLoader one(0.0, [] { return 0.0; }, 1);
        PartitionStreamer st(scene, grid, store, one);
        GameObject* p = scene.CreateGameObject("P");
        auto* s = p->AddComponent<StreamingSource>();
        s->loadRadius = 1000.0f; s->unloadRadius = 1000.0f;
        p->transform->localPosition = {-30, 0, 20};                         // in cell (-2, 1)
        std::vector<CellCoord> order;
        std::set<CellCoord> have;
        for (int i = 0; i < 30 && have.size() < 25; ++i) {
            st.Tick();
            for (const CellCoord& c : st.LoadedCells()) if (have.insert(c).second) order.push_back(c);
        }
        CHECK(order.size() == 25);
        CHECK(order[0] == CellCoord({-2, 0, 1}));
        for (std::size_t i = 1; i < order.size(); ++i)
            CHECK(grid.DistanceToCell(Vec3{-30, 0, 20}, order[i - 1]) <= grid.DistanceToCell(Vec3{-30, 0, 20}, order[i]));
        // Cancel: with one load a frame, move away so queued cells fall outside the unload radius.
        Scene scene2("cancel");
        BudgetedCellLoader one2(0.0, [] { return 0.0; }, 1);
        PartitionStreamer st2(scene2, grid, store, one2);
        GameObject* p2 = scene2.CreateGameObject("P");
        auto* s2 = p2->AddComponent<StreamingSource>();
        s2->loadRadius = 40.0f; s2->unloadRadius = 50.0f;
        p2->transform->localPosition = {-30, 0, -30};
        TickReport r = st2.Tick();
        CHECK(r.loaded == 1 && r.pending > 3);
        p2->transform->localPosition = {60, 0, 60};
        r = st2.Tick();
        CHECK(r.cancelled > 0);
        for (const CellCoord& c : st2.KnownCells()) CHECK(st2.State(c) != CellState::Queued || grid.DistanceToCell(Vec3{60, 0, 60}, c) <= 50.0f);
        CHECK(storeUntouched(store));
    }

    // ---- stable ids, round trip equals the original, state survives unload ----
    {
        Scene scene("rt");
        MemoryChunkStore store = baseline;
        SyncCellLoader loader;
        PartitionStreamer st(scene, grid, store, loader);
        st.SetExtraSources({Src(8, 8, 1000, 1000)});
        st.Tick();
        CHECK(st.LoadedCount() == 25);
        CHECK(Snapshot(scene) == original);                                  // splitter then load all cells = the original
        CHECK(scene.Objects().size() == world.Objects().size());
        // Every guid the original had is here, with the same parent.
        for (const auto& o : world.Objects()) {
            const GameObject* twin = scene.FindByGuid(o->Guid());
            CHECK(twin && twin->name == o->name);
        }
        // Unload everything and bring it back: the same objects (same guids), nothing else changed.
        CHECK(st.UnloadAll() == 25);
        CHECK(scene.Objects().empty() && st.LoadedCount() == 0);
        CHECK(st.ModifiedCount() == 0);                                      // nothing changed, so no overlay
        st.Tick();
        CHECK(Snapshot(scene) == original);

        // Change objects in two cells, destroy one, then stream them out and in.
        GameObject* crate = scene.Find("Crate1_1");
        const std::string crateGuid = crate->Guid();
        crate->transform->localPosition = {kCell + 5.5f, 2.0f, kCell + 6.5f};
        crate->name = "Moved";
        crate->tag = "hot";
        crate->GetComponent<SpriteRenderer>()->glyph = 'Z';
        GameObject* door = scene.Find("Door-1_-1");
        const std::string doorGuid = door->Guid();
        door->active = false;
        GameObject* gone = scene.Find("Crate2_2");
        scene.Destroy(gone);                                                  // queued, not flushed yet
        auto expect = Snapshot(scene);
        expect.erase(gone->Guid());
        CHECK(st.UnloadNow({1, 0, 1}) && st.UnloadNow({-1, 0, -1}) && st.UnloadNow({2, 0, 2}));
        CHECK(!scene.FindByGuid(crateGuid) && !scene.FindByGuid(doorGuid));
        CHECK(st.ModifiedCount() == 3);
        scene.FlushDestroyed();
        CHECK(st.LoadNow({1, 0, 1}) && st.LoadNow({-1, 0, -1}) && st.LoadNow({2, 0, 2}));
        // Same ids, same state.
        const GameObject* c2 = scene.FindByGuid(crateGuid);
        CHECK(c2 && c2->name == "Moved" && c2->tag == "hot" && c2->GetComponent<SpriteRenderer>()->glyph == 'Z');
        CHECK(c2 && std::fabs(c2->transform->localPosition.x - (kCell + 5.5f)) < 1e-4f);
        CHECK(scene.FindByGuid(doorGuid) && !scene.FindByGuid(doorGuid)->active);
        CHECK(scene.Find("Crate2_2") == nullptr);                             // the destroyed one stays gone
        CHECK(Snapshot(scene) == expect);
        CHECK(storeUntouched(store));                                         // authored chunks never written
        // Unloading the same cell again with no change adds nothing new.
        const std::size_t mod = st.ModifiedCount();
        CHECK(st.UnloadNow({3, 0, 3}) && st.UnloadNow({0, 0, 0}) && st.LoadNow({0, 0, 0}));
        CHECK(st.ModifiedCount() == mod);
        // LoadNow of a loaded cell and UnloadNow of an unloaded one are harmless.
        CHECK(st.LoadNow({0, 0, 0}) && st.UnloadNow({0, 0, 0}) && st.UnloadNow({0, 0, 0}));
        // A cell nobody made a chunk for cannot be loaded.
        CHECK(!st.LoadNow({40, 0, 40}, &err) && !err.empty());
        CHECK(!st.LoadNow({kMaxCellIndex + 5, 0, 0}, &err));
        // Reparented root (an object a game moved under another cell's object) does not corrupt anything.
        CHECK(st.LoadNow({0, 0, 0}));
    }

    // ---- saves keep streamed-out state ----
    {
        MemoryChunkStore store = baseline;
        SyncCellLoader loader;
        std::string savedText;
        std::string crateGuid;
        {
            Scene scene("game1");
            PartitionStreamer st(scene, grid, store, loader);
            st.SetExtraSources({Src(8, 8, 1000, 1000)});
            st.Tick();
            GameObject* crate = scene.Find("Crate-2_1");
            crateGuid = crate->Guid();
            crate->transform->localPosition = {-2 * kCell + 1.25f, 7.0f, kCell + 2.5f};
            crate->GetComponent<SpriteRenderer>()->glyph = 'S';
            scene.Find("Door0_-2")->tag = "opened";
            GameObject* shot = scene.Find("Hut2_-1");
            scene.DestroyImmediate(shot);
            // The player walks far away: everything streams out (changes are written back).
            st.SetExtraSources({Src(5000, 5000, 10, 20)});
            st.Tick();
            CHECK(st.LoadedCount() == 0 && scene.Objects().empty());
            SaveDocument doc;
            SaveWriter w = doc.Writer("streaming");
            st.WriteState(w);
            doc.Writer("player").Int("hp", 7);
            savedText = doc.Serialize();
            CHECK(st.ModifiedCount() == 3);
        }
        CHECK(storeUntouched(store));

        // A new run: parse the save text, restore, walk back.
        SaveDocument doc;
        CHECK(SaveDocument::Parse(savedText, doc, &err));
        CHECK(doc.RoundTrips());
        Scene scene("game2");
        SyncCellLoader loader2;
        PartitionStreamer st(scene, grid, store, loader2);
        CHECK(st.ReadState(doc.Reader("streaming"), &err));
        CHECK(st.ModifiedCount() == 3);
        st.SetExtraSources({Src(8, 8, 1000, 1000)});
        st.Tick();
        CHECK(st.LoadedCount() == 25);
        const GameObject* crate = scene.FindByGuid(crateGuid);
        CHECK(crate && crate->name == "Crate-2_1" && crate->GetComponent<SpriteRenderer>()->glyph == 'S');
        CHECK(crate && std::fabs(crate->transform->localPosition.y - 7.0f) < 1e-5f);
        CHECK(scene.Find("Door0_-2") && scene.Find("Door0_-2")->tag == "opened");
        CHECK(scene.Find("Hut2_-1") == nullptr && scene.Find("Door2_-1") == nullptr);   // the destroyed hut and its child stay gone
        CHECK(scene.Find("Crate2_-1") != nullptr);
        CHECK(scene.Objects().size() == world.Objects().size() - 2);

        // CaptureState while cells are LOADED also keeps their changes (a save made mid-game).
        scene.Find("Crate0_0")->name = "Renamed";
        const std::vector<std::string> lines = st.CaptureState();
        CHECK(lines.size() == 4);
        for (const std::string& l : lines) CHECK(l.find('\n') == std::string::npos && l.find('\r') == std::string::npos);
        Scene scene3("game3");
        SyncCellLoader loader3;
        PartitionStreamer st3(scene3, grid, store, loader3);
        CHECK(st3.RestoreState(lines, &err));
        st3.SetExtraSources({Src(8, 8, 1000, 1000)});
        st3.Tick();
        CHECK(scene3.Find("Renamed") != nullptr && scene3.Find("Crate0_0") == nullptr);
        // Restoring over a running world drops what was streamed in and loads from the new overlay.
        CHECK(st.RestoreState({}, &err));
        CHECK(scene.Objects().empty() && st.ModifiedCount() == 0);
        st.Tick();
        CHECK(Snapshot(scene) == original);
        CHECK(storeUntouched(store));
    }

    // ---- hostile state lines and damaged chunks ----
    {
        Scene scene("h");
        MemoryChunkStore store = baseline;
        SyncCellLoader loader;
        PartitionStreamer st(scene, grid, store, loader);
        st.SetExtraSources({Src(8, 8, 1000, 1000)});
        st.Tick();
        scene.Find("Crate0_0")->name = "Keep";
        const auto good = st.CaptureState();
        CHECK(good.size() == 1);
        const auto snap = Snapshot(scene);
        // Every bad line changes nothing.
        std::string t0; baseline.Read("cell_0_0_0", t0);
        const std::string esc = PartitionStreamer::EscapeLine(t0);
        const std::vector<std::vector<std::string>> bad = {
            {"nonsense"}, {"cell_0_0_0"}, {"cell_0_0_0\t"}, {"cell_0_0_0\tnot a chunk"}, {"cell_0_0_1\t" + esc},   // wrong cell for the text
            {"cell_0_0_0\t" + esc + "\\q"}, {"cell_0_0_0\t" + esc.substr(0, esc.size() / 2)},
            {"../evil\t" + esc}, {"cell_1048576_0_0\t" + esc}, {"persistent\t" + esc}, {"cell_0_0_0\t" + esc, "cell_0_0_0\t" + esc},
            {good[0], "cell_0_0_1\tx"}, {"cell_0_1_0\t" + esc},    // y must be 0 on an XZ grid
        };
        for (const auto& lines : bad) {
            CHECK(!st.RestoreState(lines, &err));
            CHECK(!err.empty());
            CHECK(Snapshot(scene) == snap && st.LoadedCount() == 25);       // the running world is untouched
        }
        // A save without streaming state, or from the future.
        SaveDocument d;
        CHECK(!st.ReadState(d.Reader("streaming"), &err));
        d.Writer("streaming").Int("version", 99);
        CHECK(!st.ReadState(d.Reader("streaming"), &err));
        CHECK(Snapshot(scene) == snap);
        // Escape / unescape round trip, including NUL and CR.
        std::string tricky = std::string("a\\b\nc\r\td") + std::string(1, '\0') + "e\\n\\\\";
        std::string back;
        CHECK(PartitionStreamer::UnescapeLine(PartitionStreamer::EscapeLine(tricky), back) && back == tricky);
        CHECK(PartitionStreamer::EscapeLine(tricky).find('\n') == std::string::npos);
        CHECK(!PartitionStreamer::UnescapeLine("abc\\", back));
        // Fuzz the state lines.
        tk::TkRng rng(7);
        int accepted = 0;
        for (int i = 0; i < tk::Seeds(400); ++i) {
            std::string l = good[0];
            for (int e = 0, n = 1 + rng.range(3); e < n; ++e) {
                const std::size_t at = static_cast<std::size_t>(rng.range(static_cast<int>(l.size())));
                if (rng.chance(0.5)) l[at] = static_cast<char>(rng.range(256)); else l.erase(at, 1 + rng.range(5));
            }
            if (st.RestoreState({l}, &err)) {
                ++accepted;
                st.RestoreState(good, &err);                                  // put the good state back
            } else {
                CHECK(Snapshot(scene) == snap);
            }
            st.Tick();
        }
        CHECK(accepted == 0 || accepted < 400);

        // A damaged chunk in the store: that cell fails and is retried later, the others load.
        Scene s2("d");
        MemoryChunkStore dmg = baseline;
        std::string text; dmg.Read("cell_1_0_1", text);
        text[text.size() / 2] ^= 0x10;
        dmg.Write("cell_1_0_1", text);
        std::string wrongCell; baseline.Read("cell_1_0_2", wrongCell);
        dmg.Write("cell_0_0_0", wrongCell);                                    // a chunk filed under another cell's name
        SyncCellLoader l2;
        PartitionStreamer st2(s2, grid, dmg, l2);
        st2.SetRetryTicks(5);
        st2.SetExtraSources({Src(8, 8, 1000, 1000)});
        TickReport r = st2.Tick();
        CHECK(r.failures == 2 && r.loaded == 23 && r.errors.size() == 2);
        CHECK(st2.State({1, 0, 1}) == CellState::Failed && st2.State({0, 0, 0}) == CellState::Failed);
        int failuresSeen = 0;
        for (int i = 0; i < 4; ++i) failuresSeen += st2.Tick().failures;
        CHECK(failuresSeen == 0);                                              // waits out the retry delay
        CHECK(st2.Tick().failures == 2);                                       // then tries again
        // Fix the store: the next try works.
        dmg = baseline;
        for (int i = 0; i < 6; ++i) st2.Tick();
        CHECK(st2.State({1, 0, 1}) == CellState::Loaded && st2.State({0, 0, 0}) == CellState::Loaded);
    }

    // ---- degenerate streamers ----
    {
        Scene scene("deg");
        MemoryChunkStore store = baseline;
        SyncCellLoader loader;
        PartitionStreamer bad(scene, WorldGrid(0.0f), store, loader);
        bad.SetExtraSources({Src(0, 0, 100, 100)});
        TickReport r = bad.Tick();
        CHECK(bad.KnownCells().empty() && r.loaded == 0 && scene.Objects().empty());
        CHECK(!bad.LoadNow({0, 0, 0}, &err));
        MemoryChunkStore empty;
        PartitionStreamer none(scene, grid, empty, loader);
        none.SetExtraSources({Src(0, 0, 100, 100)});
        CHECK(none.Tick().wanted == 0);
        // A source that wants a very large area on a huge catalogue is bounded by the catalogue.
        MemoryChunkStore store2 = baseline;
        PartitionStreamer st(scene, grid, store2, loader);
        st.SetExtraSources({Src(0, 0, 1e30f, 1e30f)});
        CHECK(st.Tick().loaded == 25);
    }

    TEST_MAIN_RESULT();
}
