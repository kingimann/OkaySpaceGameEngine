// Float and double arguments of a call through a pointer: a virtual function, a function pointer,
// a std::function and a variadic function through a pointer (System V AMD64 ABI 3.2.3: in xmm0-7,
// al their count for a variadic callee). Round 28 left them out: Update(float dt) read 0.
#include <cstdio>
#include <functional>
struct Component {
    virtual ~Component() {}
    virtual void Update(float dt) { std::printf("base %g\n", dt); }
    virtual double Mix(int a, double x, float y, long b) const { return a + x + y + b; }
};
struct Mover : Component {
    float pos = 1.0f;
    void Update(float dt) override {
        pos += dt * 4.0f;
        std::printf("mover %g\n", pos);
    }
    double Mix(int a, double x, float y, long b) const override { return a * x - y * b; }
};
static double half(double d) { return d / 2; }
static float lerp(float a, float b, float t) { return a + (b - a) * t; }
static double many(double a, double b, double c, double d, double e, double f, double g, double h, double i) {
    return a + b + c + d + e + f + g + h + i * 100;
}
int main() {
    Mover m;
    Component *c = &m;
    c->Update(0.25f);
    Component base;
    Component *b = &base;
    b->Update(1.5f);
    std::printf("%g %g\n", c->Mix(2, 3.5, 0.5f, 4), b->Mix(2, 3.5, 0.5f, 4));
    double (*h)(double) = half;
    float (*l)(float, float, float) = lerp;
    double (*mn)(double, double, double, double, double, double, double, double, double) = many;
    std::printf("%g %g %g\n", h(3.0), l(1.0f, 3.0f, 0.25f), mn(1, 2, 3, 4, 5, 6, 7, 8, 9));
    int (*pf)(const char *, ...) = std::printf;
    pf("%g %d %g\n", 2.5, 7, 0.125);
    std::function<float(float)> sq = [](float x) { return x * x; };
    std::printf("%g\n", sq(1.5f));
    return 0;
}
