// std::string rfind of a string, compare of a part ([string.compare]), find_first_of of a string,
// and the relational operators.
#include <cstdio>
#include <string>
int main() {
    std::string k = "quadinout", d = "in";
    std::printf("%d %d\n", (int)k.rfind(d, 0), (int)k.rfind(std::string("out")));
    std::string h = "steps(";
    std::string t = "steps(4)";
    std::printf("%d %d %d\n", t.compare(0, h.size(), h), (int)(t.compare(0, 3, "stx") < 0), t.compare(6, 1, "4"));
    std::printf("%d %d\n", t.compare(0, 5, h, 0, 5), (int)t.find_first_of(std::string("()")));
    std::printf("%d %d %d %d\n", (int)(k > d), (int)(k <= d), (int)(d >= d), (int)(t.find_last_of('s')));
}
