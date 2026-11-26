// Round 32: sizeof of an expression in a global initializer ([expr.sizeof], JavaScript/JsLexer.cpp:24), a range-for
// over a braced list of addresses with auto * ([stmt.ranged], JsRuntime.cpp:138), ++ and -- of float and double
// ([expr.pre.incr], JsLibArray.cpp:147), and c ? base * : derived * ([expr.cond] p7, JsInterpreter.cpp:1203).
#include <cstdio>
#include <vector>
struct Obj { int k; };
struct Fn : Obj { int extra; };
const char* const kWords[] = {"a", "bb", "ccc", "dd"};
const int kCount = (int)(sizeof(kWords) / sizeof(kWords[0]));
int main() {
    std::vector<int> a{1, 2}, b{3};
    int n = 0;
    for (auto* list : {&a, &b})
        for (int x : *list) n += x;
    double s = 0;
    for (double i = 3.5; i >= 0; --i) s += i;
    float f = 1.5f;
    float g = f++;
    double d = 2.0;
    double e = d--;
    Fn fn; fn.k = 7;
    Obj other{9};
    bool pick = n > 100;
    Obj* o = pick ? &other : &fn;
    std::printf("%d %d %g %g %g %g %g %d\n", kCount, n, s, f, g, d, e, o->k);
}
