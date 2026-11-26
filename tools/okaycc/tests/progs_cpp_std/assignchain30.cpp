// A chained assignment of class objects into members and elements (p.color = p.color0 = c,
// Components/ParticleSystem.hpp:641), and a static local `auto *s = new T` (Render/TextureAsset.cpp:133).
#include <cstdio>
#include <unordered_set>
struct Color {
    float r, g, b, a;
};
struct Small {
    int x, y;
};
struct P {
    Color color, color0;
    Small s, s0;
};
static std::unordered_set<const int*>& Live() {
    static auto* s = new std::unordered_set<const int*>();
    return *s;
}
int main() {
    P p;
    P ps[2];
    Color start{1, 2, 3, 4};
    Small sm{5, 6};
    p.color = p.color0 = start;
    p.s = p.s0 = sm;
    ps[1].color = ps[0].color0 = p.color;
    Color c2 = start;
    c2.b = 9;
    ps[0].color = c2 = p.color0;
    int x = 3, y = 4;
    Live().insert(&x);
    Live().insert(&y);
    Live().insert(&x);
    std::printf("%g %g %d %d\n", p.color.b, p.color0.a, p.s.y, p.s0.x);
    std::printf("%g %g %g %g\n", ps[1].color.r, ps[0].color0.g, ps[0].color.b, c2.b);
    std::printf("%d\n", (int)Live().size());
    return 0;
}
