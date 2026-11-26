// A member function or data member with the name of a class from a namespace hides that class
// inside the member functions ([class.member.lookup]): Value(i) calls the member function
// (Components/UIAppParts.hpp:927, with okay::uivars::Value).
#include <cstdio>
#include <string>
namespace vars {
struct Value {
    int n;
    explicit Value(int v) : n(v) {}
};
struct Scale {
    float f = 2.0f;
};
}  // namespace vars
using namespace vars;
struct Form {
    std::string names[3] = {"a", "bb", "ccc"};
    int Scale = 3;
    std::string Value(int i) const { return names[i]; }
    std::string All() const {
        std::string all;
        for (int i = 0; i < 3; ++i) {
            all += Value(i);
            all += ',';
        }
        return all + std::to_string(Scale * 2);
    }
    int Later() const;
};
int Form::Later() const { return (int)Value(2).size() + Scale; }
struct Sub : Form {
    std::string Twice() const { return Value(1) + Value(1); }
};
struct Maker {
    static vars::Scale Scale(float f) {
        vars::Scale s;
        s.f = f;
        return s;
    }
    float Use() const { return Scale(1.5f).f; }
    float Out() const;
};
float Maker::Out() const { return Scale(4.0f).f; }
int main() {
    Form f;
    Sub s;
    vars::Value v(7);
    std::printf("%s %d %s %d\n", f.All().c_str(), f.Later(), s.Twice().c_str(), v.n);
    Maker m;
    std::printf("%.1f %.1f\n", m.Use(), m.Out());
    return 0;
}
