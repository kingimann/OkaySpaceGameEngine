#pragma once

#include <cstdio>
#include <string>

namespace okaycc_test {

inline int gPass = 0;
inline int gFail = 0;

inline void expect(bool cond, const char* expr, const char* file, int line) {
    if (cond) {
        ++gPass;
        return;
    }
    ++gFail;
    std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
}

inline void expectEq(const std::string& got, const std::string& want, const char* file, int line) {
    if (got == want) {
        ++gPass;
        return;
    }
    ++gFail;
    std::fprintf(stderr, "FAIL %s:%d:\n--- got ---\n%s\n--- want ---\n%s\n", file, line, got.c_str(),
                 want.c_str());
}

inline int done(const char* suite) {
    std::printf("%s: %d passed, %d failed\n", suite, gPass, gFail);
    return gFail == 0 ? 0 : 1;
}

}  // namespace okaycc_test

#define EXPECT(c) ::okaycc_test::expect(static_cast<bool>(c), #c, __FILE__, __LINE__)
#define EXPECT_EQ(a, b) ::okaycc_test::expectEq((a), (b), __FILE__, __LINE__)
