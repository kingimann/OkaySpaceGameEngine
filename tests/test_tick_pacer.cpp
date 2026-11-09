// 1.1 TickPacer: the fixed-rate pacing arithmetic of DedicatedServer's loop, driven with a made-up
// clock (nanoseconds as plain integers): ordinary ticks, a long stall, a clock that jumps backwards.
// The reference loop below is the server's own pacing code, copied verbatim on std::chrono types.
#include "test_framework.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Core/TickPacer.hpp"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace okay;

namespace {

std::uint32_t g_s = 55555u;
std::uint32_t Rnd() { g_s ^= g_s << 13; g_s ^= g_s >> 17; g_s ^= g_s << 5; return g_s; }

// DedicatedServer.cpp's pacing, on a fake clock. Returns the sleep it would do (ns).
struct ServerReference {
    using clock = std::chrono::steady_clock;
    clock::duration tick;
    clock::time_point next;
    explicit ServerReference(double hz, std::int64_t startNs)
        : tick(std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / hz))),
          next(clock::time_point(clock::duration(startNs))) {}
    std::int64_t Finish(std::int64_t nowNs) {
        const clock::time_point t1{clock::duration(nowNs)};
        next += tick;
        if (next < t1 - tick * 4) next = t1;
        const auto wait = next - t1;                          // the server samples the clock again here; the fake one has not moved
        return wait > clock::duration::zero() ? (std::int64_t)wait.count() : 0;
    }
};

}  // namespace

int main() {
    RUN_SUITE("tick_pacer");

    // ---- the rate -----------------------------------------------------------------------------------------------------------------------
    {
        TickPacer p(30.0);
        CHECK(p.Rate() == 30.0 && p.TickNs() == 33333333 && p.MaxLagTicks() == 4);
        CHECK(TickPacer(60.0).TickNs() == 16666666 && TickPacer(20.0).TickNs() == 50000000);
        CHECK(TickPacer(0.0).Rate() == 1.0 && TickPacer(-5.0).Rate() == 1.0 && TickPacer(5000.0).Rate() == 1000.0);
        CHECK(TickPacer(std::nan("")).Rate() == 30.0);
        p.SetMaxLagTicks(0);
        CHECK(p.MaxLagTicks() == 1);
        CHECK(!p.Started());
    }

    // ---- on time: the schedule is steady and does not drift ------------------------------------------------------------------------------------
    {
        TickPacer p(30.0);
        const std::int64_t tick = p.TickNs();
        std::int64_t now = 1000000000;
        p.Start(now);
        CHECK(p.Started());
        bool exact = true;
        for (int i = 1; i <= 3000; ++i) {
            now += 2000000;                                   // a tick that takes 2 ms
            const std::int64_t sleep = p.FinishTick(now);
            if (sleep != tick - 2000000) exact = false;
            now += sleep;                                     // slept exactly that long: back on the schedule
            if (now != 1000000000 + (std::int64_t)i * tick) exact = false;   // tick i starts exactly i ticks after the start: no drift
        }
        CHECK(exact);
        CHECK(p.Ticks() == 3000 && p.Resyncs() == 0 && p.BackJumps() == 0);
        CHECK(p.LagTicks(now) == 0.0);
        // a tick that takes longer than the tick runs the next one at once, and catches up
        TickPacer q(30.0);
        std::int64_t t = 0;
        q.Start(t);
        t += 50000000;                                        // 1.5 ticks of work
        CHECK(q.FinishTick(t) == 0);                          // behind: no sleep
        t += 1000000;                                         // a short tick
        const std::int64_t s2 = q.FinishTick(t);
        CHECK(s2 == 2 * q.TickNs() - 51000000);               // the second was due at 2 ticks: sleeps the remainder, back on schedule
        CHECK(q.Resyncs() == 0);
        // FinishTick without Start starts it there
        TickPacer lazy(10.0);
        CHECK(lazy.FinishTick(5000) == lazy.TickNs() && lazy.Started());
    }

    // ---- a long stall: forget the backlog, once, then carry on at the normal rate ------------------------------------------------------------------------------
    {
        TickPacer p(30.0);
        const std::int64_t tick = p.TickNs();
        std::int64_t now = 0;
        p.Start(now);
        for (int i = 0; i < 10; ++i) { now += 1000000; now += p.FinishTick(now); }
        const std::int64_t before = now;
        now += 10000000000LL;                                 // a ten second stall inside one tick
        CHECK(p.FinishTick(now) == 0);                        // no sleep, and the schedule restarted from now
        CHECK(p.Resyncs() == 1);
        std::int64_t sleeps = 0;
        int flatOut = 0;
        for (int i = 0; i < 100; ++i) {
            now += 1000000;
            const std::int64_t s = p.FinishTick(now);
            if (s == 0) ++flatOut;
            sleeps += s;
            now += s;
        }
        CHECK(flatOut == 0);                                  // the stall is not paid back with a burst of ticks
        CHECK(p.Resyncs() == 1);
        CHECK(sleeps + 100 * 1000000 == 100 * tick);          // 100 ticks take 100 tick lengths again
        CHECK(before < now);
        // a stall just under the limit is caught up with back-to-back ticks (at most 4), no resync
        TickPacer c(30.0);
        std::int64_t t = 0;
        c.Start(t);
        t += 4 * tick - 1;                                    // 4 ticks late (less a nanosecond)
        int burst = 0;
        std::int64_t s;
        while ((s = c.FinishTick(t)) == 0 && burst < 20) { ++burst; t += 1000; }
        CHECK(c.Resyncs() == 0);
        CHECK(burst >= 3 && burst <= 4);                      // up to the lag limit, then it is on time again
        // 5 ticks late: forgotten at once
        TickPacer d(30.0);
        t = 0;
        d.Start(t);
        t += 6 * tick;
        CHECK(d.FinishTick(t) == 0 && d.Resyncs() == 1);
        // the game time of a stalled server: one tick of simulated time per tick, never the stall
        Time::ResetForTesting();
        TickPacer g(30.0);
        t = 0;
        g.Start(t);
        double game = 0.0;
        for (int i = 0; i < 50; ++i) {
            Time::Step(1.0f / 30.0f);
            game += (double)Time::DeltaTime();
            t += (i == 20 ? 5000000000LL : 3000000);          // tick 20 stalls for five seconds
            t += g.FinishTick(t);
        }
        CHECK_NEAR(game, 50.0 / 30.0, 1e-4);                  // 50 ticks of game time whatever the wall clock did
        CHECK(g.Resyncs() == 1);
        Time::ResetForTesting();
    }

    // ---- a clock that goes backwards never makes it sleep for the size of the jump ----------------------------------------------------------------------------------
    {
        TickPacer p(30.0);
        const std::int64_t tick = p.TickNs();
        std::int64_t now = 100000000000LL;
        p.Start(now);
        for (int i = 0; i < 5; ++i) { now += 1000000; now += p.FinishTick(now); }
        now -= 3600LL * 1000000000LL;                         // the clock jumps back an hour
        const std::int64_t sleep = p.FinishTick(now);
        CHECK(sleep == tick && p.BackJumps() == 1);           // one tick, not an hour
        now += sleep;
        bool steady = true;
        for (int i = 0; i < 20; ++i) { now += 1000000; const std::int64_t s = p.FinishTick(now); if (s != tick - 1000000) steady = false; now += s; }
        CHECK(steady && p.BackJumps() == 1 && p.Resyncs() == 0);
        // a smaller step back (2 ticks) is within what a late wakeup can look like: nothing changes
        TickPacer q(30.0);
        now = 10 * tick;
        q.Start(now);
        now += 1000000;
        const std::int64_t s1 = q.FinishTick(now);
        now += s1 - 2 * tick;
        const std::int64_t s2 = q.FinishTick(now);
        CHECK(s2 > 2 * tick && q.BackJumps() == 0);           // tolerated up to the lag limit
        // never a negative sleep, never above (lag limit + 1) ticks, for any clock at all
        bool bounded = true;
        TickPacer r(60.0);
        r.Start(0);
        std::int64_t clockNs = 0;
        for (int i = 0; i < 20000; ++i) {
            const std::uint32_t pick = Rnd() % 10u;
            if (pick == 0) clockNs -= (std::int64_t)(Rnd() % 5000000000u);
            else if (pick == 1) clockNs += (std::int64_t)(Rnd() % 5000000000u);
            else clockNs += (std::int64_t)(Rnd() % 30000000u);
            const std::int64_t s = r.FinishTick(clockNs);
            if (s < 0 || s > r.TickNs() * (r.MaxLagTicks() + 1)) bounded = false;
            clockNs += s;
        }
        CHECK(bounded);
    }

    // ---- the same decisions as the server's own loop, on random work and stalls --------------------------------------------------------------------------------------------
    {
        const double rates[] = {1.0, 10.0, 20.0, 30.0, 60.0, 64.0, 120.0, 240.0};
        bool identical = true;
        long long compared = 0;
        for (double hz : rates) {
            TickPacer p(hz);
            ServerReference ref(hz, 123456789);
            p.Start(123456789);
            std::int64_t now = 123456789;
            for (int i = 0; i < 4000; ++i) {
                const std::uint32_t kind = Rnd() % 100u;
                std::int64_t work;
                if (kind < 70) work = (std::int64_t)(Rnd() % (std::uint32_t)(p.TickNs() / 2));              // light ticks
                else if (kind < 90) work = (std::int64_t)(Rnd() % (std::uint32_t)(p.TickNs() * 2));          // some over the tick
                else if (kind < 97) work = p.TickNs() * 3 + (std::int64_t)(Rnd() % (std::uint32_t)p.TickNs());   // a few ticks late
                else work = (std::int64_t)Rnd() * 4;                                                          // a stall of seconds
                now += work;
                const std::int64_t a = p.FinishTick(now), b = ref.Finish(now);
                ++compared;
                if (a != b) identical = false;
                now += a;
            }
        }
        CHECK(identical);
        CHECK(compared == 8 * 4000);
    }

    // ---- sleeping a little early (a coarse timer) changes nothing -------------------------------------------------------------------------------------------------------------
    {
        TickPacer p(60.0);
        ServerReference ref(60.0, 0);
        p.Start(0);
        std::int64_t now = 0;
        bool same = true;
        for (int i = 0; i < 2000; ++i) {
            now += 100000;                                    // a 0.1 ms tick
            const std::int64_t a = p.FinishTick(now), b = ref.Finish(now);
            if (a != b) same = false;
            now += a - (std::int64_t)(Rnd() % 900000u);       // wakes up to 0.9 ms early (Windows Sleep truncates to milliseconds)
        }
        CHECK(same);
        CHECK(p.BackJumps() == 0);
    }

    TEST_MAIN_RESULT();
}
