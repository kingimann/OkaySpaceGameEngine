// A generic lambda with no captures converted to a pointer to a function
// ([expr.prim.lambda.closure] p8): in an initialization, an assignment, an argument.
#include <cstdio>
#include <cstdlib>
typedef int (*BinOp)(int, int);
static int apply(BinOp f, int a, int b) { return f(a, b); }
static double twice(double (*f)(double), double x) { return f(f(x)); }
static int cmpInts(const void* a, const void* b) { return *(const int*)a - *(const int*)b; }
int main() {
    BinOp add = [](auto a, auto b) { return a + b; };
    int (*mul)(int, int) = [](auto a, auto b) { return a * b; };
    std::printf("%d %d %d\n", add(2, 3), mul(4, 5), apply([](auto x, auto y) { return x - y; }, 9, 4));
    std::printf("%.2f\n", twice([](auto v) { return v * 1.5; }, 2.0));
    void (*say)(const char*) = [](const auto* s) { std::printf("[%s]\n", s); };
    say("hi");
    int arr[] = {5, 1, 4};
    int (*cmp)(const void*, const void*) = [](const auto* a, const auto* b) { return cmpInts(a, b); };
    if (cmp(&arr[0], &arr[1]) > 0) { int t = arr[0]; arr[0] = arr[1]; arr[1] = t; }
    BinOp pick;
    pick = [](auto a, auto) { return a; };
    std::printf("%d %d %d %d\n", arr[0], arr[1], arr[2], pick(7, 8));
    return 0;
}
