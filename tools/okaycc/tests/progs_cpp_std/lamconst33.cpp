// Round 33: a const local read in a lambda that captures nothing ([basic.def.odr] p4,
// [expr.prim.lambda.capture] p7): a constant initializer gives its value (the lambda still converts
// to a function pointer); a value known only at run time is taken by copy.
// engine/src/Components/Volume.cpp's constexpr float inf = numeric_limits<float>::infinity().
#include <cmath>
#include <cstdio>
#include <limits>
int main() {
    constexpr float inf = std::numeric_limits<float>::infinity();
    const int kScale = 3;
    constexpr double kHalf = 0.5;
    auto axis = [](float l, float o, float s) {
        if (!(s > 1e-6f) || !std::isfinite(s)) return inf;
        return (l - o) / (s * 0.5f);
    };
    int (*twice)(int) = [](int v) { return v * kScale; };
    auto half = [](double x) { return x * kHalf; };
    std::printf("%.2f %d %d %.2f %.1f\n", axis(3, 1, 2), (int)std::isinf(axis(1, 0, 0)), twice(7), half(5), (double)inf > 1e30 ? 1.0 : 0.0);
    return 0;
}
