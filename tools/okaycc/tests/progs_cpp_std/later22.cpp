// A member function body is a complete-class context ([class.mem] p7): new C(..) inside C runs the
// constructor (silently skipped before round 22), and a nested type declared later is visible.
#include <cstdio>
struct N {
    int v;
    N(int x) : v(x) { std::printf("ctor %d\n", x); }
    N *clone() const { return new N(v + 1); }
    int twice() const { return later(v) * 2; }
    int later(int x) const { return x + 100; }
    int mk() const { Mark m; m.a = v; return m.a + 1000; }
    struct Mark { int a; };
};
template <typename T>
struct H {
    T v;
    H(T x) : v(x) { std::printf("hctor\n"); }
    H<T> *clone() const { return new H<T>(v); }
};
int main() {
    N n(1);
    N *c = n.clone();
    std::printf("%d %d %d\n", c->v, c->twice(), n.mk());
    H<int> h(3);
    H<int> *d = h.clone();
    std::printf("%d\n", d->v);
    return 0;
}
