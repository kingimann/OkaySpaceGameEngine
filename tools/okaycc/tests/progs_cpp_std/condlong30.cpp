// The type of c ? a : b with integer branches is the usual arithmetic conversions of the two
// ([expr.cond] p7.3): long when a branch is long. It was always int (silent wrong code once a
// narrowing to int is kept: c ? f() : 0L with a long f() lost its top half).
#include <cstdio>
static long L() { return 1L << 40; }
static unsigned U() { return 4000000000u; }
static int gOn = 1;
int main() {
    int on = gOn;
    long v = on ? L() : 0L;
    long w = on ? L() : 0;
    unsigned long x = on ? (unsigned long)U() * 2 : 1u;
    long y = !on ? 5 : U();  // int and unsigned: unsigned, then long
    std::printf("%d %d %d %d\n", (int)sizeof(on ? L() : 0L), (int)sizeof(on ? 0 : 1u), (int)sizeof(on ? 'a' : 1),
                (int)sizeof(on ? L() : 0));
    std::printf("%ld %ld %lu %ld\n", v, w, x, y);
    std::printf("%ld %u\n", on ? L() + 1 : 0L, on ? U() : 7u);
    return 0;
}
