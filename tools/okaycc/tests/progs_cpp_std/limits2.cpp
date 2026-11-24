// numeric_limits<float> and <double> (explicit specializations in our <limits>), used at run time
// and in constant initializers of static members; std::round, lround, llround, trunc.
#include <cmath>
#include <limits>

extern "C" int printf(const char *fmt, ...);

struct K {
    static constexpr float Inf = std::numeric_limits<float>::infinity();
    static constexpr float Big = std::numeric_limits<float>::max();
    static constexpr double Eps = std::numeric_limits<double>::epsilon();
};

int main() {
    printf("%g %g %g %g\n", (double)std::numeric_limits<float>::max(), (double)std::numeric_limits<float>::lowest(),
           (double)std::numeric_limits<float>::epsilon(), (double)std::numeric_limits<float>::min());
    printf("%.17g %.17g %.17g\n", std::numeric_limits<double>::max(), std::numeric_limits<double>::epsilon(),
           std::numeric_limits<double>::denorm_min());
    printf("%d %d %d %d\n", std::numeric_limits<float>::digits, std::numeric_limits<double>::digits,
           (int)std::isinf(K::Inf), K::Inf > K::Big);
    printf("%g %g %.17g\n", (double)K::Inf, (double)K::Big, K::Eps);
    printf("%d %d\n", std::numeric_limits<int>::max(), std::numeric_limits<int>::is_integer);
    double xs[10] = {2.5, -2.5, 0.5, -0.5, 1.49999999999999989, 3.7, -3.7, 1e300, -0.0, 4503599627370497.0};
    for (double x : xs)
        printf("%g %g %ld %lld %g | ", std::round(x), std::trunc(x), std::fabs(x) < 1e18 ? std::lround(x) : 0L,
               std::fabs(x) < 1e18 ? std::llround(x) : 0LL, (double)std::round((float)x));
    printf("\n");
    return 0;
}
