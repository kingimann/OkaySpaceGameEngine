// Round 23: structured bindings ([dcl.struct.bind]): over a map in a range-for (by reference, so
// the values can be changed), of a pair, of a struct returned by value, of an array, nested scopes.
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>
struct Hit {
    float t;
    int id;
    std::string what;
};
static Hit cast(int k) { return Hit{0.5f * k, k, k ? "box" : "none"}; }
int main() {
    std::map<std::string, int> m = {{"b", 2}, {"a", 1}, {"c", 3}};
    for (auto &[k, v] : m) v *= 10;
    for (const auto &[k, v] : m) std::printf("%s=%d ", k.c_str(), v);
    std::printf("\n");
    std::pair<int, double> p(3, 1.5);
    auto [i, d] = p;
    auto &[ri, rd] = p;
    ri = 7;
    std::printf("%d %.1f %d\n", i, d, p.first);
    auto [t, id, what] = cast(4);
    std::printf("%.1f %d %s\n", t, id, what.c_str());
    int arr[3] = {4, 5, 6};
    auto [x, y, z] = arr;
    std::printf("%d\n", x + y * z);
    std::vector<std::pair<std::string, int>> list = {{"q", 1}, {"r", 2}};
    int total = 0;
    for (const auto &[name, n] : list) {
        int k = n * 2;
        total += k + (int)name.size();
    }
    {
        auto [i, d] = std::make_pair(9, 2.5);
        std::printf("inner %d %.1f\n", i, d);
    }
    std::printf("outer %d total %d\n", i, total);
    return 0;
}
