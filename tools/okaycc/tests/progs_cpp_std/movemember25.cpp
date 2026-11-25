// A class with a member whose copy constructor is deleted ([class.copy.ctor] p10): its own copy
// constructor is deleted and its implicit move constructor moves the member, so a vector of it
// can grow (round 24 made a copy constructor that called the deleted one: an undefined symbol).
#include <cstdio>
#include <utility>
#include <vector>
struct M {
    int *p;
    M() : p(nullptr) {}
    explicit M(int v) : p(new int(v)) {}
    M(const M &) = delete;
    M(M &&o) : p(o.p) { o.p = nullptr; }
    M &operator=(M &&o) {
        delete p;
        p = o.p;
        o.p = nullptr;
        return *this;
    }
    ~M() { delete p; }
};
struct W {
    M m;
    int k = 0;
};
struct D : W {
    M n;
};
int main() {
    W w;
    w.m = M(4);
    std::printf("%d\n", *w.m.p);
    std::vector<W> v;
    for (int i = 0; i < 9; ++i) {
        W x;
        x.m = M(i * 3);
        x.k = i;
        v.push_back(std::move(x));
    }
    int s = 0;
    for (auto &e : v) s += *e.m.p + e.k;
    std::printf("%d %d\n", s, (int)v.size());
    D d;
    d.n = M(8);
    d.m = M(9);
    D e = std::move(d);
    std::printf("%d %d %d\n", *e.n.p, *e.m.p, d.n.p == nullptr);
}
