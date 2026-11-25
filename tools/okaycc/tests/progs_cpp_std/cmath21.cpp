// Round 21: ldexp, frexp, fmod, nearbyint, rint, copysign in our own <cmath> against glibc (all exact
// operations, so every bit must match).
#include <cmath>
#include <cstdio>

static unsigned long bits(double x) { return *(unsigned long *)&x; }

int main() {
    double xs[] = {1.0, -2.5, 3.75, 1e-310, 4.9e-324, 1e300, -0.0, 0.5, 2.5, -3.5, 1234567.891, 7.0, -7.0, 1e-20};
    int ks[] = {0, 1, -1, 10, -1074, 1023, -1022, 60, -60, 2000, -2000};
    unsigned long h = 0;
    for (double x : xs)
        for (int k : ks) h = h * 31 + bits(std::ldexp(x, k));
    std::printf("ldexp %lx\n", h);
    h = 0;
    for (double x : xs) {
        int e = 0;
        double m = std::frexp(x, &e);
        h = h * 31 + bits(m) + (unsigned long)e;
    }
    std::printf("frexp %lx\n", h);
    h = 0;
    for (double x : xs)
        for (double y : xs) h = h * 31 + bits(std::fmod(x, y));
    std::printf("fmod %lx %.17g %.17g %.17g\n", h, std::fmod(10.0, 3.0), std::fmod(-7.5, 2.0), std::fmod(5.3, 0.1));
    h = 0;
    for (double x : xs) h = h * 31 + bits(std::nearbyint(x)) + bits(std::rint(x * 1.5));
    std::printf("nearbyint %lx %.1f %.1f %.1f\n", h, std::nearbyint(2.5), std::nearbyint(3.5), std::nearbyint(-0.4));
    std::printf("copysign %.1f %.1f %d\n", std::copysign(3.0, -1.0), std::copysign(-2.0, 0.0), (int)std::signbit(std::copysign(0.0, -1.0)));
    std::printf("float %.9g %.9g %.9g\n", (double)std::ldexp(1.5f, -140), (double)std::fmod(7.25f, 2.0f), (double)std::nearbyint(-1.5f));
    return 0;
}
