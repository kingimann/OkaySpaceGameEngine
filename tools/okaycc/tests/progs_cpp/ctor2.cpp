// Members with their own constructors, implicit constructors and destructors,
// member functions defined outside the class, overloads, and "this".
extern "C" int printf(const char *fmt, ...);

struct Tracker {
    int id;
    Tracker(int n) : id(n) { printf("  make %d\n", id); }
    ~Tracker() { printf("  free %d\n", id); }
};

struct Holder {
    Tracker first;
    Tracker second;
    int extra;
    Holder(int a, int b);
    ~Holder();
    int total() const;
    int add(int x) { return extra + x; }
    int add(int x, int y) { return extra + x + y; }
    Holder *self() { return this; }
    int twice() { return add(1) + this->add(2, 3); }
};

Holder::Holder(int a, int b) : first(a), second(b), extra(a * b) {
    printf("  Holder body %d\n", extra);
}

Holder::~Holder() { printf("  Holder dtor\n"); }

int Holder::total() const { return first.id + second.id + extra; }

struct Wrapper {
    Holder h;
    Tracker t;
    Wrapper() : h(3, 4), t(9) { printf("  Wrapper ctor\n"); }
};

struct Plain {
    int a;
    int b;
};

struct Quiet {
    Plain p;
    Tracker t;
    Quiet() : t(77) {}
};

int main() {
    printf("one\n");
    {
        Holder h(2, 5);
        printf("total %d twice %d same %d\n", h.total(), h.twice(), h.self() == &h);
    }
    printf("two\n");
    {
        Wrapper w;
        printf("w total %d\n", w.h.total());
    }
    printf("three\n");
    {
        Quiet q;
        q.p.a = 4;
        q.p.b = 5;
        printf("quiet %d %d %d\n", q.p.a, q.p.b, q.t.id);
    }
    printf("four\n");
    Tracker *t = new Tracker(5);
    Holder *h = new Holder(6, 7);
    printf("heap %d %d\n", t->id, h->total());
    delete h;
    delete t;
    printf("sizes %d %d %d\n", (int)sizeof(Holder), (int)sizeof(Wrapper), (int)sizeof(Quiet));
    return 0;
}
