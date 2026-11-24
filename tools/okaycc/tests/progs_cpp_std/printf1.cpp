// Round 19: our printf (lib/printf.c) prints %f and %e exactly as glibc does: the decimal expansion
// of the double, rounded half to even. Also precision, flags, %lu, %lx, inf and nan.
extern "C" int printf(const char *fmt, ...);

int main() {
    const double vals[12] = {0.0, -0.0, 1.0, 0.1, 2.5, 3.5, 0.125, 1e300, 1e-300, 123456789.987654321, -2.675,
                             4.9406564584124654e-324};
    for (int i = 0; i < 12; ++i) {
        double v = vals[i];
        printf("%f|%.0f|%.2f|%.17f|%e|%.0e|%.3e|%.25e|%12.3f|%-12.2e|%+.1f|%08.2f\n", v, v, v, v, v, v, v, v, v, v,
               v, v);
    }
    float fl = 3.25f;
    printf("float %.2f %e %.1f\n", fl, fl * 2, (float)(fl / 3));  // a float to "..." becomes a double
    printf("%.40f\n", 0.1);
    printf("%.0f %.0f %.0f %.0f\n", 0.5, 1.5, 2.5, -0.5);
    printf("%.320f\n", 1e-300);
    printf("%.1f\n", 1e300);
    volatile double zero = 0.0;
    printf("%f %e %5.1f|%-6f|%E\n", 1.0 / zero, -1.0 / zero, zero / zero, 1.0 / zero, 1.0 / zero);
    printf("%lu %lx %ld %u %x %05d %-4d| %+d %s %c %%\n", 18446744073709551615ul, 3735928559ul, -9000000000l,
           4000000000u, 255u, -42, 7, 9, "str", 'q');
    return 0;
}
