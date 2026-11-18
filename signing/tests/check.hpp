#pragma once
// Tiny test helper for the signing tests. NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY.
#include <cstdio>
#include <cstdlib>

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond)                                                                    \
    do {                                                                               \
        ++g_checks;                                                                    \
        if (!(cond)) {                                                                 \
            ++g_failures;                                                              \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                \
        }                                                                              \
    } while (0)
#define FINISH(name)                                                                   \
    do {                                                                               \
        std::printf("%s: %d checks, %d failures\n", name, g_checks, g_failures);       \
        return g_failures == 0 ? 0 : 1;                                                \
    } while (0)
