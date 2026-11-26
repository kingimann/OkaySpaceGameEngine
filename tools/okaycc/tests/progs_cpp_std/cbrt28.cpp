// Round 28: <cmath> cbrt (Render/Mesh.hpp:411). Printed to 12 digits: glibc's cbrt may be one unit in
// the last place off (123456.789 gives ...7446 correctly rounded, glibc ...746), ours rounds right.
#include <cmath>
#include <cstdio>
int main() {
    double xs[] = {27.0, -8.0, 2.0, 1e-300, 1e300, 0.001, 123456.789, -0.0, 0.0};
    for (double x : xs) std::printf("%.12g\n", std::cbrt(x));
    std::printf("%.7g %.7g\n", (double)std::cbrt(64.0f), (double)std::cbrt((float)1000.5));
    std::printf("%d %d\n", (int)std::isinf(std::cbrt(INFINITY)), (int)std::isnan(std::cbrt(NAN)));
    return 0;
}
