// A local aggregate initialized by a braced list converts each value to the type of its element or
// member ([dcl.init.aggr] p4, [conv.fpint]): float x[2] = {1, 2} holds 1.0f and 2.0f (round 25
// stored the bits of the ints: 1.4e-45).
#include <cstdio>
struct P { float x, y; };
struct Q { double d; int i; bool b; short s; };
struct M { int f() const { return 1; } float a; double b; };
struct V3 {
    float x, y, z;
    bool operator==(const V3 &o) const { return x == o.x && y == o.y && z == o.z; }
};
int main() {
    P one = {1, 2};
    P parr[2] = {{1, 2}, {4, 5}};
    float fa[3] = {1, 2, 'a'};
    double da[2] = {3, -4};
    int ia[2] = {(int)2.5, 7};
    Q qa[1] = {{3, 4, 1, 9}};
    M m = {5, 6};
    V3 arr[2] = {{1, 2, 3}, {4, 5, 6}};
    V3 a{1, 2, 3};
    long long big = 1LL << 40;
    double dd[1] = {(double)big};
    std::printf("%g %g %g %g %g %g %g %d %d\n", one.x, one.y, parr[0].x, parr[1].y, fa[2], da[1], qa[0].d, qa[0].i, qa[0].s);
    std::printf("%g %g %d %d %g %d\n", m.a, m.b, arr[0] == a, arr[1] == a, dd[0], ia[0] + ia[1]);
}
