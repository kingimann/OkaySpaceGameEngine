// A lambda that owns a copy (a class temporary) given to a constructor taking std::function by
// value ([over.ics.user]; Core/Clock.hpp's Stopwatch).
#include <cstdio>
#include <functional>
#include <string>
struct Watch {
    std::function<double()> f;
    explicit Watch(std::function<double()> g) : f(std::move(g)) {}
};
struct W2 {
    std::function<double()> f;
    W2(const std::function<double()>& g) : f(g) {}
};
static double use(std::function<double()> g) { return g(); }
int main() {
    std::string name = "abc";
    std::printf("%.1f\n", use([name] { return (double)name.size(); }));
    W2 w2([name] { return (double)name.size(); });
    Watch w([name] { return (double)name.size(); });
    Watch w3 = Watch([name] { return (double)name.size(); });
    std::printf("%.1f %.1f %.1f\n", w.f(), w2.f(), w3.f());
    return 0;
}
