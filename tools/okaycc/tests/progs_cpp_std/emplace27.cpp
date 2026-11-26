// vector::emplace_back with any number of arguments, built in place and returning the element
// ([sequence.reqmts] p71; emplace_back() in Render/Mesh.hpp:662), and an argument that is an element
// of the same vector while it grows.
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
struct P { int a; float b; std::string c; P(int x, float y, std::string z) : a(x), b(y), c(z) {} };
int main() {
    std::vector<std::vector<int>> g;
    g.emplace_back();
    g.emplace_back(3, 7);
    g[0].push_back(5);
    std::vector<P> ps;
    for (int i = 0; i < 6; ++i) ps.emplace_back(i, i * 0.5f, std::string(i, 'x'));
    std::vector<std::pair<int, int>> pr;
    pr.emplace_back(1, 2);
    auto &back = pr.emplace_back(3, 4);
    back.first = 9;
    std::vector<int> v{1, 2, 3, 4};
    v.emplace_back(v[0]);
    std::vector<std::string> s;
    s.emplace_back("abc");
    s.emplace_back(s[0]);
    std::printf("%zu %zu %d %zu %s %d %d %d %s\n", g.size(), g[1].size(), g[0][0], ps.size(), ps[5].c.c_str(), pr[1].first,
                v[4], (int)v.size(), s[1].c_str());
}
