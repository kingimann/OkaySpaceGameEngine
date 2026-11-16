#include "test_framework.hpp"
#include "okay/Render/GpuFallback.hpp"
#include <iostream>

using namespace okay;

// When the GPU renderer fails the game draws on the CPU; GpuFallback decides when to try the GPU again (the player used to give up for good after
// three failures, which left a machine with one bad spell at 4 fps for the rest of the run).
int main() {
    RUN_SUITE("gpu_fallback");
    using E = GpuFallback::Event;

    // --- a healthy GPU is always allowed and never reports anything ---------------------------------------------------------------------
    {
        GpuFallback g;
        for (int i = 0; i < 100; ++i) { CHECK(g.Allowed(i * 0.016)); CHECK(g.Report(true, i * 0.016) == E::None); }
        CHECK(!g.InFallback() && g.TotalFailures() == 0);
    }

    // --- one or two failures do not fall back; a success clears the count ----------------------------------------------------------------
    {
        GpuFallback g;
        CHECK(g.Report(false, 0.0) == E::None && g.Report(false, 0.1) == E::None);
        CHECK(g.Allowed(0.2) && !g.InFallback());
        CHECK(g.Report(true, 0.2) == E::None && g.ConsecutiveFailures() == 0);
        CHECK(g.Report(false, 0.3) == E::None && g.Report(false, 0.4) == E::None && !g.InFallback());   // counting starts over
    }

    // --- three in a row: fall back, wait 2 s, retry, a failed retry doubles the wait up to a cap -----------------------------------------------
    {
        GpuFallback g;
        g.Report(false, 10.0); g.Report(false, 10.1);
        CHECK(g.Report(false, 10.2) == E::FellBack);
        CHECK(g.InFallback() && !g.Allowed(10.3) && !g.Allowed(12.1));
        CHECK(g.RetryInSeconds(10.2) > 1.99 && g.RetryInSeconds(10.2) < 2.01);
        CHECK(g.Allowed(12.2));                                        // 2 s later it may try once
        CHECK(g.Report(false, 12.2) == E::None);                       // the retry failed: wait 4 s
        CHECK(!g.Allowed(15.0) && g.Allowed(16.3));
        CHECK(g.Report(false, 16.3) == E::None && g.CurrentWaitSeconds() == 8.0);
        double t = 16.3; int tries = 0;
        while (g.CurrentWaitSeconds() < 60.0 && tries < 20) { t += g.CurrentWaitSeconds() + 0.01; CHECK(g.Allowed(t)); g.Report(false, t); ++tries; }
        CHECK(g.CurrentWaitSeconds() == 60.0);                         // capped, not growing for ever
        t += 60.01; CHECK(g.Allowed(t)); g.Report(false, t);
        CHECK(g.CurrentWaitSeconds() == 60.0);
        std::cout << "  waits 2, 4, 8, ... capped at " << g.CurrentWaitSeconds() << " s after " << g.Retries() << " failed retries\n";
    }

    // --- recovery: one success on a retry ends the fallback and resets the waits ---------------------------------------------------------------
    {
        GpuFallback g;
        g.Report(false, 0.0); g.Report(false, 0.1); g.Report(false, 0.2);
        g.Report(false, 2.3);                                          // first retry fails: 4 s
        CHECK(g.Allowed(6.4));
        CHECK(g.Report(true, 6.4) == E::Recovered);
        CHECK(!g.InFallback() && g.Allowed(6.5) && g.Retries() == 0 && g.CurrentWaitSeconds() == 0.0);
        // and a later bad spell starts again from the short wait
        g.Report(false, 20.0); g.Report(false, 20.1);
        CHECK(g.Report(false, 20.2) == E::FellBack && g.CurrentWaitSeconds() == 2.0);
    }

    // --- custom options -------------------------------------------------------------------------------------------------------------------------
    {
        GpuFallback::Options o; o.failuresToFallBack = 1; o.firstRetrySeconds = 0.5; o.maxRetrySeconds = 1.0; o.growth = 3.0;
        GpuFallback g(o);
        CHECK(g.Report(false, 0.0) == E::FellBack && !g.Allowed(0.4) && g.Allowed(0.5));
        g.Report(false, 0.5); CHECK(g.CurrentWaitSeconds() == 1.0);   // 0.5 * 3 = 1.5, capped at 1
    }

    TEST_MAIN_RESULT();
}
