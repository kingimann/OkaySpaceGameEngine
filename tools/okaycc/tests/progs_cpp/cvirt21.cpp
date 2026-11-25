// A const virtual function overridden by a const one (round 21: the base slot was missed, so the
// base's function ran).
extern "C" int printf(const char *fmt, ...);
struct B { int id; B(int i) : id(i) {} virtual ~B() {} virtual int kind() const { return 1; } };
struct D : B { int e; D(int i, int x) : B(i), e(x) {} int kind() const override { return 2; } };
struct B2 { int id; B2(int i) : id(i) {} virtual ~B2() {} virtual int kind() { return 1; } };
struct D2 : B2 { D2(int i) : B2(i) {} int kind() override { return 2; } };
int main() {
    B *r = new D(1, 2);
    B2 *s = new D2(1);
    printf("%d %d\n", r->kind(), s->kind());
    return 0;
}
