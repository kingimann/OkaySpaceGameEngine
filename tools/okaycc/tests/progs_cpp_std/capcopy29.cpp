// A lambda that captures a class with a destructor by copy ([expr.prim.lambda.capture] p10, p15):
// the closure holds a copy made by the copy constructor, copying the closure copies it, and
// destroying the closure destroys it. Counts live objects at each step (the same with or without
// copy elision).
#include <cstdio>
#include <functional>
#include <memory>
#include <string>

static int live = 0;
struct Tracked {
    int v;
    Tracked(int x) : v(x) { ++live; }
    Tracked(const Tracked &o) : v(o.v) { ++live; }
    ~Tracked() { --live; }
};

struct State {
    float progress = 0.0f;
    int phase = 0;
};

static int apply(std::function<int(int)> f, int x) { return f(x); }

int main() {
    {
        Tracked t(5);
        std::printf("a %d\n", live);
        auto f = [t](int k) { return t.v * k; };
        std::printf("b %d %d\n", live, f(3));
        {
            auto g = f;
            std::printf("c %d %d\n", live, g(2));
        }
        std::printf("d %d\n", live);
        std::function<int(int)> h = f;
        std::printf("e %d %d\n", live, h(4));
        int r = apply(h, 6);
        std::printf("f %d %d\n", r, live);
    }
    std::printf("g %d\n", live);

    auto state = std::make_shared<State>();
    std::function<void(float)> onProgress;
    std::function<void(bool)> onDone;
    onProgress = [state](float p) { state->progress = p; };
    onDone = [state](bool ok) { state->phase = ok ? 1 : 2; };
    std::printf("h %ld\n", (long)state.use_count());
    onProgress(0.5f);
    onDone(true);
    std::printf("i %.2f %d\n", state->progress, state->phase);
    {
        std::function<void(float)> copy = onProgress;
        std::printf("j %ld\n", (long)state.use_count());
    }
    onProgress = nullptr;
    std::printf("k %ld\n", (long)state.use_count());

    std::string name = "loading";
    int n = 2;
    auto show = [=]() { return name + ":" + std::to_string(n); };
    name = "changed";
    std::printf("l %s %s\n", show().c_str(), name.c_str());
    {
        Tracked a(1), b(2);
        auto both = [a, &b]() { return a.v + b.v; };
        b.v = 10;
        std::printf("m %d %d\n", both(), live);
    }
    std::printf("n %d\n", live);
    return 0;
}
