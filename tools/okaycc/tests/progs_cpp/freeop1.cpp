// Round 20: non-member operators ([over.match.oper]), found in the current namespace, an enclosing
// one, or the namespace of a class operand (argument-dependent lookup), with overloads.
extern "C" int printf(const char *fmt, ...);

namespace m {
struct V {
    float x, y;
    V operator*(float s) const { return V{x * s, y * s}; }
    V operator+(const V &o) const { return V{x + o.x, y + o.y}; }
};
inline V operator*(float s, const V &v) { return v * s; }
inline bool operator==(const V &a, const V &b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(const V &a, const V &b) { return !(a == b); }
inline V operator-(const V &v) { return V{-v.x, -v.y}; }

struct I {
    int v;
};
inline I operator+(I a, int b) { return I{a.v + b}; }
inline I operator+(int a, I b) { return I{a * 100 + b.v}; }
inline I operator+(I a, I b) { return I{a.v * 10 + b.v}; }
inline bool operator<(const I &a, const I &b) { return a.v < b.v; }

float len1(const V &v) { return (2.0f * v).x; }  // found in this namespace
}  // namespace m

namespace other {
struct W {
    int k;
};
inline int operator*(int s, const W &w) { return s * w.k + 1; }  // not viable for m::V
}  // namespace other

int main() {
    m::V a{1.0f, 2.0f};
    m::V b = 3.0f * a;  // argument-dependent lookup from the global namespace
    printf("%d %d\n", (int)b.x, (int)b.y);
    printf("%d %d %d\n", b == m::V{3.0f, 6.0f}, b != a, a == a);
    m::V c = -(a + b);
    printf("%d %d %d\n", (int)c.x, (int)c.y, (int)m::len1(c));
    m::I i{4}, j{7};
    m::I k = i + 1;
    m::I l = 2 + j;
    m::I n = i + j;
    printf("%d %d %d %d %d\n", k.v, l.v, n.v, i < j, j < i);
    other::W w{5};
    printf("%d\n", 3 * w);
    int plain = 6 * 7;  // two numbers: never an operator call
    printf("%d\n", plain);
    return 0;
}
