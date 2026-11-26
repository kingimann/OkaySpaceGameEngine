// numeric_limits ([numeric.limits.members]): quiet_NaN and signaling_NaN (positive NaNs, by their
// bits, as g++ prints them: "nan", not "-nan"), has_quiet_NaN, digits10, round_error and the
// exponent limits of float and double (Core/SaveData.hpp:84 in 14 engine files).
#include <cmath>
#include <cstdio>
#include <limits>
int main() {
    float f = std::numeric_limits<float>::quiet_NaN();
    double d = std::numeric_limits<double>::quiet_NaN();
    float g = -std::numeric_limits<float>::quiet_NaN();
    std::printf("%f %f %f %d %d %d\n", f, d, g, f != f, std::isnan(d) ? 1 : 0, (int)std::numeric_limits<float>::has_quiet_NaN);
    std::printf("%d %d %d %d %d\n", std::numeric_limits<int>::digits10, std::numeric_limits<unsigned long>::digits10,
                std::numeric_limits<short>::digits10, std::numeric_limits<float>::max_exponent10,
                std::numeric_limits<double>::min_exponent);
    std::printf("%g %d %d %d\n", std::numeric_limits<double>::round_error(), (int)std::numeric_limits<int>::has_infinity,
                (int)std::numeric_limits<double>::is_iec559, std::numeric_limits<long>::digits10);
    return 0;
}
