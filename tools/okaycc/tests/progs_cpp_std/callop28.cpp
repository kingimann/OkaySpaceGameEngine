// Round 28: an operator on a member of a call result ([expr.ref], [over.match.oper]): the member
// of the temporary is an lvalue that lives to the end of the full expression, and a member
// operator (std::string's +, ==, a class's operator+, an iterator's ->) applies to it.
// (Render/Mesh.hpp:1491: idOf.emplace(..).first->second; GaitFor(2).name + x.)
#include <cstdio>
#include <map>
#include <string>
#include <tuple>

struct Gait {
    std::string name;
    int n;
};
static Gait GaitFor(int i) { return Gait{std::string("gait") + std::to_string(i), i}; }
struct V {
    float x, y;
    V operator+(const V& o) const { return V{x + o.x, y + o.y}; }
    bool operator==(const V& o) const { return x == o.x && y == o.y; }
};
struct H {
    V v;
    Gait g;
};
static H mk() { return H{V{1, 2}, Gait{"inner", 3}}; }

int main() {
    std::map<std::tuple<float, float, float>, int> idOf;
    int a = idOf.emplace(std::make_tuple(1.f, 2.f, 3.f), (int)idOf.size()).first->second;
    int b = idOf.emplace(std::make_tuple(1.f, 2.f, 4.f), (int)idOf.size()).first->second;
    int c = idOf.emplace(std::make_tuple(1.f, 2.f, 3.f), 7).first->second;
    std::map<int, std::string> names;
    std::size_t len = names.emplace(5, "five").first->second.size();
    std::printf("%d %d %d %d %d\n", a, b, c, (int)idOf.size(), (int)len);

    std::string s = GaitFor(2).name + "!";
    std::string t = "<" + GaitFor(3).name;
    std::string u = mk().g.name + "/" + GaitFor(9).name;
    V w = mk().v + V{3, 4};
    bool eq = GaitFor(4).name == "gait4";
    bool ne = GaitFor(4).name != "gait5";
    bool veq = mk().v == V{1, 2};
    std::printf("%s %s %s %g %g %d %d %d\n", s.c_str(), t.c_str(), u.c_str(), w.x, w.y, (int)eq, (int)ne, (int)veq);
    return 0;
}
