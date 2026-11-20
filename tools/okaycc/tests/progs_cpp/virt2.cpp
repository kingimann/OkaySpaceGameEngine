// More virtual dispatch: calls from constructors and destructors, inherited virtuals that are
// not overridden, overloads, a call on an object versus through a pointer, and class layout.
extern "C" int printf(const char *fmt, ...);

class Base {
public:
    int x;
    Base() : x(1) { printf("Base ctor sees %d\n", kind()); }
    virtual ~Base() { printf("Base dtor sees %d\n", kind()); }
    virtual int kind() { return 10; }
    virtual int scale(int v) { return v * 2; }
    virtual int scale(int v, int w) { return v * w; }
    int run() { return kind() + scale(3) + scale(2, 5); }
};

class Mid : public Base {
public:
    int y;
    Mid() : y(2) { printf("Mid ctor sees %d\n", kind()); }
    ~Mid() { printf("Mid dtor sees %d\n", kind()); }
    int kind() { return 20; }
};

class Leaf : public Mid {
public:
    int z;
    Leaf() : z(3) { printf("Leaf ctor sees %d\n", kind()); }
    ~Leaf() { printf("Leaf dtor sees %d\n", kind()); }
    int scale(int v) { return v * 100; }
    int kind() { return 30; }
};

// Layout checks (Itanium C++ ABI data layout).
struct PodBase {
    int a;
    char c;
};
struct FromPod : PodBase {
    char d;
};
class NonPodBase {
public:
    int a;
    char c;
    NonPodBase() : a(0), c(0) {}
};
class FromNonPod : public NonPodBase {
public:
    char d;
};
class WithVptr {
public:
    char c;
    virtual int f() { return 1; }
};
class WithVptr2 : public WithVptr {
public:
    char d;
    int e;
};

int main() {
    {
        Leaf l;
        printf("run %d\n", l.run());
        Base *b = &l;
        printf("via base %d %d %d\n", b->kind(), b->scale(4), b->scale(4, 5));
        Mid *m = &l;
        printf("via mid %d\n", m->kind());
        Base plain;
        printf("object %d\n", plain.kind());
    }
    Base *d = new Leaf();
    printf("delete via base\n");
    delete d;
    FromPod fp;
    FromNonPod fn;
    WithVptr2 wv;
    printf("sizeof %d %d %d %d %d %d\n", (int)sizeof(PodBase), (int)sizeof(FromPod), (int)sizeof(NonPodBase),
           (int)sizeof(FromNonPod), (int)sizeof(WithVptr), (int)sizeof(WithVptr2));
    printf("offsets %d %d %d %d %d\n", (int)((char *)&fp.d - (char *)&fp), (int)((char *)&fn.d - (char *)&fn),
           (int)((char *)&wv.c - (char *)&wv), (int)((char *)&wv.d - (char *)&wv), (int)((char *)&wv.e - (char *)&wv));
    return 0;
}
