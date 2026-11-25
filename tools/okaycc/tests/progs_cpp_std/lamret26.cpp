// A lambda whose return type is deduced from a call of a template instance asked for in the same
// round (std::make_tuple), a lambda that compares captured class objects with a member == (v[(size_t)i]
// == p), vector::resize(n, value), and v = {{..}, {..}} through the initializer-list constructor.
#include <tuple>
#include <map>
#include <vector>
#include <cstdio>
struct V3 {
    float x, y, z;
    bool operator==(const V3 &o) const { return x == o.x && y == o.y && z == o.z; }
};
struct Mesh {
    std::vector<V3> vertices;
    std::vector<int> colors;
    int Remap() {
        colors.resize(vertices.size() + 2, 7);
        std::map<std::tuple<float, float, float>, int> at;
        for (int i = 0; i < (int)vertices.size(); ++i) at.emplace(std::make_tuple(vertices[(size_t)i].x, vertices[(size_t)i].y, vertices[(size_t)i].z), i);
        auto find = [&](int hint, const V3 &p) {
            if (hint >= 0 && hint < (int)vertices.size() && vertices[(size_t)hint] == p) return hint;
            auto it = at.find(std::make_tuple(p.x, p.y, p.z)); return it == at.end() ? -1 : it->second; };
        return find(0, V3{1, 2, 3}) * 100 + find(5, V3{4, 5, 6}) * 10 + find(-1, V3{9, 9, 9});
    }
};
int main() {
    auto cell = [&](int p) { return std::make_tuple((long long)p, (long long)p * 2); };
    const auto c = cell(3);
    std::printf("%lld %lld\n", std::get<0>(c), std::get<1>(c));
    Mesh m;
    m.vertices = {V3{1, 2, 3}, V3{4, 5, 6}};
    int r = m.Remap();
    std::printf("%d %zu %d\n", r, m.colors.size(), m.colors[3]);
    std::vector<V3> w;
    w = {{1, 2, 3}, {4, 5, 6}};
    std::vector<int> one;
    one = {5};
    std::printf("%zu %g %zu %d\n", w.size(), w[1].y, one.size(), one[0]);
}
