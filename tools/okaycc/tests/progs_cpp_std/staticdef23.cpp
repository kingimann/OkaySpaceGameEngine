// Round 23: definitions outside the class of static data members of class type (T C::x = e;),
// through a using-declaration (using ok::Log; Log::Cat, Log::Level::B), a class parameter given a
// value of another type (f(std::string) with "abc", [over.ics.user]), a member enum as C::E, and a
// path parameter given a std::string. Each failed before ("cannot assign this value", "cannot
// assign argument", "undeclared identifier", "cannot bind this reference argument").
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
namespace ok {
struct Log {
    enum class Level { A, B, C };
    struct Cat {
        explicit Cat(std::string n) : m(std::move(n)) {}
        std::string m;
    };
    using Sink = std::function<void(int)>;
    static Sink sink;
    static std::string name;
    static Level level;
};
inline Log::Sink Log::sink = nullptr;
std::string Log::name = "logger";
Log::Level Log::level = Log::Level::C;
}  // namespace ok
static int show(std::string s, int n) { std::printf("[%s] %d\n", s.c_str(), n); return n; }
int main() {
    using ok::Log;
    Log::Cat c("net");
    ok::Log::Cat d("x");
    int lv = (int)Log::Level::B;
    std::printf("%s %s %d %d %s %d\n", c.m.c_str(), d.m.c_str(), lv, (int)(bool)Log::sink, Log::name.c_str(),
                (int)Log::level);
    Log::sink = [](int v) { std::printf("sink %d\n", v); };
    Log::sink(5);
    show("literal", 1);
    const char* p = "pointer";
    show(p, 2);
    std::error_code ec;
    std::string missing = "/nonexistent/okaycc_r23";
    std::printf("size ok=%d\n", (int)(std::filesystem::file_size(missing, ec) == (std::uintmax_t)-1));
    std::filesystem::remove(missing + ".1", ec);
    std::printf("removed=%d\n", (int)!ec);
    return 0;
}
