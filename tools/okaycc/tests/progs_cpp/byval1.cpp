// By-value class arguments (copy constructor, destructor after the statement), implicit
// member-wise copy constructors, a const reference bound to a temporary, temporaries that are
// destroyed, and operator chains whose left operand is a call result (a + b + c).
extern "C" int printf(const char *fmt, ...);

struct A {
    int v;
    A(int x) : v(x) { printf("  A(%d)\n", v); }
    A(const A &o) : v(o.v + 1) { printf("  copy A(%d) from %d\n", v, o.v); }
    ~A() { printf("  ~A(%d)\n", v); }
};

struct H {
    A a;
    int z;
    H(int q) : a(q), z(q * 2) {}
};

struct D : A {
    int w;
    D(int q) : A(q), w(1) {}
};

struct V {
    int x;
    int y;
    V(int a, int b) : x(a), y(b) {}
    V operator+(const V &o) const { return V(x + o.x, y + o.y); }
    V operator*(int k) const { return V(x * k, y * k); }
    ~V() { printf("  ~V(%d,%d)\n", x, y); }
};

int take(A a) { return a.v; }
int takeH(H h) { return h.a.v + h.z; }
int peek(const A &a) { return a.v; }
int sumV(const V &v) { return v.x + v.y; }

int main() {
    A a(10);
    printf("by value\n");
    printf("%d\n", take(a));
    printf("implicit copy\n");
    H h(5);
    H h2 = h;
    printf("%d\n", takeH(h2));
    D d(3);
    D e = d;
    printf("%d %d\n", d.v, e.v);
    printf("const ref to temporary\n");
    printf("%d\n", peek(A(7)));
    printf("temporary destroyed\n");
    V r(0, 0);
    r = V(8, 9);
    printf("%d\n", r.x + r.y);
    printf("chain\n");
    V p(1, 2);
    V q(10, 20);
    V s(100, 200);
    V t = p + q + s;
    printf("%d %d\n", t.x, t.y);
    printf("%d\n", sumV(p + q * 2));
    printf("end\n");
    return 0;
}
