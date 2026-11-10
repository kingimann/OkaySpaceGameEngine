// Tweens that run for hours: the timeline is kept in double precision, so an endless loop that has
// been playing for ten hours still moves at its real pace, hits its end values exactly, and a tween
// with a delay completes at its exact length however the float delay rounds. Everything runs on fake
// frame times (no clock, no sleeping). Block N runs alone with `test_tween_long_run N`.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <vector>

using namespace okay;

// The same file also builds against the older float timeline (-DOLD_TWEEN_API) to show what it fixed.
#ifdef OLD_TWEEN_API
#define ELAPSED_D(t) ((double)(t)->Elapsed())
#define FULL_D(t) ((double)(t)->FullDuration())
#else
#define ELAPSED_D(t) ((t)->ElapsedD())
#define FULL_D(t) ((t)->FullDurationD())
#endif

static int g_only = 0;
#define BLOCK(n) if (g_only == 0 || g_only == (n))

int main(int argc, char** argv) {
    if (argc > 1) g_only = std::atoi(argv[1]);
    RUN_SUITE("tween_long_run");

    constexpr double kTenHours = 36000.0;

    BLOCK(1) {   // ten hours of 0.1 s frames: the tween is where the clock says, not 1.5% off
        TweenManager m;
        float v = 0;
        auto t = m.To([&] { return v; }, [&](float x) { v = x; }, 1.0f, 1.0f);
        t->SetEase(Ease::Linear).SetLoops(-1, LoopType::Restart).SetAutoKill(false);
        const float dt = 0.1f;
        const long frames = 360000;
        for (long i = 0; i < frames; ++i) m.Update(dt, dt);
        const double expected = (double)frames * (double)dt;           // what the clock added up to
        CHECK_NEAR(ELAPSED_D(t), expected, 1e-4);
        CHECK_NEAR(t->Elapsed(), expected, 1.0);                       // the float view of it
        const double phase = std::fmod(expected, 1.0);
        CHECK_NEAR(v, phase, 1e-4);
        CHECK(t->CompletedLoops() > 35999 && t->CompletedLoops() < 36002);
    }

    BLOCK(2) {   // jump to hour ten: values inside a loop are exact to double rounding, not to float rounding
        TweenManager m;
        float v = 0;
        auto t = m.To([&] { return v; }, [&](float x) { v = x; }, 1.0f, 1.0f);
        t->SetEase(Ease::Linear).SetLoops(-1, LoopType::Restart).SetAutoKill(false);
        t->RenderAt(kTenHours + 0.5);
        CHECK_NEAR(v, 0.5, 1e-6);
        t->RenderAt(kTenHours + 0.999);
        CHECK_NEAR(v, 0.999, 1e-6);                                    // a float timeline would be off by ~1e-3 here
        t->RenderAt(kTenHours + 0.001);
        CHECK_NEAR(v, 0.001, 1e-6);
    }

    BLOCK(3) {   // yoyo: at hour ten the turning points are exactly the end values
        TweenManager m;
        float v = 0;
        auto t = m.To([&] { return v; }, [&](float x) { v = x; }, 10.0f, 2.0f);
        t->SetEase(Ease::QuadOut).SetLoops(-1, LoopType::Yoyo).SetAutoKill(false);
        t->RenderAt(kTenHours + 2.0 * 1.0);          // loop boundary: odd loop begins at the far end
        CHECK(v == 10.0f);
        t->RenderAt(kTenHours + 4.0);                // next boundary: back at the start value
        CHECK(v == 0.0f);
        t->RenderAt(kTenHours + 1.0);                // half way through an even loop: QuadOut(0.5) = 0.75
        CHECK_NEAR(v, 7.5, 1e-5);
        t->RenderAt(kTenHours + 3.0);                // half way through an odd loop (running back): the same value
        CHECK_NEAR(v, 7.5, 1e-5);
    }

    BLOCK(4) {   // Complete() on an endless tween hours in lands on the exact end of its loop
        TweenManager m;
        float v = 0;
        int completes = 0;
        auto t = m.To([&] { return v; }, [&](float x) { v = x; }, 4.0f, 3.0f);
        t->SetEase(Ease::Linear).SetLoops(-1, LoopType::Restart).SetAutoKill(false).OnComplete([&] { ++completes; });
        t->RenderAt(kTenHours + 1.25);
        t->Complete();
        CHECK(v == 4.0f);
        CHECK(completes == 1);
    }

    BLOCK(5) {   // a finite ten hour tween completes at 36000 s, once, with the exact end value
        TweenManager m;
        float v = 0;
        int completes = 0, steps = 0;
        auto t = m.To([&] { return v; }, [&](float x) { v = x; }, 100.0f, 3600.0f);
        t->SetEase(Ease::Linear).SetLoops(10).OnComplete([&] { ++completes; }).OnStepComplete([&] { ++steps; });
        const float dt = 0.1f;
        long frames = 0;
        while (t->IsActive() && frames < 400000) { m.Update(dt, dt); ++frames; }
        CHECK(!t->IsActive());
        CHECK(completes == 1 && steps == 10);
        CHECK(v == 100.0f);
        // 36000 s of 0.1 s frames is 360000 frames; the float 0.1 is a hair more than a tenth, so at most that many.
        CHECK(frames >= 359000 && frames <= 360000);
    }

    BLOCK(6) {   // a delay that does not fit a float (0.1, 0.3, ...) must not stop a tween from completing
        const float delays[] = {0.1f, 0.2f, 0.3f, 0.7f, 1.1f, 2.3f, 0.01f};
        const float lengths[] = {0.1f, 0.3f, 1.0f / 3.0f, 0.7f, 1.0f};
        const int loopCounts[] = {1, 2, 3, 7, 10};
        int notDone = 0, wrongCompletes = 0, total = 0;
        for (float delay : delays)
            for (float len : lengths)
                for (int loops : loopCounts) {
                    TweenManager m;
                    float v = 0;
                    int completes = 0;
                    auto t = m.To([&] { return v; }, [&](float x) { v = x; }, 1.0f, len);
                    t->SetEase(Ease::Linear).SetDelay(delay).SetLoops(loops).SetAutoKill(false).OnComplete([&] { ++completes; });
                    m.Update(1.0e6f, 1.0e6f);                // one enormous step lands exactly on the end
                    ++total;
                    if (!t->IsComplete() || v != 1.0f) ++notDone;
                    if (completes != 1) ++wrongCompletes;
                    // and stepping to the end in small frames completes too (within a frame of the full length)
                    TweenManager m2;
                    float w = 0;
                    int completes2 = 0;
                    auto t2 = m2.To([&] { return w; }, [&](float x) { w = x; }, 1.0f, len);
                    t2->SetEase(Ease::Linear).SetDelay(delay).SetLoops(loops).SetAutoKill(false).OnComplete([&] { ++completes2; });
                    const double full = FULL_D(t2);
                    for (double elapsed = 0; elapsed < full + 0.05 && !t2->IsComplete(); elapsed += 0.0125) m2.Update(0.0125f, 0.0125f);
                    if (!t2->IsComplete() || w != 1.0f || completes2 != 1) ++notDone;
                }
        CHECK(total == 7 * 5 * 5);
        CHECK(notDone == 0);
        CHECK(wrongCompletes == 0);
    }

    BLOCK(7) {   // the plain Tween struct also keeps double precision: an hour long tween is at half way at half time
        Tween t(0.0f, 1.0f, 3600.0f, Ease::Linear);
        for (int i = 0; i < 18000; ++i) t.Update(0.1f);      // 1800 s
        CHECK_NEAR(t.Progress(), 0.5, 1e-5);
        CHECK(!t.Done());
        for (int i = 0; i < 17990; ++i) t.Update(0.1f);
        CHECK(!t.Done());
        for (int i = 0; i < 20; ++i) t.Update(0.1f);
        CHECK(t.Done());
        CHECK(t.Value() == 1.0f);
    }

    BLOCK(8) {   // a loop of 0.1 ms that runs for a long time never freezes at its end (the loop counter is bounded, the phase is not)
        TweenManager m;
        float v = 0;
        auto t = m.To([&] { return v; }, [&](float x) { v = x; }, 1.0f, 1e-4f);
        t->SetEase(Ease::Linear).SetLoops(-1, LoopType::Restart).SetAutoKill(false);
        t->RenderAt(5.0e8);                                    // 5e12 loops: past what an int counts
        CHECK(std::isfinite(v) && v >= 0.0f && v <= 1.0f);
        std::vector<float> seen;
        for (int k = 1; k <= 6; ++k) {                          // it keeps moving: the phase is not frozen at the end of a loop
            t->RenderAt(5.0e8 + (double)k * 0.25e-4);
            CHECK(std::isfinite(v) && v >= 0.0f && v <= 1.0f);
            if (std::find(seen.begin(), seen.end(), v) == seen.end()) seen.push_back(v);
        }
        CHECK(seen.size() >= 3);
        t->RenderAt(1.0e9);
        CHECK(std::isfinite(v) && v >= 0.0f && v <= 1.0f);
        CHECK(t->CompletedLoops() >= 0);
    }

    TEST_MAIN_RESULT();
}
