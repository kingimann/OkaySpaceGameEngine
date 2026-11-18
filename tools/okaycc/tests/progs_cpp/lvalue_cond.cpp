extern "C" int printf(const char *fmt, ...);
struct P { int x; int y; };
struct Q { int v; Q(int a) : v(a) {} int get() const { return v; } void set(int n) { v = n; } };
int main() {
    P a; P b;
    a.x = 1; a.y = 2; b.x = 10; b.y = 20;
    int pick = 1;
    (pick ? a : b).x = 7;
    (pick ? b : a).y = 8;
    printf("%d %d %d %d\n", a.x, a.y, b.x, b.y);
    Q q(3);
    Q r(4);
    (pick ? q : r).set(9);
    printf("%d %d\n", q.get(), r.get());
    P c = pick ? a : b;
    printf("%d\n", c.x + c.y);
    return 0;
}
