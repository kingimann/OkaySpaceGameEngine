// decltype(auto) keeps a reference result ([dcl.type.auto.deduct] p5): a forwarding wrapper
// returns the std::string & of erase, so a chain changes the object itself. Round 24 returned a
// copy (silent wrong code: the chained append went to the copy).
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
struct Tags : std::string {
    int touched = 0;
    void Touch() { ++touched; }
    template <class... A>
    decltype(auto) erase(A &&...a) {
        Touch();
        return std::string::erase(std::forward<A>(a)...);
    }
};
static std::vector<int> g = {1, 2, 3};
decltype(auto) at(int i) { return g[i]; }
decltype(auto) value(int i) {
    int x = g[i];
    return x;
}
int main() {
    Tags t;
    t.assign("abcdef");
    t.erase(1, 2).append("!");
    std::printf("%s %d\n", t.c_str(), t.touched);
    at(1) = 20;
    int v = value(1);
    v = 5;
    std::printf("%d %d %d\n", g[1], v, g[0]);
}
