// ==, != and < of vectors ([container.reqmts]): element by element, < in lexicographical order,
// also of a vector of pairs of strings in a class's own == (Inventory/ItemDatabase.hpp:20).
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
struct D {
    float dur = -1;
    unsigned long uid = 0;
    std::vector<std::pair<std::string, std::string>> values;
    bool operator==(const D &o) const { return dur == o.dur && uid == o.uid && values == o.values; }
    bool operator!=(const D &o) const { return !(*this == o); }
};
int main() {
    std::vector<int> a{1, 2, 3}, b{1, 2, 3}, c{1, 2}, e{1, 3};
    std::vector<std::pair<std::string, std::string>> p{{"a", "b"}}, q{{"a", "b"}}, r{{"a", "c"}};
    D x, y;
    x.values = p;
    y.values = q;
    std::printf("%d %d %d %d %d %d %d\n", a == b, a == c, a != c, p == q, p == r, x == y, x != y);
    y.values = r;
    std::printf("%d %d %d %d %d\n", x == y, c < a, a < c, a < e, e < a);
    return 0;
}
