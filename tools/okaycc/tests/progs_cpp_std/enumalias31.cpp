// Enumerators through a member alias of an enumeration (B::K::Int with using K = ParamDef::Kind)
// and an alias in a member function (using I = Transition::Interrupt), and std::tolower from
// <string>.
#include <cstdio>
#include <string>
namespace eng {
struct ParamDef {
    enum class Kind { Float, Int, Bool, Choice };
    Kind kind = Kind::Float;
};
}  // namespace eng
namespace kit {
using namespace eng;
struct B {
    using K = ParamDef::Kind;
    int count = 0;
    void P(const char* id, K k = K::Float) { count += (int)k; std::printf("%s %d\n", id, (int)k); }
};
}  // namespace kit
using namespace kit;
struct Machine {
    struct Transition {
        enum class Interrupt { None, NextState, CurrentState };
        Interrupt rule = Interrupt::NextState;
    };
    Transition tr;
    int Step() const {
        using I = Transition::Interrupt;
        const I rule = tr.rule;
        switch (rule) {
            case I::None: return 0;
            case I::NextState: return 5;
            case I::CurrentState: return 9;
        }
        return -1;
    }
};
int main() {
    Machine m;
    std::printf("step %d\n", m.Step());
    B b;
    b.P("a");
    b.P("shelves", B::K::Int);
    b.P("open", kit::B::K::Bool);
    std::string s = "MiXeD";
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    std::printf("%d %s\n", b.count, s.c_str());
    return 0;
}
