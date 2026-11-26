// A call of a member object of a call result ([expr.call] p1, [over.call]): world().rectOf(..)
// with rectOf a std::function, as in Components/UIWorldContext.hpp, and through -> and by value.
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
struct W {
    std::function<bool(int, float &)> rectOf;
    int calls = 0;
    int bump(int k) { return calls += k; }
};
struct Adder {
    int base;
    int operator()(int x) const { return base + x; }
};
struct Holder {
    Adder add{10};
    std::function<int(int)> twice = [](int v) { return 2 * v; };
};
static int live = 0;
struct Count {
    int v;
    Count(int x) : v(x) { ++live; }
    Count(const Count &o) : v(o.v) { ++live; }
    ~Count() { --live; }
    int operator()(int k) const { return v * k; }
    int operator+(int k) const { return v + k; }
};
struct Pack {
    Count c{3};
    std::string name = "pack";
};
static Pack makePack() { return Pack(); }
static W gw;
static W &world() { return gw; }
static W *worldPtr() { return &gw; }
static Holder makeHolder() { return Holder(); }
int main() {
    float f = 0;
    int n = 0;
    gw.rectOf = [](int a, float &o) {
        o = a * 1.5f;
        return a > 1;
    };
    if (!world().rectOf(1, f)) n += 1;
    if (world().rectOf(2, f)) n += 10;
    if (worldPtr()->rectOf(3, f)) n += 100;
    std::printf("%d %g %d\n", n, f, world().bump(3));
    // the contextual conversion to bool of a member of a call result ([conv] p4)
    std::function<bool(int)> none;
    W *wp = worldPtr();
    int m = 0;
    if (!world().rectOf) m += 1;
    if (world().rectOf && world().rectOf(5, f)) m += 10;
    if (worldPtr()->rectOf) m += 100;
    if (wp->rectOf || none) m += 1000;
    std::printf("%d\n", m);
    std::printf("%d %d\n", makeHolder().add(5), makeHolder().twice(21));
    auto h = std::make_unique<Holder>();
    std::printf("%d\n", h->add(1) + h->twice(2));
    int a = makePack().c(4);
    int b = makePack().c + 1;
    std::printf("%d %d %d\n", a, b, live);
    std::string s = makePack().name + "!";
    std::printf("%s %d\n", s.c_str(), live);
    return 0;
}
