// A class with a destructor or a copy constructor passed by value ([expr.call] p7; Itanium C++ ABI
// 3.1.2.3): the parameter object is destroyed once, and what the callee does to it is what gets
// destroyed. Round 28 never destroyed a parameter made from a temporary (Tracked(3)), and gave the
// callee a copy of the bytes of the caller's object (a pointer the callee replaced was freed twice).
#include <cstdio>
#include <string>
#include <utility>
static int live = 0;
struct Tracked {
    int v;
    Tracked(int x) : v(x) { ++live; }
    Tracked(const Tracked &o) : v(o.v) { ++live; }
    ~Tracked() { --live; }
};
struct Box {
    int *p;
    Box(int x) : p(new int(x)) { ++live; }
    Box(const Box &o) : p(new int(*o.p)) { ++live; }
    ~Box() {
        std::printf("free %d\n", *p);
        delete p;
        --live;
    }
};
static int use(Tracked t) { return t.v; }
template <class F>
static int tuse(F f) { return f.v; }
static int grow(Box b) {
    delete b.p;
    b.p = new int(99);
    return *b.p;
}
static size_t longer(std::string s) {
    s += " and a much longer tail that leaves the small buffer behind";
    return s.size();
}
static std::string keep;
static void take(std::string s) { keep = std::move(s); }
static Tracked make(int v) { return Tracked(v); }
static int many(int a, int b, int c, int d, int e, int f, Tracked t, Box x) { return a + b + c + d + e + f + t.v + *x.p; }
int main() {
    int r = use(Tracked(3));
    std::printf("a %d %d\n", r, live);
    Tracked k(2);
    r = use(k);
    std::printf("b %d %d\n", r, live);
    r = tuse(Tracked(4));
    std::printf("c %d %d\n", r, live);
    r = tuse(k);
    std::printf("d %d %d\n", r, live);
    r = use(make(7));
    std::printf("e %d %d\n", r, live);
    {
        Box bx(2);
        r = grow(bx);
        std::printf("f %d %d\n", r, live);
        r = grow(Box(5));
        std::printf("g %d %d\n", r, live);
        r = many(1, 2, 3, 4, 5, 6, Tracked(7), bx);
        std::printf("h %d %d\n", r, live);
    }
    std::string t = "x";
    size_t n = longer(t);
    std::printf("i %zu %s\n", n, t.c_str());
    n = longer(std::string("yy"));
    std::printf("j %zu\n", n);
    take(std::string("a string long enough to live on the heap, not in the object"));
    std::printf("k %s\n", keep.c_str());
    std::printf("l %d\n", live);
    return 0;
}
