// Assignments through float and double references (they were lost), compound ones too, and a
// constructor parameter with the name of the member it sets (x(x)).
extern "C" int printf(const char *fmt, ...);
void P(float in, float &l) { l = in * 2; }
void D(double in, double &l) { l = in + 0.25; }
void Acc(float &a, double &b) {
    a += 1.5f;
    b *= 3.0;
    a -= 0.25f;
}
struct V {
    float x, y;
    V(float x, float y) : x(x), y(y) {}
    void Out(float &l, float &r) const {
        l = x + y;
        r = x - y;
    }
};
int main() {
    float a = 0, c = 0, d = 0;
    double b = 1;
    P(3, a);
    D(2, b);
    printf("%g %g\n", (double)a, b);
    Acc(a, b);
    printf("%g %g\n", (double)a, b);
    V v(5, 2);
    v.Out(c, d);
    printf("%g %g %g %g\n", (double)v.x, (double)v.y, (double)c, (double)d);
    float &ref = c;
    ref = 9;
    ref += 1;
    printf("%g\n", (double)c);
    return 0;
}
