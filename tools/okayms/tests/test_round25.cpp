// Round 25: our own <variant> and <tuple> (and the headers listed in docs/platform/own_msvc_plan.md, Round 25), and
// the compiler fixes they needed. Each program returns 0 when all of its checks hold, or the number of the first check
// that failed; it is built by okay-cl at /O0, /O1 and /O2 and run under Wine, and MinGW g++ builds the same text with
// its own headers and must agree (program_check.hpp). Written from ISO/IEC 14882 [variant], [tuple], [temp.inst],
// [temp.point], [temp.deduct.type], [temp.arg.explicit], [temp.class.spec.match], [expr.cond], [expr.const],
// [expr.sizeof], [lex.bool], [lex.ccon], [expr.type.conv], [class.dtor]. No code from other compilers or libraries.

#include "program_check.hpp"

#include <string>

namespace {
using okayms_test::Tally;
const okayms_test::ProgramRun kRun{"r25", {}};
}  // namespace

// The bump allocator of program_check.hpp (no C runtime is linked on the okay-ms side).
static const char* kAlloc = okayms_test::bump_alloc();

// the compiler fixes of this round: class template members made inside the body of their argument's class, c ? a : b of constants, a pack deduced from box<T...> after an explicit argument, sizeof through a reference, T& patterns, f(true) and f('a') deduction, ns::Empty(), static_cast<H*>(p)->~H()
static void test_fixes(Tally& tally) {
    std::printf("test_fixes\n");
    okayms_test::run_both(kRun, tally, "fixes", std::string(kAlloc) + R"OKMS(
#include <string>
#include <type_traits>
#include <vector>
#include <new>
struct Part {
  std::string t;
  Part() {}
  Part(const char* s) : t(s) {}
  Part(const Part& o) : t(o.t) {}
  // vector<Part> is made inside Part's own body: its members must wait until Part is complete
  std::vector<Part> split() const { std::vector<Part> out; out.push_back(Part("a")); out.push_back(Part("bc")); return out; }
};
template <class A, class B> struct same_ { static const bool value = false; };
template <class A> struct same_<A, A> { static const bool value = true; };
template <class X, class... T> struct find_ { static const int value = 100; };
template <class X, class H, class... R> struct find_<X, H, R...> {
  static const int value = same_<X, H>::value ? 0 : 1 + find_<X, R...>::value;
};
template <class... T> struct box { int n; };
template <class X, class... T> int count_after(box<T...>* b) { return (int)sizeof...(T) + (int)sizeof(X) + b->n; }
template <class U> int size_through_ref(const U& u) { return (int)sizeof(u); }
template <class T> int kind(const T&) { return std::is_same<T, bool>::value ? 1 : std::is_same<T, char>::value ? 2 : 3; }
namespace ns { struct Empty {}; }
static int live = 0;
struct Tr { int v; Tr(int x) : v(x) { ++live; } ~Tr() { --live; } };
template <class H> void kill(void* p) { static_cast<H*>(p)->~H(); }
int entry(void) {
  Part p("x");
  std::vector<Part> v = p.split();
  CK(1, v.size() == 2 && v[0].t == "a" && v[1].t == "bc");
  std::vector<Part> w = v;
  w.push_back(Part("d"));
  CK(2, w.size() == 3 && w[2].t == "d" && v.size() == 2);
  CK(3, (find_<int, float, int, char>::value == 1) && (find_<double, float, int>::value == 102));
  CK(4, (4 > 3 ? 7 : 8) == 7);
  box<int, float> b; b.n = 1;
  CK(5, count_after<char>(&b) == 4);
  CK(6, size_through_ref(1.0) == 8 && size_through_ref('a') == 1);
  CK(7, kind(true) == 1 && kind('a') == 2 && kind(5) == 3);
  CK(8, !std::is_reference<int>::value && std::is_reference<int&>::value && std::is_lvalue_reference<int&>::value);
  CK(9, !std::is_lvalue_reference<int>::value && !std::is_lvalue_reference<int&&>::value);
  ns::Empty e = ns::Empty();
  (void)e;
  unsigned long long buf[2];
  new (buf) Tr(3);
  CK(10, live == 1);
  kill<Tr>(buf);
  CK(11, live == 0);
  return 0;
}
)OKMS", 0);
}

// <variant>: index, holds_alternative, get_if, get, converting construction and assignment, copies, destruction of the held object, monostate, variant_size
static void test_variant(Tally& tally) {
    std::printf("test_variant\n");
    okayms_test::run_both(kRun, tally, "variant", std::string(kAlloc) + R"OKMS(
#include <string>
#include <variant>
struct V3 { float x, y, z; bool operator==(const V3& o) const { return x == o.x && y == o.y && z == o.z; } bool operator<(const V3& o) const { return x < o.x; } };
static int live = 0;
struct Tracked { int v; Tracked(int x) : v(x) { ++live; } Tracked(const Tracked& o) : v(o.v) { ++live; } ~Tracked() { --live; } };
int entry(void) {
  std::variant<float, bool, V3, std::string> a;
  CK(1, a.index() == 0 && std::holds_alternative<float>(a) && *std::get_if<float>(&a) == 0.0f);
  a = true;
  CK(2, a.index() == 1 && std::holds_alternative<bool>(a) && std::get_if<float>(&a) == 0);
  a = std::string("hello");
  CK(3, a.index() == 3 && std::get_if<std::string>(&a)->size() == 5);
  std::variant<float, bool, V3, std::string> b = a;
  CK(4, b.index() == 3 && std::get<std::string>(b) == "hello" && b == a);
  a = 2.5f;
  CK(5, a.index() == 0 && std::get<float>(a) == 2.5f && !(a == b));
  V3 v{1, 2, 3};
  a = v;
  CK(6, std::get_if<V3>(&a)->z == 3.0f);
  const std::variant<float, bool, V3, std::string>& ca = b;
  CK(7, std::get_if<std::string>(&ca) != 0 && *std::get_if<std::string>(&ca) == "hello");
  {
    std::variant<std::monostate, Tracked> t;
    CK(8, t.index() == 0 && live == 0);
    t = Tracked(5);
    CK(9, t.index() == 1 && live == 1 && std::get<Tracked>(t).v == 5);
    std::variant<std::monostate, Tracked> u = t;
    CK(10, live == 2);
    t = std::monostate();
    CK(11, live == 1 && t.index() == 0);
  }
  CK(12, live == 0);
  std::variant<int, std::string> s = std::string("x");
  CK(13, s.index() == 1);
  std::variant<int, std::string> s2 = 7;
  CK(14, s2.index() == 0 && std::get<int>(s2) == 7 && (std::variant_size<std::variant<int, std::string>>::value == 2));
  return 0;
}
)OKMS", 0);
}

// <tuple>: construction, get<I>, make_tuple, comparison, tuple keys of a std::map, tuple_size
static void test_tuple(Tally& tally) {
    std::printf("test_tuple\n");
    okayms_test::run_both(kRun, tally, "tuple", std::string(kAlloc) + R"OKMS(
#include <map>
#include <string>
#include <tuple>
#include <vector>
int entry(void) {
  std::tuple<int, long, double> t(1, 2L, 3.5);
  CK(1, std::get<0>(t) == 1 && std::get<1>(t) == 2 && std::get<2>(t) == 3.5);
  std::get<1>(t) = 9;
  CK(2, std::get<1>(t) == 9);
  auto u = std::make_tuple(1, 2L, 3.5);
  CK(3, u < t && !(t < u) && u != t);
  std::get<1>(u) = 9;
  CK(4, u == t);
  std::map<std::tuple<int, int, int>, int> m;
  m[std::make_tuple(1, 2, 3)] = 5;
  m[std::make_tuple(1, 2, 2)] = 4;
  m[std::make_tuple(0, 9, 9)] = 3;
  CK(5, m.size() == 3 && m[std::make_tuple(1, 2, 3)] == 5);
  CK(6, std::get<0>(m.begin()->first) == 0);
  auto it = m.find(std::make_tuple(1, 2, 2));
  CK(7, it != m.end() && it->second == 4);
  std::tuple<std::string, int> s("ab", 3);
  const std::tuple<std::string, int>& cs = s;
  CK(8, std::get<0>(cs) == "ab" && std::get<1>(cs) == 3);
  std::vector<std::tuple<int, int, double>> trip;
  trip.push_back(std::make_tuple(1, 2, 0.5));
  CK(9, trip.size() == 1 && std::get<2>(trip[0]) == 0.5);
  CK(10, (std::tuple_size<std::tuple<int, char>>::value == 2));
  return 0;
}
)OKMS", 0);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    Tally tally;
    test_fixes(tally);
    test_variant(tally);
    test_tuple(tally);
    return tally.finish(3);
}
