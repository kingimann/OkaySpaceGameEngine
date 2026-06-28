#include "test_framework.hpp"
#include "okay/Core/Profiler.hpp"
#include <thread>

using namespace okay;

int main() {
    RUN_SUITE("profiler");

    Profiler& P = Prof();

    // Disabled by default: scopes are no-ops, nothing is recorded.
    {
        P.enabled = false;
        P.BeginFrame();
        { OKAY_PROFILE("Nope"); }
        P.EndFrame(16.0);
        CHECK(P.Count() == 0);
    }

    // Enabled: nested scopes record self vs inclusive and call counts; the frame
    // snapshots into the ring with totals.
    {
        P.enabled = true;
        for (int frame = 0; frame < 3; ++frame) {
            P.BeginFrame();
            {
                OKAY_PROFILE("Scripts.Update");
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                { OKAY_PROFILE("Physics.3D"); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
            }
            P.Stat3(5, 1200, 30);
            P.Gpu(3.5);
            P.EndFrame(16.0);
        }
        CHECK(P.Count() == 3);

        const Profiler::Frame& f = P.Displayed();
        CHECK(f.total == 16.0);
        CHECK(f.gpu == 3.5);
        CHECK(f.objects == 5 && f.triangles == 1200 && f.particles == 30);

        // Two sections recorded; the parent's self excludes the child, inclusive includes it.
        const Profiler::Bucket* parent = nullptr;
        const Profiler::Bucket* child = nullptr;
        for (const auto& b : f.buckets) {
            if (b.name == "Scripts.Update") parent = &b;
            if (b.name == "Physics.3D")     child = &b;
        }
        CHECK(parent != nullptr);
        CHECK(child != nullptr);
        if (parent && child) {
            CHECK(parent->depth == 0);
            CHECK(child->depth == 1);                       // nested
            CHECK(parent->calls == 1 && child->calls == 1);
            CHECK(parent->inclusive >= parent->self);       // inclusive includes the child
            CHECK(parent->inclusive >= child->inclusive - 0.001);
            CHECK(child->self > 0.0);
        }

        // CPU = sum of depth-0 inclusive; here just the one top-level scope.
        CHECK(f.cpu > 0.0);
        CHECK_NEAR(f.cpu, parent ? parent->inclusive : 0.0, 0.001);

        // Category sum picks up by name prefix.
        CHECK_NEAR(Profiler::CategorySelf(f, "Scripts"), parent ? parent->self : 0.0, 0.001);
        CHECK_NEAR(Profiler::CategorySelf(f, "Physics"), child ? child->self : 0.0, 0.001);

        // Per-section stats over the window (3 frames each contributed once).
        Profiler::Stat st = P.SectionStat("Physics.3D");
        CHECK(st.frames == 3);
        CHECK(st.mx >= st.avg && st.avg >= st.mn);
    }

    // Pause freezes the ring (scrubbing): new EndFrames don't append.
    {
        int before = P.Count();
        P.paused = true;
        P.BeginFrame();
        { OKAY_PROFILE("ShouldNotRecord"); }
        P.EndFrame(99.0);
        CHECK(P.Count() == before);     // unchanged while paused
        P.paused = false;
    }

    P.enabled = false;
    TEST_MAIN_RESULT();
}
