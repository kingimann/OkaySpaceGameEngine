#pragma once
// When the GPU renderer fails, the game draws with the software renderer. This class decides WHEN to try the GPU again, so one
// bad spell (a driver reset, a lost device, a minimised window) does not leave the game on the CPU renderer for the rest of the run,
// and a GPU that really is broken is not retried every frame.
//
//     GpuFallback gpu;                                   // one per host
//     if (gpu.Allowed(now)) { px = RenderOnGpu(); ev = gpu.Report(px != nullptr, now); }
//     if (!px) px = RenderOnCpu();
//     if (ev == GpuFallback::Event::FellBack)  log("falling back, retry in ", gpu.RetryInSeconds(now));
//     if (ev == GpuFallback::Event::Recovered) log("GPU renderer is back");
//
// Rules: `failuresToFallBack` failures in a row put it in fallback (the software renderer takes over). It then waits
// `firstRetrySeconds`, tries once, and every failed retry doubles the wait up to `maxRetrySeconds`. One success ends the fallback and
// resets everything. `now` is any steady clock in seconds. Plain data, no clock of its own, so a test can drive it.
#include <algorithm>

namespace okay {

class GpuFallback {
public:
    struct Options { int failuresToFallBack = 3; double firstRetrySeconds = 2.0, maxRetrySeconds = 60.0, growth = 2.0; };
    enum class Event { None, FellBack, Recovered };

    GpuFallback() = default;
    explicit GpuFallback(const Options& o) : opt_(o) {}

    /// True when the GPU renderer should be tried this frame (not in fallback, or the wait is over).
    bool Allowed(double now) const { return !fellBack_ || now >= retryAt_; }
    /// Report the result of an attempt made while Allowed().
    Event Report(bool ok, double now) {
        if (ok) {
            const bool was = fellBack_;
            fails_ = 0; fellBack_ = false; wait_ = 0.0; retries_ = 0;
            return was ? Event::Recovered : Event::None;
        }
        ++totalFails_;
        if (fellBack_) {                                        // a retry failed: wait longer
            ++retries_;
            wait_ = std::min(opt_.maxRetrySeconds, wait_ * opt_.growth);
            retryAt_ = now + wait_;
            return Event::None;
        }
        if (++fails_ >= opt_.failuresToFallBack) {
            fellBack_ = true; wait_ = opt_.firstRetrySeconds; retryAt_ = now + wait_;
            return Event::FellBack;
        }
        return Event::None;
    }
    bool   InFallback() const { return fellBack_; }
    int    ConsecutiveFailures() const { return fails_; }
    int    TotalFailures() const { return totalFails_; }
    int    Retries() const { return retries_; }
    double RetryInSeconds(double now) const { return fellBack_ ? std::max(0.0, retryAt_ - now) : 0.0; }
    double CurrentWaitSeconds() const { return wait_; }

private:
    Options opt_;
    int fails_ = 0, totalFails_ = 0, retries_ = 0;
    bool fellBack_ = false;
    double retryAt_ = 0.0, wait_ = 0.0;
};

} // namespace okay
