// Class scope before namespace scope, a lambda in a member function using members, static
// constexpr members used in other static members, ?: with a braced temporary.
extern "C" int printf(const char *fmt, ...);
namespace okay {
struct V {
    float x, y;
    V operator/(float d) const { return {x / d, y / d}; }
};
inline int Rotate(const V &a, int b) { return (int)a.x + b; }
struct M {
    static constexpr float PI = 3.5f;
    static constexpr float Half = PI / 2.0f;
    static constexpr float Twice = 2.0f * PI;
    static float Area(float r) { return PI * r * r; }
};
struct Tree {
    int nodes[4] = {1, 2, 3, 4};
    int hits = 0;
    void Rotate(int id) { hits += id; }
    void Walk() {
        Rotate(2);
        int best = 0;
        auto consider = [&](int i) {
            if (nodes[i] > best) best = nodes[i];
            ++hits;
        };
        for (int i = 0; i < 4; ++i) consider(i);
        printf("walk %d %d\n", best, hits);
    }
    V Norm(V v) const {
        float m = v.x;
        return m > 1.0f ? v / m : V{};
    }
};
}  // namespace okay
int main() {
    okay::Tree t;
    t.Walk();
    printf("free %d\n", okay::Rotate(okay::V{3, 4}, 1));
    printf("pi %g %g %g %g\n", (double)okay::M::Half, (double)okay::M::Twice, (double)okay::M::Area(2),
           (double)okay::M::PI);
    okay::V n = t.Norm(okay::V{4, 2});
    okay::V z = t.Norm(okay::V{0.5f, 2});
    printf("norm %g %g %g %g\n", (double)n.x, (double)n.y, (double)z.x, (double)z.y);
    return 0;
}
