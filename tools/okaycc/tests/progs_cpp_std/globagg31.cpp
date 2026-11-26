// A global of an aggregate class with default member initializers given a braced list (W x{};,
// W u{7};, inline const W W::One{};): the fields the list leaves out get their initializers
// ([dcl.init.aggr] p5). They were zero (silent wrong values). Also V::One inside namespace eng
// finds eng::V before a global V ([basic.lookup.qual] p1).
#include <cstdio>
struct W {
    int a = 2;
    int b = 3;
    static const W One;
};
inline const W W::One{};
W x{};
W y;
const W z{};
W u{7};
static W s = {};
struct V {
    int a = 1;
    static const V One;
};
inline const V V::One{};
namespace eng {
struct V {
    int a = 2;
    int b = 3;
    static const V One;
};
inline const V V::One{};
struct Mixer {
    static V get() { return V::One; }
};
}  // namespace eng
int main() {
    W l{};
    std::printf("%d %d %d %d %d %d %d %d\n", x.b, y.b, z.b, u.b, u.a, s.b, l.b, W::One.b);
    std::printf("%d %d %d\n", eng::Mixer::get().b, eng::V::One.a, V::One.a);
    return 0;
}
