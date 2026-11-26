// An array element or a member of class type initialized by a class value in a braced list
// (S a[2] = {S{1}, make()}) gets the value's bytes. Before round 30 it got the value's address
// (silent wrong code).
#include <cstdio>
#include <string>
struct Add {
    int n;
};
struct P {
    int x, y;
};
struct Big {
    long a, b, c;
};
struct W {
    Add a;
    int k;
    P p;
};
static P makeP(int v) { return P{v, v + 1}; }
static Big makeBig(long v) { return Big{v, v * 2, v * 3}; }
int main() {
    Add arr[2] = {Add{1}, Add{3}};
    P ps[3] = {P{1, 2}, makeP(5), {7, 8}};
    Add b[2] = {Add{5}};
    Big bs[2] = {makeBig(2), Big{4, 5, 6}};
    W w = {Add{4}, 2, makeP(10)};
    std::string ss[2] = {std::string("one"), "two"};
    std::printf("%d %d %d %d\n", arr[0].n, arr[1].n, b[0].n, b[1].n);
    std::printf("%d %d %d %d %d %d\n", ps[0].x, ps[0].y, ps[1].x, ps[1].y, ps[2].x, ps[2].y);
    std::printf("%ld %ld %ld %ld %ld %ld\n", bs[0].a, bs[0].b, bs[0].c, bs[1].a, bs[1].b, bs[1].c);
    std::printf("%d %d %d %d\n", w.a.n, w.k, w.p.x, w.p.y);
    std::printf("%s %s\n", ss[0].c_str(), ss[1].c_str());
    return 0;
}
