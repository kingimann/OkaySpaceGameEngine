/* Round 18 regressions, each wrong before: a double parameter changed on one path and then passed
 * to a call (copy propagation took the branch's copy); -s.hi on a double field (integer
 * negation); a 16-byte struct returned in two registers passed straight to another call; a long
 * expression passed to a long parameter (the checker called it int and cut it to 32 bits); a float
 * field stored through a pointer. */
int printf(char *fmt, ...);
typedef struct {
    double hi;
    double lo;
} dd;
typedef struct {
    int a;
    float w;
} fw;
static dd mk(double a, double b) {
    dd r;
    r.hi = a;
    r.lo = b;
    return r;
}
static dd neg(dd x) { return mk(-x.hi, -x.lo); }
static dd add2(dd x, dd y) { return mk(x.hi + y.hi, x.lo + y.lo); }
static double first(dd x) { return x.hi; }
static unsigned long keep(unsigned long v) { return v >> 40; }
static void setw(fw *p) { p->w = 2.5f; }
double f(double x, double y) {
    int n = 0;
    if (x < 0) {
        n = 1;
        x = -x;
    }
    if (x == 0.0) return n ? -1 : 1;
    return first(mk(x, 0)) * y;
}
int main() {
    dd a = mk(1.5, 0.25);
    dd b = add2(a, neg(mk(0.5, 0.125)));
    unsigned long u = 3;
    fw pair[2];
    pair[0].a = 7;
    pair[1].a = 9;
    setw(&pair[0]);
    printf("%ld %ld %ld %ld\n", (long)(f(3.0, 2.0) * 10), (long)(f(-3.0, 2.0) * 10), (long)(b.hi * 100),
           (long)(b.lo * 1000));
    printf("%ld %d %d %ld\n", (long)keep((u << 44) | 4503599627370495ul), pair[1].a, pair[0].a,
           (long)(pair[0].w * 10));
    return 0;
}
