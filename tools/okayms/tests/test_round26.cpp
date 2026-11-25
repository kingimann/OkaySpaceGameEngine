// Round 26: return type deduction (auto, decltype(auto), trailing return types), and the headers and compiler fixes
// the engine files stopped at after Round 25. Each program returns 0 when all of its checks hold, or the number of the
// first check that failed; it is built by okay-cl at /O0, /O1 and /O2 and run under Wine, and MinGW g++ builds the
// same text with its own headers and must agree (program_check.hpp). Written from ISO/IEC 14882 [dcl.spec.auto],
// [dcl.type.decltype], [dcl.fct]. No code from other compilers or libraries.

#include "program_check.hpp"

#include <string>

namespace {
using okayms_test::Tally;
const okayms_test::ProgramRun kRun{"r26", {}};
const okayms_test::ProgramRun kRunRtti{"r26", {}, true};
const okayms_test::ProgramRun kRunIo{"r26",
                                     {"msvcrt.dll:fopen", "msvcrt.dll:_wfopen", "msvcrt.dll:fwrite", "msvcrt.dll:fread",
                                      "msvcrt.dll:fseek", "msvcrt.dll:ftell", "msvcrt.dll:rewind", "msvcrt.dll:fclose",
                                      "msvcrt.dll:_wremove"}};
}  // namespace

// The bump allocator of program_check.hpp (no C runtime is linked on the okay-ms side).
static const char* kAlloc = okayms_test::bump_alloc();

// auto and decltype(auto) return types: free functions, members, member templates, function templates, a trailing
// return type, auto&, a class result returned in memory, a method that calls an auto method.
static void r26_auto_return(Tally& tally) {
    std::printf("r26_auto_return\n");
    okayms_test::run_both(kRun, tally, "auto_return", std::string(kAlloc) + R"OKMS(
#include <string>
#include <utility>
auto twice(int x) { return x * 2; }
auto wide(int x) -> long long { return (long long)x << 33; }
auto none(int* p) { *p = 9; }
auto name() { return std::string("abc"); }
struct S {
  int v;
  std::string s;
  auto sooner() const { return v; }
  auto later() const { return sooner() + 1; }
  auto& ref() { return v; }
  decltype(auto) dref() { return ref(); }
  decltype(auto) dval() { return v + 0; }
  template <class T> auto add(T t) const { return t + v; }
  template <class... A> decltype(auto) app(A&&... a) { return s.append(std::forward<A>(a)...); }
};
template <class T> auto ident(T a) { return a; }
template <class T> auto ptr(T* p) { return p; }
int entry(void) {
  CK(1, twice(4) == 8);
  CK(2, wide(1) == 8589934592LL);
  int k = 0;
  none(&k);
  CK(3, k == 9);
  std::string n = name();
  CK(4, n == "abc" && n.size() == 3);
  S s;
  s.v = 3;
  CK(5, s.sooner() == 3 && s.later() == 4);
  s.ref() = 7;
  CK(6, s.v == 7);
  s.dref() = 11;
  CK(7, s.v == 11);
  int dv = s.dval();
  dv = 1;
  CK(8, s.v == 11 && dv == 1);
  CK(9, s.add(2) == 13 && s.add(1.5) > 12.4 && s.add(1.5) < 12.6);
  s.app("xy").append("z");
  CK(10, s.s == "xyz");
  CK(11, ident(5) == 5 && ident('c') == 'c' && ident(std::string("q")) == "q");
  int arr[2] = {1, 2};
  CK(12, ptr(arr + 1) == &arr[1]);
  CK(13, sizeof(twice(1)) == 4 && sizeof(wide(1)) == 8);
  return 0;
}
)OKMS", 0);
}

// std::shared_ptr, std::weak_ptr, std::make_shared, std::enable_shared_from_this, static_pointer_cast, and a virtual call
// through a smart pointer's operator->.
static void r26_shared_ptr(Tally& tally) {
    std::printf("r26_shared_ptr\n");
    okayms_test::run_both(kRun, tally, "shared_ptr", std::string(kAlloc) + R"OKMS(
#include <memory>
#include <string>
#include <vector>
static int alive = 0;
struct B { int v; B(int x) : v(x) { ++alive; } virtual ~B() { --alive; } virtual int k() const { return 1; } };
struct D : B { D(int x) : B(x) {} int k() const { return 2; } };
struct Node : std::enable_shared_from_this<Node> { int id; Node(int i) : id(i) { ++alive; } ~Node() { --alive; } std::shared_ptr<Node> self() { return shared_from_this(); } };
struct Holder { std::vector<Holder> kids; std::shared_ptr<std::vector<Holder>> arr; };
int entry(void) {
  {
    std::shared_ptr<B> a = std::make_shared<D>(5);
    if (alive != 1 || a->v != 5 || a->k() != 2 || a.use_count() != 1) return 1;
    std::shared_ptr<B> b = a;
    if (a.use_count() != 2 || b.get() != a.get()) return 2;
    std::weak_ptr<B> w = a;
    a.reset();
    if (b.use_count() != 1 || w.expired() || alive != 1) return 3;
    std::shared_ptr<B> c = w.lock();
    if (!c || c->v != 5 || c.use_count() != 2) return 4;
    c = nullptr;
    b.reset();
    if (alive != 0 || !w.expired() || w.lock()) return 5;
  }
  {
    std::shared_ptr<Node> n(new Node(7));
    std::shared_ptr<Node> m = n->self();
    if (m.get() != n.get() || n.use_count() != 2) return 6;
    std::shared_ptr<std::string> s = std::make_shared<std::string>("hey");
    if (*s != "hey" || s->size() != 3) return 7;
    std::shared_ptr<int> e;
    if (e || e != nullptr || e.use_count() != 0) return 8;
    std::shared_ptr<D> d = std::static_pointer_cast<D>(std::shared_ptr<B>(new D(3)));
    if (d->k() != 2 || d.use_count() != 1) return 9;
  }
  if (alive != 0) return 10;
  Holder h; h.arr = std::make_shared<std::vector<Holder>>();
  h.arr->push_back(Holder());
  if (h.arr->size() != 1) return 11;
  return 0;
}
)OKMS", 0);
}

// The compiler fixes of this round: a base named std::string or ns::Base<T> in a member initializer, a base class given by
// a template-id (CRTP) or a typedef, initializer_list<std::string>, a global C g{args}, a member that points to a
// function, a class whose implicit default constructor is deleted (a member without one), a type pack named in a pack
// expansion.
static void r26_fixes(Tally& tally) {
    std::printf("r26_fixes\n");
    okayms_test::run_both(kRunRtti, tally, "fixes", std::string(kAlloc) + R"OKMS(
#include <string>
#include <vector>
#include <utility>
#include <typeindex>
#include <typeinfo>
namespace n {
struct B { int v; B(int x) : v(x) {} };
template <class T> struct TB { T* self; int t; TB() : self(0), t(5) {} int get() const { return t; } };
}
struct S : public std::string { S(const char* p) : std::string(p) {} };
struct D : n::B { D() : n::B(4) {} };
struct E : n::TB<E> { int k; E() : k(2) {} };
typedef n::TB<int> TI;
struct F : TI { };
struct Counter { int c; Counter(int s) : c(s) {} };
Counter g_counter{40};
static int twice(int x) { return 2 * x; }
struct Rule { std::type_index type; int (*fn)(int); };
static int total(const std::vector<std::string>& v) { int n = 0; for (const std::string& s : v) n += (int)s.size(); return n; }
static int add2(int a, int b) { return a * 10 + b; }
template <class... A> int cast_each(A... a) { return add2(static_cast<A>(a)...); }
int entry(void) {
  S s("abc");
  CK(1, s.size() == 3 && s == "abc");
  D d;
  CK(2, d.v == 4);
  E e;
  CK(3, e.get() == 5 && e.k == 2);
  F f;
  CK(4, f.t == 5);
  std::vector<std::string> v = {"one", "three", std::string("x")};
  CK(5, v.size() == 3 && v[1] == "three" && v[2] == "x");
  CK(6, total({"ab", "cde"}) == 5);
  CK(7, g_counter.c == 40);
  Rule r{typeid(int), &twice};
  CK(8, r.fn(4) == 8 && r.type == std::type_index(typeid(int)));
  std::vector<Rule> rules;
  rules.push_back(r);
  CK(9, rules[0].fn(5) == 10);
  CK(10, cast_each(3, 'a') == 127);
  return 0;
}
)OKMS", 0);
}

// std::filesystem::path iteration (begin, end, ++, --, *, ->) and a temporary made for a loop condition on every pass
// (`it != p.end()` binds p.end() to a const reference).
static void r26_path_iter(Tally& tally) {
    std::printf("r26_path_iter\n");
    okayms_test::run_both(kRun, tally, "path_iter", std::string(kAlloc) + R"OKMS(
#include <filesystem>
#include <string>
int entry(void) {
  std::filesystem::path p("C:\\a\\bc\\d.txt");
  int n = 0; std::string all;
  for (auto it = p.begin(); it != p.end(); ++it) { ++n; all += it->string(); all += "|"; }
  if (n != 5) return 10 + n;
  if (all != "C:|\\|a|bc|d.txt|") return 2;
  std::filesystem::path q("x/y");
  auto r = q.begin(), e = q.end();
  if (*r != std::filesystem::path("x")) return 3;
  ++r; ++r;
  if (r != e) return 4;
  return 0;
}
)OKMS", 0);
}

// return {a, b,} for a class with an initializer-list constructor (a trailing comma too); a range for over a class whose
// begin() and end() return iterator objects (std::filesystem::path).
static void r26_return_list_range(Tally& tally) {
    std::printf("r26_return_list_range\n");
    okayms_test::run_both(kRun, tally, "return_list_range", std::string(kAlloc) + R"OKMS(
#include <vector>
#include <string>
struct M { std::string n; float f; };
static M Make(const char* n, float f) { M m; m.n = n; m.f = f; return m; }
static std::vector<M> Build() { return { Make("a", 1.0f), Make("bb", 2.0f), }; }
static std::vector<int> Ints() { return {1, 2, 3}; }
static int part1(void) {
  std::vector<M> v = Build();
  if (v.size() != 2 || v[1].n != "bb") return 1;
  std::vector<int> w = Ints();
  if (w.size() != 3 || w[2] != 3) return 2;
  return 0;
}

#include <filesystem>
#include <string>
static int part2(void) {
  std::filesystem::path q("x/yy/zzz");
  int m = 0; int len = 0;
  for (const auto& part : q) { ++m; len += (int)part.string().size(); }
  if (m != 3 || len != 6) return 1;
  int n = 0;
  for (const auto& c : q.relative_path()) if (!c.empty()) ++n;
  if (n != 3) return 2;
  return 0;
}

int entry(void) {
  int r = part1();
  if (r != 0) return r;
  r = part2();
  return r == 0 ? 0 : 20 + r;
}
)OKMS", 0);
}

// A static member function template that calls another one with no object (Log::Error calls Emit);
// compare_exchange_weak with a success and a failure order.
static void r26_static_tmpl_cas(Tally& tally) {
    std::printf("r26_static_tmpl_cas\n");
    okayms_test::run_both(kRun, tally, "static_tmpl_cas", std::string(kAlloc) + R"OKMS(
#include <string>
#include <utility>
struct L {
  template <class... A> static int Error(A&&... a) { return Emit(1, std::forward<A>(a)...); }
private:
  template <class... A> static int Emit(int lv, A&&... a) { return lv + (int)sizeof...(A); }
};
static int part1(void) { return L::Error(1, 2.0, "x") == 4 ? 0 : 1; }

#include <atomic>
static int part2(void){ std::atomic<const void*> o{nullptr}; int x; const void* e = nullptr; if (!o.compare_exchange_weak(e, &x, std::memory_order_acquire, std::memory_order_relaxed)) return 1; return o.load() == &x ? 0 : 2; }

int entry(void) {
  int r = part1();
  if (r != 0) return r;
  r = part2();
  return r == 0 ? 0 : 20 + r;
}
)OKMS", 0);
}

// L"..." (UTF-16, from UTF-8 source text), u8"...", \u and \U escapes (a surrogate pair above U+FFFF), std::wstring from a wide literal, std::FILE,
// and a local char array initialized from a string literal (it held the literal's address before this round).
static void r26_wide_literals(Tally& tally) {
    std::printf("r26_wide_literals\n");
    okayms_test::run_both(kRunIo, tally, "wide_literals", std::string(kAlloc) + R"OKMS(
#include <cstdio>
#include <string>
#include <algorithm>
#include <vector>
static int wlen(const wchar_t* s) { int n = 0; while (s[n]) ++n; return n; }
static int part1(void) {
  const wchar_t* w = L"rbé";
  if (wlen(w) != 3 || w[0] != 'r' || w[2] != 0xE9) return 1;
  const wchar_t* e = L"\U0001F600";
  if (wlen(e) != 2 || e[0] != 0xD83D || e[1] != 0xDE00) return 2;
  std::wstring ws = L"abc";
  if (ws.size() != 3 || ws[1] != 'b') return 3;
  std::FILE* f = nullptr;
  if (f != nullptr) { std::fseek(f, 0, SEEK_END); std::fclose(f); }
  std::vector<int> v = {1, 2, 1};
  v.erase(std::remove(v.begin(), v.end(), 1), v.end());
  if (v.size() != 1) return 4;
  const char* u8s = u8"xé";
  if (u8s[1] != (char)0xC3) return 5;
  const wchar_t* w2 = L"\u00e9";
  if (w2[0] != 0xE9 || w2[1] != 0) return 6;
  return 0;
}

static int part2(void) {
  const char msg[] = "hello file";
  if (msg[0] != 104) return 10;
  char m2[] = "hi";
  if (m2[0] != 104) return 11;
  return 0;
}

int entry(void) {
  int r = part1();
  if (r != 0) return r;
  r = part2();
  return r == 0 ? 0 : 20 + r;
}
)OKMS", 0);
}

// std::fopen, fwrite, fseek, ftell, rewind, fread, fclose and Microsoft's _wfopen and _wremove with wide names, called in
// msvcrt.dll (the declarations of our <cstdio>).
static void r26_file_io(Tally& tally) {
    std::printf("r26_file_io\n");
    okayms_test::run_both(kRunIo, tally, "file_io", std::string(kAlloc) + R"OKMS(
#include <cstdio>
#include <cstring>
int entry(void) {
  std::FILE* f = _wfopen(L"okayms_r26_io.bin", L"wb");
  if (!f) return 1;
  const char msg[] = "hello file";
  if (std::fwrite(msg, 1, 10, f) != 10) return 2;
  std::fclose(f);
  f = std::fopen("okayms_r26_io.bin", "rb");
  if (!f) return 3;
  if (std::fseek(f, 0, SEEK_END) != 0) return 4;
  if (std::ftell(f) != 10) return 5;
  std::rewind(f);
  char buf[16];
  if (std::fread(buf, 1, 10, f) != 10) return 6;
  std::fclose(f);
  if (buf[0] != 104 || buf[9] != 101) return 7;
  _wremove(L"okayms_r26_io.bin");
  return 0;
}
)OKMS", 0);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    Tally tally;
    r26_auto_return(tally);
    r26_shared_ptr(tally);
    r26_fixes(tally);
    r26_path_iter(tally);
    r26_return_list_range(tally);
    r26_static_tmpl_cas(tally);
    r26_wide_literals(tally);
    r26_file_io(tally);
    return tally.finish(8);
}
