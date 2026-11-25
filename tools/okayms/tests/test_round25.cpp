// Round 25: our own <variant> <tuple> <exception> <stdexcept> <clocale> <iterator> <string_view> <queue> <optional>
// <random> <cerrno>, u16string/u32string/wstring, atomic_flag, thread_local (Microsoft x64 TLS), and the compiler fixes
// these needed. Each program returns 0 when all of its checks hold, or the number of the first check that failed; it
// is built by okay-cl at /O0, /O1 and /O2 and run under Wine, and MinGW g++ builds the same text with its own headers
// and must agree (program_check.hpp). Written from ISO/IEC 14882 [variant], [tuple], [support.exception],
// [std.exceptions], [c.locales], [iterators], [string.view], [queue], [priority.queue], [optional], [rand], [cerrno.syn],
// [basic.string], [atomics.flag], [basic.stc.thread], [temp.inst], [temp.point], [temp.deduct.type],
// [temp.arg.explicit], [temp.class.spec.match], [temp.param], [expr.cond], [expr.const], [expr.sizeof], [lex.bool],
// [lex.ccon], [lex.icon], [expr.type.conv], [class.dtor], [dcl.ref], [class.base.init], [over.ics.user],
// [namespace.qual], [cpp.stringize], [cpp.concat], [cpp.subst], [stmt.ranged], the Microsoft PE/COFF specification
// (.tls section, TLS directory, SECREL) and the Microsoft x64 TLS access sequence (gs:[0x58], _tls_index). No code from
// other compilers or libraries.

#include "program_check.hpp"

#include <string>

namespace {
using okayms_test::Tally;
const okayms_test::ProgramRun kRun{"r25", {}};
const okayms_test::ProgramRun kRunThreads{"r25", {"CreateThread", "WaitForSingleObject"}};
const okayms_test::ProgramRun kRunRandom{"r25", {"bcrypt.dll:BCryptGenRandom"}};
}  // namespace

// The bump allocator of program_check.hpp (no C runtime is linked on the okay-ms side).
static const char* kAlloc = okayms_test::bump_alloc();

// the compiler fixes of this round (1): class template members made inside the body of their argument's class, c ? a : b of constants, a pack deduced from box<T...> after an explicit argument, sizeof through a reference, T& patterns, f(true) and f('a') deduction, ns::Empty(), static_cast<H*>(p)->~H()
static void r25_fixes(Tally& tally) {
    std::printf("r25_fixes\n");
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

// the compiler fixes of this round (2): ::ns::Type, reference members, a by-value class parameter made by a converting constructor, calling a static std::function member, std::function from nullptr, # and ## in macros, digit separators, c ? a : b in a for-init
static void r25_fixes2(Tally& tally) {
    std::printf("r25_fixes2\n");
    okayms_test::run_both(kRun, tally, "fixes2", std::string(kAlloc) + R"OKMS(
#include <functional>
#include <string>
#define CAT2(a, b) a##b
#define CAT(a, b) CAT2(a, b)
#define STR(x) #x
#define XSTR(x) STR(x)
#define NUM 42
namespace okay { struct Scope { int v; Scope(const char* n) : v(n[0]) {} }; int twice(int x) { return 2 * x; } }
struct Counter { int n; Counter() : n(0) {} void bump() { ++n; } };
struct Guard {
  Counter& c;
  int& raw;
  explicit Guard(Counter& x, int& r) : c(x), raw(r) { c.bump(); raw += 10; }
  ~Guard() { c.bump(); }
};
static int via(const Guard& g) { return g.c.n + g.raw; }
struct B2;
struct A2 { long long p, n, z; A2(long long v) : p(v), n(1), z(0) {} A2(const B2& b); };
struct B2 { long long d; B2() : d(9) {} };
inline A2::A2(const B2& b) : p(b.d), n(2), z(0) {}
static long long take(A2 a) { return a.p * 10 + a.n; }
struct Holder { static std::function<int(int)> fn; static int use(int v) { return fn ? fn(v) : -1; } };
std::function<int(int)> Holder::fn = nullptr;
static int add1(int x) { return x + 1; }
static bool same(const char* a, const char* b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
static int last_of(unsigned n) { int r = -1; for (unsigned i = (n > 3) ? n - 1 : 0; ; --i) { r = (int)i; if (i == 0 || r >= 0) break; } return r; }
int entry(void) {
  ::okay::Scope s("a");
  CK(1, s.v == 'a' && ::okay::twice(2) == 4);
  Counter k; int v = 1;
  {
    Guard g(k, v);
    CK(2, k.n == 1 && v == 11);
    g.c.bump(); g.raw = 5;
    CK(3, k.n == 2 && v == 5 && via(g) == 7 && &g.c == &k);
  }
  CK(4, k.n == 3);
  B2 b;
  CK(5, take(b) == 92 && take(A2(4)) == 41);
  CK(6, Holder::use(1) == -1);
  Holder::fn = add1;
  CK(7, Holder::use(1) == 2 && Holder::fn(4) == 5);
  int CAT2(x, y) = 4;
  CK(8, xy == 4 && CAT(N, UM) == 42 && same(STR(a + b), "a + b") && same(XSTR(NUM), "42") && same(STR(NUM), "NUM"));
  CK(9, !same(XSTR(__LINE__), "__LINE__"));
  unsigned long long big = 50'000'000;
  CK(10, big == 50000000ull && 0x7F'FF == 0x7FFF);
  CK(11, last_of(5) == 4);
  return 0;
}
)OKMS", 0);
}

// <variant>: index, holds_alternative, get_if, get, converting construction and assignment, copies, destruction of the held object, monostate, variant_size
static void r25_variant(Tally& tally) {
    std::printf("r25_variant\n");
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
static void r25_tuple(Tally& tally) {
    std::printf("r25_tuple\n");
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

// <exception> <stdexcept> <new>: what() through a base reference, derived classes, exception_ptr
static void r25_exception(Tally& tally) {
    std::printf("r25_exception\n");
    okayms_test::run_both(kRun, tally, "exception", std::string(kAlloc) + R"OKMS(
#include <exception>
#include <new>
#include <stdexcept>
#include <string>
static int tell(const std::exception& e) { return (int)std::string(e.what()).size(); }
struct MyErr : public std::exception { const char* what() const noexcept override { return "mine"; } };
int entry(void) {
  std::runtime_error r("bad thing");
  CK(1, std::string(r.what()) == "bad thing" && tell(r) == 9);
  std::out_of_range o(std::string("idx"));
  CK(2, tell(o) == 3);
  const std::logic_error& le = o;
  CK(3, std::string(le.what()) == "idx");
  MyErr m;
  CK(4, tell(m) == 4);
  std::bad_alloc ba;
  CK(5, std::string(ba.what()).size() > 0);
  std::exception_ptr p;
  CK(6, !p && p == std::exception_ptr());
  std::runtime_error copy = r;
  CK(7, std::string(copy.what()) == "bad thing");
  return 0;
}
)OKMS", 0);
}

// <clocale>: localeconv and setlocale of the C locale
static void r25_clocale(Tally& tally) {
    std::printf("r25_clocale\n");
    okayms_test::run_both(kRun, tally, "clocale", std::string(kAlloc) + R"OKMS(
#include <clocale>
static bool is_c(const char* s) { return s != 0 && s[0] == 'C' && s[1] == 0; }
int entry(void) {
  const char* dp = std::localeconv()->decimal_point;
  CK(1, dp[0] == '.' && dp[1] == 0);
  CK(2, is_c(std::setlocale(LC_ALL, 0)));
  CK(3, is_c(std::setlocale(LC_NUMERIC, "C")));
  CK(4, std::localeconv()->thousands_sep[0] == 0);
  return 0;
}
)OKMS", 0);
}

// <iterator>: next, prev, distance, begin, end, size, back_inserter, istreambuf_iterator; range constructors and assign of string and vector
static void r25_iterator(Tally& tally) {
    std::printf("r25_iterator\n");
    okayms_test::run_both(kRun, tally, "iterator", std::string(kAlloc) + R"OKMS(
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>
int entry(void) {
  std::vector<int> v;
  for (int i = 0; i < 5; ++i) v.push_back(i * 10);
  CK(1, *std::next(v.begin()) == 10 && *std::next(v.begin(), 3) == 30 && *std::prev(v.end()) == 40);
  CK(2, std::distance(v.begin(), v.end()) == 5);
  std::map<int, int> m;
  m[1] = 2; m[5] = 6; m[3] = 4;
  auto it = std::next(m.begin());
  CK(3, it->first == 3 && std::distance(m.begin(), m.end()) == 3);
  std::vector<int> arr; arr.push_back(1); arr.push_back(2); arr.push_back(3); arr.push_back(4);
  int sum = 0;
  for (auto p = std::begin(arr); p != std::end(arr); ++p) sum += *p;
  CK(4, sum == 10 && std::size(arr) == 4);
  std::vector<int> out;
  auto bi = std::back_inserter(out);
  *bi = 7; ++bi; *bi = 8;
  CK(5, out.size() == 2 && out[1] == 8);
  std::istringstream in("abc def");
  std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CK(6, all == "abc def");
  std::istringstream in2("xyz");
  std::vector<char> bytes;
  bytes.assign(std::istreambuf_iterator<char>(in2), std::istreambuf_iterator<char>());
  CK(7, bytes.size() == 3 && bytes[2] == 'z');
  std::vector<int> five(3, 9);
  CK(8, five.size() == 3 && five[2] == 9);
  std::vector<int> part(v.begin() + 1, v.begin() + 3);
  CK(9, part.size() == 2 && part[0] == 10 && part[1] == 20);
  std::string s("hello");
  std::string t(s.begin() + 1, s.end());
  CK(10, t == "ello");
  return 0;
}
)OKMS", 0);
}

// <string_view>: find, rfind, substr, compare, remove_prefix/suffix, find_first_of..., to and from std::string
static void r25_string_view(Tally& tally) {
    std::printf("r25_string_view\n");
    okayms_test::run_both(kRun, tally, "string_view", std::string(kAlloc) + R"OKMS(
#include <string>
#include <string_view>
static int count_dots(std::string_view s) { int n = 0; for (size_t i = 0; i < s.size(); ++i) if (s[i] == '.') ++n; return n; }
int entry(void) {
  std::string_view a("hello.world.txt");
  CK(1, a.size() == 15 && a.find('.') == 5 && a.rfind('.') == 11 && a.find("world") == 6);
  CK(2, a.substr(12) == "txt" && a.substr(0, 5) == std::string_view("hello"));
  std::string s(a.substr(6, 5));
  CK(3, s == "world" && s.size() == 5);
  std::string_view b = s;
  CK(4, b == "world" && b.size() == 5 && b.data() == s.data());
  CK(5, count_dots(a) == 2 && count_dots(s) == 0 && count_dots("a.b") == 1);
  a.remove_prefix(6); a.remove_suffix(4);
  CK(6, a == "world" && a.compare("worle") < 0 && a < std::string_view("x"));
  CK(7, a.find_first_of("rl") == 2 && a.find_last_of("o") == 1 && a.find_first_not_of("wo") == 2);
  std::string t = "x"; t += a; t.append(std::string_view("!"));
  CK(8, t == "xworld!");
  CK(9, a.find("zz") == std::string_view::npos && std::string_view().empty());
  return 0;
}
)OKMS", 0);
}

// char16_t, char32_t and std::u16string / std::u32string
static void r25_u16string(Tally& tally) {
    std::printf("r25_u16string\n");
    okayms_test::run_both(kRun, tally, "u16string", std::string(kAlloc) + R"OKMS(
#include <string>
static std::u16string to16(const char* s) { std::u16string out; out.reserve(4); while (*s) { out += (char16_t)(unsigned char)*s; ++s; } return out; }
int entry(void) {
  std::u16string a = to16("abc");
  CK(1, a.size() == 3 && a[1] == 'b' && a.data()[3] == 0);
  a += (char16_t)0xD83D;
  CK(2, a.size() == 4 && a[3] == 0xD83D && sizeof(char16_t) == 2 && sizeof(char32_t) == 4);
  std::u32string w(a.begin(), a.end());
  CK(3, w.size() == 4 && w[3] == 0xD83D);
  std::u16string b = a.substr(1, 2);
  CK(4, b == to16("bc") && b != a && a.find((char16_t)'c') == 2 && a.rfind((char16_t)'a') == 0);
  char32_t sum = 0;
  for (char32_t c : w) sum += c;
  CK(5, sum == (char32_t)('a' + 'b' + 'c' + 0xD83D));
  std::u16string z(3, (char16_t)'z');
  CK(6, z.size() == 3 && (z + b).size() == 5);
  return 0;
}
)OKMS", 0);
}

// static C x = f(); and C g = f(); for a class returned in memory (both wrote a stack slot or failed before)
static void r25_static_sret(Tally& tally) {
    std::printf("r25_static_sret\n");
    okayms_test::run_both(kRun, tally, "static_sret", std::string(kAlloc) + R"OKMS(
#include <string>
struct P { long long a, b, c; P() : a(0), b(0), c(0) {} P(long long x) : a(x), b(x), c(x) {} P(const P& o) : a(o.a), b(o.b), c(o.c + 1000) {} ~P() { a = -1; } };
static P mk() { return P(6); }
static P mk3() { P r(7); return r; }
static const P& b() { static P s = mk(); return s; }
static const P& d() { static P s = mk3(); return s; }
static std::string l2() { return std::string("t1"); }
static const std::string& c() { static std::string s = l2(); return s; }
static P gp = mk();
int entry(void) {
  CK(1, b().a == 6 && b().c == 6);
  CK(2, d().a == 7);
  CK(3, c().size() == 2 && c() == "t1");
  CK(4, gp.a == 6 && gp.c == 6);
  return 0;
}
)OKMS", 0);
}

// thread_local globals, static locals and a per-thread first-use guard, in one thread
static void r25_thread_local(Tally& tally) {
    std::printf("r25_thread_local\n");
    okayms_test::run_both(kRun, tally, "thread_local", std::string(kAlloc) + R"OKMS(
static thread_local int counter = 5;
thread_local double scale = 2.5;
static int bump() { static thread_local int calls; return ++calls; }
static const void* tag() { static thread_local char t; return &t; }
int entry(void) {
  counter += 2;
  CK(1, counter == 7);
  CK(2, scale == 2.5);
  scale = scale * 2.0;
  CK(3, scale == 5.0);
  bump(); bump();
  CK(4, bump() == 3);
  CK(5, tag() == tag() && tag() != 0);
  int* p = &counter;
  *p = 40;
  CK(6, counter == 40);
  return 0;
}
)OKMS", 0);
}

// thread_local in two threads (CreateThread): each thread has its own copy, a new thread starts from the initial value
static void r25_thread_local_threads(Tally& tally) {
    std::printf("r25_thread_local_threads\n");
    okayms_test::run_both(kRunThreads, tally, "thread_local_threads", std::string(kAlloc) + R"OKMS(
extern "C" __declspec(dllimport) void* CreateThread(void* attrs, unsigned long long stack, unsigned long (*start)(void*), void* param, unsigned flags, unsigned* id);
extern "C" __declspec(dllimport) unsigned WaitForSingleObject(void* handle, unsigned ms);
static thread_local int tl_value = 7;
static int calls() { static thread_local int n; return ++n; }
static unsigned long worker(void* p) {
  tl_value += 100;
  calls(); calls();
  *(int*)p = tl_value * 10 + calls();
  return 0;
}
int entry(void) {
  tl_value = 1;
  calls();
  int out = 0;
  void* h = CreateThread(0, 0, worker, &out, 0, 0);
  if (h == 0) return 50;
  WaitForSingleObject(h, 0xFFFFFFFFu);
  CK(1, out == 1073);
  CK(2, tl_value == 1);
  CK(3, calls() == 2);
  return 0;
}
)OKMS", 0);
}

// thread_local std::vector and std::string static locals with a dynamic initializer in two threads
static void r25_thread_local_objects(Tally& tally) {
    std::printf("r25_thread_local_objects\n");
    okayms_test::run_both(kRunThreads, tally, "thread_local_objects", std::string(kAlloc) + R"OKMS(
#include <string>
#include <vector>
extern "C" __declspec(dllimport) void* CreateThread(void* attrs, unsigned long long stack, unsigned long (*start)(void*), void* param, unsigned flags, unsigned* id);
extern "C" __declspec(dllimport) unsigned WaitForSingleObject(void* handle, unsigned ms);
static int made = 0;
static std::string label() { ++made; return std::string("t") + std::to_string(made); }
static std::vector<int>& pool() { static thread_local std::vector<int> v; return v; }
static const std::string& name() { static thread_local std::string s = label(); return s; }
static unsigned long worker(void* p) {
  pool().push_back(5);
  *(int*)p = (int)pool().size() * 100 + (name() == "t2" ? 1 : 0) * 10 + (name() == "t2" ? 1 : 0);
  return 0;
}
int entry(void) {
  pool().push_back(1); pool().push_back(2);
  CK(1, name() == "t1" && name() == "t1" && made == 1);
  int out = 0;
  void* h = CreateThread(0, 0, worker, &out, 0, 0);
  WaitForSingleObject(h, 0xFFFFFFFFu);
  CK(2, out == 111);
  CK(3, pool().size() == 2 && made == 2 && name() == "t1");
  return 0;
}
)OKMS", 0);
}

// <random> (mt19937 and mt19937_64 10000th values of [rand.predef], distributions in range), <queue>, <optional>, <cerrno>
static void r25_random_queue_optional(Tally& tally) {
    std::printf("r25_random_queue_optional\n");
    okayms_test::run_both(kRunRandom, tally, "random_queue_optional", std::string(kAlloc) + R"OKMS(
#include <cerrno>
#include <optional>
#include <queue>
#include <random>
#include <string>
#include <vector>
int entry(void) {
  std::mt19937 g;
  for (int i = 1; i < 10000; ++i) g();
  CK(1, g() == 4123659995u);
  std::mt19937_64 g64;
  for (int i = 1; i < 10000; ++i) g64();
  CK(2, g64() == 9981545732273789042ull);
  std::mt19937 h(7);
  std::uniform_real_distribution<double> j(-0.06, 0.06);
  std::uniform_int_distribution<int> k(3, 9);
  bool in_range = true;
  for (int i = 0; i < 200; ++i) { double v = j(h); int w = k(h); if (v < -0.06 || v >= 0.06 || w < 3 || w > 9) in_range = false; }
  CK(3, in_range);
  std::priority_queue<int> pq;
  pq.push(5); pq.push(1); pq.push(9); pq.push(3); pq.emplace(7);
  CK(4, pq.size() == 5 && pq.top() == 9);
  pq.pop(); CK(5, pq.top() == 7);
  pq.pop(); pq.pop(); CK(6, pq.top() == 3);
  std::priority_queue<int, std::vector<int>, std::greater<int>> mn;
  mn.push(4); mn.push(2); mn.push(8);
  CK(7, mn.top() == 2);
  std::queue<int> q; q.push(1); q.push(2); q.push(3);
  CK(8, q.front() == 1 && q.back() == 3 && q.size() == 3);
  q.pop(); CK(9, q.front() == 2);
  std::optional<std::string> o;
  CK(10, !o && !o.has_value() && o == std::nullopt);
  o = std::string("hi");
  CK(11, o && o->size() == 2 && *o == "hi" && o.value_or("x") == "hi");
  o.reset();
  CK(12, o.value_or("x") == "x");
  o.emplace("abc");
  CK(13, o.has_value() && o.value() == "abc");
  errno = 0;
  CK(14, errno == 0);
  errno = ERANGE;
  CK(15, errno == ERANGE && ERANGE != EINVAL);
  return 0;
}
)OKMS", 0);
}

// std::atomic_flag and ATOMIC_FLAG_INIT
static void r25_atomic_flag(Tally& tally) {
    std::printf("r25_atomic_flag\n");
    okayms_test::run_both(kRun, tally, "atomic_flag", std::string(kAlloc) + R"OKMS(
#include <atomic>
static std::atomic_flag& F() { static std::atomic_flag f = ATOMIC_FLAG_INIT; return f; }
int entry(void) {
  CK(1, !F().test_and_set());
  CK(2, F().test_and_set());
  F().clear();
  CK(3, !F().test_and_set());
  return 0;
}
)OKMS", 0);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    Tally tally;
    r25_fixes(tally);
    r25_fixes2(tally);
    r25_variant(tally);
    r25_tuple(tally);
    r25_exception(tally);
    r25_clocale(tally);
    r25_iterator(tally);
    r25_string_view(tally);
    r25_u16string(tally);
    r25_static_sret(tally);
    r25_thread_local(tally);
    r25_thread_local_threads(tally);
    r25_thread_local_objects(tally);
    r25_random_queue_optional(tally);
    r25_atomic_flag(tally);
    return tally.finish(15);
}
