// T{a, b, c, d} of an aggregate whose one member is an array (Values = std::array<float, 4>):
// brace elision for a temporary ([dcl.init.aggr] p15, [expr.type.conv]). Round 24: "too many initializers".
#include <array>
#include <cstdio>
struct V { float x, y; };
class T {
public:
    using Values = std::array<float, 4>;
    T &Set(float s) { d = s > 0 ? (s < kMax ? s : kMax) : 0; return *this; }
    T &End(const Values &v) { e = v; return *this; }
    T &End(float x, float y = 0.0f, float z = 0.0f, float w = 0.0f) { return End(Values{x, y, z, w}); }
    T &End(const V &v) { return End(Values{v.x, v.y, 0.0f, 0.0f}); }
    float d = 0;
    Values e{};
    static constexpr float kMax = 100.0f;
};
int main() {
    T t;
    t.Set(5).End(1, 2);
    std::printf("%g %g %g %g %g\n", t.d, t.e[0], t.e[1], t.e[2], t.e[3]);
    t.Set(500).End(V{7, 8});
    std::printf("%g %g %g %g %g\n", t.d, t.e[0], t.e[1], t.e[2], t.e[3]);
    std::array<int, 3> q = std::array<int, 3>{4, 5, 6};
    std::printf("%d %d %d\n", q[0], q[1], q[2]);
}
