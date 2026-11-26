// Round 32: member initializers in braces for members whose class is an aggregate
// (engine/src/Scripting/OkayScriptVMInternal.hpp:568, `tf{&r}, findNamed{&r, HostSceneFn{&r}}`):
// aggregate initialization ([class.base.init] p7, [dcl.init.list] p3.4). One value of the class
// itself is a copy (p3.2). Before: `tf{&r}` assigned the pointer to the whole member.
#include <cstdio>
#include <map>
#include <string>
struct Runtime { std::map<std::string, int> builtins; int host = 3; };
using Table = decltype(Runtime::builtins);
struct TfFn { Runtime* rt; int operator()() const { return rt->host; } };
struct SceneFn { Runtime* rt; };
struct FindFn { Runtime* rt; SceneFn sc; int operator()() const { return sc.rt->host + 1; } };
template <class T> struct AnimFn { TfFn self; T operator()() const { return (T)self() * 2; } };
struct Named { std::string name; int n = 7; };
struct Ctx {
    explicit Ctx(Runtime& r, const TfFn& other)
        : b(r.builtins), tf{&r}, copy{other}, findNamed{&r, SceneFn{&r}}, half{TfFn{&r}}, named{"nm"}, pair{"pr", 2} {}
    Table& b;
    TfFn tf;
    TfFn copy;
    FindFn findNamed;
    AnimFn<float> half;
    Named named;
    Named pair;
};
int main() {
    Runtime r;
    Runtime r2;
    r2.host = 40;
    Ctx c(r, TfFn{&r2});
    c.b["x"] = 1;
    std::printf("%d %d %d %g %d | %s %d %s %d\n", c.tf(), c.copy(), c.findNamed(), c.half(), (int)r.builtins.size(),
                c.named.name.c_str(), c.named.n, c.pair.name.c_str(), c.pair.n);
    return 0;
}
