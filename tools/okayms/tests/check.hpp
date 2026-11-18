#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace okayms_test {

struct Tally {
    int pass = 0;
    int fail = 0;

    void check(bool ok, const char* file, int line, const char* expr) {
        if (ok) {
            ++pass;
            return;
        }
        std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
        ++fail;
    }

    int finish(int tests) const {
        std::printf("tests %d, checks passed %d, failed %d\n", tests, pass, fail);
        return fail == 0 ? 0 : 1;
    }
};

inline void dump_mismatch(const std::vector<uint8_t>& got, const uint8_t* expect, size_t n) {
    size_t i = 0;
    const size_t shared = got.size() < n ? got.size() : n;
    while (i < shared && got[i] == expect[i]) ++i;
    std::fprintf(stderr, "byte mismatch at %zu (got %zu bytes, expected %zu)\n", i, got.size(), n);
    const size_t begin = i > 8 ? i - 8 : 0;
    size_t end = i + 16;
    if (end < begin) end = begin;
    std::fprintf(stderr, "expected:");
    for (size_t j = begin; j < end && j < n; ++j) std::fprintf(stderr, " %02X", expect[j]);
    std::fprintf(stderr, "\n     got:");
    for (size_t j = begin; j < end && j < got.size(); ++j) std::fprintf(stderr, " %02X", got[j]);
    std::fprintf(stderr, "\n");
}

inline bool same_bytes(const std::vector<uint8_t>& got, const uint8_t* expect, size_t n) {
    if (got.size() != n) {
        dump_mismatch(got, expect, n);
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        if (got[i] != expect[i]) {
            dump_mismatch(got, expect, n);
            return false;
        }
    }
    return true;
}

}  // namespace okayms_test

#define CHECK(tally, cond) (tally).check(static_cast<bool>(cond), __FILE__, __LINE__, #cond)
