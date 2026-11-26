// Round 28: inheriting constructors, using B::B; ([namespace.udecl] p3, [class.inhctor.init];
// Scripting/ScriptApiRegistry.hpp:33 inherits std::runtime_error's).
#include <cstdio>
#include <stdexcept>
#include <string>
struct ScriptApiError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct B {
    int a, b;
    std::string s;
    B(int x) : a(x), b(0) {}
    B(int x, int y, const char* t = "dflt") : a(x), b(y), s(t) {}
    B(std::string&& t) : a(-1), b(-1), s(static_cast<std::string&&>(t)) {}
};
struct D : B {
    using B::B;
    int c = 7;
};
int main() {
    try {
        throw ScriptApiError("bad call");
    } catch (const std::exception& e) {
        std::printf("%s\n", e.what());
    }
    ScriptApiError e2(std::string("str"));
    D d(3), d2(4, 5), d3(6, 7, "given"), d4(std::string("moved"));
    std::printf("%s %d %d %d | %d %d %s | %s %d | %s %d\n", e2.what(), d.a, d.b, d.c, d2.a, d2.b, d2.s.c_str(),
                d3.s.c_str(), d3.c, d4.s.c_str(), d4.a);
    return 0;
}
