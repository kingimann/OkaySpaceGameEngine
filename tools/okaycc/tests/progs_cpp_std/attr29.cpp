// Attributes ([dcl.attr.grammar], [dcl.attr.noreturn], [dcl.attr.deprecated], [dcl.attr.fallthrough],
// [dcl.attr.unused], [dcl.attr.nodiscard]) on functions, members, statements and variables, as in
// Lua/Lua.hpp.
#include <cstdio>
#include <cstdlib>
#include <string>
struct State {
    [[noreturn]] void Error(const std::string &msg);
    [[nodiscard]] int Count() const { return n; }
    [[deprecated("use Count")]] int Size() const { return n; }
    int n = 3;
};
void State::Error(const std::string &msg) {
    std::printf("error: %s\n", msg.c_str());
    std::exit(0);
}
[[nodiscard]] static int twice(int v) { return 2 * v; }
int main() {
    [[maybe_unused]] int unused = 4;
    int a[3] = {1, 2, 3};
    int k = a[a[0]];
    State s;
    switch (s.Count()) {
    case 3:
        k += 10;
        [[fallthrough]];
    case 4:
        k += 100;
        break;
    }
    std::printf("%d %d\n", k, twice(s.Count()));
    s.Error("stop");
}
