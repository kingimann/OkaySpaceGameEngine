// Round 24: argument-dependent lookup of ordinary functions ([basic.lookup.argdep]): a function
// of the argument's namespace called without qualification, from another namespace and from the
// global one; a hidden friend defined in the class (found only this way); a pointer argument.
#include <cstdio>
namespace geo {
struct Vec {
    int x, y;
    friend int dot(const Vec &a, const Vec &b) { return a.x * b.x + a.y * b.y; }
};
int length2(const Vec &v) { return v.x * v.x + v.y * v.y; }
void scale(Vec *v, int k) {
    v->x *= k;
    v->y *= k;
}
}  // namespace geo
namespace app {
int use(const geo::Vec &v) { return length2(v) + dot(v, v); }
}  // namespace app
int main() {
    geo::Vec v{3, 4};
    scale(&v, 2);
    printf("%d %d %d\n", length2(v), dot(v, v), app::use(v));
}
