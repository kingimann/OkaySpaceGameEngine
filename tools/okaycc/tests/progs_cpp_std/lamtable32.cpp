// Round 32: a constant table of pointers to functions made from lambdas with no captures ([expr.prim.lambda.closure]
// p8, [expr.const]): static const NativeFn fns[3] = {[](..) -> Value {..}, ..} (JavaScript/JsLibString.cpp:370).
#include <cstdio>
struct V { int x; };
using Fn = V (*)(int);
static V Twice(int a) { return V{a * 2}; }
int Use(int i) {
    static const Fn fns[3] = {
        [](int a) -> V { return V{a + 1}; },
        [](int a) -> V { return V{a + 2}; },
        Twice,
    };
    return fns[i](10).x;
}
int main() { std::printf("%d %d %d\n", Use(0), Use(1), Use(2)); return 0; }
