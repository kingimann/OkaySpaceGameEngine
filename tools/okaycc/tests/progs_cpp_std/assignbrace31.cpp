// v.assign(n, {}) takes the count and a value-initialized element ([dcl.init.list] p3; the
// iterator template does not take a braced list), and std::string::replace ([string.replace]).
#include <cstdio>
#include <string>
#include <vector>
struct Bind { int a = 7; float b = 1.5f; };
int main() {
    std::vector<Bind> v;
    v.assign(3, {});
    std::printf("%zu %d %.1f\n", v.size(), v[2].a, v[2].b);
    std::vector<int> w;
    w.assign(2, {});
    std::printf("%zu %d\n", w.size(), w[1]);
    std::string name = "LeftHand";
    const std::string A = "Left", B = "Right";
    std::size_t at = name.find(A);
    name.replace(at, A.size(), B);
    std::printf("%s\n", name.c_str());
    std::string s = "abcdef";
    s.replace(1, 2, "XYZ");
    std::printf("%s\n", s.c_str());
    s.replace(0, 1, 3, '-');
    std::printf("%s\n", s.c_str());
    return 0;
}
