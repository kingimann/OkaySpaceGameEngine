// std::function ([func.wrap.func]): a class template partial specialization with a parameter pack
// (function<R(A...)>), holding lambdas, function pointers and function objects.
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

static int twice(int x) { return 2 * x; }

struct Adder {
    int base;
    int operator()(int a, int b) const { return base + a + b; }
};

using Sink = std::function<void(int, const std::string &)>;
static Sink gSink;

static void emit(int level, const std::string &s) {
    if (gSink) gSink(level, s);
    else std::printf("no sink %d %s\n", level, s.c_str());
}

int main() {
    std::function<int(int)> f = twice;
    std::printf("%d\n", f(21));
    int k = 5;
    f = [k](int x) { return x + k; };
    std::printf("%d\n", f(1));
    std::function<int(int, int)> g = Adder{100};
    std::printf("%d\n", g(2, 3));
    std::function<int()> h;
    std::printf("%d %d\n", (bool)h, h == nullptr);
    h = [] { return 7; };
    std::printf("%d %d\n", (bool)h, h());
    std::function<int()> h2 = h;
    std::function<int()> h3 = std::move(h2);
    std::printf("%d %d\n", h3(), (bool)h);
    emit(1, "a");
    std::string log;
    gSink = [&log](int lv, const std::string &s) { log += (char)('0' + lv); log += ":" + s + ";"; };
    emit(2, "b");
    emit(3, "c");
    std::printf("%s\n", log.c_str());
    std::vector<std::function<void()>> tasks;
    int total = 0;
    for (int i = 1; i <= 4; ++i) tasks.push_back([&total, i] { total += i * i; });
    for (auto &t : tasks) t();
    std::printf("%d\n", total);
    gSink = nullptr;
    emit(4, "d");
    return 0;
}
