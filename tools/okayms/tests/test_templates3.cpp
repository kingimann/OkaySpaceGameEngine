// Round 16 template gaps: static data members and class-scope constants (T::value), members of an
// explicit or partial specialization defined outside the class, member function templates, and
// variadic templates (sizeof..., function parameter packs expanded in calls). Each program is also
// built by MinGW g++ and run under Wine; the exit codes must match. Written from ISO/IEC 14882
// [class.static], [temp.expl.spec], [temp.mem], [temp.variadic] and [expr.sizeof].

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
    std::printf("round16 program %s\n", name.c_str());
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
    const std::string dir = "/tmp/okayms-tmpl3";
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

static void test_static_members(Tally& tally) {
    std::printf("test_static_members\n");
    run_both(tally, "statics",
             "template <typename T> struct Box {\n"
             "  static int count;\n"
             "  static const int k = 5;\n"
             "  enum { E = 3 };\n"
             "  T v;\n"
             "  int bump() const { return count + k + E; }\n"
             "};\n"
             "template <typename T> int Box<T>::count = 10;\n"
             "struct P { static int n; static const int m = 4; };\n"
             "int P::n = 7;\n"
             "template <typename T> struct Tr { static const int value = 2; };\n"
             "template <> struct Tr<char> { static const int value = 9; };\n"
             "template <typename T> int get() { return Tr<T>::value; }\n"
             "int entry(void) {\n"
             "  Box<int> a; Box<char> b;\n"
             "  Box<int>::count = 20;\n"
             "  b.count = b.count + 1;\n"
             "  return a.bump() + b.bump() + P::n + P::m + Tr<int>::value + Tr<char>::value + get<char>() + Box<int>::k;\n"
             "}\n",
             83);
}

static void test_static_misc(Tally& tally) {
    std::printf("test_static_misc\n");
    run_both(tally, "statics2",
             "template <typename T> struct Tr { static const int value = 2; };\n"
             "template <> struct Tr<char> { static const int value = 9; };\n"
             "template <typename T> int get() { return Tr<T>::value; }\n"
             "template <typename T> struct W { enum { n = 4 }; int f() const { return T::value + n; } };\n"
             "int entry(void) {\n"
             "  W<Tr<char> > w;\n"
             "  return Tr<int>::value + Tr<char>::value + get<char>() + get<int>() + w.f();\n"
             "}\n",
             35);
}

static void test_static_plain(Tally& tally) {
    std::printf("test_static_plain\n");
    run_both(tally, "plainstatic",
             "struct P { static int n; static const int m = 4; int v; int f() const { return n + m + v; } static int g() { return n * 2; } };\n"
             "int P::n = 7;\n"
             "int entry(void) { P p; p.v = 1; P::n = P::n + 1; return p.f() + P::g() + P::m; }\n",
             33);
}

static void test_outside_spec(Tally& tally) {
    std::printf("test_outside_spec\n");
    run_both(tally, "outspec",
             "template <typename T> struct Box { T v; int get() const; static int cnt; int twice(int a); };\n"
             "template <typename T> int Box<T>::get() const { return 1; }\n"
             "template <typename T> int Box<T>::twice(int a) { return a * 2; }\n"
             "template <typename T> int Box<T>::cnt = 100;\n"
             "template <typename T> struct Box<T *> { T *p; int get() const; static int cnt; };\n"
             "template <typename T> int Box<T *>::get() const { return 3; }\n"
             "template <typename U> int Box<U *>::cnt = 40;\n"
             "template <> struct Box<char> { char c; int get() const; int add(int x); static int cnt; Box(int z); };\n"
             "int Box<char>::get() const { return 7; }\n"
             "int Box<char>::add(int x) { return x + c; }\n"
             "int Box<char>::cnt = 5;\n"
             "Box<char>::Box(int z) { c = z; }\n"
             "int entry(void) {\n"
             "  Box<int> a; Box<int *> p; Box<char> c(3);\n"
             "  return a.get() + p.get() + c.get() + c.add(10) + a.twice(4) + Box<int>::cnt + Box<int *>::cnt + Box<char>::cnt;\n"
             "}\n",
             177);
}

static void test_outside_inst(Tally& tally) {
    std::printf("test_outside_inst\n");
    run_both(tally, "outinst",
             "template <typename T> struct Box { T v; int get() const; int other() const { return 1; } };\n"
             "template <typename T> int Box<T>::get() const { return 1; }\n"
             "template <> int Box<char>::get() const { return 50; }\n"
             "int entry(void) {\n"
             "  Box<int> a; Box<char> c;\n"
             "  return a.get() + c.get() + c.other();\n"
             "}\n",
             52);
}

static void test_member_templates(Tally& tally) {
    std::printf("test_member_templates\n");
    run_both(tally, "membertmpl",
             "struct S {\n"
             "  int base;\n"
             "  template <typename T> T add(T x) { return x + base; }\n"
             "  template <typename A, typename B> int mix(A a, B b) { return a * 10 + b; }\n"
             "  template <typename T> int twice(T x) { return add(x) + add(x); }\n"
             "  template <typename R, typename T> R conv(T x) { return x + 1; }\n"
             "  template <int N> int fixed() const { return N + base; }\n"
             "  template <typename T> static T id(T x) { return x; }\n"
             "};\n"
             "template <typename T> struct Box {\n"
             "  T v;\n"
             "  template <typename U> U plus(U u) const { return u + v; }\n"
             "};\n"
             "int entry(void) {\n"
             "  S s; s.base = 5; S *p = &s;\n"
             "  Box<int> b; b.v = 10;\n"
             "  char c = 2;\n"
             "  return s.add(3) + s.add(c) + p->add<int>(1) + s.mix(1, 2) + s.twice(4) + s.conv<int>(c) + s.fixed<7>() + S::id(2) + b.plus(1) + b.plus(c);\n"
             "}\n",
             91);
}

static void test_variadic(Tally& tally) {
    std::printf("test_variadic\n");
    run_both(tally, "variadic",
             "template <typename... Ts> int count(Ts... xs) { return sizeof...(Ts) + sizeof...(xs); }\n"
             "template <typename T> T sum(T x) { return x; }\n"
             "template <typename T, typename... Rest> T sum(T x, Rest... rest) { return x + sum(rest...); }\n"
             "template <typename... Ts> struct Cnt { static const int value = sizeof...(Ts); int n() const { return sizeof...(Ts); } };\n"
             "template <typename A, typename... Ts> int first_plus_count(A a, Ts... ts) { return a + count(ts...); }\n"
             "int entry(void) {\n"
             "  Cnt<int, char, int> c3;\n"
             "  Cnt<> c0;\n"
             "  char ch = 4;\n"
             "  return count() + count(1) * 10 + count(1, ch, 3) + sum(1, 2, 3) + sum(5) + sum(1, ch) + Cnt<int, char>::value * 7 + c3.n() + c0.n() + first_plus_count(10, 1, 2);\n"
             "}\n",
             73);
}

int main() {
    Tally tally;
    test_static_members(tally);
    test_static_misc(tally);
    test_static_plain(tally);
    test_outside_spec(tally);
    test_outside_inst(tally);
    test_member_templates(tally);
    test_variadic(tally);
    // Symbol names of the new instances (public MSVC decoration).
    {
        okayms::CompileInput in;
        in.source =
            "struct S { int base; template <typename T> T add(T x) { return x + base; } static int g(int a) { return a; } };\n"
            "template <typename T> struct Box { static int count; };\n"
            "template <typename T> int Box<T>::count = 1;\n"
            "template <typename... Ts> int cnt(Ts... xs) { return sizeof...(Ts); }\n"
            "int entry(void) { S s; Box<int> b; return s.add(1) + S::g(2) + Box<int>::count + cnt(1, 2); }\n";
        const auto compiled = okayms::compile_c(in);
        CHECK(tally, compiled.ok());
        if (compiled.ok()) {
            const okayms::ObjectFile& object = compiled.value.object;
            CHECK(tally, has_symbol(object, "?g@S@@SAHH@Z"));
            CHECK(tally, has_symbol(object, "?count@?$Box@H@@2HA"));
            CHECK(tally, has_symbol(object, "??$add@H@S@@QEAAHH@Z"));
            CHECK(tally, has_symbol(object, "??$cnt@HH@@YAHHH@Z"));
        } else {
            std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        }
    }
    return tally.finish(1);
}
