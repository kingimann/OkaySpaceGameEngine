// Round 23: delegating constructors ([class.base.init] p6; "'Random' is not a member or base" in 8
// engine files) and braced member initializers m{v} ("expected '(' after a member initializer").
#include <cstdio>
#include <string>
struct Gen {
    Gen() : Gen(0x9E37u) {}
    explicit Gen(unsigned seed) : s{seed}, name{"gen"} { ++made; }
    Gen(float f) : Gen((unsigned)(f * 10.0f)) { name += "-f"; }
    unsigned s;
    std::string name;
    static int made;
};
int Gen::made = 0;
struct Arg {
    Arg(int v) : text(std::to_string(v)), number(v) {}
    Arg(float v) : Arg((int)v) { text += "f"; }
    std::string text;
    double number;
};
int main() {
    Gen a;
    Gen b(7u);
    Gen c(2.5f);
    Arg x(3);
    Arg y(4.5f);
    std::printf("%u %u %u %s %s %d\n", a.s, b.s, c.s, b.name.c_str(), c.name.c_str(), Gen::made);
    std::printf("%s %.1f %s %.1f\n", x.text.c_str(), x.number, y.text.c_str(), y.number);
    return 0;
}
