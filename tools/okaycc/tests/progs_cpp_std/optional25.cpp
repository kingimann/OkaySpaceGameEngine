// <optional> ([optional]): empty and engaged states, value_or, emplace, reset, comparisons, copies
// and moves of a class value, and bad_optional_access.
#include <cstdio>
#include <optional>
#include <string>
static std::optional<int> parse(const std::string &s) {
    if (s.empty() || s[0] < '0' || s[0] > '9') return std::nullopt;
    return s[0] - '0';
}
struct P {
    int x, y;
    P(int a, int b) : x(a), y(b) {}
};
int main() {
    std::optional<int> a = parse("7"), b = parse("x");
    std::printf("%d %d %d %d\n", (int)a.has_value(), (int)(bool)b, *a, b.value_or(-1));
    std::optional<std::string> s;
    std::printf("%d\n", (int)(s == std::nullopt));
    s = std::string("hello");
    std::optional<std::string> t = s;
    t->append(" world");
    std::printf("%s|%s|%d\n", s->c_str(), t.value().c_str(), (int)(s != t));
    std::optional<std::string> u = std::move(t);
    std::printf("%s %d\n", u->c_str(), (int)u.has_value());
    s.reset();
    std::printf("%d %s\n", (int)s.has_value(), s.value_or("none").c_str());
    std::optional<P> p;
    p.emplace(3, 4);
    std::printf("%d %d\n", p->x, (*p).y);
    try {
        b.value();
    } catch (const std::bad_optional_access &e) {
        std::printf("caught\n");
    }
    std::optional<int> c = 7;
    std::printf("%d %d %d\n", (int)(a == c), (int)(b < a), (int)(c == 7));
    auto m = std::make_optional(2.5);
    std::printf("%g\n", *m);
}
