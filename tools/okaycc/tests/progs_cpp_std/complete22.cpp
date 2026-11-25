// A member function body is a complete-class context ([class.mem] p7): new C(..) inside a member of
// C ran no constructor before round 22 (okay-cc printed "0 200", g++ "ctor 2" and "2 204").
#include <cstdio>
struct N {
    int v;
    N(int x) : v(x) { std::printf("ctor %d\n", x); }
    N *clone() const { return new N(v + 1); }
    int twice() const { return later(v) * 2; }
    int later(int x) const { return x + 100; }
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
    std::printf("%d %d\n", c->v, c->twice());
    H<int> h(3);
    H<int> *d = h.clone();
    std::printf("%d\n", d->v);
    return 0;
}
