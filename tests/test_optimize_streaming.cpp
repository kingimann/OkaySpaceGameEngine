// Optimization tools 27.4: the streaming cell grid and the texture mip budget.
#include "test_framework.hpp"
#include "okay/Optimize/MipBudget.hpp"
#include "okay/Optimize/StreamingGrid.hpp"
#include <algorithm>
#include <cmath>
#include <set>
#include <string>

using namespace okay;
using namespace okay::optimize;

namespace {

/// Records what the grid asks the world to do.
struct Log {
    std::vector<CellCoord> loads, unloads, cancels;
    std::vector<std::string> assets;
    bool accept = true;
};

StreamingHooks Hooks(Log& log) {
    StreamingHooks h;
    h.requestLoad = [&log](const CellCoord& c, const std::string& asset) {
        if (!log.accept) return false;
        log.loads.push_back(c);
        log.assets.push_back(asset);
        return true;
    };
    h.requestUnload = [&log](const CellCoord& c, const std::string&) { log.unloads.push_back(c); };
    h.cancelLoad = [&log](const CellCoord& c, const std::string&) { log.cancels.push_back(c); };
    return h;
}

/// Finish every load that is in flight.
void FinishLoads(StreamingGrid& g) {
    for (const CellCoord& c : g.LoadingCells()) g.NotifyLoaded(c);
}

}  // namespace

int main() {
    RUN_SUITE("optimize_streaming");

    // ---- coordinates and distances ----------------------------------------------------------------------
    {
        StreamingConfig cfg;
        cfg.cellSize = 10.0f;
        StreamingGrid g(cfg);
        CHECK(g.CellOf(Vec3{5, 99, 5}) == (CellCoord{0, 0, 0}));                       // XZ ignores height
        CHECK(g.CellOf(Vec3{-0.1f, 0, 25}) == (CellCoord{-1, 0, 2}));                  // floor, not truncation
        CHECK_NEAR(g.DistanceToCell(Vec3{5, 0, 5}, CellCoord{0, 0, 0}), 0.0, 1e-6);    // inside
        CHECK_NEAR(g.DistanceToCell(Vec3{15, 50, 5}, CellCoord{0, 0, 0}), 5.0, 1e-5);  // to the box, height ignored
        CHECK_NEAR(g.DistanceToCell(Vec3{13, 0, 14}, CellCoord{0, 0, 0}), 5.0, 1e-5);  // 3-4-5 from the corner
        cfg.plane = GridPlane::XYZ;
        StreamingGrid g3(cfg);
        CHECK(g3.CellOf(Vec3{5, 15, -5}) == (CellCoord{0, 1, -1}));
        CHECK_NEAR(g3.DistanceToCell(Vec3{5, 30, 5}, CellCoord{0, 0, 0}), 20.0, 1e-5);
        cfg.plane = GridPlane::XY;
        StreamingGrid g2(cfg);
        CHECK(g2.CellOf(Vec3{5, 15, 80}) == (CellCoord{0, 1, 0}));                     // XY ignores depth
        // Cells outside the active axes fold onto 0.
        StreamingGrid flat;
        CHECK(flat.RegisterCell(CellCoord{3, 7, 4}));
        CHECK(!flat.RegisterCell(CellCoord{3, 0, 4}));                                 // same cell
        CHECK(flat.IsRegistered(CellCoord{3, 99, 4}));
        // Range registration names the cells and counts the new ones.
        StreamingGrid r;
        CHECK(r.RegisterRange(CellCoord{0, 0, 0}, CellCoord{3, 0, 2}) == 12);
        CHECK(r.RegisterRange(CellCoord{2, 0, 1}, CellCoord{4, 0, 3}) == 5);             // 9 cells, 4 of them already there
        CHECK(r.RegisteredCount() == 17);
        CHECK(r.UnregisterCell(CellCoord{0, 0, 0}) && !r.UnregisterCell(CellCoord{0, 0, 0}));
        // The unload radius is never below the load radius.
        StreamingConfig bad;
        bad.loadRadius = 50;
        bad.unloadRadius = 10;
        CHECK(StreamingGrid(bad).Config().unloadRadius == 50.0f);
    }

    // ---- loading order: nearest first, cells ahead before cells behind ------------------------------------------
    {
        StreamingConfig cfg;
        cfg.cellSize = 10.0f;
        cfg.loadRadius = 25.0f;
        cfg.unloadRadius = 35.0f;
        cfg.maxLoadsPerFrame = 2;
        cfg.maxInFlight = 100;
        StreamingGrid g(cfg);
        Log log;
        g.SetHooks(Hooks(log));
        g.RegisterRange(CellCoord{-10, 0, -10}, CellCoord{10, 0, 10});
        const Vec3 viewer{5, 0, 5};                                                   // inside cell (0, 0)
        StreamingFrame f = g.Update(viewer, Vec3{1, 0, 0});
        CHECK(f.loadRequested.size() == 2);                                           // the per-frame budget
        CHECK(f.loadRequested[0] == (CellCoord{0, 0, 0}));                             // the cell the viewer stands in
        CHECK(f.loadRequested[1] == (CellCoord{1, 0, 0}));                             // then the one straight ahead, not the one behind
        CHECK(log.loads == f.loadRequested);
        CHECK(log.assets[0] == "cell_0_0_0");
        CHECK(f.deferredByLimit > 0);
        CHECK(g.State(CellCoord{0, 0, 0}) == CellState::Loading);
        CHECK(g.State(CellCoord{-1, 0, 0}) == CellState::Unloaded);
        // Looking the other way flips the order of equally distant cells.
        StreamingGrid h(cfg);
        Log log2;
        h.SetHooks(Hooks(log2));
        h.RegisterRange(CellCoord{-10, 0, -10}, CellCoord{10, 0, 10});
        StreamingFrame f2 = h.Update(viewer, Vec3{-1, 0, 0});
        CHECK(f2.loadRequested[1] == (CellCoord{-1, 0, 0}));
        // Without a direction (or with weight 0) distance alone decides and ties go by coordinate.
        StreamingConfig plain = cfg;
        plain.viewDirectionWeight = 0.0f;
        StreamingGrid p(plain);
        p.RegisterRange(CellCoord{-10, 0, -10}, CellCoord{10, 0, 10});
        StreamingFrame f3 = p.Update(viewer, Vec3{1, 0, 0});
        CHECK(f3.loadRequested[1] == (CellCoord{-1, 0, 0}));                           // the lowest coordinate among the ties
        // Over several frames exactly the cells inside the radius are requested, never more than the budget per frame.
        std::set<CellCoord> asked;
        for (int frame = 0; frame < 40; ++frame) {
            StreamingFrame fr = g.Update(viewer, Vec3{1, 0, 0});
            CHECK(fr.loadRequested.size() <= 2);
            for (const CellCoord& c : fr.loadRequested) asked.insert(c);
            FinishLoads(g);
        }
        for (const CellCoord& c : log.loads) asked.insert(c);
        std::set<CellCoord> expected;
        for (int x = -10; x <= 10; ++x) for (int z = -10; z <= 10; ++z)
            if (g.DistanceToCell(viewer, CellCoord{x, 0, z}) <= 25.0f) expected.insert(CellCoord{x, 0, z});
        CHECK(asked == expected);
        CHECK(g.LoadedCells().size() == expected.size());
        CHECK(g.LoadingCells().empty());
        // The cell the viewer is in was first, and nothing is requested twice.
        CHECK(log.loads.size() == expected.size());
        CHECK(std::set<CellCoord>(log.loads.begin(), log.loads.end()).size() == log.loads.size());
        // Settled: a further update does nothing.
        StreamingFrame idle = g.Update(viewer, Vec3{1, 0, 0});
        CHECK(idle.loadRequested.empty() && idle.unloadRequested.empty() && idle.loadCancelled.empty());
        CHECK(idle.loaded == static_cast<int>(expected.size()) && idle.wanted == static_cast<int>(expected.size()));
    }

    // ---- hysteresis: a viewer on the boundary does not thrash ---------------------------------------------------------
    {
        StreamingConfig cfg;
        cfg.cellSize = 10.0f;
        cfg.loadRadius = 25.0f;
        cfg.unloadRadius = 35.0f;
        cfg.maxLoadsPerFrame = 0;                                                       // unlimited
        cfg.maxInFlight = 0;
        StreamingGrid g(cfg);
        Log log;
        g.SetHooks(Hooks(log));
        g.RegisterRange(CellCoord{0, 0, 0}, CellCoord{30, 0, 0});                        // a corridor along x
        const CellCoord target{10, 0, 0};                                                // spans x 100..110
        // Distance to the target's box is 100 - x. At x = 76 that is 24 (inside the load radius): it loads.
        g.Update(Vec3{76, 0, 5}, Vec3{1, 0, 0});
        CHECK(g.State(target) == CellState::Loading);
        FinishLoads(g);
        CHECK(g.State(target) == CellState::Loaded);
        // Walk away to distance 30 (between the radii): stays loaded, nothing asked.
        const std::size_t unloadsBefore = log.unloads.size();
        g.Update(Vec3{70, 0, 5}, Vec3{-1, 0, 0});
        CHECK(g.State(target) == CellState::Loaded);
        CHECK(log.unloads.size() == unloadsBefore || std::find(log.unloads.begin(), log.unloads.end(), target) == log.unloads.end());
        // Jitter across the load radius while loaded: still nothing.
        for (int i = 0; i < 20; ++i) {
            g.Update(Vec3{i % 2 ? 74.0f : 78.0f, 0, 5}, Vec3{1, 0, 0});
            CHECK(g.State(target) == CellState::Loaded);
        }
        // Past the unload radius (distance 36): unloaded exactly once.
        StreamingFrame gone = g.Update(Vec3{64, 0, 5}, Vec3{-1, 0, 0});
        CHECK(g.State(target) == CellState::Unloaded);
        CHECK(std::count(gone.unloadRequested.begin(), gone.unloadRequested.end(), target) == 1);
        CHECK(std::count(log.unloads.begin(), log.unloads.end(), target) == 1);
        // Coming back to distance 30 does NOT reload it (it is outside the load radius) ...
        const std::size_t loadsBefore = log.loads.size();
        StreamingFrame back = g.Update(Vec3{70, 0, 5}, Vec3{1, 0, 0});
        CHECK(g.State(target) == CellState::Unloaded);
        CHECK(std::find(back.loadRequested.begin(), back.loadRequested.end(), target) == back.loadRequested.end());
        (void)loadsBefore;
        // ... only getting inside the load radius does.
        g.Update(Vec3{76, 0, 5}, Vec3{1, 0, 0});
        CHECK(g.State(target) == CellState::Loading);
        // Counting loads while jittering across the load radius edge: a viewer that wobbles around the load
        // radius requests the cell once and unloads it never.
        StreamingGrid j(cfg);
        Log jl;
        j.SetHooks(Hooks(jl));
        j.RegisterRange(CellCoord{0, 0, 0}, CellCoord{30, 0, 0});
        for (int i = 0; i < 100; ++i) {
            j.Update(Vec3{75.0f + (i % 2 ? 2.0f : -2.0f), 0, 5}, Vec3{1, 0, 0});     // distance 23 .. 27 from cell 10
            FinishLoads(j);
        }
        CHECK(std::count(jl.loads.begin(), jl.loads.end(), target) == 1);
        CHECK(jl.unloads.empty());
    }

    // ---- budgets: in flight, per frame cost, unloads per frame, refusals --------------------------------------------------------
    {
        StreamingConfig cfg;
        cfg.cellSize = 10.0f;
        cfg.loadRadius = 55.0f;
        cfg.unloadRadius = 60.0f;
        cfg.maxLoadsPerFrame = 100;
        cfg.maxInFlight = 3;
        StreamingGrid g(cfg);
        Log log;
        g.SetHooks(Hooks(log));
        g.RegisterRange(CellCoord{-8, 0, -8}, CellCoord{8, 0, 8});
        StreamingFrame f = g.Update(Vec3{0, 0, 0}, Vec3{0, 0, 1});
        CHECK(f.loadRequested.size() == 3 && f.loading == 3);                           // capped by concurrent loads
        CHECK(f.deferredByLimit > 0);
        StreamingFrame same = g.Update(Vec3{0, 0, 0}, Vec3{0, 0, 1});
        CHECK(same.loadRequested.empty());                                               // all three are still in flight
        CHECK(g.NotifyLoaded(log.loads[0]));
        StreamingFrame next = g.Update(Vec3{0, 0, 0}, Vec3{0, 0, 1});
        CHECK(next.loadRequested.size() == 1);                                           // one finished, one more starts
        CHECK(g.LoadingCells().size() == 3);
        CHECK(!g.NotifyLoaded(CellCoord{5, 0, 5}));                                      // never loading: told to drop it
        CHECK(!g.NotifyLoaded(log.loads[0]));                                            // already loaded

        // A per-frame cost cap (one request is always allowed).
        StreamingConfig cost = cfg;
        cost.maxInFlight = 0;
        cost.maxLoadCostPerFrame = 25.0f;
        StreamingGrid c(cost);
        c.RegisterRange(CellCoord{-3, 0, -3}, CellCoord{3, 0, 3}, 10.0f);
        CHECK(c.Update(Vec3{0, 0, 0}, Vec3{0, 0, 1}).loadRequested.size() == 2);          // 10 + 10 fits, a third would be 30
        StreamingGrid huge(cost);
        huge.RegisterRange(CellCoord{0, 0, 0}, CellCoord{1, 0, 1}, 100.0f);
        CHECK(huge.Update(Vec3{5, 0, 5}, Vec3{0, 0, 1}).loadRequested.size() == 1);        // an oversized cell still goes, alone

        // Unloads are spread over frames, farthest first.
        StreamingConfig un = cfg;
        un.maxInFlight = 0;
        un.maxUnloadsPerFrame = 2;
        un.loadRadius = 25.0f;
        un.unloadRadius = 30.0f;
        StreamingGrid u(un);
        Log ul;
        u.SetHooks(Hooks(ul));
        u.RegisterRange(CellCoord{-30, 0, -30}, CellCoord{30, 0, 30});
        u.Update(Vec3{5, 0, 5}, Vec3{1, 0, 0});
        FinishLoads(u);
        const std::size_t resident = u.LoadedCells().size();
        CHECK(resident > 10);
        StreamingFrame jump = u.Update(Vec3{500, 0, 500}, Vec3{1, 0, 0});                  // teleport far away
        CHECK(jump.unloadRequested.size() == 2);
        CHECK(u.LoadedCells().size() == resident - 2);
        for (int i = 0; i < 100 && !u.LoadedCells().empty(); ++i) {
            StreamingFrame s = u.Update(Vec3{500, 0, 500}, Vec3{1, 0, 0});
            CHECK(s.unloadRequested.size() <= 2);
        }
        CHECK(u.LoadedCells().empty());
        CHECK(ul.unloads.size() == resident);
        CHECK(std::set<CellCoord>(ul.unloads.begin(), ul.unloads.end()).size() == resident);

        // The loader can refuse (its queue is full): the cell stays unloaded and is asked again.
        StreamingConfig rc = cfg;
        rc.maxInFlight = 0;
        rc.loadRadius = 5.0f;
        rc.unloadRadius = 10.0f;
        StreamingGrid r(rc);
        Log rl;
        r.SetHooks(Hooks(rl));
        r.RegisterCell(CellCoord{0, 0, 0});
        rl.accept = false;
        StreamingFrame refused = r.Update(Vec3{5, 0, 5}, Vec3{1, 0, 0});
        CHECK(refused.loadRequested.empty() && r.State(CellCoord{0, 0, 0}) == CellState::Unloaded);
        CHECK(refused.deferredByLimit == 1);
        rl.accept = true;
        CHECK(r.Update(Vec3{5, 0, 5}, Vec3{1, 0, 0}).loadRequested.size() == 1);
        // With no hooks at all the grid still tracks state.
        StreamingGrid bare(rc);
        bare.RegisterCell(CellCoord{0, 0, 0});
        CHECK(bare.Update(Vec3{5, 0, 5}, Vec3{1, 0, 0}).loadRequested.size() == 1);
        CHECK(bare.NotifyLoaded(CellCoord{0, 0, 0}) && bare.State(CellCoord{0, 0, 0}) == CellState::Loaded);
    }

    // ---- cancel, failure and retry -----------------------------------------------------------------------------------------------
    {
        StreamingConfig cfg;
        cfg.cellSize = 10.0f;
        cfg.loadRadius = 15.0f;
        cfg.unloadRadius = 20.0f;
        cfg.maxLoadsPerFrame = 0;
        cfg.maxInFlight = 0;
        cfg.retryDelayFrames = 5;
        StreamingGrid g(cfg);
        Log log;
        g.SetHooks(Hooks(log));
        g.RegisterRange(CellCoord{0, 0, 0}, CellCoord{10, 0, 0});
        g.Update(Vec3{5, 0, 5}, Vec3{1, 0, 0});
        CHECK(g.State(CellCoord{0, 0, 0}) == CellState::Loading && g.State(CellCoord{2, 0, 0}) == CellState::Loading);
        // Walk off before the loads finish: the ones now beyond the unload radius are cancelled.
        StreamingFrame moved = g.Update(Vec3{105, 0, 5}, Vec3{1, 0, 0});
        CHECK(moved.loadCancelled.size() >= 2 && log.cancels.size() == moved.loadCancelled.size());
        CHECK(g.State(CellCoord{0, 0, 0}) == CellState::Unloaded);
        CHECK(log.unloads.empty());                                                       // nothing had loaded yet
        CHECK(!g.NotifyLoaded(CellCoord{0, 0, 0}));                                       // the late result is refused
        // Failure: not retried for retryDelayFrames updates, then asked again.
        StreamingGrid f(cfg);
        Log fl;
        f.SetHooks(Hooks(fl));
        f.RegisterCell(CellCoord{0, 0, 0});
        f.Update(Vec3{5, 0, 5}, Vec3{1, 0, 0});
        CHECK(fl.loads.size() == 1);
        f.NotifyLoadFailed(CellCoord{0, 0, 0});
        CHECK(f.State(CellCoord{0, 0, 0}) == CellState::Unloaded && f.FailureCount(CellCoord{0, 0, 0}) == 1);
        for (int i = 0; i < 4; ++i) f.Update(Vec3{5, 0, 5}, Vec3{1, 0, 0});
        CHECK(fl.loads.size() == 1);                                                       // still backing off
        f.Update(Vec3{5, 0, 5}, Vec3{1, 0, 0});
        CHECK(fl.loads.size() == 2);                                                       // retried
        f.NotifyLoaded(CellCoord{0, 0, 0});
        CHECK(f.FailureCount(CellCoord{0, 0, 0}) == 0);                                    // success clears the count
        // Unregistering a loaded or loading cell tells the world.
        f.UnregisterCell(CellCoord{0, 0, 0});
        CHECK(fl.unloads.size() == 1);
        f.RegisterCell(CellCoord{1, 0, 0});
        f.Update(Vec3{15, 0, 5}, Vec3{1, 0, 0});
        f.UnregisterCell(CellCoord{1, 0, 0});
        CHECK(fl.cancels.size() == 1);
    }

    // ---- memory budget -------------------------------------------------------------------------------------------------------------------
    {
        StreamingConfig cfg;
        cfg.cellSize = 10.0f;
        cfg.loadRadius = 22.0f;
        cfg.unloadRadius = 40.0f;
        cfg.maxLoadsPerFrame = 0;
        cfg.maxInFlight = 0;
        cfg.memoryBudget = 35.0f;                                                           // three cells of cost 10
        StreamingGrid g(cfg);
        Log log;
        g.SetHooks(Hooks(log));
        g.RegisterRange(CellCoord{0, 0, 0}, CellCoord{40, 0, 0}, 10.0f);
        float peak = 0.0f;
        bool deferred = false, evicted = false;
        for (int step = 0; step < 300; ++step) {
            const Vec3 pos{static_cast<float>(step) * 1.2f, 0, 5};                           // walk along the corridor
            StreamingFrame f = g.Update(pos, Vec3{1, 0, 0});
            FinishLoads(g);
            peak = std::max(peak, g.ResidentCost());
            CHECK(g.ResidentCost() <= 35.0f + 1e-4f);                                          // never over budget
            deferred = deferred || f.deferredByBudget > 0;
            evicted = evicted || f.evictedForBudget > 0;
            // The cell the viewer stands in is always resident once things settle.
            if (step > 5) CHECK(g.State(g.CellOf(pos)) != CellState::Unloaded);
        }
        CHECK(peak > 20.0f);
        CHECK(deferred);                                                                   // the budget actually held cells back
        CHECK(evicted);                                                                    // and far ones were dropped to make room
        // A tight budget keeps the nearest cells: five are wanted, three fit, and they are the closest three.
        StreamingGrid h(cfg);
        h.RegisterRange(CellCoord{0, 0, 0}, CellCoord{5, 0, 0}, 10.0f);
        StreamingFrame hf = h.Update(Vec3{25, 0, 5}, Vec3{1, 0, 0});
        FinishLoads(h);
        CHECK(h.LoadedCells().size() == 3);                                                // 10 + 10 + 10 <= 35, the fourth does not fit
        CHECK(h.State(CellCoord{2, 0, 0}) == CellState::Loaded);                           // the viewer's own cell
        CHECK(h.State(CellCoord{1, 0, 0}) == CellState::Loaded && h.State(CellCoord{3, 0, 0}) == CellState::Loaded);
        CHECK(h.State(CellCoord{0, 0, 0}) == CellState::Unloaded && h.State(CellCoord{4, 0, 0}) == CellState::Unloaded);
        CHECK(hf.deferredByBudget == 2);
        // Walk on: nearer cells push farther loaded ones out, the budget still holds, nothing churns when standing still.
        StreamingFrame hm = h.Update(Vec3{45, 0, 5}, Vec3{1, 0, 0});
        FinishLoads(h);
        CHECK(hm.evictedForBudget > 0 && h.ResidentCost() <= 35.0f);
        CHECK(h.State(CellCoord{4, 0, 0}) == CellState::Loaded);
        h.Update(Vec3{45, 0, 5}, Vec3{1, 0, 0});
        FinishLoads(h);
        StreamingFrame still2 = h.Update(Vec3{45, 0, 5}, Vec3{1, 0, 0});
        CHECK(still2.loadRequested.empty() && still2.unloadRequested.empty());
        // Unlimited budget loads everything in range.
        StreamingConfig unlimited = cfg;
        unlimited.memoryBudget = 0.0f;
        StreamingGrid u(unlimited);
        u.RegisterRange(CellCoord{0, 0, 0}, CellCoord{5, 0, 0}, 10.0f);
        u.Update(Vec3{25, 0, 5}, Vec3{1, 0, 0});
        CHECK(u.LoadingCells().size() >= 4);
    }

    // ---- 3D grid, sparse worlds, determinism ---------------------------------------------------------------------------------------------------
    {
        StreamingConfig cfg;
        cfg.cellSize = 10.0f;
        cfg.plane = GridPlane::XYZ;
        cfg.loadRadius = 12.0f;
        cfg.unloadRadius = 15.0f;
        cfg.maxLoadsPerFrame = 0;
        cfg.maxInFlight = 0;
        StreamingGrid g(cfg);
        g.RegisterRange(CellCoord{-3, -3, -3}, CellCoord{3, 3, 3});
        g.Update(Vec3{5, 5, 5}, Vec3{0, 1, 0});
        std::set<CellCoord> expected;
        for (int x = -3; x <= 3; ++x) for (int y = -3; y <= 3; ++y) for (int z = -3; z <= 3; ++z)
            if (g.DistanceToCell(Vec3{5, 5, 5}, CellCoord{x, y, z}) <= 12.0f) expected.insert(CellCoord{x, y, z});
        CHECK(!expected.empty());
        std::set<CellCoord> got;
        for (const CellCoord& c : g.LoadingCells()) got.insert(c);
        CHECK(got == expected);
        CHECK(got.count(CellCoord{0, 1, 0}) == 1 && got.count(CellCoord{0, 2, 0}) == 0);   // height matters in 3D (5 vs 15 away)
        // XZ mode loads by ground distance only, however high the viewer is.
        StreamingConfig ground = cfg;
        ground.plane = GridPlane::XZ;
        StreamingGrid gz(ground);
        gz.RegisterRange(CellCoord{-3, 0, -3}, CellCoord{3, 0, 3});
        gz.Update(Vec3{5, 500, 5}, Vec3{0, 0, 1});
        CHECK(gz.State(CellCoord{0, 0, 0}) == CellState::Loading);
        // A big sparse registry: the box scan and the full scan agree.
        StreamingConfig sparse;
        sparse.cellSize = 10.0f;
        sparse.loadRadius = 30.0f;
        sparse.unloadRadius = 40.0f;
        sparse.maxLoadsPerFrame = 0;
        sparse.maxInFlight = 0;
        StreamingGrid big(sparse);
        big.RegisterRange(CellCoord{0, 0, 0}, CellCoord{99, 0, 99});                          // 10000 cells >> the 11 x 11 box
        big.Update(Vec3{500, 0, 500}, Vec3{1, 0, 0});
        std::set<CellCoord> want;
        for (int x = 0; x < 100; ++x) for (int z = 0; z < 100; ++z)
            if (big.DistanceToCell(Vec3{500, 0, 500}, CellCoord{x, 0, z}) <= 30.0f) want.insert(CellCoord{x, 0, z});
        std::set<CellCoord> have;
        for (const CellCoord& c : big.LoadingCells()) have.insert(c);
        CHECK(have == want && !want.empty());
        // Deterministic: the same walk gives the same sequence of requests.
        auto run = []() {
            StreamingConfig c;
            c.cellSize = 10.0f;
            c.loadRadius = 40.0f;
            c.unloadRadius = 55.0f;
            c.maxLoadsPerFrame = 3;
            c.maxInFlight = 5;
            c.memoryBudget = 200.0f;
            StreamingGrid s(c);
            Log l;
            s.SetHooks(Hooks(l));
            s.RegisterRange(CellCoord{-20, 0, -20}, CellCoord{20, 0, 20}, 7.0f);
            for (int i = 0; i < 120; ++i) {
                s.Update(Vec3{std::sin(i * 0.1f) * 90.0f, 0, std::cos(i * 0.07f) * 90.0f}, Vec3{std::cos(i * 0.1f), 0, -std::sin(i * 0.1f)});
                if (i % 3 == 0) FinishLoads(s);
            }
            return std::make_pair(l.loads, l.unloads);
        };
        CHECK(run() == run());
    }

    // ================= texture mip budget ===========================================================================================================
    {
        CHECK(FullMipCount(1, 1) == 1 && FullMipCount(1024, 512) == 11 && FullMipCount(5, 3) == 3);
        // 4x4 RGBA8: 64 + 16 + 4 bytes.
        CHECK_NEAR(MipChainBytes(4, 4, 4.0f, 0, 3), 84.0, 1e-3);
        CHECK_NEAR(MipChainBytes(4, 4, 4.0f, 1, 3), 20.0, 1e-3);
        CHECK_NEAR(MipChainBytes(4, 4, 4.0f, 2, 3), 4.0, 1e-3);
        CHECK_NEAR(MipChainBytes(4, 4, 4.0f, 3, 3), 0.0, 1e-3);
        // A full chain is about a third bigger than the base level.
        CHECK(MipChainBytes(1024, 1024, 4.0f, 0, 11) > 1024.0 * 1024 * 4 * 1.33 && MipChainBytes(1024, 1024, 4.0f, 0, 11) < 1024.0 * 1024 * 4 * 1.34);

        MipTexture t;
        t.width = t.height = 2048;
        t.screenPixels = 512.0f;
        CHECK(DesiredMip(t) == 2);                                                           // 2048 texels over 512 pixels = mip 2
        t.screenPixels = 4096.0f;
        CHECK(DesiredMip(t) == 0);                                                           // never above the real texture
        t.screenPixels = 512.0f;
        t.uvScale = 4.0f;                                                                    // tiled 4 times: 8192 texels over 512 pixels
        CHECK(DesiredMip(t) == 4);
        t.uvScale = 1.0f;
        MipOptions bias;
        bias.mipBias = 1.0f;
        CHECK(DesiredMip(t, bias) == 3);
        bias.mipBias = -1.0f;
        CHECK(DesiredMip(t, bias) == 1);
        t.screenPixels = 0.0f;                                                               // not visible: the smallest resident mip
        CHECK(DesiredMip(t) == 9);                                                           // 2048 >> 9 = 4
        MipOptions floor8;
        floor8.minDimension = 16;
        CHECK(DesiredMip(t, floor8) == 7);
        t.pinned = true;
        CHECK(DesiredMip(t) == 0);
        t.pinned = false;
        t.mipCount = 3;                                                                      // only three mips exist
        t.screenPixels = 10.0f;
        CHECK(DesiredMip(t) == 2);
        t.mipCount = 1;
        CHECK(DesiredMip(t) == 0);

        // Hysteresis around a mip boundary.
        MipTexture h;
        h.width = h.height = 1024;
        h.previousMip = 2;
        auto desired = [&](float d) {
            h.screenPixels = 1024.0f / std::exp2(d);
            return DesiredMip(h);
        };
        CHECK(desired(2.5f) == 2);
        CHECK(desired(3.1f) == 2);                                                           // floor says 3, but within the margin of 2
        CHECK(desired(3.3f) == 3);
        CHECK(desired(1.9f) == 2);
        CHECK(desired(1.6f) == 1);
        h.previousMip = -1;
        CHECK(desired(3.1f) == 3 && desired(1.9f) == 1);                                      // no history: plain rounding down

        // ---- budget ----
        std::vector<MipTexture> tex;
        for (int i = 0; i < 8; ++i) {
            MipTexture m;
            m.id = 100 + i;
            m.width = m.height = 2048;
            m.screenPixels = i < 4 ? 2048.0f : 300.0f;                                         // 4 close up, 4 far away
            tex.push_back(m);
        }
        MipPlan open = PlanMips(tex);
        CHECK(open.choices.size() == 8);
        CHECK(open.demotions == 0 && !open.overBudget);
        CHECK(open.totalBytes == open.desiredBytes);
        for (int i = 0; i < 8; ++i) CHECK(open.choices[i].id == 100 + i && open.choices[i].mip == open.choices[i].desiredMip);
        CHECK(open.choices[0].mip == 0 && open.choices[7].mip == 2);
        CHECK(open.choices[0].width == 2048 && open.choices[7].width == 512);
        CHECK_NEAR(open.choices[0].bytes, MipChainBytes(2048, 2048, 4.0f, 0, 12), 1.0);
        CHECK(open.fullBytes > open.desiredBytes);
        // A budget that is exactly enough changes nothing.
        MipOptions exact;
        exact.budgetBytes = open.desiredBytes;
        CHECK(PlanMips(tex, exact).demotions == 0);
        // A tighter budget demotes until it fits.
        for (float fraction : {0.9f, 0.5f, 0.25f, 0.1f}) {
            MipOptions o;
            o.budgetBytes = open.desiredBytes * fraction;
            MipPlan p = PlanMips(tex, o);
            CHECK(p.totalBytes <= o.budgetBytes);
            CHECK(!p.overBudget);
            CHECK(p.demotions > 0);
            float sum = 0;
            for (const MipChoice& c : p.choices) {
                CHECK(c.mip >= c.desiredMip);                                                   // never sharper than asked
                CHECK(c.demotedForBudget == (c.mip > c.desiredMip));
                CHECK_NEAR(c.bytes, MipChainBytes(2048, 2048, 4.0f, c.mip, 12), 1.0);
                CHECK(c.width == std::max(1, 2048 >> c.mip));
                sum += c.bytes;
            }
            CHECK_NEAR(sum, p.totalBytes, 16.0);
        }
        // A smaller budget never gives any texture a sharper mip.
        MipOptions loose, tight;
        loose.budgetBytes = open.desiredBytes * 0.5f;
        tight.budgetBytes = open.desiredBytes * 0.25f;
        MipPlan pl = PlanMips(tex, loose), pt = PlanMips(tex, tight);
        for (int i = 0; i < 8; ++i) CHECK(pt.choices[i].mip >= pl.choices[i].mip);
        // Importance steers the demotions: the important texture keeps its mip, the others pay.
        std::vector<MipTexture> imp = tex;
        imp[0].importance = 1000.0f;
        MipOptions half;
        half.budgetBytes = open.desiredBytes * 0.5f;
        MipPlan pi = PlanMips(imp, half);
        CHECK(pi.choices[0].mip == 0);
        CHECK(pi.totalBytes <= half.budgetBytes);
        MipPlan pn = PlanMips(tex, half);
        int others = 0, othersPlain = 0;
        for (int i = 1; i < 8; ++i) { others += pi.choices[i].mip; othersPlain += pn.choices[i].mip; }
        CHECK(others >= othersPlain);
        // Pinned textures are never demoted, even when that leaves the plan over budget.
        std::vector<MipTexture> pin = tex;
        for (MipTexture& m : pin) m.pinned = true;
        MipOptions starved;
        starved.budgetBytes = 1000.0f;
        MipPlan pp = PlanMips(pin, starved);
        CHECK(pp.overBudget && pp.demotions == 0);
        for (const MipChoice& c : pp.choices) CHECK(c.mip == 0);
        // Not enough budget even at the floor: everything ends at the floor and it says so.
        MipOptions starving;
        starving.budgetBytes = 100.0f;                                                      // 8 textures need 8 x 84 bytes even at the floor
        MipPlan ps = PlanMips(tex, starving);
        CHECK(ps.overBudget);
        for (const MipChoice& c : ps.choices) CHECK(c.mip == 9 && c.width == 4);
        // Textures without a mip chain cannot be demoted.
        std::vector<MipTexture> flat(3);
        for (MipTexture& m : flat) { m.width = m.height = 512; m.mipCount = 1; m.screenPixels = 512.0f; }
        MipOptions fo;
        fo.budgetBytes = 1000.0f;
        MipPlan pf = PlanMips(flat, fo);
        CHECK(pf.overBudget && pf.demotions == 0);
        // Compressed formats count their real size.
        MipTexture bc;
        bc.width = bc.height = 1024;
        bc.bytesPerPixel = 0.5f;
        bc.screenPixels = 1024.0f;
        MipTexture rgba = bc;
        rgba.bytesPerPixel = 4.0f;
        CHECK_NEAR(PlanMips({rgba}).totalBytes / PlanMips({bc}).totalBytes, 8.0, 1e-3);
        // Deterministic, and empty input is fine.
        MipPlan a = PlanMips(tex, half), b = PlanMips(tex, half);
        for (int i = 0; i < 8; ++i) CHECK(a.choices[i].mip == b.choices[i].mip);
        CHECK(PlanMips({}).choices.empty());
        // The plan feeds back as hysteresis: re-planning with each texture's last mip changes nothing.
        std::vector<MipTexture> again = tex;
        for (int i = 0; i < 8; ++i) again[i].previousMip = open.choices[i].mip;
        MipPlan same = PlanMips(again);
        for (int i = 0; i < 8; ++i) CHECK(same.choices[i].mip == open.choices[i].mip);
    }

    TEST_MAIN_RESULT();
}
