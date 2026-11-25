// Round 24: std::nextafter for double and float (C 7.12.11.3): up, down, from zero both ways,
// towards itself, across the largest value, below one, as Core/Random.hpp uses it.
#include <cmath>
#include <cstdio>
int main() {
    double a = std::nextafter(1.0, 2.0) - 1.0;
    double b = 1.0 - std::nextafter(1.0, 0.0);
    double c = std::nextafter(0.0, 1.0);
    double d = std::nextafter(0.0, -1.0);
    float e = std::nextafter(1.0f, 0.0f);
    float f = std::nextafter(0.0f, 1.0f);
    float g = std::nextafter(3.0f, 3.0f);
    printf("%.20g %.20g %.5g %.5g %.10f %.5g %.1f\n", a, b, c, d, (double)e, (double)f, (double)g);
}
