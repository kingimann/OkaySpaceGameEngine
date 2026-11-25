// Round 20: a static local array with a braced list (static const E all[] = {..}) was all zero, and
// a static char array set from a string literal was zero too (engine/src/Texture/Bc.cpp
// ParseBcFormat found only "bc1").
extern "C" int printf(const char *fmt, ...);

namespace tex {
enum class F { A, C, D, B };
int index(int want) {
    static const F all[] = {F::A, F::B, F::C, F::D};
    int i = 0;
    for (F f : all) {
        if ((int)f == want) return i;
        ++i;
    }
    return -1;
}
}  // namespace tex

int counter(int k) {
    static int tab[4] = {5, 6};
    static char tag[8] = "ok";
    tab[3] += k;
    tag[2] = (char)('a' + k);
    printf("%s ", tag);
    return tab[k] + tab[3];
}

// A static local float set from a constant.
float scaled(float v) {
    static const float k = 0.25f;
    static double off = -1.5;
    return v * k + (float)off;
}

int main() {
    printf("%d %d %d %d %d\n", tex::index(0), tex::index(1), tex::index(2), tex::index(3), tex::index(7));
    int a = counter(0);
    int b = counter(1);
    int c = counter(2);
    printf("%d %d %d\n", a, b, c);
    printf("%.3f %.3f\n", scaled(2.0f), scaled(10.0f));
    return 0;
}

