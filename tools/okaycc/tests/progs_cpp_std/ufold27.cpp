// Constants of unsigned types are compared, divided and shifted as unsigned values
// ([expr.arith.conv], [expr.const]). Round 26 folded them as signed numbers: (unsigned long)-1 <
// 0ul was true, so numeric_limits<unsigned long>::digits was 63 and is_signed was true (silent
// wrong code), and a signed >> of two constants shifted in zeros.
#include <cstdio>
#include <limits>
struct K {
    static constexpr bool neg = (unsigned int)(-1) < (unsigned int)(0);
    static constexpr int bits = (int)sizeof(unsigned long) * 8 - ((unsigned long)(-1) < (unsigned long)(0) ? 1 : 0);
    static constexpr unsigned long big = 0xffffffffffffffffUL / 3;
    static constexpr unsigned int half = 0xffffffffu >> 1;
    static constexpr int sh = -16 >> 2;
    static constexpr long mix = 1u < -1L ? 1 : 2;  // unsigned int converts to long: 1 < -1 is false
};
int main() {
    constexpr int z = ((int)sizeof(unsigned long) * 8 - ((unsigned long)(-1) < (unsigned long)(0) ? 1 : 0)) * 30103 / 100000;
    std::printf("%d %d %d %d\n", std::numeric_limits<unsigned long>::digits, (int)std::numeric_limits<unsigned long>::is_signed,
                std::numeric_limits<unsigned int>::digits, std::numeric_limits<unsigned long>::digits10);
    std::printf("%d %d %d %lu %u %d %ld\n", z, (int)K::neg, K::bits, K::big, K::half, K::sh, K::mix);
    unsigned long a = (unsigned long)-1 / 2;
    long b = -64 >> 3;
    unsigned c = (unsigned)-8 >> 1;
    std::printf("%lu %ld %u %d %d\n", a, b, c, (unsigned)-1 > 5u, -1 < 5);
    return 0;
}
