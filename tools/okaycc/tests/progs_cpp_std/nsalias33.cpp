// Round 33: N::T of an alias declared in namespace N, when another namespace's T was brought into
// an enclosing namespace by a using-declaration ([namespace.qual], [namespace.udecl]);
// engine/src/JavaScript/JavaScriptVM.cpp's sc::Value{..} beside using js::Value.
#include <cstdint>
#include <cstdio>
#include <string>
#include <variant>
namespace okay {
namespace js {
struct Value {
    double n = 0;
    static Value Num(double d) {
        Value v;
        v.n = d;
        return v;
    }
};
}  // namespace js
namespace script {
using Value = std::variant<std::monostate, bool, std::int32_t, float, std::string>;
typedef struct Pair {
    int a = 4, b = 5;
} Two;
}  // namespace script
using js::Value;
namespace sc = script;
struct Two {
    int only = 9;
};
bool FromJs(const Value& v, sc::Value& out) {
    if (v.n < 0) return false;
    out = sc::Value{(float)v.n};
    return true;
}
int Sum() {
    sc::Two t;
    okay::Two u;
    return t.a + t.b + u.only;
}
}  // namespace okay
int main() {
    okay::script::Value o;
    bool ok = okay::FromJs(okay::js::Value::Num(2.5), o);
    bool no = okay::FromJs(okay::js::Value::Num(-1), o);
    okay::Value plain = okay::Value::Num(7);
    std::printf("%d %d %d %.1f %.0f %d\n", ok, no, (int)o.index(), std::get<float>(o), plain.n, okay::Sum());
    return 0;
}
