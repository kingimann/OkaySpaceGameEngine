// strtod, strtof, atof (ISO C 7.22.1.3) and getenv (7.22.4.6) from lib/extra.c. Every value here is
// in the exact fast path (at most 15 digits, |exponent| <= 22), so the bits must match glibc.
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void show(const char *s) {
    char *end = nullptr;
    double v = std::strtod(s, &end);
    unsigned long bits;
    std::memcpy(&bits, &v, 8);
    std::printf("[%s] %.17g %016lx rest=[%s]\n", s, v, bits, end);
}

int main() {
    const char *cases[] = {"0",     "1.5",      "-2.25",   "  3.14159", "0.1",     "1e10",  "2.5e-3",
                           "1E+5x", "+.5",      "7.",      "abc",       "-0",      "123456789012345",
                           "1e22",  "1e-22",    "inf",     "-Infinity", "0.000001", "65504.0",
                           "42abc", "1e",       "9.81",    "-1.0e-5"};
    for (const char *c : cases) show(c);
    float f = std::strtof("0.3", nullptr);
    std::printf("%.9g %.17g\n", f, std::atof("12.75"));
    char *nan_end = nullptr;
    double n = std::strtod("nan", &nan_end);
    std::printf("%d %s\n", n != n, nan_end);
    const char *home = std::getenv("OKAY_NO_SUCH_VARIABLE_22");
    std::printf("%s\n", home ? "set" : "unset");
    const char *path = std::getenv("PATH");
    std::printf("%s\n", path && std::strlen(path) > 0 ? "path" : "no path");
    return 0;
}
