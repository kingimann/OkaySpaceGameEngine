// Round 32: a condition that declares a variable in while ([stmt.while] p2: made and destroyed on each
// pass; JavaScript/JsRuntime.cpp:1502 `while (GcObj* o = *link)`), and the length of an array declared
// extern without one and then defined (JavaScript/JsLexer.cpp:24, [basic.types.general] p6).
#include <cstdio>
namespace kw {
extern const char* const kWords[];
const char* const kWords[] = {"break", "case", "catch"};
const int kCount = (int)(sizeof(kWords) / sizeof(kWords[0]));
}
static int live = 0, made = 0;
struct Lease {
    int v;
    Lease(int x) : v(x) { ++live; ++made; }
    Lease(const Lease& o) : v(o.v) { ++live; ++made; }
    ~Lease() { --live; }
    explicit operator bool() const { return v > 0; }
};
struct G { G* next; int marked; };
int main() {
    G c{nullptr, 0}, b{&c, 1}, a{&b, 0};
    G* list = &a;
    G** link = &list;
    int n = 0;
    while (G* o = *link) {
        if (o->marked) n += 10;
        link = &o->next;
        n++;
    }
    int k = 4, sum = 0;
    while (Lease l = Lease(k--)) {
        if (l.v == 3) continue;
        sum += l.v;
        if (l.v == 2) std::printf("in loop live %d\n", live);
    }
    std::printf("%d %d %d live %d made %d\n", kw::kCount, n, sum, live, made);
    return 0;
}
