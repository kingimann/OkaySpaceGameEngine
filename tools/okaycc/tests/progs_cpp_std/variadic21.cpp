// Variadic templates ([temp.variadic]): parameter packs, pack expansions, sizeof..., fold expressions.
extern "C" int printf(const char *fmt, ...);
#include <utility>

struct P {
    int a, b, c;
    P() : a(0), b(0), c(0) {}
    P(int x) : a(x), b(0), c(0) {}
    P(int x, int y) : a(x), b(y), c(0) {}
    P(int x, int y, int z) : a(x), b(y), c(z) {}
};

template <typename T, typename... Args>
T *make(Args &&...args) {
    return new T(std::forward<Args>(args)...);
}

template <typename... Args>
int count(Args... args) {
    return (int)sizeof...(Args);
}

template <typename... Args>
long sum(Args... args) {
    return (0 + ... + args);
}

template <typename... Args>
long sumR(Args... args) {
    return (args + ... + 0);
}

template <typename... Args>
bool all(Args... args) {
    return (... && args);
}

static int seen = 0;
void note(int v) { seen = seen * 10 + v; }

template <typename... Args>
void each(Args... args) {
    (note(args), ...);
}

int add3(int a, int b, int c) { return a * 100 + b * 10 + c; }

template <typename... Args>
int fwd(Args &&...args) {
    return add3(std::forward<Args>(args)...);
}

template <typename F, typename... Args>
int callWith(F f, Args... args) {
    return f(args..., 7);
}

int two(int a, int b) { return a - b; }

struct Box {
    int v = 0;
    template <typename... A>
    void set(A... a) {
        v = (int)sizeof...(a) * 1000 + (0 + ... + a);
    }
};

template <typename T>
struct Holder {
    T items[4];
    int n = 0;
    template <typename... A>
    void emplace(A &&...a) {
        items[n++] = T(std::forward<A>(a)...);
    }
};

int main() {
    P *p0 = make<P>();
    P *p1 = make<P>(4);
    P *p3 = make<P>(1, 2, 3);
    int x = 5;
    P *p2 = make<P>(x, 6);
    printf("%d %d %d | %d | %d %d %d | %d %d\n", p0->a, p0->b, p0->c, p1->a, p3->a, p3->b, p3->c, p2->a, p2->b);
    delete p0;
    delete p1;
    delete p2;
    delete p3;
    printf("%d %d %d %d\n", count(), count(1), count(1, 2.0, 'c'), count(1, 2, 3, 4, 5, 6));
    printf("%ld %ld %ld %ld\n", sum(), sum(1, 2, 3), sum(10, 20, 30, 40, 50), sumR(4, 5));
    printf("%d %d %d\n", (int)all(), (int)all(1, 2, 3), (int)all(1, 0, 3));
    each(1, 2, 3);
    printf("%d\n", seen);
    int a = 1;
    printf("%d %d\n", fwd(a, 2, 3), callWith(two, 10));
    Box b;
    b.set(1, 2, 3);
    printf("%d\n", b.v);
    b.set();
    printf("%d\n", b.v);
    Holder<P> h;
    h.emplace(1, 2);
    h.emplace(9);
    h.emplace();
    printf("%d %d %d %d %d\n", h.n, h.items[0].a, h.items[0].b, h.items[1].a, h.items[2].a);
    return 0;
}
