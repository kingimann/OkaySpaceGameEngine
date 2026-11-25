// A bool& parameter (round 21: it was turned into 0 or 1 on entry like a bool, so a store through
// it wrote to address 1).
extern "C" int printf(const char *fmt, ...);
struct Nd { int v; };
void f1(Nd *&parent) { parent = nullptr; }
void f2(bool &left, int k) { left = k > 2; }
void f3(const int &k, bool &left) { left = k > 2; }
int main() {
    Nd *p = (Nd *)8;
    f1(p);
    printf("f1 %d\n", p == nullptr ? 1 : 0);
    bool l = false;
    f2(l, 3);
    printf("f2 %d\n", (int)l);
    l = false;
    f3(3, l);
    printf("f3 %d\n", (int)l);
    return 0;
}
