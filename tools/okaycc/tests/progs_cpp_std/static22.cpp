// Static local objects with an initializer ([stmt.dcl] p3): "static T x = e;" and "static T x{a};"
// are built once, the first time through. Before round 22: "a static local object cannot have an
// initializer".
#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

static std::string &Name() {
    static std::string n = std::string("first") + "-call";
    return n;
}
static std::atomic<unsigned long> &Calls() {
    static std::atomic<unsigned long> c{5};
    return c;
}
static std::vector<int> &Table() {
    static std::vector<int> t = {1, 2, 3};
    return t;
}
static int Counter() {
    static std::atomic<int> k(100);
    return ++k;
}

int main() {
    Name() += "!";
    std::printf("%s %s\n", Name().c_str(), Name().c_str());
    ++Calls();
    ++Calls();
    std::printf("%lu\n", (unsigned long)Calls().load());
    Table().push_back(4);
    std::printf("%d %d\n", (int)Table().size(), Table()[3]);
    int a = Counter();
    int b = Counter();
    std::printf("%d %d\n", a, b);
    return 0;
}
