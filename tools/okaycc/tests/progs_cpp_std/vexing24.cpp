// Round 24: T a(U("x")); and T a(U{..}); declare objects ([dcl.ambig.res] p1: a literal cannot
// start a parameter declaration); round 23 read them as function declarations.
#include <cstdio>
#include <string>
#include <variant>
struct W {
    std::string s;
    W(const std::string &x) : s(x) {}
};
struct P {
    int a, b;
};
struct H {
    int sum;
    H(P p) : sum(p.a + p.b) {}
};
int main() {
    W a(std::string("hello"));
    std::variant<int, std::string> v(std::string("text"));
    H h(P{2, 3});
    printf("%s %s %d\n", a.s.c_str(), std::get<std::string>(v).c_str(), h.sum);
}
