// Pieces of Render/Mesh.hpp and AI/BehaviorTree.cpp: vector::assign of a range, a map keyed by a
// tuple with std::lround parts, a std::function that calls itself through its own name (the name
// is in scope from its declarator, [basic.scope.pdecl] p1, also when std::visit exists), make_tuple of
// a const int deduces int ([temp.deduct.call] p2), a static local pointer to a string literal
// (Math/ColorSpace.hpp: static const char *hx = "0123456789ABCDEF").
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>
struct Hex {
    static char Digit(int i) {
        static const char *hx = "0123456789ABCDEF";
        return hx[i & 15];
    }
};
int main() {
    std::printf("%c%c%c\n", Hex::Digit(10), Hex::Digit(15), Hex::Digit(3));
    std::set<std::pair<int, int>> out{{1, 2}, {0, 5}, {3, 3}};
    std::vector<std::pair<int, int>> seams;
    seams.assign(out.begin(), out.end());
    std::vector<int> n;
    n.assign(3, 4);
    std::printf("%zu %d %d %zu %d\n", seams.size(), seams[0].second, seams[2].first, n.size(), n[2]);
    std::map<std::tuple<int, long, long>, int> copy;
    float tx = 0.25f, ty = 0.5f;
    const int v = 3;
    const auto key = std::make_tuple(v, std::lround(tx * 1e6f), std::lround(ty * 1e6f));
    auto it = copy.find(key);
    if (it == copy.end()) it = copy.emplace(key, 7).first;
    std::printf("%d %ld %zu\n", it->second, std::get<1>(it->first), copy.size());
    std::vector<std::vector<int>> kids{{1, 2}, {3}, {}, {}};
    std::vector<int> order;
    std::function<void(int)> visit = [&](int id) {
        order.push_back(id);
        for (int c : kids[(std::size_t)id]) visit(c);
    };
    visit(0);
    for (int v : order) std::printf("%d ", v);
    std::printf("\n");
}
