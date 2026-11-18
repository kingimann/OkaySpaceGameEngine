// A tiny test harness for the link-and-run programs of okay-ms (tools/okayms/linktests). It has the same CHECK and
// CHECK_NEAR macros as tests/test_framework.hpp, but needs only <cstdio> and <cmath> (no <iostream>), so both okay-cl
// (with its own headers and runtime) and MinGW g++ can build the same program text. Every check counts; a failure prints
// its line. `Show` prints a value in a form that is the same on every compiler (integers and hex only), so the
// output of the two builds can be compared byte for byte.
#pragma once
#include <cmath>
#include <cstdio>

namespace mini {
inline int g_checks = 0;
inline int g_failures = 0;
inline double Abs(double v) { return v < 0.0 ? -v : v; }
inline void Fail(int line) {
    ++g_failures;
    std::printf("FAIL line %d\n", line);
}
// A float as a whole number of millionths (rounded), so the text does not depend on a float formatter.
inline long long Micro(double v) { return (long long)(v * 1000000.0 + (v < 0.0 ? -0.5 : 0.5)); }
}  // namespace mini

#define CHECK(cond)                        \
    do {                                   \
        ++mini::g_checks;                  \
        if (!(cond)) mini::Fail(__LINE__); \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                          \
    do {                                                               \
        ++mini::g_checks;                                              \
        if (mini::Abs((double)(a) - (double)(b)) > (double)(eps)) mini::Fail(__LINE__); \
    } while (0)

#define RUN_SUITE(name) std::printf("[suite] %s\n", name)

// Prints "<n> checks, <m> failures" and returns the exit code (0 when every check held).
#define TEST_MAIN_RESULT()                                                    \
    do {                                                                      \
        std::printf("%d checks, %d failures\n", mini::g_checks, mini::g_failures); \
        return mini::g_failures == 0 ? 0 : 1;                                 \
    } while (0)
