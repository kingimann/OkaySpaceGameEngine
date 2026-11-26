// Round 28: <algorithm> minmax, replace, rotate, transform, stable_partition, nth_element,
// partial_sort, equal, binary_search, set_union, set_intersection, min/max of a list
// (Render/Mesh.hpp, Core/SaveData.hpp:235), and <cmath> remainder (Net/PlayerInput.hpp:46).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

static void show(const char* what, const std::vector<int>& v) {
    std::printf("%s:", what);
    for (int x : v) std::printf(" %d", x);
    std::printf("\n");
}

int main() {
    int a7 = 7, a3 = 3, a2 = 2, a9 = 9;
    auto mm = std::minmax(a7, a3);  // (references to its arguments in the standard: they must live)
    auto mm2 = std::minmax(a2, a9);
    std::printf("%d %d %d %d\n", mm.first, mm.second, mm2.first, mm2.second);
    std::string path = "a\\b\\c.txt";
    std::replace(path.begin(), path.end(), '\\', '/');
    std::printf("%s\n", path.c_str());
    std::vector<int> v = {1, 2, 3, 4, 5, 6, 7};
    auto r = std::rotate(v.begin(), v.begin() + 2, v.end());
    show("rotate", v);
    std::printf("new first at %d\n", (int)(r - v.begin()));
    std::vector<int> w(v.size());
    std::transform(v.begin(), v.end(), w.begin(), [](int x) { return x * 10; });
    show("transform", w);
    std::vector<int> s(v.size());
    std::transform(v.begin(), v.end(), w.begin(), s.begin(), [](int a, int b) { return a + b; });
    show("transform2", s);
    std::vector<int> p = {5, 2, 8, 1, 9, 4, 7, 6, 3};
    auto mid = std::stable_partition(p.begin(), p.end(), [](int x) { return x % 2 == 0; });
    show("stable_partition", p);
    std::printf("evens %d\n", (int)(mid - p.begin()));
    std::vector<int> q = {5, 2, 8, 1, 9, 4};
    std::nth_element(q.begin(), q.begin() + 2, q.end());
    std::printf("nth %d\n", q[2]);
    std::vector<int> ps = {5, 2, 8, 1, 9, 4};
    std::partial_sort(ps.begin(), ps.begin() + 3, ps.end(), [](int a, int b) { return a > b; });
    std::printf("partial %d %d %d\n", ps[0], ps[1], ps[2]);
    std::vector<int> a = {1, 3, 5, 7}, b = {1, 3, 5, 7}, c = {3, 4, 5, 8};
    std::printf("equal %d %d %d\n", (int)std::equal(a.begin(), a.end(), b.begin()),
                (int)std::equal(a.begin(), a.end(), c.begin()), (int)std::equal(a.begin(), a.end(), b.begin(), b.end()));
    std::printf("bs %d %d\n", (int)std::binary_search(a.begin(), a.end(), 5), (int)std::binary_search(a.begin(), a.end(), 4));
    std::vector<int> u, in;
    std::set_union(a.begin(), a.end(), c.begin(), c.end(), std::back_inserter(u));
    std::set_intersection(a.begin(), a.end(), c.begin(), c.end(), std::back_inserter(in));
    show("union", u);
    show("intersection", in);
    std::printf("%d %d\n", std::min({4, 2, 8}), std::max({4, 2, 8}));
    std::printf("%g %g %g %g %g %g\n", std::remainder(370.0, 360.0), std::remainder(-190.0, 360.0),
                std::remainder(5.0, 2.0), std::remainder(7.0, 2.0), std::remainder(-4.0, 2.0),
                (double)std::remainder(725.5f, 360.0f));
    return 0;
}
