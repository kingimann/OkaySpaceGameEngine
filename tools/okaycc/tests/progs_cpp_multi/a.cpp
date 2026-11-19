extern "C" int printf(const char *fmt, ...);
struct G { int v; G(int n) : v(n) { printf("G(%d)\n", n); } ~G() { printf("~G(%d)\n", v); } };
G ga(5);
int geta() { return ga.v; }
