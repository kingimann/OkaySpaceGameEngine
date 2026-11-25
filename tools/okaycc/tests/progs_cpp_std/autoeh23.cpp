// Round 23: an "auto x = e;" local in a function that also has an object with a destructor. Its
// scope end and every return popped an exception-unwinding record although none was pushed for it,
// so the record of the caller's object was lost: "~outer" was missing when the exception unwound
// (and Core/Log.hpp crashed on its second EffectiveLevel call).
#include <cstdio>
#include <map>
struct G {
    const char *n;
    explicit G(const char *s) : n(s) {}
    ~G() { std::printf("~%s\n", n); }
};
static std::map<int, int> m;
static int f(int k) {
    G a("a");
    auto it = m.find(k);
    return it != m.end() ? it->second : -1;
}
static int g(int k) {
    G b("b");
    {
        auto it = m.find(k);
        if (it == m.end()) return 0;
    }
    return 1;
}
int main() {
    m[1] = 10;
    try {
        G outer("outer");
        int a = f(1);
        int b = f(2);
        int c = g(1);
        int d = g(3);
        std::printf("%d %d %d %d\n", a, b, c, d);
        throw 7;
    } catch (int e) {
        std::printf("caught %d\n", e);
    }
    return 0;
}
