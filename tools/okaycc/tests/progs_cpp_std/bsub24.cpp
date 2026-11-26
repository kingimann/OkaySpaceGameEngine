// Round 24: a braced list as a subscript ([expr.sub] p1): map<pair<int, int>, int> counted by
// ++edges[{a, b}], as Render/Mesh.hpp counts the edges of a mesh.
#include <cstdio>
#include <map>
#include <utility>
int main() {
    std::map<std::pair<int, int>, int> edges;
    int tri[6] = {0, 1, 2, 2, 1, 3};
    for (int t = 0; t < 2; ++t)
        for (int k = 0; k < 3; ++k) ++edges[{tri[t * 3 + k], tri[t * 3 + (k + 1) % 3]}];
    ++edges[{1, 2}];
    for (auto &kv : edges) printf("%d-%d:%d ", kv.first.first, kv.first.second, kv.second);
    printf("\n");
}
