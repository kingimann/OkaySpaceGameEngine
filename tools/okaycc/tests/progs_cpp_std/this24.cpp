// Round 24: a statement that starts with this (this->d = d / l; in a constructor), and static
// members declared two to a line (static constexpr float a = 1, b = 2;).
#include <cstdio>
struct V { float x; V(float a) : x(a) {} float M() const { return x; } };
struct Plane {
    V normal{0};
    float d = 0;
    Plane() {}
    Plane(const V& n, float d) { float l = n.M(); if (l > 1e-12f) { normal = n; this->d = d / l; } }
};
struct P { static constexpr float kA = 0.5f, kB = 0.25f; static const int a = 1, b = 2; };
int main() { Plane p(V(2), 4); printf("%.2f %.2f %.2f %d\n", p.d, P::kA, P::kB, P::a + P::b); }
