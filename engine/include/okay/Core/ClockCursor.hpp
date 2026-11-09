#pragma once
// Follows a Clock channel: how many seconds it advanced since you last asked.
//
// Scheduler tasks and tweens that run on a named channel (Options::clock,
// Tweener::SetClock) each keep one of these. Asking twice between two ticks gives 0 the
// second time, so updating a scene twice in one frame does not run its channel timers
// twice; asking after several ticks gives the sum; a clock that was Reset (a new Play
// session) resyncs instead of running backwards. The first ask takes the current
// frame's share, as a task on scene time would get in its first Update.
#include <cstdint>

namespace okay {

struct ClockCursor {
    /// Seconds a Clock advanced since the last Take (0 while it is paused or held). Pass the
    /// clock's Ticks(), Elapsed() and Delta(). (Takes the numbers, not the Clock: this header is
    /// part of <Okay.hpp> and must not name okay::Clock, so a program with its own `Clock` and
    /// `using namespace okay;` keeps compiling.)
    float Take(std::uint64_t clockTicks, double clockElapsed, float clockDelta);

    bool primed = false;
    std::uint64_t ticks = 0;
    double elapsed = 0.0;
};

} // namespace okay
