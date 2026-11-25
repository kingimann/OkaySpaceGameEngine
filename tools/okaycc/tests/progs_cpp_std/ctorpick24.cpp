// Round 24: a copy constructor beats a constructor template that fits as well, also for an
// rvalue ([over.match.best] p2.4); round 23 took the template for W<int>(std::move(a)).
#include <cstdio>
#include <utility>
template <class T> struct W {
    int k;
    W() : k(0) {}
    W(const W<T> &o) : k(o.k + 100) { printf("copy\n"); }
    template <class U> W(const U &u) : k((int)sizeof(U)) { printf("template\n"); }
};
int main() { W<int> a; W<int> b(a); const W<int> &r = a; W<int> c(r); W<int> d(std::move(a)); W<int> e = W<int>(); printf("%d %d %d %d\n", b.k, c.k, d.k, e.k); }
