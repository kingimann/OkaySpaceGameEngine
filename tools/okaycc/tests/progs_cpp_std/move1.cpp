// Round 18: rvalue references, std::move, move constructors and assignments, forwarding
// references, overloaded function templates, the implicit copy assignment of a class with a vector.
#include <algorithm>
#include <utility>
#include <vector>

extern "C" int printf(const char *fmt, ...);

struct Res {
    int id;
    int *buf;
    Res(int i) : id(i), buf(new int[4]) { buf[0] = i; }
    Res(const Res &o) : id(o.id + 100), buf(new int[4]) {
        buf[0] = o.buf[0];
        printf("copy %d\n", o.id);
    }
    Res(Res &&o) : id(o.id + 1000), buf(o.buf) {
        o.buf = nullptr;
        printf("move %d\n", o.id);
    }
    ~Res() { delete[] buf; }
};

int kind(const Res &) { return 1; }
int kind(Res &&) { return 2; }

template <typename F>
int callTwice(F &&f) {
    return f(1) + f(2);
}

template <typename T>
int pick(T a) { return (int)a; }
template <typename T>
int pick(T a, T b) { return (int)(a + b) * 10; }

struct Surface {
    int w = 0;
    std::vector<float> px;
};

Surface make(int n) {
    Surface s;
    s.w = n;
    for (int i = 0; i < n; ++i) s.px.push_back((float)i);
    return s;
}

int main() {
    Res a(1);
    Res b = a;
    Res c = std::move(a);
    printf("ids %d %d %d buf %d %d\n", b.id, c.id, a.id, a.buf == nullptr, c.buf[0]);
    printf("kind %d %d %d\n", kind(b), kind(std::move(b)), kind(Res(7)));
    int k = 5;
    printf("fwd %d\n", callTwice([&](int v) { return v * k; }));
    printf("pick %d %d\n", pick(3.5), pick(2, 3));
    std::vector<int> v;
    v.push_back(4);
    v.push_back(9);
    std::vector<int> w = std::move(v);
    printf("vec %d %d %d\n", (int)v.size(), (int)w.size(), w[1]);
    std::vector<int> x;
    x = std::move(w);
    printf("vec2 %d %d\n", (int)w.size(), x[0]);
    printf("minmax %d %d\n", std::min(3, 8), std::max(2.5, 1.5) > 2);
    Surface s1 = make(3);
    Surface s2;
    s2 = s1;
    s1.px[0] = 42;
    s2 = make(2);
    s1 = make(5);
    printf("surface %d %d %d %d\n", s2.w, (int)s2.px.size(), (int)s1.px[4], s1.w);
    return 0;
}
