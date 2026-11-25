// Our own <charconv> and <cwctype>.
#include <charconv>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <system_error>

static void readInt(const char *s, int base = 10) {
    int v = -1;
    auto r = std::from_chars(s, s + std::strlen(s), v, base);
    std::printf("int [%s] %d used=%d ok=%d range=%d\n", s, v, (int)(r.ptr - s), r.ec == std::errc(),
                r.ec == std::errc::result_out_of_range);
}
static void readDouble(const char *s) {
    double v = -1;
    auto r = std::from_chars(s, s + std::strlen(s), v);
    std::printf("dbl [%s] %.17g used=%d ok=%d\n", s, v, (int)(r.ptr - s), r.ec == std::errc());
}

int main() {
    readInt("123");
    readInt("-42xyz");
    readInt("+5");
    readInt("ff", 16);
    readInt("2147483648");
    readInt("-2147483648");
    readInt("");
    readDouble("3.25");
    readDouble("-0.5e3rest");
    readDouble("1e10");
    readDouble(" 1");
    readDouble("abc");
    unsigned long big = 0;
    const char *t = "18446744073709551615";
    auto r = std::from_chars(t, t + std::strlen(t), big);
    std::printf("%lu %d\n", big, r.ec == std::errc());
    char buf[64];
    auto w = std::to_chars(buf, buf + sizeof(buf), -12345);
    *w.ptr = 0;
    std::printf("%s\n", buf);
    w = std::to_chars(buf, buf + sizeof(buf), 255u, 16);
    *w.ptr = 0;
    std::printf("%s\n", buf);
    const double ds[] = {0.1, 1.5, 100.0, 1e20, 1.0 / 3.0, 2.5e-5, 123456.75};
    for (double d : ds) {
        w = std::to_chars(buf, buf + sizeof(buf), d);
        *w.ptr = 0;
        std::printf("%s\n", buf);
    }
    w = std::to_chars(buf, buf + 3, 123456);
    std::printf("%d\n", w.ec == std::errc::value_too_large);
    std::printf("%d %d %d %c %c\n", std::iswspace(L' ') != 0, std::iswdigit(L'7') != 0, std::iswalpha(L'!') != 0,
                (char)std::towupper(L'q'), (char)towlower(L'Q'));
    return 0;
}
