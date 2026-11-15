#pragma once
// Helpers for tests that must look at elapsed time. See docs/automated_testing.md, "Timing tests: rules".
//
//   BestOfMs(n, f)   run f() n times, return the FASTEST run in milliseconds. A busy machine only makes runs slower,
//                    so the minimum is the least noisy number (a median still moves with load).
//   CalibrationMs()  how long a fixed piece of plain CPU work takes on THIS machine right now (best of 7, in ms).
//                    Compare a measurement against a multiple of it, instead of against a number of milliseconds
//                    that was true on one developer's computer.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

namespace okaytest {

inline double NowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

template <class F> double BestOfMs(int n, F&& f) {
    double best = 1e300;
    for (int i = 0; i < n; ++i) {
        const double t0 = NowMs();
        f();
        best = std::min(best, NowMs() - t0);
    }
    return best;
}

// Fixed work: a pseudo-random sort plus a pointer-free arithmetic loop (cache and ALU, like engine code). About 5 ms on a
// current desktop core. The result is written to a volatile so the compiler cannot remove the work.
inline double CalibrationMs() {
    static volatile std::uint64_t sink;
    return BestOfMs(7, [] {
        std::vector<std::uint32_t> v(60000);
        std::uint32_t x = 2463534242u;
        for (auto& e : v) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; e = x; }
        std::sort(v.begin(), v.end());
        std::uint64_t acc = 0;
        for (std::size_t i = 0; i < v.size(); ++i) acc += (std::uint64_t)v[i] * (i | 1);
        sink = acc;
    });
}

} // namespace okaytest
