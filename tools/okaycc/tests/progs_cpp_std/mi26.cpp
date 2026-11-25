// Multiple inheritance ([class.mi]; Itanium C++ ABI 2.4 layout, 2.5.2 vtable groups, 3.1.4
// thunks): a class with two polymorphic bases (as Components/ModelAnimator.hpp: Behaviour and
// AnimBackend). Layout, conversions to the second base (pointer, reference, null), static_cast
// back, virtual calls and virtual destruction through the second base, members of both bases.
#include <cstdio>
struct Named {
    virtual ~Named() { std::printf("~Named %d\n", id); }
    virtual const char *Name() const { return "named"; }
    int id = 1;
};
struct Backend {
    virtual ~Backend() { std::printf("~Backend %d\n", b); }
    virtual int Play(int x) = 0;
    virtual int Speed() const { return 10 + b; }
    int Twice(int x) { return Play(x) * 2; }
    int b = 5;
};
struct Anim : public Named, public Backend {
    explicit Anim(int k0) : Backend(), k(k0) { b = 7; id = 2; }
    ~Anim() override { std::printf("~Anim %d\n", k); }
    const char *Name() const override { return "anim"; }
    int Play(int x) override { return x + k + id; }
    int Both() { return Twice(1) + b + id; }
    int k;
};
struct Fancy : Anim {  // a class derived from one with two bases
    Fancy() : Anim(8) {}
    int Speed() const override { return 99; }
};
struct Plain { int p = 3; };
struct Mixed : Named, Plain { int m = 4; };
static int use(Backend &be, int x) { return be.Play(x) + be.Speed(); }
static int usep(Backend *be) { return be ? be->Twice(1) : -1; }
static Backend *asBackend(Anim *a) { return a; }
int main() {
    Anim a(3);
    Backend *bp = &a;
    Named *np = &a;
    std::printf("%zu %zu %zu %zu\n", sizeof(Named), sizeof(Backend), sizeof(Anim), sizeof(Mixed));
    std::printf("%d %d\n", (int)((char *)bp - (char *)&a), (int)((char *)np - (char *)&a));
    std::printf("%d %d %d %s\n", bp->Play(2), use(a, 1), usep(&a), np->Name());
    Anim *back = static_cast<Anim *>(bp);
    std::printf("%d %d\n", back == &a, back->k);
    Backend *nb = nullptr;
    Anim *na = nullptr;
    Backend *viaNull = na;
    std::printf("%d %d %d\n", usep(nb), viaNull == nullptr, usep(asBackend(&a)));
    std::printf("%d %d %d %d\n", a.b, a.Speed(), a.Twice(2), a.Both());
    Backend &br = a;
    br.b = 9;
    std::printf("%d %d\n", a.b, br.Speed());
    Mixed m;
    Plain *pp = &m;
    std::printf("%d %d %d\n", pp->p, m.p + m.m, (int)((char *)pp - (char *)&m));
    Anim copy = a;
    Backend &cb = copy;
    copy.k = 6;
    std::printf("%d %d %s\n", cb.Play(1), cb.Speed(), static_cast<Named &>(copy).Name());
    Fancy f;
    Backend *fb = &f;
    std::printf("%d %d %s %d\n", fb->Play(1), fb->Speed(), f.Name(), use(f, 0));
    Backend *heap = new Anim(4);
    std::printf("%d\n", heap->Play(1));
    delete heap;
    return 0;
}
