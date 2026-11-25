// Round 24: <tuple> ([tuple]): construction, get<I>, tuple_size, tuple_element, make_tuple, tie,
// the comparisons, tuples as map keys (the use in Render/Mesh.hpp), an empty tuple.
#include <cstdio>
#include <map>
#include <string>
#include <tuple>
#include <vector>

std::tuple<int, double> divmod(int a, int b) { return std::make_tuple(a / b, (double)(a % b)); }

int main() {
    std::tuple<int, char, long> t(1, 'x', 30L);
    std::printf("%d %c %ld\n", std::get<0>(t), std::get<1>(t), std::get<2>(t));
    std::get<0>(t) = 7;
    std::printf("%d size %d\n", std::get<0>(t), (int)std::tuple_size<std::tuple<int, char, long>>::value);
    auto m = std::make_tuple(3, 4.5, std::string("hi"));
    std::printf("%d %.1f %s\n", std::get<0>(m), std::get<1>(m), std::get<2>(m).c_str());
    std::map<std::tuple<int, int, int>, int> grid;
    grid[std::make_tuple(1, 2, 3)] = 10;
    grid[std::make_tuple(1, 2, 2)] = 20;
    grid[std::make_tuple(0, 9, 9)] = 30;
    for (auto &kv : grid) std::printf("(%d %d %d) %d\n", std::get<0>(kv.first), std::get<1>(kv.first), std::get<2>(kv.first), kv.second);
    auto it = grid.find(std::make_tuple(1, 2, 2));
    std::printf("found %d\n", it == grid.end() ? -1 : it->second);
    int q = 0;
    double r = 0;
    std::tie(q, r) = divmod(17, 5);
    std::printf("q %d r %.1f\n", q, r);
    std::tuple<int, int> a(1, 2), b(1, 3);
    std::printf("%d %d %d %d\n", a < b, a == b, a != b, b > a);
    std::tuple<> e;
    std::printf("empty %d\n", (int)std::tuple_size<std::tuple<>>::value + (e == e ? 1 : 0));
    std::tuple_element<1, std::tuple<int, double, char>>::type d = 2.25;
    std::printf("%.2f\n", d);
    std::tuple<long long, long long, long long> c = std::make_tuple(1LL, 2LL, 3LL);
    std::printf("%lld\n", std::get<0>(c) + std::get<1>(c) + std::get<2>(c));
    return 0;
}
