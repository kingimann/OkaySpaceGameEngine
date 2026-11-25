// An unscoped enumeration declared in a class: its enumerators are members of that class
// ([dcl.enum] p11, [class.member.lookup]). Outside the class, a function of the same name is
// called (BtValue::Number(f) beside BtParamDef::Number in AI/BehaviorTree.hpp).
#include <cstdio>
struct Value {
    float n = 0;
    static Value Number(float v) { Value b; b.n = v; return b; }
    static Value FromText(float f);
};
struct ParamDef {
    enum Kind { Number, Int, Text };
    Kind kind = Int;
    int Twice() const { return kind == Number ? 0 : (int)kind * 2; }
    struct Inner { int k = Text; };
};
struct Derived : ParamDef {
    int Base() const { return Text + Number; }
};
Value Value::FromText(float f) { return Number(f * 2); }
// A scoped enumeration whose list names its own Text, after ParamDef::Text (Input/GameInput.hpp).
enum class Lock : unsigned { Bag = 1u << 1, Text = 1u << 2, World = Bag | Text };
int main() {
    Value v = Value::FromText(1.5f);
    ParamDef p;
    Derived d;
    ParamDef::Inner in;
    std::printf("%g %d %d %d %d %d %u\n", v.n, p.Twice(), d.Base(), in.k, (int)ParamDef::Text, ParamDef::Number,
                (unsigned)Lock::World);
}
