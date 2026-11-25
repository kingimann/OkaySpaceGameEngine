// Round 24: a class with a nested class declared in it and defined outside (struct Dec::Impl),
// held by unique_ptr (the pointer-to-implementation of Audio/Mp3Decoder.hpp).
#include <cstdio>
#include <memory>
struct Q {
    float w;
    Q operator*(const Q &o) const { Q r; r.w = w * o.w; return r; }
    Q N() const { Q r; r.w = w + 1; return r; }
    static Q Make(float a, float b, float c) {
        Q x, y, z;
        x.w = a; y.w = b; z.w = c;
        return (y * x * z).N();
    }
};
class Dec {
public:
    Dec();
    int get() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m;
};
struct Dec::Impl { int v = 7; };
Dec::Dec() : m(new Impl) {}
int Dec::get() const { return m->v; }
int main() { Dec d; printf("%.1f %d\n", Q::Make(2, 3, 4).w, d.get()); }
