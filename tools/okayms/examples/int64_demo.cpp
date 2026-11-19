// A small program for okay-ms: 64-bit integers, float arrays and constants, default arguments. It returns 0 when
// every check holds, or the number of the first check that failed.
//   tools/okayms/build/okay-cl tools/okayms/examples/int64_demo.cpp /Feint64_demo.exe
//   wine int64_demo.exe; echo $?        (prints 0)
#include <cstdint>

struct Rect { float x, y, w, h; };

std::uint64_t mix(std::uint64_t h, std::uint8_t byte) { return (h ^ byte) * 1099511628211ull; }
Rect make(float x, float y) { return {x, y, x * 2, y * 2}; }
int add(int a, int b = 5) { return a + b; }

int entry(void) {
    long long big = 3000000000LL;
    if (big * 3 != 9000000000LL) return 1;
    if (sizeof(long long) != 8 || sizeof(std::size_t) != 8) return 2;
    unsigned long long all = 0xFFFFFFFFFFFFFFFFull;
    if ((all >> 60) != 15 || all / 3 != 0x5555555555555555ull) return 3;
    std::uint64_t h = 14695981039346656037ull;
    const char* text = "abc";
    for (int i = 0; i < 3; i++) h = mix(h, (std::uint8_t)text[i]);
    if (h != 0xE71FA2190541574Bull) return 4;
    float a[2];
    a[0] = 1.5f;
    a[1] = 2.5f;
    if ((int)(a[1] * 4.0f) != 10) return 5;
    Rect r = make(1.0f, 2.0f);
    if (r.w != 2.0f || r.h != 4.0f) return 6;
    if (add(1) != 6 || add(1, 2) != 3) return 7;
    return 0;
}
