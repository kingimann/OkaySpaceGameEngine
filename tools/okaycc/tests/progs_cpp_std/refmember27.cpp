// A braced initializer for a class with a reference member binds the reference ([dcl.init.aggr]
// p4, [dcl.init.ref]). Round 26 assigned through the reference's empty slot (a crash at run
// time), and an unnamed class in a function (struct { T &r; } cv{x}; Render/Mesh.hpp:652) was
// "cannot assign this value".
#include <cstdio>
#include <utility>
#include <vector>
struct Named { std::vector<int> &v; int k; };
struct Holder {  // an unnamed class in a member function, as in Render/Mesh.hpp
    std::vector<std::vector<std::pair<int, int>>> corners;
    int Sum() const {
        int total = 0;
        for (auto &cvv : corners) {
            struct { const std::vector<std::pair<int, int>> &second; } cv{cvv};
            for (auto &tk : cv.second) total += tk.first * 10 + tk.second;
        }
        return total;
    }
};
int main() {
    std::vector<std::vector<std::pair<int, int>>> corners(3);
    corners[1].push_back({4, 5});
    corners[2].push_back({6, 7});
    int total = 0;
    for (auto &cvv : corners) {
        if (cvv.empty()) continue;
        struct { std::vector<std::pair<int, int>> &second; } cv{cvv};
        for (auto &tk : cv.second) total += tk.first * 10 + tk.second;
        cv.second.push_back({1, 1});
    }
    std::vector<int> w{1, 2};
    Named n{w, 3};
    n.v.push_back(n.k);
    struct Local { std::vector<int> &v; } a{w};
    a.v.push_back(9);
    struct { int &r; double d; } b{total, 0.5};
    b.r += 1000;
    std::printf("%d %zu %zu %zu %d %g\n", total, corners[1].size(), corners[2].size(), w.size(), w.back(), b.d);
    Holder h;
    h.corners.resize(2);
    h.corners[1].push_back({4, 5});
    std::printf("%d\n", h.Sum());
    return 0;
}
