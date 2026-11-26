// A local class's member functions use the static locals of the function around it
// ([class.local] p1; Components/VisualScriptComponent.hpp's event depth guard).
#include <cstdio>
struct Comp {
    int sent = 0;
    void Send(int n) {
        static int depth = 0;
        static const int kMax = 3;
        if (depth >= kMax) { std::printf("dropped at %d\n", n); return; }
        struct Depth { Depth() { ++depth; } ~Depth() { --depth; } } guard;
        ++sent;
        std::printf("send %d depth %d\n", n, depth);
        if (n > 0) Send(n - 1);
    }
};
static int counter() {
    static int calls;
    struct Bump { static int next() { return ++calls; } };
    return Bump::next();
}
int main() {
    Comp c;
    c.Send(5);
    c.Send(1);
    counter();
    std::printf("%d %d\n", c.sent, counter());
    return 0;
}
