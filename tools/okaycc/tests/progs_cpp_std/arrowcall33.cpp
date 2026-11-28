// Round 33: a class's operator-> reached through a call's result, f()->prog->global
// ([over.ref], [expr.ref]); engine/src/JavaScript/JsRegExp.cpp's getters. The call runs once.
#include <cstdio>
#include <memory>
struct Prog {
    bool global = true;
    int flags = 6;
    int Twice() const { return flags * 2; }
};
struct Value {
    int t;
    static Value Bool(bool b) { return Value{b ? 7 : 3}; }
};
struct RegExp {
    std::shared_ptr<Prog> prog;
    struct Inner {
        std::unique_ptr<Prog> p;
    } inner;
};
static RegExp gr;
static int calls = 0;
RegExp* This(int bump) {
    calls += bump;
    return &gr;
}
using Getter = Value (*)(int);
static int Run(Getter g) { return g(1).t; }
auto Deduced() { return Value::Bool(This(1)->prog->global); }
int main() {
    gr.prog = std::make_shared<Prog>();
    gr.inner.p.reset(new Prog());
    gr.inner.p->flags = 11;
    bool b = This(1)->prog->global;
    auto l = [] { return This(1)->prog->flags; };
    int t = Run([](int k) { return Value::Bool(!This(k)->prog->global); });
    This(1)->prog->flags += 4;
    std::printf("%d %d %d %d %d\n", b, Deduced().t, l(), t, This(1)->inner.p->Twice());
    std::printf("calls %d\n", calls);
    return 0;
}
