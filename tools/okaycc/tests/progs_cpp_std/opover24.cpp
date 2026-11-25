// Round 24: overloaded member operators on a result that is not an object (qy * qx * qz with
// Quat * Quat beside Quat * Vec3): the result type of the chosen overload, not of the last one.
#include <cstdio>
struct V { float x, y, z; };
struct Q {
    float w;
    Q operator*(const Q &o) const { Q r; r.w = w * o.w; return r; }
    V operator*(const V &v) const { V r; r.x = v.x * w; r.y = 0; r.z = 0; return r; }
    Q N() const { Q r; r.w = w + 1; return r; }
    static Q E(float a, float b, float c) {
        Q x, y, z;
        x.w = a; y.w = b; z.w = c;
        return (y * x * z).N();
    }
};
int main() { printf("%.1f\n", Q::E(2, 3, 4).w); }
