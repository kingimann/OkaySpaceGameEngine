// Arrays of objects with constructors whose length is a constant of the block ([dcl.array] p1,
// [expr.const]): constexpr int kMax = 3; Vec rep[kMax]; and kMax * 2.
#include <cstdio>
struct Vec {
    float x = 1, y = 2;
    Vec() {}
    Vec(float a, float b) : x(a), y(b) {}
};
struct Noisy {
    int id;
    Noisy() : id(0) { std::printf("make\n"); }
    ~Noisy() { std::printf("drop %d\n", id); }
};
int main() {
    constexpr int kMax = 3;
    Vec rep[kMax];
    rep[2] = Vec(5, 6);
    float s = 0;
    for (int i = 0; i < kMax; ++i) s += rep[i].x + rep[i].y;
    std::printf("%g\n", s);
    const int n = 2;
    Vec more[n * 2];
    std::printf("%g %d\n", more[3].y, (int)(sizeof(more) / sizeof(more[0])));
    {
        constexpr int k = 2;
        Noisy z[k];
        z[1].id = 7;
    }
    return 0;
}
