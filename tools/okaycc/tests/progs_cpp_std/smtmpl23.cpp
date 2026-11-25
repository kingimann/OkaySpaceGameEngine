// Round 23: static member function templates ([temp.mem], [class.static.mfct]), with packs, called
// as C::f, N::C::f, f inside the class, from a nested class, and with explicit arguments.
#include <cstdio>
#include <sstream>
#include <string>
#include <utility>
namespace ok {
struct L {
    template <typename T>
    static int Twice(T x) { return Show((int)x) * 2; }
    template <typename... A>
    static int Sum(A... a) { return (0 + ... + a); }
    static int Show(int x) { std::printf("show %d\n", x); return x; }
    template <typename... Args>
    static void Info(Args&&... args) { Emit("INFO", std::forward<Args>(args)...); }
    struct Cat {
        template <typename... Args>
        void Warn(Args&&... args) const { L::Emit("WARN", std::forward<Args>(args)...); }
    };
private:
    template <typename... Args>
    static void Emit(const char* tag, Args&&... args) {
        std::ostringstream oss;
        (oss << ... << args);
        std::printf("[%s] %s\n", tag, oss.str().c_str());
    }
};
}  // namespace ok
int main() {
    int a = ok::L::Twice(4);
    int b = ok::L::Sum(1, 2, 3);
    int c = ok::L::Twice<long>(5);
    std::printf("%d %d %d %d\n", a, b, c, ok::L::Sum());
    ok::L::Info("x=", 3, " y=", 2.5);
    ok::L::Cat cat;
    cat.Warn("lost ", 7);
    std::string s = "str";
    ::ok::L::Info(s);
    return 0;
}
