// Adjacent string literals are one literal ([lex.phases] phase 6). Before round 22 "a" "b" as a
// call argument was "expected ')'".
#include <cstdio>
#include <string>

static void show(const std::string &s) { std::printf("[%s] %d\n", s.c_str(), (int)s.size()); }

int main() {
    show("one " "two "
         "three");
    const char *p = "x" "y" "z";
    std::printf("%s %d\n", p, (int)sizeof("ab" "cd"));
    std::printf("%s-%s\n", "left" "", "" "right");
    return 0;
}
