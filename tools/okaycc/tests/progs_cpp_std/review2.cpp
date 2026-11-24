// Fixes from the review of round 18 (each wrong before): overload ranking with a float argument,
// std::forward of an lvalue, std::move of a const object, member calls and member access on
// temporaries with destructors, sin of a huge argument, signed zeros, std::queue::pop releasing.
#include <cmath>
#include <queue>
#include <string>
#include <utility>

extern "C" int printf(const char *fmt, ...);

int f(int) { return 1; }
int f(long) { return 2; }
int f(double) { return 3; }
int k(long) { return 4; }
int k(double) { return 5; }

struct B {
    int v = 1;
    B() {}
    B(const B &o) : v(o.v + 10) {}
    B(B &&o) : v(o.v + 100) { o.v = -1; }
};
int g(const B &) { return 6; }
int g(B &&) { return 7; }
template <typename T>
int w(T &&x) {
    return g(std::forward<T>(x));
}

int live = 0;
struct S {
    int n;
    int *buf;
    S(int v) : n(v), buf(new int[2]) {
        buf[0] = v;
        ++live;
    }
    S(const S &o) : n(o.n), buf(new int[2]) {
        buf[0] = o.buf[0];
        ++live;
    }
    ~S() {
        buf[0] = -999;
        delete[] buf;
        --live;
    }
    int plus(int k2) const { return buf[0] + k2; }
    S plusS(int k2) const { return S(buf[0] + k2); }
};

// A class used inside its own member functions (copy, construct, destroy, return by value),
// and main falling off its end (returns 0).
int cdt = 0;
struct C {
    int v;
    C(int x) : v(x) {}
    C(const C &o) : v(o.v + 1) {}
    ~C() { ++cdt; }
    C dup() const {
        C r = *this;
        return r;
    }
    int mk() {
        C r(7);
        C q = r;
        return r.v * 10 + q.v;
    }
};

int bigLive = 0;
struct Big {
    std::string s;
    Big() { ++bigLive; }
    Big(const char *t) : s(t) { ++bigLive; }
    Big(const Big &o) : s(o.s) { ++bigLive; }
    ~Big() { --bigLive; }
};

int main() {
    printf("rank %d %d %d\n", f(1.5f), k(1.5f), f((short)2));
    B b;
    printf("forward %d %d\n", w(b), w(B()));
    const B cb;
    B moved = std::move(cb);
    printf("constmove %d %d\n", moved.v, cb.v);
    printf("temp %d %d\n", S(2).plus(3), S(7).plusS(1).n);
    printf("live %d\n", live);
    std::string cat = std::string("ab") + "cd";
    printf("string %s %d\n", cat.c_str(), (int)(std::string("xy") + "z").size());
    double s = std::sin(1e19);
    printf("bigsin %d %d\n", (s >= -1.0 && s <= 1.0) || s != s, std::cos(-1e300) == std::cos(-1e300) || 1);
    printf("zeros %d %d %d %d %d\n", std::signbit(std::floor(-0.0)), std::signbit(std::ceil(-0.0)),
           std::signbit(std::pow(-0.0, 3.0)), std::pow(-0.0, -1.0) < 0, std::signbit(std::ceil(-0.25)));
    std::queue<Big> q;
    for (int i = 0; i < 100; ++i) q.push(Big("payload"));
    for (int i = 0; i < 95; ++i) q.pop();
    printf("queue %d %s\n", (int)q.size(), q.front().s.c_str());
    // A queue that never empties must still release what it popped.
    std::queue<Big> q2;
    for (int i = 0; i < 20000; ++i) {
        q2.push(Big("x"));
        if (q2.size() > 5) q2.pop();
    }
    printf("bounded %d %d\n", (int)q2.size(), bigLive < 400);
    {
        C a(1);
        int m = a.mk();
        int d = a.dup().v;
        printf("self %d %d %d\n", m, d, cdt);
    }
}
