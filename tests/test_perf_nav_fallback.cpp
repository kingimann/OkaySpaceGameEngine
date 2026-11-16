#include "test_framework.hpp"
#include "perf_nav_fallback_scenes.hpp"
#include "okay/AI/NavProbeIndex.hpp"
#include "okay/AI/Navigation.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// Finding 107: the no-NavMesh path search (NavGrid3D) and agent replans.
//  1. The paths are the same as before the probe index: golden values recorded on main before the change,
//     and the index on vs off on many more queries (also after a collider moves, and with the agent's own
//     collider ignored).
//  2. One search is much faster with the index (timings printed, before and after).
//  3. Timer replans are spread over frames: at most Navigation::ReplanBudget() per frame, where all agents
//     used to replan on the same frame.
// Set OKAY_NAV_PERF_FULL=1 to also time 20 agents with the index off and no budget (the old behaviour; slow).
using namespace okay;
using Clock = std::chrono::steady_clock;

namespace {

double Ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

// Recorded on main before the probe index (61af2f39), tests/test_perf_nav_fallback.cpp recorder run.
const char* const kGoldenPaths[] = {
    "6 0x0p+0,0x0p+0,0x0p+0 0x1.2p+2,0x0p+0,0x1.2p+1 0x1.bp+3,0x0p+0,0x1.2p+1 0x1.08p+4,0x0p+0,0x1.2p+2 0x1.2p+4,0x0p+0,0x1.2p+2 0x1.2p+4,0x0p+0,0x1.8p+1",
    "4 0x0p+0,0x0p+0,0x0p+0 -0x1.bp+3,0x0p+0,0x1.2p+3 -0x1.bp+3,0x0p+0,0x1.8p+3 -0x1.ep+3,0x0p+0,0x1.8p+3",
    "2 0x1p+0,0x0p+0,-0x1p+0 0x1.2p+3,0x0p+0,-0x1.2p+3",
    "4 0x0p+0,0x0p+0,0x0p+0 0x1.38p+3,0x0p+0,0x1.bp+2 0x1.5p+3,0x0p+0,0x1.bp+3 0x1.cp+3,0x1.333334p-2,0x1.cp+3",
    "0",
    "0",
    "5 0x0p+0,0x0p+0,0x0p+0 -0x1.2p+1,0x0p+0,0x1.8p+1 -0x1.2p+1,0x0p+0,0x1.ep+2 0x0p+0,0x0p+0,0x1.08p+3 0x0p+0,0x0p+0,0x1.2p+3",
    "4 -0x1p+1,0x0p+0,0x1p+1 -0x1.cp+1,0x0p+0,0x1.ap+2 -0x1.3p+3,0x0p+0,0x1.d8p+3 -0x1.6p+4,0x0p+0,0x1.6p+4",
    "3 0x1.333334p-2,0x0p+0,-0x1.99999ap-3 0x1.e9999ap+3,0x0p+0,0x1.49999ap+3 0x1.7p+4,0x0p+0,0x1.2p+4",
    "0",
};
const char* const kRockPaths[] = {   // open goal, walled-off goal
    "4 0x0p+0,0x0p+0,0x0p+0 0x1.98p+4,0x0p+0,0x1.8p+0 0x1.d4p+5,0x0p+0,0x1.8p-1 0x1.ep+5,0x0p+0,0x0p+0",
    "0",
};

std::vector<Vec3> Search(Scene& s, const Vec3& a, const Vec3& b, bool indexOn, GameObject* self = nullptr, int maxExpand = 4096) {
    NavProbeIndex::SetEnabled(indexOn);
    std::vector<Vec3> p = NavGrid3D::FindPath(s, a, b, self, 0.75f, 0.4f, maxExpand);
    NavProbeIndex::SetEnabled(true);
    return p;
}

struct FrameStats { double avg = 0, worst = 0, worstAfterFirst = 0; int maxSearchesAfterFirst = 0, firstFrameSearches = 0; std::uint64_t searches = 0; };

// `count` NavMeshAgents in a block near the origin, all sent to (60, 0, 0) on the same frame; `frames` frames of 1/60 s.
FrameStats RunAgents(int count, int rocks, int frames) {
    Scene s("agents");
    navperf::BuildRockField(s, rocks, false);
    std::vector<NavMeshAgent*> agents;
    for (int i = 0; i < count; ++i) {
        GameObject* g = s.CreateGameObject("Agent");
        g->transform->localPosition = {(float)(i % 10) * 1.5f - 7.0f, 0.0f, (float)(i / 10) * 1.5f - 4.0f};
        agents.push_back(g->AddComponent<NavMeshAgent>());
    }
    s.Update(1.0f / 60.0f);
    for (auto* a : agents) a->SetDestination({60, 0, 0});
    FrameStats st;
    double total = 0;
    for (int f = 0; f < frames; ++f) {
        const std::uint64_t before = Navigation::SearchCount();
        auto t0 = Clock::now(); s.Update(1.0f / 60.0f); auto t1 = Clock::now();
        const int searches = (int)(Navigation::SearchCount() - before);
        st.searches += (std::uint64_t)searches;
        const double ms = Ms(t0, t1);
        total += ms; st.worst = std::max(st.worst, ms);
        if (f == 0) st.firstFrameSearches = searches;
        else { st.worstAfterFirst = std::max(st.worstAfterFirst, ms); st.maxSearchesAfterFirst = std::max(st.maxSearchesAfterFirst, searches); }
    }
    st.avg = total / frames;
    return st;
}

} // namespace

int main() {
    RUN_SUITE("perf_nav_fallback");
    CHECK(NavProbeIndex::Enabled());   // on by default

    // ---- 1a. The golden scene: every recorded path, index on and off -----------------------------------
    {
        Scene s("golden"); navperf::BuildGoldenScene(s);
        const auto qs = navperf::GoldenQueries();
        CHECK(qs.size() == sizeof(kGoldenPaths) / sizeof(kGoldenPaths[0]));
        int same = 0;
        for (std::size_t i = 0; i < qs.size(); ++i) {
            const std::string on = navperf::PathText(Search(s, qs[i].from, qs[i].to, true, nullptr, qs[i].maxExpand));
            const std::string off = navperf::PathText(Search(s, qs[i].from, qs[i].to, false, nullptr, qs[i].maxExpand));
            CHECK(on == kGoldenPaths[i]);
            CHECK(off == kGoldenPaths[i]);
            if (on == kGoldenPaths[i]) ++same;
            else std::printf("  golden %zu differs:\n    got  %s\n    want %s\n", i, on.c_str(), kGoldenPaths[i]);
        }
        std::printf("  golden paths identical with the index: %d of %zu\n", same, qs.size());
        const NavProbeIndex& idx = NavProbeIndex::For(s);
        CHECK(idx.ColliderCount() > 150);
        CHECK(idx.AlwaysCount() >= 1);          // the floor is too big for the grid: every probe tests it
        CHECK(idx.CellsX() > 1 && idx.CellsZ() > 1);
    }

    // ---- 1b. Many more queries, index on vs off; the agent's own collider; a collider that moves --------
    {
        Scene s("golden"); navperf::BuildGoldenScene(s);
        GameObject* self = s.CreateGameObject("Walker");
        auto* cap = self->AddComponent<CapsuleCollider3D>(); cap->radius = 0.4f; cap->height = 1.8f;
        navperf::Lcg r(99u);
        int compared = 0, equal = 0, nonEmpty = 0;
        for (int i = 0; i < 40; ++i) {
            Vec3 a{r.Range(-24.0f, 24.0f), 0.0f, r.Range(-24.0f, 24.0f)};
            Vec3 b{r.Range(-24.0f, 24.0f), 0.0f, r.Range(-24.0f, 24.0f)};
            GameObject* me = (i % 2) ? self : nullptr;
            if (me) self->transform->localPosition = {a.x, 0.9f, a.z};
            const auto on = Search(s, a, b, true, me, 1500), off = Search(s, a, b, false, me, 1500);
            ++compared; if (navperf::PathText(on) == navperf::PathText(off)) ++equal;
            if (!on.empty()) ++nonEmpty;
        }
        CHECK(equal == compared);
        CHECK(nonEmpty >= 20);   // the sample covers real routes, not only failures
        std::printf("  random queries: %d of %d identical (index on vs off), %d found a route\n", equal, compared, nonEmpty);

        // A wall moved across a route between two searches: the index sees it (boxes are measured every search).
        const Vec3 a{-20, 0, -24}, b{-20, 0, -16};
        const std::string before = navperf::PathText(Search(s, a, b, true));
        GameObject* wall = navperf::Box(s, "Moving wall", {200, 1, 200}, {8, 2, 0.6f});
        (void)Search(s, a, b, true);   // index rebuilt with the wall far away
        const std::uint64_t builds = NavProbeIndex::GridBuilds();
        (void)Search(s, a, b, true);
        CHECK(NavProbeIndex::GridBuilds() == builds);   // nothing moved: the grid is reused
        wall->transform->localPosition = {-20, 1, -20};
        const std::string onMoved = navperf::PathText(Search(s, a, b, true));
        const std::string offMoved = navperf::PathText(Search(s, a, b, false));
        CHECK(onMoved == offMoved);
        CHECK(onMoved != before);                        // the route had to change
        CHECK(NavProbeIndex::GridBuilds() > builds);
    }

    // ---- 2. One search on a 400 m floor with 1,000 rocks: same path, much faster ----------------------
    {
        for (int pen = 0; pen < 2; ++pen) {
            Scene s("rocks"); navperf::BuildRockField(s, 1000, pen != 0);
            (void)Search(s, {0, 0, 0}, {60, 0, 0}, true);   // first use builds the index
            auto t0 = Clock::now();
            const std::string on = navperf::PathText(Search(s, {0, 0, 0}, {60, 0, 0}, true));
            auto t1 = Clock::now();
            const std::string off = navperf::PathText(Search(s, {0, 0, 0}, {60, 0, 0}, false));
            auto t2 = Clock::now();
            CHECK(on == kRockPaths[pen]);
            CHECK(off == kRockPaths[pen]);
            const double msOn = Ms(t0, t1), msOff = Ms(t1, t2);
            std::printf("  1,000 rocks, %s goal: one search %.2f ms with the index, %.2f ms without (x%.0f)\n",
                        pen ? "walled-off" : "open", msOn, msOff, msOn > 0 ? msOff / msOn : 0.0);
            CHECK(msOn * 5.0 < msOff);
        }
    }

    // ---- 3. Replans spread over frames --------------------------------------------------------------------
    {
        CHECK(Navigation::ReplanBudget() == 4);   // the default (no OKAY_NAV_REPLAN_BUDGET in the test run)
        // The old behaviour, without the index cost: no budget. The agents' timers end together.
        Navigation::SetReplanBudget(0);
        FrameStats burst = RunAgents(50, 300, 90);
        // Now: at most 4 timer replans a frame.
        Navigation::SetReplanBudget(4);
        FrameStats spread = RunAgents(50, 300, 90);
        std::printf("  50 agents, 300 rocks: first frame %d searches; most in one later frame: %d with no budget, %d with a budget of 4\n",
                    spread.firstFrameSearches, burst.maxSearchesAfterFirst, spread.maxSearchesAfterFirst);
        CHECK(spread.firstFrameSearches == 50);   // every agent plans at once when it gets a destination
        CHECK(burst.maxSearchesAfterFirst > 8);
        CHECK(spread.maxSearchesAfterFirst <= 8);   // the budget, plus the odd must-plan (an agent wedged on a rock)
        CHECK(spread.searches >= 50 + 50 * 2);      // and every agent still replans (90 frames = 1.5 s, 0.5 s timer)

        // The timing of Finding 107's shape: 50 agents, 1,000 rocks, open goal, 120 frames.
        FrameStats now50 = RunAgents(50, 1000, 120);
        FrameStats now20 = RunAgents(20, 1000, 120);
        std::printf("  50 agents, 1,000 rocks: frame avg %.2f ms, worst %.2f ms (first frame, 50 plans), worst later %.2f ms\n",
                    now50.avg, now50.worst, now50.worstAfterFirst);
        std::printf("  20 agents, 1,000 rocks: frame avg %.2f ms, worst %.2f ms, worst later %.2f ms\n",
                    now20.avg, now20.worst, now20.worstAfterFirst);
        CHECK(now50.worstAfterFirst < 1000.0);   // was seconds-long freezes every half second
        if (const char* full = std::getenv("OKAY_NAV_PERF_FULL"); full && full[0] == '1') {
            Navigation::SetReplanBudget(0);
            NavProbeIndex::SetEnabled(false);
            FrameStats old20 = RunAgents(20, 1000, 120);
            NavProbeIndex::SetEnabled(true);
            Navigation::SetReplanBudget(4);
            std::printf("  OLD (index off, no budget) 20 agents, 1,000 rocks: frame avg %.2f ms, worst %.2f ms\n", old20.avg, old20.worst);
        }
    }

    TEST_MAIN_RESULT();
}
