// Round 24: our own <filesystem> <system_error> <typeinfo> <typeindex> <functional> (std::function) <charconv>
// <type_traits> <sstream> <iostream> <fstream> <iomanip>, typeid, the type-trait intrinsics, alias templates, if constexpr,
// decltype, and the compiler bugs these headers found. Each program returns 0 when all of its checks hold, or the number
// of the first check that failed; it is built by okay-cl at /O0, /O1 and /O2 and run under Wine, and MinGW g++ builds the
// same text and must agree (program_check.hpp). Written from ISO/IEC 14882 [filesystems], [syserr], [expr.typeid],
// [type.info], [type.index], [func.wrap.func], [charconv], [meta], [temp.alias], [stmt.if], [dcl.type.decltype],
// [input.output], [temp.deduct], [over.ics.user], [lex.ccon], [lex.bool] and IEEE 754. No code from other compilers or
// libraries.

#include "program_check.hpp"

#include <string>

namespace {
using okayms_test::Tally;
const okayms_test::ProgramRun kRun{"r24", {}};
const okayms_test::ProgramRun kRunRtti{"r24", {}, true};
}  // namespace

// The bump allocator of program_check.hpp (no C runtime is linked on the okay-ms side).
static const char* kAlloc = okayms_test::bump_alloc();

// <filesystem>: path decomposition, operator/, lexically_normal and lexically_relative; a namespace alias
static void test_filesystem_path(Tally& tally) {
    std::printf("test_filesystem_path\n");
    okayms_test::run_both(kRun, tally, "fs_path",
             std::string(kAlloc) +
             "#include <filesystem>\n"
             "#include <string>\n"
             "#include <system_error>\n"
             "namespace fs = std::filesystem;\n"
             "static bool same(const fs::path& p, const char* s) { return p.string() == s; }\n"
             "int entry(void) {\n"
             "  fs::path p(\"assets/models/ship.glb\");\n"
             "  CK(1, same(p.filename(), \"ship.glb\") && same(p.stem(), \"ship\") && same(p.extension(), \".glb\"));\n"
             "  CK(2, same(p.parent_path(), \"assets/models\"));\n"
             "  fs::path q = fs::path(\"assets\") / \"tex\" / \"a.png\";\n"
             "  CK(3, same(q, \"assets\\\\tex\\\\a.png\"));\n"
             "  CK(4, same(fs::path(\".profile\").extension(), \"\") && same(fs::path(\".profile\").stem(), \".profile\"));\n"
             "  CK(5, same(fs::path(\"a/b/\").filename(), \"\") && same(fs::path(\"..\").extension(), \"\"));\n"
             "  CK(6, fs::path(\"C:\\\\x\\\\y\").is_absolute() && !fs::path(\"x/y\").is_absolute() && same(fs::path(\"C:\\\\x\\\\y\").root_name(), \"C:\"));\n"
             "  fs::path r(\"dir/file.txt\"); r.replace_extension(\".bin\"); CK(7, same(r, \"dir/file.bin\"));\n"
             "  CK(8, same(fs::path(\"a/./b/../c\").lexically_normal(), \"a\\\\c\"));\n"
             "  CK(9, same(fs::path(\"a/b/c\").lexically_relative(\"a\"), \"b\\\\c\") && same(fs::path(\"a\").lexically_relative(\"a/b\"), \"..\"));\n"
             "  CK(10, same(fs::path(\"C:/x\") / \"D:/y\", \"D:/y\") && same(fs::path(\"C:/x\") / \"/y\", \"C:/y\"));\n"
             "  fs::path e; CK(11, e.empty() && !p.empty() && p.has_extension() && !fs::path(\"dir\").has_extension());\n"
             "  CK(12, fs::path(\"a/b\").generic_string() == \"a/b\" && fs::path(\"a\\\\b\").generic_string() == \"a/b\");\n"
             "  std::error_code ec; CK(13, !ec && ec.value() == 0);\n"
             "  fs::path s = \"x\"; s += \".tmp\"; CK(14, same(s, \"x.tmp\") && fs::path(\"b\") < fs::path(\"c\"));\n"
             "  return 0;\n"
             "}\n",
             0);
}

// typeid of a type and of an object of a class with virtual functions; type_index as a map key (a member of a template instance that cannot be made is only an error when used)
static void test_typeid(Tally& tally) {
    std::printf("test_typeid\n");
    okayms_test::run_both(kRunRtti, tally, "typeid",
             std::string(kAlloc) +
             "#include <typeinfo>\n"
             "#include <typeindex>\n"
             "#include <map>\n"
             "struct Base { virtual ~Base() {} virtual int id() const { return 1; } };\n"
             "struct Derived : public Base { int id() const override { return 2; } };\n"
             "struct Other : public Base { };\n"
             "struct Plain { int x; };\n"
             "template <typename T> static bool is_a(const Base* b) { return typeid(*b) == typeid(T); }\n"
             "int entry(void) {\n"
             "  Derived d; Other o; Base b; Base* pd = &d; Base& ro = o;\n"
             "  CK(1, typeid(int) == typeid(int) && typeid(int) != typeid(float));\n"
             "  CK(2, typeid(*pd) == typeid(Derived) && typeid(*pd) != typeid(Base));\n"
             "  CK(3, typeid(ro) == typeid(Other) && typeid(b) == typeid(Base));\n"
             "  CK(4, typeid(pd) == typeid(Base*) && typeid(Plain) != typeid(Base));\n"
             "  CK(5, is_a<Derived>(pd) && !is_a<Other>(pd) && is_a<Other>(&ro));\n"
             "  const int ci = 3; CK(6, typeid(ci) == typeid(int) && typeid(const int&) == typeid(int));\n"
             "  std::type_index a(typeid(Derived)), c(typeid(Derived)), e(typeid(Other));\n"
             "  CK(7, a == c && a != e && (a < e) != (e < a));\n"
             "  std::map<std::type_index, int> m; m[typeid(Derived)] = 5; m[typeid(Other)] = 6; m[std::type_index(typeid(*pd))] += 1;\n"
             "  CK(8, m.size() == 2 && m[typeid(Derived)] == 6 && m[typeid(Other)] == 6);\n"
             "  CK(9, typeid(Derived).hash_code() == typeid(*pd).hash_code() && typeid(int).name() != nullptr);\n"
             "  return 0;\n"
             "}\n",
             0);
}

// std::function from a function, a capturing lambda and another function; in a vector; with a reference parameter
static void test_function(Tally& tally) {
    std::printf("test_function\n");
    okayms_test::run_both(kRun, tally, "function",
             std::string(kAlloc) +
             "#include <functional>\n"
             "#include <string>\n"
             "#include <vector>\n"
             "static int twice(int x) { return x * 2; }\n"
             "struct Bus { std::vector<std::function<void(int)>> subs; void emit(int v) { for (auto& s : subs) s(v); } };\n"
             "struct Cvar { using Callback = std::function<int(const std::string&)>; Callback cb; };\n"
             "int entry(void) {\n"
             "  std::function<int(int)> f = twice; CK(1, f(4) == 8 && (bool)f);\n"
             "  int base = 10; std::function<int(int)> g = [base](int x) { return x + base; }; CK(2, g(5) == 15);\n"
             "  std::function<int(int)> h = g; CK(3, h(1) == 11);\n"
             "  std::function<int(int)> e; CK(4, !e); e = f; CK(5, e(3) == 6);\n"
             "  int total = 0; Bus bus; bus.subs.push_back([&total](int v) { total += v; }); bus.subs.push_back([&total](int v) { total += v * 10; });\n"
             "  bus.emit(2); CK(6, total == 22);\n"
             "  Cvar c; c.cb = [](const std::string& s) { return (int)s.size(); }; CK(7, c.cb(\"four\") == 4);\n"
             "  std::function<void()> none; CK(8, !none);\n"
             "  return 0;\n"
             "}\n",
             0);
}

// from_chars and to_chars
static void test_charconv(Tally& tally) {
    std::printf("test_charconv\n");
    okayms_test::run_both(kRun, tally, "charconv",
             std::string(kAlloc) +
             "#include <charconv>\n"
             "#include <system_error>\n"
             "static bool span_is(const char* b, const char* e, const char* s) { size_t n = (size_t)(e - b); size_t m = 0; while (s[m]) ++m; if (n != m) return false; for (size_t i = 0; i < n; ++i) if (b[i] != s[i]) return false; return true; }\n"
             "int entry(void) {\n"
             "  const char* t = \"123abc\"; int v = 0; auto r = std::from_chars(t, t + 6, v);\n"
             "  CK(1, r.ec == std::errc() && v == 123 && r.ptr == t + 3);\n"
             "  const char* n = \"-42\"; int w = 0; auto r2 = std::from_chars(n, n + 3, w); CK(2, r2.ec == std::errc() && w == -42);\n"
             "  const char* bad = \"x1\"; int z = 7; auto r3 = std::from_chars(bad, bad + 2, z); CK(3, r3.ec == std::errc::invalid_argument && z == 7 && r3.ptr == bad);\n"
             "  const char* big = \"99999999999\"; int q = 1; auto r4 = std::from_chars(big, big + 11, q); CK(4, r4.ec == std::errc::result_out_of_range && q == 1 && r4.ptr == big + 11);\n"
             "  const char* hx = \"ff\"; unsigned u = 0; auto r5 = std::from_chars(hx, hx + 2, u, 16); CK(5, r5.ec == std::errc() && u == 255);\n"
             "  const char* d = \"2.5e2x\"; double dv = 0; auto r6 = std::from_chars(d, d + 6, dv); CK(6, r6.ec == std::errc() && dv == 250.0 && r6.ptr == d + 5);\n"
             "  const char* fl = \"0.125\"; float fv = 0; auto r7 = std::from_chars(fl, fl + 5, fv); CK(7, r7.ec == std::errc() && fv == 0.125f);\n"
             "  const char* mn = \"-9223372036854775808\"; long long ll = 0; auto r8 = std::from_chars(mn, mn + 20, ll); CK(8, r8.ec == std::errc() && ll == (-9223372036854775807LL - 1));\n"
             "  char buf[32]; auto w1 = std::to_chars(buf, buf + 32, -305); CK(9, w1.ec == std::errc() && span_is(buf, w1.ptr, \"-305\"));\n"
             "  auto w2 = std::to_chars(buf, buf + 32, 255u, 16); CK(10, span_is(buf, w2.ptr, \"ff\"));\n"
             "  auto w3 = std::to_chars(buf, buf + 2, 12345); CK(11, w3.ec == std::errc::value_too_large && w3.ptr == buf + 2);\n"
             "  const char* plus = \"+5\"; int pv = 0; CK(12, std::from_chars(plus, plus + 2, pv).ec == std::errc::invalid_argument);\n"
             "  CK(13, std::from_chars(d, d + 1, dv).ec == std::errc() && dv == 2.0);\n"
             "  return 0;\n"
             "}\n",
             0);
}

// <type_traits> on the intrinsics, alias templates, enable_if with a default template argument, if constexpr, decltype
static void test_type_traits(Tally& tally) {
    std::printf("test_type_traits\n");
    okayms_test::run_both(kRun, tally, "traits",
             std::string(kAlloc) +
             "#include <type_traits>\n"
             "struct Component { virtual ~Component() {} virtual int kind() const { return 0; } };\n"
             "struct Light : public Component { int kind() const override { return 1; } };\n"
             "struct Plain { int x; };\n"
             "struct WithDtor { ~WithDtor() {} };\n"
             "struct NoDefault { NoDefault(int) {} };\n"
             "enum class Mode { A, B };\n"
             "enum Old { O1 };\n"
             "template <class E, class = typename std::enable_if<std::is_enum<E>::value>::type>\n"
             "static int as_int(E e) { return (int)e + 100; }\n"
             "template <typename T> static int which() {\n"
             "  if constexpr (std::is_base_of<Component, T>::value) return 1;\n"
             "  else if constexpr (std::is_integral<T>::value) return 2;\n"
             "  else return 3;\n"
             "}\n"
             "template <typename T> static int probe(T* p) {\n"
             "  using U = std::remove_pointer_t<decltype(p)>;\n"
             "  return std::is_same<U, T>::value ? 7 : 0;\n"
             "}\n"
             "int entry(void) {\n"
             "  CK(1, (std::is_same<int, int>::value) && !(std::is_same<int, float>::value));\n"
             "  CK(2, (std::is_base_of<Component, Light>::value) && !(std::is_base_of<Light, Component>::value) && (std::is_base_of<Component, Component>::value));\n"
             "  CK(3, std::is_enum<Mode>::value && std::is_enum<Old>::value && !std::is_enum<int>::value && !std::is_enum<Plain>::value);\n"
             "  CK(4, std::is_class<Plain>::value && !std::is_class<int>::value && std::is_polymorphic<Light>::value && !std::is_polymorphic<Plain>::value);\n"
             "  CK(5, std::is_trivially_destructible<Plain>::value && !std::is_trivially_destructible<WithDtor>::value && std::is_trivially_destructible<int>::value);\n"
             "  CK(6, std::is_default_constructible<Plain>::value && !std::is_default_constructible<NoDefault>::value);\n"
             "  CK(7, std::is_integral<unsigned char>::value && !std::is_integral<double>::value && std::is_floating_point<float>::value && std::is_arithmetic<long long>::value);\n"
             "  CK(8, (std::is_same<std::remove_reference_t<int&>, int>::value) && (std::is_same<std::remove_pointer_t<Plain*>, Plain>::value) && (std::is_same<std::conditional_t<true, int, float>, int>::value));\n"
             "  CK(9, std::is_pointer<int*>::value && !std::is_pointer<int>::value && std::is_signed<int>::value && std::is_unsigned<unsigned>::value);\n"
             "  CK(10, as_int(Mode::B) == 101);\n"
             "  CK(11, which<Light>() == 1 && which<int>() == 2 && which<Plain>() == 3);\n"
             "  Plain pl; CK(12, probe(&pl) == 7);\n"
             "  int k = 5; decltype(k) k2 = k + 1; CK(13, k2 == 6);\n"
             "  return 0;\n"
             "}\n",
             0);
}

// ostringstream formatting, istringstream reading, getline, manipulators
static void test_streams(Tally& tally) {
    std::printf("test_streams\n");
    okayms_test::run_both(kRun, tally, "streams",
             std::string(kAlloc) +
             "#include <sstream>\n"
             "#include <iomanip>\n"
             "#include <string>\n"
             "struct V2 { float x, y; };\n"
             "static std::ostream& operator<<(std::ostream& os, const V2& v) { return os << \"(\" << v.x << \", \" << v.y << \")\"; }\n"
             "static std::string show(double d) { std::ostringstream os; os << d; return os.str(); }\n"
             "int entry(void) {\n"
             "  std::ostringstream os; os << \"n=\" << 42 << ' ' << -7 << ' ' << 3000000000LL << ' ' << 2.5;\n"
             "  CK(1, os.str() == \"n=42 -7 3000000000 2.5\");\n"
             "  CK(2, show(0.1) == \"0.1\" && show(1234567.0) == \"1.23457e+06\" && show(1e-5) == \"1e-05\" && show(100.0) == \"100\" && show(-0.5) == \"-0.5\");\n"
             "  std::ostringstream a; a << std::setprecision(3) << 3.14159; CK(3, a.str() == \"3.14\");\n"
             "  std::ostringstream b; b << std::fixed << std::setprecision(2) << 2.5 << \" \" << 1.005f; CK(4, b.str().substr(0, 4) == \"2.50\");\n"
             "  std::ostringstream c; c << std::hex << 255 << std::dec << \" \" << 10 << \" \" << std::boolalpha << true; CK(5, c.str() == \"ff 10 true\");\n"
             "  std::ostringstream d; d << std::setw(5) << std::setfill('0') << 42 << \"|\" << std::setw(3) << 7; CK(6, d.str() == \"00042|007\");\n"
             "  V2 v; v.x = 1.5f; v.y = -2; std::ostringstream e; e << v; CK(7, e.str() == \"(1.5, -2)\");\n"
             "  std::istringstream in(\"12 34.5 word\\nsecond line\\n\"); int i = 0; double x = 0; std::string w;\n"
             "  in >> i >> x >> w; CK(8, i == 12 && x == 34.5 && w == \"word\");\n"
             "  std::string line; std::getline(in, line); std::getline(in, line); CK(9, line == \"second line\");\n"
             "  CK(10, !std::getline(in, line));\n"
             "  std::stringstream ss; ss << \"7 8\"; int p = 0, q = 0; ss >> p >> q; CK(11, p == 7 && q == 8);\n"
             "  std::istringstream bad(\"x\"); int n = 5; bad >> n; CK(12, bad.fail() && n == 0);\n"
             "  std::ostringstream f; f << std::endl; CK(13, f.str() == \"\\n\");\n"
             "  return 0;\n"
             "}\n",
             0);
}

// bugs found on the way: a struct returned by a call passed by value, a virtual call with a reference argument, char and bool literals in overloads, E{}, template <..> static, a call through a function pointer, a huge double literal, a typedef of the base class, a class whose only constructor is a template, a variadic macro (the preprocessor looped forever on `...`)
static void test_fixes(Tally& tally) {
    std::printf("test_fixes\n");
    okayms_test::run_both(kRun, tally, "fixes",
             std::string(kAlloc) +
             "#define SUM3(a, ...) add3(a, __VA_ARGS__)\n"
             "#define MUL2(...) mul2(__VA_ARGS__)\n"
             "static int add3(int a, int b, int c) { return a + b + c; }\n"
             "static int mul2(int a, int b) { return a * b; }\n"
             "struct P { int n; int pad[3]; };\n"
             "static P mk(int n) { P p; p.n = n; return p; }\n"
             "static int use(P p) { return p.n; }\n"
             "struct S { int v; int pad[4]; };\n"
             "struct H { virtual ~H() {} virtual int call(const S& s) { return s.v; } };\n"
             "struct W { H* h; int go(const S& a) { return h->call(a); } };\n"
             "static int kind(char) { return 1; }\n"
             "static int kind(int) { return 2; }\n"
             "static int kind(bool) { return 3; }\n"
             "enum class E { a = 1, b = 2 };\n"
             "template <typename T> static int tw(int a) { return a * (int)sizeof(T); }\n"
             "static int addf(int a, int b) { return a + b; }\n"
             "static int via(int (*f)(int, int), int x) { return f(x, 1); }\n"
             "struct Base { typedef int Id; };\n"
             "struct Derived : public Base { Id id = 4; };\n"
             "struct Fo { int base; int get(int x) const { return x + base; } };\n"
             "struct Wt { int v; template <typename F> Wt(F f) { v = f.get(5); } };\n"
             "int entry(void) {\n"
             "  CK(1, use(mk(3)) == 3);\n"
             "  S s; s.v = 7; H hh; W w; w.h = &hh; CK(2, w.go(s) == 7);\n"
             "  CK(3, kind('a') == 1 && kind(5) == 2 && kind(true) == 3);\n"
             "  E z = E{}; E y = E::b; CK(4, (int)z == 0 && y == E::b && (int)E() == 0);\n"
             "  CK(5, tw<int>(2) == 8);\n"
             "  CK(6, via(addf, 4) == 5);\n"
             "  double big = 1e300; CK(7, (int)(big / 1e299) == 10 && 2.5 < 1.7976931348623157e308);\n"
             "  Derived d; CK(8, d.id == 4);\n"
             "  Fo fo; fo.base = 10; Wt wt(fo); CK(9, wt.v == 15);\n"
             "  CK(10, SUM3(1, 2, 3) == 6 && MUL2(2, 3) == 6);\n"
             "  return 0;\n"
             "}\n",
             0);
}

int main(int argc, char** argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    (void)root;
    Tally tally;
    test_filesystem_path(tally);
    test_typeid(tally);
    test_function(tally);
    test_charconv(tally);
    test_type_traits(tally);
    test_streams(tally);
    test_fixes(tally);
    return tally.finish(7);
}
