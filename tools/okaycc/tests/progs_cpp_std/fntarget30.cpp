// std::function::target<T>() ([func.wrap.func.targ]): the stored callable when it is a T, else null
// (engine/src/Scripting/OkayScriptVMInternal.hpp:606).
#include <cstdio>
#include <functional>
#include <map>
#include <string>
struct Call {
    int n;
    int operator()(int x) const { return x + n; }
};
struct Other {
    int operator()(int x) const { return x * 2; }
};
static int plain(int x) { return x - 1; }
int main() {
    std::map<std::string, std::function<int(int)>> m;
    m["a"] = Call{5};
    m["b"] = Other{};
    m["c"] = plain;
    for (const auto& kv : m) {
        const Call* c = kv.second.target<Call>();
        std::printf("%s %d %d\n", kv.first.c_str(), c ? c->n : -1, kv.second(10));
    }
    auto it = m.find("a");
    if (it != m.end() && it->second.target<Call>()) std::printf("found Call\n");
    if (!m["b"].target<Call>()) std::printf("b is not Call\n");
    Call* w = m["a"].target<Call>();
    w->n = 7;
    std::printf("%d\n", m["a"](1));
    return 0;
}
