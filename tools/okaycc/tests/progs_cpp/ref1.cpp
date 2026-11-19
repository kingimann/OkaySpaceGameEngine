// Reference parameters and reference returns, const references, references to class objects.
extern "C" int printf(const char *fmt, ...);

struct P {
    int x;
    int y;
};

P gp;

P &theGlobal() { return gp; }
int &fieldX(P &p) { return p.x; }
int &pick(P &a, P &b, int which) {
    if (which) return b.y;
    return a.y;
}
void bump(int &r) { r = r + 5; }
void swapInts(int &a, int &b) {
    int t = a;
    a = b;
    b = t;
}
int sumP(const P &p) { return p.x + p.y; }

struct Counter {
    int n;
    Counter() : n(0) {}
    Counter &inc() {
        n = n + 1;
        return *this;
    }
    Counter &add(int k) {
        n = n + k;
        return *this;
    }
    int get() const { return n; }
};

int main() {
    P a;
    a.x = 1;
    a.y = 2;
    P b;
    b.x = 30;
    b.y = 40;
    bump(a.x);
    printf("%d\n", a.x);
    swapInts(a.x, b.x);
    printf("%d %d\n", a.x, b.x);
    fieldX(a) = 7;
    fieldX(b) += 3;
    printf("%d %d\n", a.x, b.x);
    pick(a, b, 0) = 100;
    pick(a, b, 1) = 200;
    printf("%d %d\n", a.y, b.y);
    theGlobal().x = 11;
    theGlobal().y = 12;
    printf("%d %d %d\n", gp.x, gp.y, sumP(gp));
    printf("%d\n", sumP(a));
    Counter c;
    c.inc().inc().add(10).inc();
    printf("%d\n", c.get());
    return a.x + b.y;
}
