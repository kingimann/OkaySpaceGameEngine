// Virtual functions, overriding, a virtual destructor, and the Itanium vtable layout.
extern "C" int printf(const char *fmt, ...);

class Shape {
public:
    int id;
    Shape(int i) : id(i) { printf("Shape(%d)\n", id); }
    virtual ~Shape() { printf("~Shape(%d)\n", id); }
    virtual int area() { return 0; }
    virtual int sides() { return 0; }
    int tag() { return id * 100 + sides(); }
    virtual void name() { printf("shape %d\n", id); }
};

class Rect : public Shape {
public:
    int w;
    int h;
    Rect(int i, int a, int b) : Shape(i), w(a), h(b) { printf("Rect(%d)\n", id); }
    ~Rect() { printf("~Rect(%d)\n", id); }
    int area() { return w * h; }
    int sides() { return 4; }
};

class Square : public Rect {
public:
    Square(int i, int s) : Rect(i, s, s) { printf("Square(%d)\n", id); }
    ~Square() { printf("~Square(%d)\n", id); }
    void name() { printf("square %d (area %d)\n", id, area()); }
};

class Tri : public Shape {
public:
    int b;
    int h;
    Tri(int i, int bb, int hh) : Shape(i), b(bb), h(hh) {}
    int area() { return b * h / 2; }
    int sides() { return 3; }
};

class Abstract {
public:
    virtual int value() = 0;
    int twice() { return value() * 2; }
};

class Impl : public Abstract {
public:
    int v;
    Impl(int x) : v(x) {}
    int value() { return v + 1; }
};

int describe(Shape *s) {
    s->name();
    return s->area() + s->sides();
}

int main() {
    Shape *list[4];
    list[0] = new Shape(1);
    list[1] = new Rect(2, 3, 4);
    list[2] = new Square(3, 5);
    list[3] = new Tri(4, 6, 7);
    int total = 0;
    for (int i = 0; i < 4; i = i + 1) {
        printf("tag %d area %d\n", list[i]->tag(), list[i]->area());
        total = total + describe(list[i]);
    }
    printf("total %d\n", total);
    Rect r(9, 2, 8);
    Shape &sr = r;
    printf("via ref %d\n", sr.area());
    Impl im(20);
    Abstract *ab = &im;
    printf("abstract %d %d\n", ab->value(), ab->twice());
    for (int i = 0; i < 4; i = i + 1) delete list[i];
    printf("sizes %d %d %d %d\n", (int)sizeof(Shape), (int)sizeof(Rect), (int)sizeof(Abstract), (int)sizeof(Impl));
    // Itanium C++ ABI: the object starts with a pointer into the vtable; offset-to-top is the
    // word just before the first function slot.
    long **vp = *(long ***)&r;
    printf("offset-to-top %ld\n", vp[-2]);
    typedef int (*AreaFn)(Rect *);
    AreaFn fn = (AreaFn)vp[2];
    printf("slot2 area %d\n", fn(&r));
    return 0;
}
