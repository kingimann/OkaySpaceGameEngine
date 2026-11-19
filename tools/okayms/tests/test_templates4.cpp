// Round 17 template gaps: member function templates defined outside the class, constructor templates,
// `s.template add<int>(1)`, expansion of expressions (`f(g(xs)...)`), fold expressions and class
// template specializations matching a pack. Each program is also
// built by MinGW g++ and run under Wine; the exit codes must match. Written from ISO/IEC 14882
// [class.static], [temp.mem], [temp.names], [temp.variadic], [expr.prim.fold] and [temp.class.spec].

#include "check.hpp"
#include "okayms/coff.hpp"
#include "okayms/compile.hpp"
#include "okayms/link.hpp"

#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {

using okayms_test::Tally;

int run_shell(const std::string& cmd) {
    const int rc = std::system(cmd.c_str());
    if (rc == -1 || !WIFEXITED(rc)) return -1;
    return WEXITSTATUS(rc);
}

int wine_exit(const std::string& exe) {
    return run_shell("WINEDEBUG=-all WINEPREFIX=$HOME/.okayms-wine timeout 60 wine " + exe + " >/dev/null 2>&1");
}

bool write_bytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path.c_str(), std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

bool has_symbol(const okayms::ObjectFile& object, const std::string& name) {
    for (const auto& symbol : object.symbols)
        if (symbol.name == name) return true;
    return false;
}

// Builds `source` with okay-cl's compiler and runs it. Returns the exit code
// or -1. When MinGW g++ exists the same source (plus a main that calls entry)
// is built and run too; the two codes must match and equal `want`.
void run_both(Tally& tally, const std::string& name, const std::string& source, int want,
              okayms::ObjectFile* object_out = nullptr) {
    std::printf("round17 program %s\n", name.c_str());
    okayms::CompileInput in;
    in.source = source;
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        return;
    }
    if (object_out != nullptr) *object_out = compiled.value.object;
    const auto linked = okayms::link_executable({compiled.value.object}, {});
    CHECK(tally, linked.ok());
    if (!linked.ok()) {
        std::fprintf(stderr, "  %s\n", linked.error.c_str());
        return;
    }
    const std::string dir = "/tmp/okayms-tmpl4";
    run_shell("mkdir -p " + dir);
    const std::string ok_exe = dir + "/" + name + "-ok.exe";
    CHECK(tally, write_bytes(ok_exe, linked.value));
    const int code = wine_exit(ok_exe);
    CHECK(tally, code == want);
    if (code != want) std::fprintf(stderr, "  okay exit %d, wanted %d\n", code, want);
    if (run_shell("command -v x86_64-w64-mingw32-g++ >/dev/null") != 0) {
        std::printf("skipping g++ comparison: MinGW g++ is not installed\n");
        return;
    }
    const std::string src = dir + "/" + name + ".cpp";
    {
        std::ofstream out(src.c_str());
        out << source << "\nint main() { return entry(); }\n";
    }
    const std::string gpp_exe = dir + "/" + name + "-gpp.exe";
    const int built = run_shell("x86_64-w64-mingw32-g++ -static -fno-exceptions -fno-rtti -o " + gpp_exe + " " + src +
                                " 2>" + dir + "/" + name + ".log");
    CHECK(tally, built == 0);
    if (built != 0) return;
    const int ref = wine_exit(gpp_exe);
    CHECK(tally, ref == code);
    if (ref != code) std::fprintf(stderr, "  g++ exit %d, okay exit %d\n", ref, code);
}

}  // namespace


static void test_ool_member_template(Tally& tally) {
    std::printf("test_ool_member_template\n");
    run_both(tally, "oolmt",
             "struct S { int base; template <typename T> T add(T x); template <int N> int fixed() const; static int id(int a) { return a; } };\n"
             "template <typename T> T S::add(T x) { return x + base; }\n"
             "template <int N> int S::fixed() const { return N + base; }\n"
             "template <typename T> struct B { T v; template <typename U> U plus(U u) const; };\n"
             "template <typename T> template <typename U> U B<T>::plus(U u) const { return u + v; }\n"
             "int entry(void) {\n"
             "  S s; s.base = 5; B<int> b; b.v = 10; char c = 2;\n"
             "  return s.add(3) + s.add(c) + s.fixed<4>() + b.plus(1) + b.plus(c) + S::id(1);\n"
             "}\n",
             8 + 7 + 9 + 11 + 12 + 1);
}

static void test_ctor_template(Tally& tally) {
    std::printf("test_ctor_template\n");
    run_both(tally, "ctortmpl",
             "struct S { int v; template <typename T> S(T x) : v(x + 1) {} S() : v(0) {} template <typename T> int add(T x) const { return v + x; } };\n"
             "template <typename X> int viaTemplate(X& s) { return s.template add<int>(1) + s.template add<char>(2); }\n"
             "int entry(void) {\n"
             "  S a(3); char c = 5; S b(c); S d;\n"
             "  return a.v * 10 + b.v + viaTemplate(a) + d.v;\n"
             "}\n",
             40 + 6 + 5 + 6 + 0);
}

static void test_pack_expr(Tally& tally) {
    std::printf("test_pack_expr\n");
    run_both(tally, "packexpr",
             "template <typename T> T twice(T x) { return x * 2; }\n"
             "template <typename T> T sum(T x) { return x; }\n"
             "template <typename T, typename... R> T sum(T x, R... r) { return x + sum(r...); }\n"
             "template <typename... Ts> int h(Ts... xs) { return sum(twice(xs)...); }\n"
             "template <typename... Ts> int g(Ts... xs) { return sum((xs + 1)...); }\n"
             "template <typename... Ts> int k(Ts... xs) { return sum(xs * xs..., 100); }\n"
             "int entry(void) { char c = 3; return h(1, 2, c) + g(1, 2) + k(2, 3); }\n",
             12 + 5 + 113);
}

static void test_fold(Tally& tally) {
    std::printf("test_fold\n");
    run_both(tally, "fold",
             "template <typename... Ts> int sumr(Ts... xs) { return (xs + ...); }\n"
             "template <typename... Ts> int suml(Ts... xs) { return (... + xs); }\n"
             "template <typename... Ts> int subr(Ts... xs) { return (xs - ... - 0); }\n"
             "template <typename... Ts> int subl(Ts... xs) { return (100 - ... - xs); }\n"
             "template <typename... Ts> int all(Ts... xs) { return (xs && ...); }\n"
             "template <typename... Ts> int sum0(Ts... xs) { return (xs + ... + 0); }\n"
             "template <typename... Ts> int mul(Ts... xs) { return (1 * ... * xs); }\n"
             "template <typename... Ts> int sq(Ts... xs) { return ((xs * xs) + ...); }\n"
             "int entry(void) { char c = 4; return sumr(1, 2, c) + suml(10, 20) + subr(10, 3, 2) + subl(1, 2) + all(1, 2, 0) * 50 + all(1, 2) + sum0() + mul(2, 3, 4) + sq(1, 2, 3); }\n",
             7 + 30 + 9 + 97 + 0 + 1 + 0 + 24 + 14);
}

static void test_class_pack_spec(Tally& tally) {
    std::printf("test_class_pack_spec\n");
    run_both(tally, "classpack",
             "template <typename... Ts> struct Len;\n"
             "template <> struct Len<> { static const int value = 0; };\n"
             "template <typename T, typename... R> struct Len<T, R...> { static const int value = 1 + Len<R...>::value; };\n"
             "template <typename... Ts> struct Sz;\n"
             "template <> struct Sz<> { static const int value = 0; };\n"
             "template <typename T, typename... R> struct Sz<T, R...> { static const int value = sizeof(T) + Sz<R...>::value; };\n"
             "int entry(void) { return Len<>::value + Len<int>::value * 2 + Len<int, char, long>::value * 10 + Sz<char, int, short>::value; }\n",
             0 + 2 + 30 + 7);
}

int main() {
    Tally tally;
    test_ool_member_template(tally);
    test_ctor_template(tally);
    test_pack_expr(tally);
    test_fold(tally);
    test_class_pack_spec(tally);
    {
        okayms::CompileInput in;
        in.source =
            "struct S { int base; template <typename T> T add(T x); };\n"
            "template <typename T> T S::add(T x) { return x + base; }\n"
            "int entry(void) { S s; s.base = 1; return s.add(1); }\n";
        const auto compiled = okayms::compile_c(in);
        CHECK(tally, compiled.ok());
        if (compiled.ok()) CHECK(tally, has_symbol(compiled.value.object, "??$add@H@S@@QEAAHH@Z"));
        else std::fprintf(stderr, "  %s\n", compiled.error.c_str());
    }
    return tally.finish(1);
}
