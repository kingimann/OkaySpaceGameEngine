// vector::insert of a count, a range and a list ([vector.modifiers], [sequence.reqmts] p14), and a
// class whose constructor is declared but defined later.
#include <cstdio>
#include <string>
#include <vector>
struct Later {
    Later();
    ~Later();
    int v;
};
Later::Later() : v(4) {}
Later::~Later() {}
static void show(const std::vector<int> &v) {
    for (int x : v) std::printf("%d ", x);
    std::printf("\n");
}
int main() {
    std::vector<int> a = {1, 2, 3};
    std::vector<int> b = {7, 8};
    a.insert(a.begin() + 1, b.begin(), b.end());
    show(a);
    a.insert(a.end(), 3, 9);
    show(a);
    a.insert(a.begin(), {5, 6});
    show(a);
    a.insert(a.begin() + 2, a.begin(), a.begin() + 3);
    show(a);
    std::vector<std::string> s = {"x"};
    std::vector<std::string> t = {"a", "b"};
    s.insert(s.end(), t.begin(), t.end());
    s.insert(s.begin(), t.begin(), t.end());
    for (auto &x : s) std::printf("%s ", x.c_str());
    std::printf("\n");
    Later *l = new Later[3];
    std::printf("%d\n", l[2].v);
    delete[] l;
}
