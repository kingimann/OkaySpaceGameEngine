// if with an init-statement ([stmt.if] p3), round 21 (it was not parsed: "expected ')'").
#include <cstdio>
#include <map>
#include <string>

struct Loud {
    int v;
    Loud(int x) : v(x) { std::printf("make %d\n", v); }
    ~Loud() { std::printf("drop %d\n", v); }
};

int main() {
    std::map<std::string, int> m;
    m["a"] = 1;
    m["b"] = 2;
    if (auto it = m.find("b"); it != m.end()) std::printf("found %d\n", it->second);
    if (auto it = m.find("z"); it != m.end()) std::printf("found z\n");
    else std::printf("no z %lu\n", m.size());
    int n = 0;
    if (n = 5; n > 3) std::printf("n %d\n", n);
    if (Loud l(7); l.v > 5) {
        std::printf("inside %d\n", l.v);
    } else {
        std::printf("else\n");
    }
    std::printf("after\n");
    return 0;
}
