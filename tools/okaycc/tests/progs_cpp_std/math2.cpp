// Round 19: tan, atan, atan2, asin, acos, log2 (lib/m.c), printed to 12 significant digits so a
// last-bit difference from glibc (whose results are not always correctly rounded) does not show.
#include <cmath>

extern "C" int printf(const char *fmt, ...);

int main() {
    for (int i = -4; i <= 4; ++i) {
        double x = i * 0.45;
        printf("%d tan %.12e atan %.12e asin %.12e acos %.12e\n", i, std::tan(x), std::atan(x * 3),
               std::asin(x / 2), std::acos(x / 2));
    }
    const double ys[5] = {1.0, -1.0, 0.0, -0.0, 3.0};
    const double xs[5] = {1.0, -2.0, -1.0, -1.0, 0.0};
    for (int i = 0; i < 5; ++i) printf("atan2 %.12e\n", std::atan2(ys[i], xs[i]));
    for (int i = 1; i <= 6; ++i)
        printf("log2 %.12e %.6e\n", std::log2(i * 1.7), (double)std::log2((float)(i * 3)));
    printf("float %.6e %.6e %.6e\n", (double)std::tan(0.5f), (double)std::atan2(1.0f, 2.0f), (double)std::acos(0.25f));
    printf("exact %.17e %.17e %.17e\n", std::log2(1024.0), std::acos(-1.0), std::atan2(0.0, -1.0));
    return 0;
}
