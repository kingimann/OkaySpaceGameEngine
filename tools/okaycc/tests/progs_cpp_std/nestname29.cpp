// "class State;" in one namespace after a class State nested in a class of another ([class.nest] p1,
// [dcl.type.elab]): State names the new class there, as in Lua/Lua.hpp after Core/Random.hpp.
#include <cstdio>
namespace okay {
class Random {
public:
    struct State { long a = 9; };
    State s;
};
}
namespace okay::lua {
class State;
using NativeFn = int (*)(State &L, int nargs);
class State {
public:
    int x = 1;
    void Register(const char *n, NativeFn fn) { std::printf("%s %d\n", n, fn(*this, 2)); }
};
}
namespace okay::lua {
namespace {
int A(State &L, int n) { return L.x + n; }
}
void Open(State &L) { L.Register("a", A); }
}
int main() {
    okay::lua::State s;
    okay::lua::Open(s);
    okay::Random r;
    std::printf("%ld\n", r.s.a);
}
