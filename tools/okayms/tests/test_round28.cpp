// Round 28: the compiler fixes the engine files stopped at after Round 27 (Windows survey), and okay-cl's predefined
// macros. Each program returns 0 when all of its checks hold, or the number of the first check that failed; it is built
// by okay-cl at /O0, /O1 and /O2 and run under Wine, and MinGW g++ builds the same text with its own headers and must
// agree (program_check.hpp). Written from ISO/IEC 14882 ([temp.pre], [temp.over], [meta.rel], [dcl.type.decltype],
// [basic.scope.param], [stmt.return], [dcl.init.list], [class.base.init], [basic.lookup.qual], [namespace.qual],
// [dcl.typedef], [over.match.viable]) and Microsoft's documentation of the predefined macros, _BitScanForward,
// _BitScanReverse and _umul128. No code from other compilers or libraries.

#include "program_check.hpp"

#include <string>

namespace {
using okayms_test::Tally;
const okayms_test::ProgramRun kRun{"r28", {}};
const okayms_test::ProgramRun kRunCrt{"r28", {"msvcrt.dll:remove"}};
const okayms_test::ProgramRun kRunThreads{"r28", {"CreateThread", "WaitForSingleObject"}};
}  // namespace

static const char* kAlloc = okayms_test::bump_alloc();

// okay-cl predefines what the Microsoft x64 compiler does (_WIN32, _WIN64, _M_X64, _M_AMD64, _MSC_VER, __cplusplus),
// and <intrin.h> has the bit scans and the 128-bit product. MinGW g++ defines _WIN32 and _WIN64 too (not _MSC_VER).
static void r28_macros_intrin(Tally& tally) {
    std::printf("r28_macros_intrin\n");
    okayms_test::run_both(kRun, tally, "macros_intrin", std::string(kAlloc) + R"OKMS(
#include <intrin.h>
int entry(void) {
#if !defined(_WIN32) || !defined(_WIN64)
  return 1;
#endif
#ifndef __GNUC__
  CK(2, _MSC_VER >= 1900 && _M_X64 == 100 && _M_AMD64 == 100 && __cplusplus >= 201703L && _MSVC_LANG >= 201703L);
#endif
  unsigned long i = 99;
  CK(3, _BitScanForward64(&i, 0x50ull) == 1 && i == 4);
  CK(4, _BitScanReverse64(&i, 0x8000000000000001ull) == 1 && i == 63);
  i = 7;
  CK(5, _BitScanForward64(&i, 0) == 0 && _BitScanReverse(&i, 0) == 0);
  CK(6, _BitScanReverse(&i, 0x00F00000ul) == 1 && i == 23 && _BitScanForward(&i, 0x00F00000ul) == 1 && i == 20);
  unsigned __int64 hi = 0;
  unsigned __int64 lo = _umul128(0xFFFFFFFFFFFFFFFFull, 0xFFFFFFFFFFFFFFFFull, &hi);
  CK(7, lo == 1 && hi == 0xFFFFFFFFFFFFFFFEull);
  lo = _umul128(0x123456789ABCDEFull, 0xFEDCBA987654321ull, &hi);
  CK(8, lo == 0x22236D88FE5618CFull && hi == 0x121FA00AD77D74ull);
  return 0;
}
)OKMS", 0);
}

// Variable templates, the _v traits and is_invocable (Jobs.hpp's ParallelFor), function template overloads that mix
// type and value parameters (std::get<0>(tuple) after <variant>), decltype of a local of the function around a lambda,
// and a trailing return type that names a parameter (SaveData.hpp's Locked).
static void r28_templates(Tally& tally) {
    std::printf("r28_templates\n");
    okayms_test::run_both(kRun, tally, "templates", std::string(kAlloc) + R"OKMS(
#include <variant>
#include <tuple>
#include <type_traits>
#include <cstddef>
template <class T> constexpr int twice_v = sizeof(T) * 2;
template <class A, class B> constexpr bool same_v2 = std::is_same<A, B>::value;
struct RangeFn {
  void* obj;
  void (*call)(void*, int, int);
};
struct Runner {
  template <class F> int Each(int n, F&& fn) {
    using Fn = std::remove_reference_t<F>;
    if constexpr (std::is_invocable_v<Fn&, int, int>) {
      fn(0, n);
      return 2;
    } else {
      auto per = [&fn](int b, int e) { for (int i = b; i < e; ++i) fn(i); };
      RangeFn r;
      r.obj = &per;
      r.call = [](void* o, int b, int e) { (*static_cast<decltype(per)*>(o))(b, e); };
      r.call(r.obj, 0, n);
      return 1;
    }
  }
};
template <class F> auto Twice(F&& f) -> decltype(f()) {
  f();
  return f();
}
int add1(int x) { return x + 1; }
int entry(void) {
  std::variant<int, float> v = 3;
  auto t = std::make_tuple(1, 2LL, 3);
  CK(1, std::get<0>(t) + (int)std::get<1>(t) + std::get<2>(t) == 6 && std::get<int>(v) == 3);
  CK(2, (twice_v<int> == 8 && twice_v<char> == 2 && same_v2<int, int> && !same_v2<int, long long>));
  CK(3, (std::is_same_v<int, int> && !std::is_pointer_v<int> && std::is_pointer_v<int*> && std::is_class_v<Runner>));
  auto one = [](int) {};
  CK(4, (std::is_invocable_v<decltype(one)&, int> && !std::is_invocable_v<decltype(one)&, int, int>));
  CK(5, (std::is_invocable_v<int (*)(int), int> && !std::is_invocable_v<int (*)(int)>));
  Runner r;
  int sum = 0;
  const int k1 = r.Each(5, [&](int i) { sum += i; });
  const int k2 = r.Each(5, [&](int b, int e) { sum += e - b; });
  CK(6, k1 == 1 && k2 == 2 && sum == 15);
  int n = 0;
  const int got = Twice([&] { return ++n; });
  CK(7, got == 2 && n == 2 && add1(got) == 3);
  return 0;
}
)OKMS", 0);
}

// return 0; / return nullptr; for a class result made by a converting constructor (a 16-byte shared_ptr and a
// 24-byte class are returned through memory), a default member initializer with a braced list for a class with an
// initializer-list constructor, std::array of a class with constructors initialized from objects, ns::path(path) with
// a parameter named path, a scoped enumeration through a local alias, and std::remove(path) beside the algorithm.
static void r28_init_lookup(Tally& tally) {
    std::printf("r28_init_lookup\n");
    okayms_test::run_both(kRunCrt, tally, "init_lookup", std::string(kAlloc) + R"OKMS(
#include <memory>
#include <string>
#include <vector>
#include <array>
#include <cstdio>
#include <algorithm>
struct A { int x; };
struct Big {
  long long a, b, c;
  Big(int v) : a(v), b(v * 2), c(v * 3) {}
};
std::shared_ptr<A> none() { return nullptr; }
std::shared_ptr<A> some() { return std::make_shared<A>(); }
Big make(int v) { return v; }
struct D {
  std::vector<std::string> opts{"ab", "c", "def"};
  std::vector<int> nums = {4, 5};
};
struct V3 {
  float x, y, z;
  V3() : x(0), y(0), z(0) {}
  V3(float a, float b, float c) : x(a), y(b), z(c) {}
};
std::array<V3, 3> corners(const V3& c) {
  std::array<V3, 3> r = {c, c, V3{1, 2, 3}};
  return r;
}
namespace ns {
struct path {
  int v;
  path(int x) : v(x + 1) {}
  int get() const { return v; }
};
}
int use(int path) { return ns::path(path).get(); }
struct LS { enum class Style { Solid, Dots, Count }; };
int pick(int v) {
  using S = LS::Style;
  switch ((S)v) {
    case S::Solid: return 10;
    case S::Dots: return 20;
    default: return (int)S::Count;
  }
}
int entry(void) {
  CK(1, !none() && some() && none().use_count() == 0 && some().use_count() == 1);
  Big b = make(7);
  CK(2, b.a == 7 && b.b == 14 && b.c == 21);
  D d;
  CK(3, d.opts.size() == 3 && d.opts[2] == "def" && d.opts[0] == "ab" && d.nums.size() == 2 && d.nums[1] == 5);
  std::array<V3, 3> cs = corners(V3{4, 5, 6});
  CK(4, cs[0].x == 4 && cs[1].z == 6 && cs[2].y == 2);
  CK(5, use(41) == 42);
  CK(6, pick(0) == 10 && pick(1) == 20 && pick(2) == 2);
  std::vector<int> v{1, 2, 1, 3};
  v.erase(std::remove(v.begin(), v.end(), 1), v.end());
  CK(7, v.size() == 2 && v[0] == 2 && v[1] == 3);
  CK(8, std::remove("okayms_r28_no_such_file.txt") != 0);
  return 0;
}
)OKMS", 0);
}

// Two classes of one name in different namespaces (AccountService's detail::Sha256 wrapping crypto::Sha256): each
// has its own members, and a qualified name picks the one of that namespace.
static void r28_namespaces(Tally& tally) {
    std::printf("r28_namespaces\n");
    okayms_test::run_both(kRun, tally, "namespaces", std::string(kAlloc) + R"OKMS(
namespace a { namespace c {
class H {
public:
  int v = 1;
  void Up(int x) { v += x; }
  int Get() const { return v; }
};
} }
namespace a { namespace d {
class H {
public:
  void Up(int x) { impl_.Up(x * 2); }
  void Up(const char* s) { impl_.Up(s[0]); }
  int Get() const { return impl_.Get() + 100; }
private:
  a::c::H impl_;
};
inline int T() {
  H h;
  h.Up(3);
  h.Up("\x05");
  return h.Get();
}
} }
int entry(void) {
  a::c::H x;
  x.Up(1);
  CK(1, x.Get() == 2);
  CK(2, a::d::T() == 112);
  a::d::H y;
  y.Up(10);
  CK(3, y.Get() == 121 && sizeof(a::c::H) == sizeof(int));
  return 0;
}
)OKMS", 0);
}

// Generic lambdas (a call operator template per `auto` parameter), closures that hold a shared_ptr by value (copied
// and destroyed through the closure's implicit copy constructor and destructor: the use count must come back), a
// while condition that declares a variable, and INFINITY.
static void r28_lambdas(Tally& tally) {
    std::printf("r28_lambdas\n");
    okayms_test::run_both(kRun, tally, "lambdas", std::string(kAlloc) + R"OKMS(
#include <vector>
#include <algorithm>
#include <utility>
#include <memory>
#include <cmath>
struct State { int hits = 0; };
template <class F> int call_twice(F f) { return f() + f(); }
int* next_of(int* a, int& i) { return i < 4 ? &a[i++] : nullptr; }
int entry(void) {
  std::vector<std::pair<float, int>> k{{3.0f, 1}, {1.0f, 2}, {2.0f, 3}};
  std::stable_sort(k.begin(), k.end(), [](auto& a, auto& b) { return a.first < b.first; });
  CK(1, k[0].second == 2 && k[1].second == 3 && k[2].second == 1);
  int base = 10;
  auto add = [base](auto x, auto y) { return base + x + y; };
  CK(2, add(1, 2) == 13 && add(1.5, 2.5) == 14.0);
  auto twice = [&](const auto& v) -> int { return (int)v * 2; };
  CK(3, twice(2.5) == 4 && twice(7) == 14);
  auto st = std::make_shared<State>();
  {
    auto bump = [st]() { st->hits += 1; return st->hits; };
    CK(4, st.use_count() == 2);
    CK(5, call_twice(bump) == 3 && st->hits == 2);
    auto copy = bump;
    CK(6, st.use_count() == 3);
  }
  CK(7, st.use_count() == 1 && st->hits == 2);
  int a[4] = {1, 2, 3, 4};
  int i = 0, sum = 0;
  while (int* p = next_of(a, i)) {
    if (*p == 2) continue;
    sum += *p;
  }
  CK(8, sum == 8 && i == 4);
  float inf = INFINITY;
  CK(9, inf > 1e38f && std::isinf(inf) && HUGE_VAL > 1e300);
  return 0;
}
)OKMS", 0);
}

// Classes: scoped enumerators named in a later enumerator, a friend function defined in its class, braced member
// initializers, R& C::f() defined outside the class, a class's typedef in the parameters of a member defined outside
// it, a nested class defined outside its class (struct Sys::Impl), a nested class and a local class calling a static
// member of the class around them, and Get<T>() called without this-> in a member template.
static void r28_classes(Tally& tally) {
    std::printf("r28_classes\n");
    okayms_test::run_both(kRun, tally, "classes", std::string(kAlloc) + R"OKMS(
#include <functional>
enum class Mask : unsigned { None = 0, A = 1u << 0, B = 1u << 1, C = 1u << 2, AB = A | B, All = AB | C };
namespace n {
struct Id {
  int v;
  friend constexpr bool operator==(Id a, Id b) { return a.v == b.v; }
  friend bool operator!=(Id a, Id b) { return !(a == b); }
};
}
struct Tables {
  unsigned short base[4];
  unsigned char extra[3];
  int k;
  constexpr Tables() : base{}, extra{}, k{5} { base[1] = 9; }
};
class Sys {
public:
  using Handler = std::function<int(int)>;
  static constexpr int kAuto = -1;
  Sys();
  ~Sys();
  static Sys& Global();
  int& Count();
  void SetHandler(Handler h);
  int Run(int x);
  static int Twice(int x) { return x * 2; }
  class Lock {
  public:
    int Probe() const { return Twice(21); }
  };
private:
  struct Impl;
  Impl* impl_;
  int count_ = 0;
};
struct Sys::Impl {
  enum class Mode { Idle, Busy };
  int base = 5;
  Mode mode = Mode::Busy;
  Handler handler;
  int Apply(int x) const { return handler ? handler(x + base) : -1; }
};
Sys::Sys() : impl_(new Impl) {}
Sys::~Sys() { delete impl_; }
Sys& Sys::Global() {
  struct Holder {
    Sys s;
    int w;
    Holder() : w(kAuto) {}
  };
  static Holder h;
  h.s.count_ = h.w;
  return h.s;
}
int& Sys::Count() { return count_; }
void Sys::SetHandler(Handler h) { impl_->handler = h; }
int Sys::Run(int x) { return impl_->Apply(x) + (impl_->mode == Impl::Mode::Busy ? 1000 : 0); }
struct Item { int v = 3; };
struct Bag {
  Item item;
  template <typename T> T* Get() { return &item; }
  template <typename T> T* Add() {
    if (T* e = Get<T>()) return e;
    return nullptr;
  }
};
int entry(void) {
  CK(1, (unsigned)Mask::AB == 3u && (unsigned)Mask::All == 7u);
  n::Id a{1}, b{1}, c{2};
  CK(2, a == b && a != c && !(a == c));
  Tables t;
  CK(3, t.base[0] == 0 && t.base[1] == 9 && t.extra[2] == 0 && t.k == 5);
  Sys s;
  s.SetHandler([](int x) { return x * 3; });
  CK(4, s.Run(1) == 1018);
  s.Count() = 4;
  CK(5, s.Count() == 4 && Sys::Global().Count() == -1);
  Sys::Lock l;
  CK(6, l.Probe() == 42);
  Bag bag;
  CK(7, bag.Add<Item>()->v == 3);
  return 0;
}
)OKMS", 0);
}

// thread_local of a class whose default member initializers are constants (Jobs.cpp's ThreadCtx): each thread starts
// from those values.
static void r28_thread_local_class(Tally& tally) {
    std::printf("r28_thread_local_class\n");
    okayms_test::run_both(kRunThreads, tally, "thread_local_class", std::string(kAlloc) + R"OKMS(
extern "C" __declspec(dllimport) void* CreateThread(void* attrs, unsigned long long stack, unsigned long (*start)(void*), void* param, unsigned flags, unsigned* id);
extern "C" __declspec(dllimport) unsigned WaitForSingleObject(void* handle, unsigned ms);
struct Ctx {
  const void* sys = nullptr;
  int depth = 0;
  int lane = -1;
};
thread_local Ctx t_ctx;
static unsigned long worker(void* p) {
  *(int*)p = (t_ctx.sys == nullptr ? 1 : 0) + t_ctx.depth * 10 + (t_ctx.lane == -1 ? 100 : 0);
  t_ctx.depth = 7;
  return 0;
}
int entry(void) {
  CK(1, t_ctx.sys == nullptr && t_ctx.depth == 0 && t_ctx.lane == -1);
  t_ctx.depth = 3;
  t_ctx.lane = 2;
  int out = 0;
  void* h = CreateThread(0, 0, worker, &out, 0, 0);
  if (h == 0) return 50;
  WaitForSingleObject(h, 0xFFFFFFFFu);
  CK(2, out == 101);
  CK(3, t_ctx.depth == 3 && t_ctx.lane == 2);
  return 0;
}
)OKMS", 0);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    Tally tally;
    r28_macros_intrin(tally);
    r28_templates(tally);
    r28_init_lookup(tally);
    r28_namespaces(tally);
    r28_lambdas(tally);
    r28_classes(tally);
    r28_thread_local_class(tally);
    return tally.finish(7);
}
