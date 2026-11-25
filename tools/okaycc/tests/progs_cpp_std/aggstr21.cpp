// T{a, "text"} for an aggregate with a std::string member (round 21: "cannot assign this value";
// the field was assigned through (&tmp)->name, which did not find string's operator=).
#include <cstdio>
#include <string>

struct Obj {
    int id;
    std::string name;
};

int main() {
    Obj a;
    a = Obj{1, "alpha"};
    Obj b = Obj{2, "beta"};
    std::string s;
    s = "gamma";
    s = 'x';
    std::printf("%d %s %d %s %s\n", a.id, a.name.c_str(), b.id, b.name.c_str(), s.c_str());
    return 0;
}
