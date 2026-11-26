// Round 28: an array member of class type with a braced default initializer, S a[4]{} and
// {{..}, {..}} (Components/Character.hpp:724): each element is an object, the rest value-initialized.
#include <cstdio>
struct V { float x, y, z; };
struct MoveSample { V p; float t; };
struct WithCtor { int a; WithCtor() : a(5) {} };
struct C {
  MoveSample hist[4]{};
  MoveSample two[3]{{{1, 2, 3}, 4}, {{5, 6, 7}, 8}};
  WithCtor w[2]{};
  int n = 1;
};
int main() {
  C c; c.hist[1].t = 3;
  printf("%g %g %g %g %g %g %d %d\n", c.hist[0].p.x, c.hist[1].t, c.two[0].p.z, c.two[1].t, c.two[2].p.x, c.two[2].t, c.w[0].a, c.w[1].a);
}
