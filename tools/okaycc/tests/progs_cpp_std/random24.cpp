// Round 24: <random>: mt19937 and mt19937_64 (default seed, a given seed, the 10000th value that
// [rand.predef] states), reseeding, uniform_real_distribution of double and float, random_device.
#include <cstdio>
#include <random>
int main() {
    std::mt19937 a;
    std::mt19937_64 b;
    for (int i = 1; i < 10000; ++i) {
        a();
        b();
    }
    printf("%u %llu\n", (unsigned)a(), (unsigned long long)b());
    std::mt19937 g(7);
    unsigned g1 = (unsigned)g();
    unsigned g2 = (unsigned)g();
    unsigned g3 = (unsigned)g();
    printf("%u %u %u\n", g1, g2, g3);
    std::mt19937_64 h(42);
    printf("%llu\n", (unsigned long long)h());
    g.seed(7);
    std::uniform_real_distribution<double> j(-0.06, 0.06);
    std::uniform_real_distribution<float> k(0.0f, 1.0f);
    for (int i = 0; i < 3; ++i) {
        double dj = j(g);
        float fk = k(g);
        printf("%.9f %.6f\n", dj, fk);
    }
    std::random_device rd;
    unsigned x = rd() | 1u;
    printf("%d\n", (int)(x & 1u));
}
