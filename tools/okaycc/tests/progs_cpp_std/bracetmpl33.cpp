// Round 33: a braced list given to a function template parameter that is not an initializer_list
// is a non-deduced context and initializes the parameter ([temp.deduct.call] p1);
// engine/src/Script/EngineApiInternal.hpp's method(n, {tFloat}, {"value"}, ..). And a local hides a
// type of its name: auto T = [&](int b) {..}; T(9) (engine/include/okay/Components/FootIK.hpp).
#include <cstdio>
#include <string>
#include <vector>
#include <functional>
struct Type { int k; };
struct M { std::string name; std::vector<Type> params; std::vector<std::string> names; Type ret; std::function<int(int)> fn; std::string doc; };
struct Ctx {
    Type tFloat{3};
    std::vector<M> ms;
    template <class Fn>
    static M method(std::string n, std::vector<Type> params, std::vector<std::string> names, Type ret, Fn fn, std::string doc) {
        M m; m.name = n; m.params = params; m.names = names; m.ret = ret; m.fn = fn; m.doc = doc; return m;
    }
    void Bind() {
        auto f1 = [&](const char* n, int (*fp)(int), const char* doc) {
            ms.push_back(method(n, {tFloat}, {"value"}, tFloat, [fp](int a) { return fp(a) + 1; }, doc));
        };
        f1("Abs", [](int x) { return x < 0 ? -x : x; }, "abs");
        ms.push_back(method("Two", {tFloat, tFloat}, {"a", "b"}, tFloat, [](int a) { return a * 2; }, "d"));
    }
};
template <class Fn>
int listArg(std::vector<Type> params, Fn fn) { return (int)params.size() + fn(1); }
template <class Fn>
int method2(std::vector<int> params, Fn fn) { return (int)params.size() + fn(1); }
template <class Fn>
int method3(std::string s, Fn fn) { return (int)s.size() + fn(1); }
typedef long T;
struct Part {
    int b;
};
int Hips(Part* parts) {
    auto T = [&](int b) -> Part* { return b < 3 ? &parts[b] : nullptr; };
    Part* hip = T(1);
    return hip ? hip->b + (T(5) == nullptr) : -1;
}
int main() {
    Ctx c;
    c.Bind();
    std::printf("%d %d %s %d %d\n", c.ms[0].fn(-4), c.ms[1].fn(5), c.ms[0].names[0].c_str(), (int)c.ms[1].params.size(),
                c.ms[0].ret.k);
    Type t{3};
    std::printf("%d %d %d\n", method2({1, 2}, [](int a) { return a; }), method3("abc", [](int a) { return a; }),
                listArg({t}, [](int a) { return a; }));
    Part parts[3] = {{10}, {20}, {30}};
    T big = 1L << 40;
    std::printf("%d %ld\n", Hips(parts), (long)(big >> 39));
    return 0;
}
