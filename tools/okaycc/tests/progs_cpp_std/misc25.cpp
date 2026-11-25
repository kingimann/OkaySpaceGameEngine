// Digit separators ([lex.icon] p1: 50'000'000, 0xFF'FF, 1'000.5), a lambda with a trailing
// reference return type ([expr.prim.lambda]), and names of the enclosing class seen in a nested
// class defined after it ([class.nest], [basic.lookup.unqual] p7).
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
class Jobs {
public:
    using ErrorHandler = std::function<void(const std::string &)>;
    enum class Kind { A, B };
    Jobs();
    ~Jobs();
    int Run();
private:
    struct Impl;
    Impl *m;
};
struct Jobs::Impl {
    ErrorHandler errorHandler;
    Kind kind = Kind::B;
    std::uint64_t stepLimit = 50'000'000;
};
Jobs::Jobs() : m(new Impl) {}
Jobs::~Jobs() { delete m; }
int Jobs::Run() { return (int)m->kind + (int)(m->stepLimit / 1'000'000); }
int main() {
    Jobs j;
    std::printf("%d %d %g\n", j.Run(), 0xFF'FF, 1'000.5);
    std::map<std::string, std::string> tie = {{"a", "x"}};
    auto tieOf = [&](const std::string &id) -> const std::string & {
        auto f = tie.find(id);
        if (f != tie.end()) return f->second;
        return id;
    };
    std::string q = "q";
    std::printf("%s %s\n", tieOf("a").c_str(), tieOf(q).c_str());
}
