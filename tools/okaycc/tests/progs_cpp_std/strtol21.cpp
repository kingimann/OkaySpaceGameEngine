// Round 21: strtol, strtoul, strtoll, strtoull, atol (lib/extra.c), <climits> and <cfloat> against glibc.
#include <cfloat>
#include <climits>
#include <cstdio>
#include <cstdlib>

int main() {
    const char *in[] = {"42", "  -17xyz", "+0x1F", "0777", "z", "", "9223372036854775807",
                        "9223372036854775808", "-9223372036854775809", "ff", "0x", "1010", "  12  "};
    int bases[] = {10, 0, 16, 2, 8, 36};
    for (const char *s : in) {
        for (int b : bases) {
            char *end = nullptr;
            long v = std::strtol(s, &end, b);
            char *end2 = nullptr;
            unsigned long u = std::strtoul(s, &end2, b);
            std::printf("[%s] %d: %ld +%ld %lu +%ld\n", s, b, v, (long)(end - s), u, (long)(end2 - s));
        }
    }
    std::printf("%lld %llu %ld\n", std::strtoll("-123", nullptr, 10), std::strtoull("18446744073709551615", nullptr, 10),
                std::atol("  77"));
    std::printf("%d %d %ld %lu %d\n", INT_MIN, INT_MAX, LONG_MIN, ULONG_MAX, CHAR_BIT);
    std::printf("%.9g %.17g %.9g %.17g %d %d\n", (double)FLT_MAX, DBL_MAX, (double)FLT_EPSILON, DBL_MIN, DBL_MANT_DIG,
                FLT_DIG);
    return 0;
}
