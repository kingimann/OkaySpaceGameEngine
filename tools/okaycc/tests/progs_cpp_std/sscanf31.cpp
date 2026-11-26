// std::sscanf ([c.files], ISO C 7.21.6.2): integers in each base and size, numbers, strings, scan
// sets, %c, %n, '*', widths, literal text, and the result on a mismatch and on empty input.
#include <cstdio>
#include <string>
int main() {
    int w = 0, h = 0;
    std::string label = "1920 x 1080";
    int r = std::sscanf(label.c_str(), "%d x %d", &w, &h);
    std::printf("%d %d %d\n", r, w, h);
    float x = 0, y = 0, z = 0;
    r = std::sscanf("1.5 -2.25e1 3", "%f %f %f", &x, &y, &z);
    std::printf("%d %.2f %.2f %.2f\n", r, x, y, z);
    double d = 0;
    long l = 0;
    unsigned u = 0;
    short sh = 0;
    char c = 0;
    r = sscanf("  -77 0x1F 0755 65000 12345678901 Z", "%lf %x %o %hd %ld %c", &d, &u, &w, &sh, &l, &c);
    std::printf("%d %.1f %u %d %d %ld %c\n", r, d, u, w, (int)sh, l, c);
    char word[16], rest[32], digits[8];
    int n = 0, i1 = 0, i2 = 0, i3 = 0;
    r = sscanf("key=value; tail end", "%15[^=]=%31[^;];%n", word, rest, &n);
    std::printf("%d [%s] [%s] %d\n", r, word, rest, n);
    r = sscanf("12345abc", "%3d%2d%s", &i1, &i2, word);
    std::printf("%d %d %d %s\n", r, i1, i2, word);
    r = sscanf("0x10 010 10", "%i %i %i", &i1, &i2, &i3);
    std::printf("%d %d %d %d\n", r, i1, i2, i3);
    r = sscanf("v 1 2", "v %*d %d", &i1);
    std::printf("%d %d\n", r, i1);
    r = sscanf("abc", "%d", &i1);
    int r2 = sscanf("", "%d", &i1);
    int r3 = sscanf("   ", "%s", word);
    std::printf("%d %d %d\n", r, r2, r3);
    r = sscanf("100%", "%d%%", &i1);
    r2 = sscanf("aabbcc9", "%[a-c]%1[0-9]", word, digits);
    std::printf("%d %d %d %s %s\n", r, r2, i1, word, digits);
    return 0;
}
