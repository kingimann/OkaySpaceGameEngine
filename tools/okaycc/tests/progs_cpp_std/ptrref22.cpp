// ++, --, += on a pointer reached through a reference or a member move it by the size of what it
// points to ([expr.pre.incr], [expr.post.incr]). Before round 22 ++ on an int *& moved it one byte
// (the program printed 335544320 for 20).
#include <cstdio>

struct S {
    int *p;
    double *d;
};
static void bump(int *&p) { ++p; }
static void bump2(int *&p) { p++; }
static void bump3(int *&p) { p += 1; }
static int *post(int *&p) { return p++; }
static int *postDown(int *&p) { return p--; }
template <typename It>
void tb(It &p) {
    ++p;
}

int main() {
    int a[6] = {10, 20, 30, 40, 50, 60};
    double ds[3] = {1.5, 2.5, 3.5};
    int *p = a;
    bump(p);
    std::printf("%d\n", *p);
    bump2(p);
    std::printf("%d\n", *p);
    bump3(p);
    std::printf("%d\n", *p);
    int *old = post(p);
    std::printf("%d %d\n", *old, *p);
    old = postDown(p);
    std::printf("%d %d\n", *old, *p);
    int *q = a;
    tb(q);
    std::printf("%d\n", *q);
    S s{a, ds};
    ++s.p;
    s.p++;
    ++s.d;
    int *was = s.p++;
    std::printf("%d %d %g\n", *was, *s.p, *s.d);
    --s.p;
    s.d--;
    std::printf("%d %g\n", *s.p, *s.d);
    return 0;
}
