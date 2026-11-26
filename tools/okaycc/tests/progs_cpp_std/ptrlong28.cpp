// Round 28, silent wrong code: a pointer cast to a signed 64-bit integer kept the pointer's step,
// so (long)p + 1 added sizeof(*p) instead of 1 ([expr.reinterpret.cast] p4: the result is a number).
#include <cstdio>
struct T {
    int a = 1;
    float m = 2.5f;
    int g = 5;
};
int main() {
    T t;
    int* p = &t.g;
    long d1 = (long)p + 1 - (long)p;
    long d2 = (long)&t.g + 3 - (long)&t.a;
    long d3 = (long)(int*)0 + 1;
    long d4 = (long)&((T*)0)->g + 1;
    long long d5 = (long long)(double*)0 + 2;
    std::printf("%ld %ld %ld %ld %lld\n", d1, d2, d3, d4, d5);
    return 0;
}
