// Names in the parameters of a member function defined outside its class are looked up in the
// class ([basic.lookup.unqual] p8 / [class.mfct]): BtValue::FromText(Type type, ...).
#include <cstdio>
#include <string>
namespace ai {
struct BtValue {
    enum class Type { Bool, Number, Text };
    using Num = float;
    struct Part { int a; };
    Type type = Type::Bool;
    float number = 0;
    static BtValue FromText(Type type, const std::string &s);
    static BtValue Make(Num n, Part p);
    int Kind(Type t) const;
    BtValue() {}
    BtValue(Type t, Num n);
};
BtValue::BtValue(Type t, Num n) : type(t), number(n) {}
BtValue BtValue::FromText(Type type, const std::string &s) {
    BtValue v;
    v.type = type;
    v.number = (float)s.size();
    return v;
}
BtValue BtValue::Make(Num n, Part p) {
    BtValue v;
    v.number = n + p.a;
    return v;
}
int BtValue::Kind(Type t) const { return (int)t * 10 + (int)type; }
}
int main() {
    ai::BtValue v = ai::BtValue::FromText(ai::BtValue::Type::Text, "abcd");
    ai::BtValue w = ai::BtValue::Make(1.5f, {2});
    std::printf("%d %g %g %d\n", (int)v.type, v.number, w.number, v.Kind(ai::BtValue::Type::Number));
    ai::BtValue c(ai::BtValue::Type::Number, 2.5f);
    std::printf("%d %g\n", (int)c.type, c.number);
}
