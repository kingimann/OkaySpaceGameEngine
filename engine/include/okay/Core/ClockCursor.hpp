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

class Clock;

struct ClockCursor {
    /// Seconds `clock` advanced since the last Take (0 while it is paused or held).
    float Take(const Clock& clock);

    bool primed = false;
    std::uint64_t ticks = 0;
    double elapsed = 0.0;
};

} // namespace okay
