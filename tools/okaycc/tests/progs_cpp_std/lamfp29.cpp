// A lambda with no captures converts to a pointer to a function ([expr.prim.lambda.closure] p8):
// assigned, passed, stored in a table and compared, as for Log::sink and Lua's printSink.
#include <cstdio>
#include <string>
using Fn = int (*)(int);
using Sink = void (*)(const std::string &);
static int apply(Fn f, int x) { return f(x); }
static Sink sink = nullptr;
struct Logger {
    void (*onLine)(int level, const char *text) = nullptr;
};
int main() {
    Fn f = [](int a) { return a + 1; };
    std::printf("%d %d\n", f(2), apply([](int a) { return a * 3; }, 5));
    sink = [](const std::string &s) { std::printf("sink: %s\n", s.c_str()); };
    sink("hello");
    Logger lg;
    lg.onLine = [](int level, const char *text) { std::printf("%d %s\n", level, text); };
    lg.onLine(2, "warn");
    Fn table[2] = {[](int a) { return -a; }, f};
    std::printf("%d %d %d\n", table[0](4), table[1](4), table[1] == f);
    double (*half)(double) = [](double d) -> double { return d / 2; };
    std::printf("%g\n", half(3.0));
    return 0;
}
