// Round 21: our own <array> against libstdc++.
#include <array>
#include <cstdio>

struct V {
    float x, y;
};

static int total(const std::array<int, 4> &a) {
    int s = 0;
    for (unsigned long i = 0; i < a.size(); ++i) s += a[i];
    return s;
}

struct Grid {
    std::array<int, 3> row{};
    std::array<V, 2> pts;
};

int main() {
    std::array<int, 4> a = {1, 2, 3, 4};
    std::array<int, 4> b{};
    std::printf("%lu %d %d %d %d\n", a.size(), a[0], a.back(), total(a), total(b));
    b.fill(7);
    for (int &v : b) v += 1;
    std::printf("%d %d %d\n", b.front(), b.at(3), (int)(a == b));
    a.swap(b);
    std::printf("%d %d\n", a[0], b[0]);
    int sum = 0;
    for (int v : b) sum += v;
    std::printf("sum %d data %d\n", sum, *(a.data() + 2));
    std::array<V, 2> pv = {{{1.5f, 2.5f}, {3.5f, 4.5f}}};
    std::printf("%.1f %.1f\n", pv[1].x, pv[0].y);
    Grid g;
    g.row[1] = 5;
    g.pts[0].x = 9.0f;
    std::printf("%d %d %.1f\n", g.row[0], g.row[1], g.pts[0].x);
    std::array<std::array<int, 2>, 2> m = {{{1, 2}, {3, 4}}};
    std::printf("%d %d\n", m[1][0], m[0][1]);
    return 0;
}
