// Generic lambdas (auto parameters, [expr.prim.lambda.closure] p3-p4): an instance of the call
// operator for each argument type; captures by reference and by copy; auto &, const auto &,
// auto && and auto *; called from a template (std::stable_sort); a lambda inside one; this.
#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
struct Tabs {
    std::vector<int> pages{1, 2, 3, 4};
    std::vector<std::string> names{"a", "b", "c", "d"};
    int closed = 0;
    void Close(int index) {
        auto drop = [&](auto& v) {
            if (index < (int)v.size()) v.erase(v.begin() + index);
        };
        drop(pages);
        drop(names);
        auto count = [this](const auto& v) { closed += (int)v.size(); };
        count(pages);
        count(names);
    }
};
struct Node {
    int v;
    Node* next;
};
int main() {
    auto twice = [](auto x) { return x + x; };
    std::printf("%d %.2f %ld\n", twice(3), twice(1.25), twice(5L));
    int base = 10;
    auto addBase = [base](auto x) { return x + base; };
    auto addRef = [&base](const auto& x) { return x * base; };
    base = 20;
    std::printf("%d %d %.1f\n", addBase(1), addRef(2), addRef(0.5));
    std::vector<std::pair<float, float>> keys{{3.0f, 1.0f}, {1.0f, 2.0f}, {2.0f, 3.0f}};
    std::stable_sort(keys.begin(), keys.end(), [](auto& a, auto& b) { return a.first < b.first; });
    for (const auto& k : keys) std::printf("(%g %g) ", k.first, k.second);
    std::printf("\n");
    std::vector<std::pair<std::string, int>> rows{{"x", 3}, {"y", 9}, {"z", 1}};
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    for (const auto& r : rows) std::printf("%s=%d ", r.first.c_str(), r.second);
    std::printf("\n");
    int total = 0;
    auto forEach = [&](auto&& fn) {
        for (int i = 0; i < 4; ++i) total += fn(i);
    };
    forEach([](int i) { return i * i; });
    forEach([&](int i) { return i + base; });
    std::printf("%d\n", total);
    Node c{3, nullptr}, b{2, &c}, a{1, &b};
    auto value = [](const auto* n) { return n ? n->v : -1; };
    std::printf("%d %d %d\n", value(&a), value(a.next), value(c.next));
    auto sum = [](auto&& self, const Node* n) -> int { return n ? n->v + self(self, n->next) : 0; };
    std::printf("%d\n", sum(sum, &a));
    Tabs t;
    t.Close(1);
    std::printf("%d %d %s %d\n", (int)t.pages.size(), t.pages[1], t.names[1].c_str(), t.closed);
    std::string s = "ab";
    auto app = [&s](auto c) { s += c; return s.size(); };
    app('c');
    app(std::string("de"));
    std::printf("%s %d\n", s.c_str(), (int)app("f"));
    return 0;
}
