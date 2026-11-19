extern "C" int printf(const char *fmt, ...);
struct H { int v; H(int n) : v(n) { printf("H(%d)\n", n); } ~H() { printf("~H(%d)\n", v); } };
H hb(7);
int geta();
int main() { printf("main %d %d\n", geta(), hb.v); return 0; }
