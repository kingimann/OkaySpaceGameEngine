#pragma once
// Minimal, dependency-free test harness.
#include <iostream>
#include <string>
#include <cmath>

namespace okaytest {
inline int g_failures = 0;
inline int g_checks   = 0;
}

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++::okaytest::g_checks;                                              \
        if (!(cond)) {                                                       \
            ++::okaytest::g_failures;                                        \
            std::cerr << "  FAIL: " << #cond << "  (" << __FILE__ << ':'     \
                      << __LINE__ << ")\n";                                  \
        }                                                                    \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                \
    do {                                                                     \
        ++::okaytest::g_checks;                                              \
        double _d = std::fabs(double(a) - double(b));                        \
        if (_d > (eps)) {                                                    \
            ++::okaytest::g_failures;                                        \
            std::cerr << "  FAIL: |" << #a << " - " << #b << "| = " << _d    \
                      << " > " << (eps) << "  (" << __FILE__ << ':'          \
                      << __LINE__ << ")\n";                                  \
        }                                                                    \
    } while (0)

#define RUN_SUITE(name)  std::cout << "[suite] " << name << "\n"

#define TEST_MAIN_RESULT()                                                   \
    do {                                                                     \
        std::cout << ::okaytest::g_checks << " checks, "                     \
                  << ::okaytest::g_failures << " failures\n";                \
        return ::okaytest::g_failures == 0 ? 0 : 1;                          \
    } while (0)
