// Round 19: a lambda that calls another lambda it captured, a lambda inside a lambda that uses a
// variable of the outer function, and a closure passed on as a template argument.
extern "C" int printf(const char *fmt, ...);

template <typename F>
int callWith(F f, int v) {
    return f(v);
}

int main() {
    int base = 100;
    int counts[3] = {1, 2, 3};
    auto bytesAt = [&](int i, int scale) { return counts[i] * scale + base; };
    int total = 0;
    auto push = [&](int i) {
        int a = bytesAt(i, 10);
        int b = bytesAt(i, 1);
        total += a - b;
    };
    for (int i = 0; i < 3; ++i) push(i);
    printf("nested %d\n", total);
    int k = 3;
    auto outer = [&](int x) {
        auto inner = [&](int y) { return x * y + k + base; };
        return inner(2) + inner(3);
    };
    printf("inner %d %d\n", outer(1), outer(5));
    auto viaCopy = [=](int x) {
        auto inner = [=](int y) { return x + y + k; };
        return inner(x);
    };
    printf("copy %d\n", viaCopy(4));
    printf("arg %d\n", callWith([&](int v) { return bytesAt(v, 2); }, 1));
    return 0;
}
