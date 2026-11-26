// Calling an element chosen by a subscript: v[i](x), m[k](x), arr[i](x) ([expr.sub], [over.call]),
// and a class temporary: std::hash<T>{}(x), T()(x), make()(x) (Render/Visibility.cpp:56).
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>
struct Add {
    int n;
    int operator()(int x) const { return x + n; }
};
struct Counter {
    int calls = 0;
    int operator()(int x) { return x * 10 + ++calls; }
};
static Add makeAdd(int n) { return Add{n}; }
int main() {
    std::map<std::string, std::function<int(int)>> m;
    m["add"] = Add{5};
    m["twice"] = [](int x) { return x * 2; };
    std::vector<Add> v{Add{2}, Add{7}};
    Add arr[2] = {Add{1}, Add{3}};
    std::vector<Counter> cs(2);
    std::printf("%d %d\n", m["add"](1), m["twice"](21));
    std::printf("%d %d %d\n", v[0](2), v[1](2), arr[1](4));
    int c1 = cs[0](1);
    int c2 = cs[0](1);
    int c3 = cs[1](3);
    std::printf("%d %d %d\n", c1, c2, c3);
    std::printf("%d %d\n", cs[0].calls, cs[1].calls);
    std::vector<std::function<void(int)>> hs;
    int total = 0;
    hs.push_back([&total](int k) { total += k; });
    hs.push_back([&total](int k) { total *= k; });
    for (std::size_t i = 0; i < hs.size(); ++i) hs[i](3);
    std::printf("%d\n", total);
    std::string key = "texture";
    std::printf("%d\n", std::hash<std::string>{}(key) == std::hash<std::string>()(key));
    std::printf("%d %d\n", Add{3}(4), makeAdd(2)(10));
    return 0;
}
