// == and != of two maps ([container.reqmts], [tab:container.req]), as in Core/SaveFramework.hpp.
#include <cstdio>
#include <map>
#include <string>
struct Block {
    std::map<std::string, std::string> values;
    bool operator==(const Block &o) const { return values == o.values; }
    bool operator!=(const Block &o) const { return !(*this == o); }
};
int main() {
    Block a, b;
    a.values["hp"] = "i:10";
    b.values["hp"] = "i:10";
    std::printf("%d %d\n", a == b, a != b);
    b.values["mp"] = "i:3";
    std::printf("%d %d\n", a == b, a != b);
    a.values["mp"] = "i:4";
    std::printf("%d\n", a == b);
    a.values["mp"] = "i:3";
    std::map<int, int> x{{1, 2}, {3, 4}}, y{{1, 2}, {3, 5}};
    std::printf("%d %d %d\n", a == b, x == y, x != y);
    return 0;
}
