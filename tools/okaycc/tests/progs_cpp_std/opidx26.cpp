// A class operator on an element chosen by an index that is itself an expression
// (vertices[triangles[t * 3 + 1]] - a in Render/Mesh.hpp, v[(size_t)i] == p): [over.match.oper].
#include <cstdio>
#include <vector>
struct V {
    float x, y;
    V operator-(const V &o) const { return V{x - o.x, y - o.y}; }
    bool operator==(const V &o) const { return x == o.x && y == o.y; }
};
int main() {
    std::vector<V> vertices{V{1, 2}, V{4, 8}, V{9, 1}};
    std::vector<int> triangles{2, 1, 0};
    V arr[3] = {{1, 2}, {4, 8}, {9, 1}};
    int t = 0;
    long i = 1;
    const V &a = vertices[triangles[t * 3 + 2]];
    V d = vertices[triangles[t * 3 + 1]] - a;
    V e = arr[triangles[t]] - arr[(int)i];
    std::printf("%g %g %g %g %d %d\n", d.x, d.y, e.x, e.y, vertices[(size_t)i] == arr[1], arr[(int)i - 1] == a);
}
