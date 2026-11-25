// ++ and -- on a reference (round 21: they changed the reference's address, not the object; the
// round 20 compiler printed "1 1 5 5 3").
extern "C" int printf(const char *fmt, ...);
void f1(int &e) { e = 1; --e; --e; }
void f2(int &e) { e = 1; e--; }
void f3(int &e) { --e; }
void f4(long &e) { --e; }
void f5(int &e) { e += 3; ++e; }
int main() {
    int a = 0, b = 0, c = 5, e = 0;
    long d = 5;
    f1(a); f2(b); f3(c); f4(d); f5(e);
    printf("%d %d %d %ld %d\n", a, b, c, d, e);
    return 0;
}
