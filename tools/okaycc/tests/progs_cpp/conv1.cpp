extern "C" int printf(const char *fmt, ...);
struct Flag {
    int v;
    Flag(int n) : v(n) {}
    operator bool() const { return v != 0; }
};
struct Meters {
    int m;
    Meters(int n) : m(n) {}
    operator int() const { return m; }
};
int twice(int x) { return x * 2; }
int main() {
    Flag on(3);
    Flag off(0);
    if (on) printf("on\n");
    if (off) printf("off wrongly\n");
    if (!off) printf("not off\n");
    Meters a(10);
    Meters b(5);
    int sum = a + b;
    int cmp = a > b;
    int viaCall = twice(a);
    int assigned;
    assigned = b;
    printf("%d %d %d %d\n", sum, cmp, viaCall, assigned);
    return a - b;
}
