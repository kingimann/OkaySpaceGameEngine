// A narrowing conversion of a parameter or another value held in a register: (unsigned char)c of a
// char parameter c is 255 for -1. Before round 30 the optimizer took the narrowing move for a plain
// copy, so the sign bits stayed (silent wrong code: (int)(unsigned char)c returned -1, and (int)v of
// a long v kept all 64 bits).
#include <cstdio>
static int ToInt(char c) { return (int)(unsigned char)c; }
static unsigned UShort(int v) { return (unsigned short)v; }
static int SShort(unsigned v) { return (short)v; }
static int SChar(int v) { return (signed char)v; }
static unsigned Low(unsigned long v) { return (unsigned)v; }
static int I(long v) { return (int)v; }
static bool One(long v) {
    int i = (int)v;
    return i == 1;
}
static long Back(long v) {
    int i = (int)v;
    return (long)i;
}
static bool IsHigh(char c) {
    unsigned char u = (unsigned char)c;
    return u >= 0x80;
}
struct T {
    static constexpr int to_int(char c) { return (int)(unsigned char)c; }
};
int main() {
    char c = (char)0xff;
    std::printf("%d %d %d\n", ToInt(c), ToInt('A'), T::to_int((char)0xe9));
    std::printf("%u %d %d %u\n", UShort(-1), SShort(0x18000u), SChar(200), Low(0x1234567890ul));
    std::printf("%d %d\n", IsHigh(c), IsHigh('a'));
    std::printf("%d %d %ld\n", I(0x100000001L), One(0x100000001L), Back(0x1ffffffffL));
    int sum = 0;
    for (int i = 250; i < 260; ++i) sum += ToInt((char)i);
    std::printf("%d\n", sum);
    return 0;
}
