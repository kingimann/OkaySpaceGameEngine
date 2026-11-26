// Round 23: in C++ a comparison, !, && and || have type bool ([expr.rel], [expr.eq],
// [expr.log.and], [expr.unary.op]); they were int. Seen through overloads, sizeof, auto and
// template deduction.
#include <cstdio>
#include <typeinfo>
static const char *f(bool) { return "bool"; }
static const char *f(int) { return "int"; }
template <class T>
static const char *g(T) { return typeid(T).name(); }
int main() {
    int a = 1, b = 2;
    double x = 0.5;
    auto c = a < b;
    auto n = !a;
    std::printf("%s %s %s %s %s\n", f(a < b), f(a == b), f(!b), f(a && b), f(x < 1.0));
    std::printf("%d %d %s %s\n", (int)sizeof(a != b), (int)sizeof(c), g(c), g(n));
    std::printf("%s %s %d\n", g(a > 0 || b > 0), g(a + b), (int)c + (int)n);
    return 0;
}
