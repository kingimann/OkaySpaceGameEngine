// Round 24: blockers of engine headers: an enumerator from a character ('A', [dcl.enum] p2);
// noexcept on a constructor; C::operator=(..) defined outside the class; a delegating constructor
// to one declared later; vector<int>{} as an operand of ?:.
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
enum class PinType : char { Any = 'A', Number = 'N' };
struct Sock {
    int fd;
    Sock() : fd(3) {}
    Sock(const Sock &) = delete;
    Sock(Sock &&o) noexcept { fd = o.fd; o.fd = -1; }
    Sock &operator=(Sock &&o) noexcept;
};
Sock &Sock::operator=(Sock &&o) noexcept { fd = o.fd; o.fd = -1; return *this; }
struct Arg {
    std::string text;
    double number;
    Arg(float v) : Arg((double)v) {}
    Arg(double v);
};
Arg::Arg(double v) : text("d"), number(v) {}
int main() {
    Sock a;
    Sock b(std::move(a));
    bool has = b.fd > 0;
    std::vector<int> src = {1, 2};
    std::vector<int> cols = has ? src : std::vector<int>{};
    Arg g(2.5f);
    Sock c;
    c = std::move(b);
    printf("moved %d %d\n", b.fd, c.fd);
    printf("%c %c %d %d %d %s %.1f\n", (char)PinType::Any, (char)PinType::Number, a.fd, b.fd, (int)cols.size(), g.text.c_str(), g.number);
}
