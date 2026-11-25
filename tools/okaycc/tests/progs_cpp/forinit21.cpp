// A for init-statement of two declarators, or of an object with a constructor (round 21: the names
// were out of scope in the loop, "undeclared identifier 'it'").
extern "C" int printf(const char *fmt, ...);
struct It {
    int v;
    It(int x) : v(x) {}
};
It mk() { return It(1); }
int main() {
    int n = 0;
    for (It it = mk(); it.v < 3; ++it.v) n += it.v;
    for (int i = 0, t = 10; i < 3; ++i, --t) n += i * t;
    int k = 0;
    for (int a = 5, *p = &k; a < 7; ++a) *p += a;
    printf("%d %d\n", n, k);
    return 0;
}
