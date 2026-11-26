// Round 24: decltype ([dcl.type.decltype]): of a name, of an expression, of a call and of a
// member; a reference from a parenthesized-free lvalue expression; const decltype(x); a trailing
// return type decltype(a * b); decltype(auto) as a return type (deduced like auto here).
#include <cstdio>
#include <string>
struct P { int x; double y; };
static long twice(long v) { return v * 2; }
static auto mul(int a, double b) -> decltype(a * b) { return a * b; }
static decltype(auto) name() { return std::string("tag"); }
int main() {
    int i = 3;
    decltype(i) j = 4;
    decltype(i + 1.5) d = 2.25;
    decltype(twice(1)) l = 1L << 40;
    P p;
    p.x = 7;
    p.y = 0.5;
    decltype(p.y) py = p.y * 3;
    int arr[3] = {1, 2, 3};
    decltype(arr[0]) ref = arr[1];
    ref = 20;
    const decltype(j) cj = 9;
    printf("%d %.2f %ld %.1f %d %d %d %d\n", j, d, l, py, arr[1], cj, (int)sizeof(decltype(d)), (int)sizeof(decltype(l)));
    printf("%.1f %s\n", mul(3, 1.5), name().c_str());
}
