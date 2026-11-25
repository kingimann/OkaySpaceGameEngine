// auto x = e; for a class with a copy constructor or a destructor: copied from an object, taken over from
// a temporary, destroyed at the end of its block. Before round 22: "'auto' for a class with a destructor
// is not supported".
#include <cstdio>
#include <string>
#include <vector>
struct Noisy {
    int v;
    Noisy(int x) : v(x) { std::printf("make %d\n", v); }
    Noisy(const Noisy &o) : v(o.v + 100) { std::printf("copy %d\n", v); }
    ~Noisy() { std::printf("drop %d\n", v); }
};
static Noisy mk(int k) { return Noisy(k); }
static std::string name(int i) { return std::string("n") + std::to_string(i); }
static Noisy keep(int k) {
    auto n = Noisy(k);
    n.v += 1;
    return n;
}
int main() {
    Noisy a(1);
    {
        auto b = a;
        auto c = mk(2);
        auto d = Noisy(3);
        std::printf("in %d %d %d\n", b.v, c.v, d.v);
    }
    auto s = name(7);
    auto t = s;
    t += "x";
    std::vector<int> v1 = {1, 2, 3};
    auto v2 = v1;
    v2.push_back(4);
    auto k = keep(5);
    std::printf("%s %s %d %d %d\n", s.c_str(), t.c_str(), (int)v1.size(), (int)v2.size(), k.v);
    return 0;
}
