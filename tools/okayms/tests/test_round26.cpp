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
const okayms_test::ProgramRun kRunClock{"r26", {"QueryPerformanceCounter", "QueryPerformanceFrequency", "GetSystemTimeAsFileTime"}};
const okayms_test::ProgramRun kRunSignal{"r26", {"msvcrt.dll:signal", "msvcrt.dll:raise"}};
const okayms_test::ProgramRun kRunThreads{
    "r26",
    {"AcquireSRWLockExclusive", "ReleaseSRWLockExclusive", "TryAcquireSRWLockExclusive", "SleepConditionVariableSRW",
     "WakeConditionVariable", "WakeAllConditionVariable", "CreateThread", "WaitForSingleObject", "CloseHandle",
     "GetCurrentThreadId", "Sleep", "SwitchToThread", "GetSystemInfo", "QueryPerformanceCounter",
     "QueryPerformanceFrequency", "GetSystemTimeAsFileTime"}};
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
static int prog_a(void) {
  std::vector<M> v = Build();
  if (v.size() != 2 || v[1].n != "bb") return 1;
  std::vector<int> w = Ints();
  if (w.size() != 3 || w[2] != 3) return 2;
  return 0;
}

#include <filesystem>
#include <string>
static int prog_b(void) {
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
  int r = prog_a();
  if (r != 0) return r;
  r = prog_b();
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
static int prog_a(void) { return L::Error(1, 2.0, "x") == 4 ? 0 : 1; }

#include <atomic>
static int prog_b(void){ std::atomic<const void*> o{nullptr}; int x; const void* e = nullptr; if (!o.compare_exchange_weak(e, &x, std::memory_order_acquire, std::memory_order_relaxed)) return 1; return o.load() == &x ? 0 : 2; }

int entry(void) {
  int r = prog_a();
  if (r != 0) return r;
  r = prog_b();
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
static int prog_a(void) {
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

static int prog_b(void) {
  const char msg[] = "hello file";
  if (msg[0] != 104) return 10;
  char m2[] = "hi";
  if (m2[0] != 104) return 11;
  return 0;
}

int entry(void) {
  int r = prog_a();
  if (r != 0) return r;
  r = prog_b();
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

// std::map and std::set with a comparator argument, shared_ptr<const T>, vector::insert of a braced list and of n
// copies, string::assign and the string constructor from a part of another string.
static void r26_containers(Tally& tally) {
    std::printf("r26_containers\n");
    okayms_test::run_both(kRun, tally, "containers", std::string(kAlloc) + R"OKMS(
#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>
#include <functional>
struct Key { int v; };
struct ByValueDesc { bool operator()(const Key& a, const Key& b) const { return a.v > b.v; } };
struct Res { int id; bool ok() const { return id > 0; } };
static std::shared_ptr<const Res> MakeRes(int id) { std::shared_ptr<Res> r(new Res()); r->id = id; return r; }
int entry(void) {
  std::map<Key, int, ByValueDesc> m;
  Key a{1}, b{3}, c{2};
  m[a] = 10; m[b] = 30; m[c] = 20;
  if (m.size() != 3 || m.begin()->first.v != 3) return 1;
  std::set<int, std::greater<int>> s = {1, 5, 3};
  if (*s.begin() != 5) return 2;
  std::shared_ptr<const Res> r = MakeRes(7);
  if (!r || r->id != 7 || !r->ok()) return 3;
  std::vector<int> v = {1, 5};
  v.insert(v.begin() + 1, {2, 3, 4});
  if (v.size() != 5 || v[1] != 2 || v[3] != 4 || v[4] != 5) return 4;
  v.insert(v.end(), 2, 9);
  if (v.size() != 7 || v[6] != 9) return 5;
  std::string src = "hello world";
  std::string part;
  part.assign(src, 6, 3);
  if (part != "wor") return 6;
  std::vector<std::string> parts;
  parts.emplace_back(src, 0, 5);
  if (parts[0] != "hello") return 7;
  std::map<std::string, int> plain;
  plain["b"] = 2; plain["a"] = 1;
  if (plain.begin()->second != 1) return 8;
  return 0;
}
)OKMS", 0);
}

// std::locale with a num_get facet that reads "inf" and a num_put facet, imbued in string streams; locale::classic().
static void r26_locale(Tally& tally) {
    std::printf("r26_locale\n");
    okayms_test::run_both(kRun, tally, "locale", std::string(kAlloc) + R"OKMS(
#include <locale>
#include <sstream>
#include <string>
#include <limits>
#include <cctype>
// A facet of the test's own: "inf" (any case) reads as infinity for float and double, other text as usual.
struct InfWord : std::num_get<char> {
    InfWord() : std::num_get<char>(0) {}
    using std::num_get<char>::do_get;
    template <class Num>
    static iter_type word(iter_type at, iter_type stop, std::ios_base::iostate& st, Num& out) {
        char got[4] = {0, 0, 0, 0};
        int n = 0;
        for (; at != stop && n < 3; ++at) got[n++] = (char)std::tolower((unsigned char)*at);
        const bool inf = got[0] == 'i' && got[1] == 'n' && got[2] == 'f';
        out = inf ? std::numeric_limits<Num>::infinity() : Num(0);
        if (!inf) st |= std::ios_base::failbit;
        if (at == stop) st |= std::ios_base::eofbit;
        return at;
    }
    iter_type do_get(iter_type b, iter_type e, std::ios_base& io, std::ios_base::iostate& st, float& out) const override {
        if (b != e && (*b == 'i' || *b == 'I')) return word(b, e, st, out);
        return std::num_get<char>::do_get(b, e, io, st, out);
    }
    iter_type do_get(iter_type b, iter_type e, std::ios_base& io, std::ios_base::iostate& st, double& out) const override {
        if (b != e && (*b == 'i' || *b == 'I')) return word(b, e, st, out);
        return std::num_get<char>::do_get(b, e, io, st, out);
    }
};
class TwoDigits : public std::num_put<char> {
public:
    explicit TwoDigits(std::size_t refs) : std::num_put<char>(refs) {}
protected:
    iter_type do_put(iter_type out, std::ios_base& str, char fill, double v) const override {
        (void)str; (void)fill;
        *out++ = '<';
        *out++ = (char)('0' + (int)v % 10);
        *out++ = '>';
        return out;
    }
};
int entry(void) {
  static const std::locale* tl = new std::locale(std::locale::classic(), new InfWord());
  std::istringstream in("inf 2.5");
  in.imbue(*tl);
  float a = 0, b = 0;
  in >> a >> b;
  if (!(a > 3.0e38f) || b != 2.5f) return 1;
  std::istringstream plain("inf");
  plain.imbue(std::locale::classic());
  float c = 1;
  plain >> c;
  if (!plain.fail()) return 2;
  std::ostringstream o;
  static const std::locale* pl = new std::locale(std::locale::classic(), new TwoDigits(0));
  o.imbue(*pl);
  o << 7.0 << ' ' << 3;
  if (o.str() != "<7> 3") return 3;
  std::ostringstream o2;
  o2.imbue(std::locale::classic());
  o2 << 2.5;
  if (o2.str() != "2.5") return 4;
  return 0;
}
)OKMS", 0);
}

// <csignal>: signal and raise in msvcrt.dll, a handler that records the signal.
static void r26_csignal(Tally& tally) {
    std::printf("r26_csignal\n");
    okayms_test::run_both(kRunSignal, tally, "csignal", std::string(kAlloc) + R"OKMS(
#include <csignal>
static volatile std::sig_atomic_t g_got = 0;
static void OnSignal(int s) { g_got = s; }
int entry(void) {
  if (std::signal(SIGTERM, OnSignal) == SIG_ERR) return 1;
  if (std::raise(SIGTERM) != 0) return 2;
  if (g_got != SIGTERM) return 3;
  std::signal(SIGTERM, SIG_DFL);
  return 0;
}
)OKMS", 0);
}

// <thread>, <mutex>, <condition_variable> on Win32: four threads add under a mutex, call_once, a waiter woken by
// notify_all, recursive_mutex, wait_for that times out, hardware_concurrency. MinGW here has the win32 thread model (no
// std::thread), so its side of this program only returns 0.
static void r26_threads(Tally& tally) {
    std::printf("r26_threads\n");
    okayms_test::run_both(kRunThreads, tally, "threads", std::string(kAlloc) + R"OKMS(
#ifdef __GNUC__
int entry(void) { return 0; }  // MinGW here has the win32 thread model: no std::thread to compare with
#else
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <vector>
struct Shared { std::mutex m; std::condition_variable cv; int count; bool ready; std::recursive_mutex rm; int depth; };
static Shared g;
static std::once_flag g_once;
static int g_once_runs = 0;
static void Work() {
  for (int i = 0; i < 1000; ++i) { std::lock_guard<std::mutex> lk(g.m); ++g.count; }
  std::call_once(g_once, [] { ++g_once_runs; });
}
int entry(void) {
  g.count = 0; g.ready = false; g.depth = 0;
  std::vector<std::thread> ts;
  for (int i = 0; i < 4; ++i) ts.push_back(std::thread([] { Work(); }));
  for (std::thread& t : ts) if (t.joinable()) t.join();
  if (g.count != 4000) return 1;
  if (g_once_runs != 1) return 2;
  std::thread waiter([] {
    std::unique_lock<std::mutex> lk(g.m);
    g.cv.wait(lk, [] { return g.ready; });
    g.count = -1;
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  { std::lock_guard<std::mutex> lk(g.m); g.ready = true; }
  g.cv.notify_all();
  waiter.join();
  if (g.count != -1) return 3;
  g.rm.lock(); g.rm.lock(); ++g.depth; g.rm.unlock(); g.rm.unlock();
  if (!g.rm.try_lock()) return 4;
  g.rm.unlock();
  if (std::thread::hardware_concurrency() == 0) return 5;
  std::unique_lock<std::mutex> lk(g.m);
  if (g.cv.wait_for(lk, std::chrono::milliseconds(5)) != std::cv_status::timeout) return 6;
  if (std::this_thread::get_id() == std::thread::id()) return 7;
  return 0;
}
#endif
)OKMS", 0);
}

// Compiler fixes: a parameter hides a type of the same name, a member template instance made inside a lambda whose
// result type is being worked out, overloaded member templates, Outer::Nested(), an out-of-class member template that
// returns T*, C casts to template types, the new <cmath> functions.
static void r26_fixes2(Tally& tally) {
    std::printf("r26_fixes2\n");
    okayms_test::run_both(kRun, tally, "fixes2", std::string(kAlloc) + R"OKMS(
#include <cmath>
#include <vector>
#include <functional>
#include <string>
typedef long long seconds;
static int scale(float seconds, int rate) { return (int)(seconds * rate); }
static bool ready = true;
struct C {
  template <class P> int w(int a, P p) { return p() ? a : 0; }
  template <class R> int over(R r) { return (int)r; }
  template <class R, class P> int over(R r, P p) { return (int)r + p(); }
  struct Inner { int v; Inner() : v(7) {} };
};
static C c;
struct G {
  int v;
  template <typename T> T* Get() const;
};
template <typename T> T* G::Get() const { return (T*)&v; }
static int via_fn(void* p) { std::function<int()>* f = static_cast<std::function<int()>*>(p); return (*f)(); }
int entry(void) {
  if (scale(2.0f, 3) != 6) return 1;
  auto outer = [] { return c.w(3, [] { return ready; }); };
  if (outer() != 3) return 2;
  if (c.over(4) != 4 || c.over(4, [] { return 1; }) != 5) return 3;
  if (C::Inner().v != 7) return 4;
  G g; g.v = 9;
  if (*g.Get<int>() != 9) return 5;
  std::vector<int> v = {1, 2};
  void* raw = &v;
  if (((std::vector<int>*)raw)->size() != 2) return 6;
  std::function<int()> f = [] { return 11; };
  if (via_fn(&f) != 11) return 7;
  if (std::lround(2.5f) != 3 || std::nearbyint(2.5f) != 2.0f || std::copysign(3.0f, -1.0f) != -3.0f) return 8;
  if (std::nextafter(1.0f, 2.0f) <= 1.0f || std::ldexp(3.0f, 4) != 48.0f) return 9;
  if (std::fabs(std::cbrt(-27.0f) + 3.0f) > 1e-5f || std::fabs(std::tanh(0.5f) - 0.46211716f) > 1e-6f) return 10;
  if (std::fabs(std::remainder(7.0f, 2.0f) + 1.0f) > 1e-6f || std::fabs(std::exp2(3.0f) - 8.0f) > 1e-4f) return 11;
  if (std::fabs(std::log1p(1e-6f) - 1e-6f) > 1e-10f) return 12;
  return 0;
}
)OKMS", 0);
}

// More fixes: m[{a, b}], static inline class members with initializers, std::minmax, find({a, b}), static const members
// defined inline, set and map from a range, static local arrays of objects (made under the guard, destroyed at the end),
// a function whose first parameter is a reference to a plain struct.
static void r26_fixes3(Tally& tally) {
    std::printf("r26_fixes3\n");
    okayms_test::run_both(kRun, tally, "fixes3", std::string(kAlloc) + R"OKMS(
#include <map>
#include <utility>
static int p1(void) {
  std::map<std::pair<int, int>, int> edges;
  ++edges[{1, 2}];
  ++edges[{1, 2}];
  ++edges[{2, 1}];
  if (edges.size() != 2 || edges[{1, 2}] != 2) return 1;
  return 0;
}

#include <atomic>
#include <string>
struct G { static inline bool s_p = false; static inline std::atomic<bool> s_quit{false}; static inline std::atomic<int> s_n{5}; static inline std::string s_name = "abc"; };
static int p2(void) { if (G::s_quit.load()) return 1; G::s_quit.store(true); if (!G::s_quit.load() || G::s_n.load() != 5) return 2; return G::s_name == "abc" ? 0 : 3; }

#include <algorithm>
#include <map>
#include <utility>
struct V { float x, y, z; static const V Zero; };
inline const V V::Zero {0, 0, 0};
static void set0(V& o) { o = V::Zero; }
static int p3(void) {
  std::map<std::pair<int, int>, int> mids;
  mids[{1, 2}] = 5;
  auto key = std::minmax(2, 1);
  if (key.first != 1 || key.second != 2) return 1;
  auto it = mids.find({key.first, key.second});
  if (it == mids.end() || it->second != 5) return 2;
  V v; v.x = 3; set0(v);
  if (v.x != 0) return 3;
  return 0;
}

#include <set>
#include <map>
#include <vector>
#include <algorithm>
struct E { float original; bool blocking; };
static float g(const E& e, float lowAlpha) { return e.blocking ? std::min(e.original, lowAlpha) : e.original; }
static int p4(void) {
  std::vector<int> faces = {3, 1, 3, 2};
  std::set<int> sel(faces.begin(), faces.end());
  if (sel.size() != 3 || *sel.begin() != 1) return 1;
  std::set<int> copy(sel);
  if (copy.size() != 3) return 2;
  std::vector<std::pair<int, int>> kv = {{1, 10}, {0, 5}};
  std::map<int, int> m(kv.begin(), kv.end());
  if (m.size() != 2 || m[0] != 5) return 3;
  E e{2.0f, true};
  if (g(e, 1.0f) != 1.0f) return 4;
  return 0;
}

#include <string_view>
#include <string>
static int f(std::string_view s) {
  static constexpr std::string_view kDev[] = {"con", "prn", "aux"};
  int n = 0;
  for (auto d : kDev) if (d == s) ++n;
  return n;
}
static int calls = 0;
static const std::string& name(int i) {
  static const std::string names[] = {"zero", std::string("one"), "two"};
  ++calls;
  return names[i];
}
struct P { int v; P() : v(4) {} };
static int pv() { static P ps[3]; ps[1].v += 1; return ps[1].v; }
static int p5(void) {
  if (f("prn") != 1 || f("xyz") != 0) return 1;
  if (name(1) != "one" || name(2) != "two" || calls != 2) return 2;
  if (pv() != 5 || pv() != 6) return 3;
  return 0;
}

struct PlainS { int k; };
struct VecR { float x; VecR() : x(0) {} };
inline VecR FromPlain(PlainS& s, int* parent, const VecR& base, float dt, VecR& outv) { outv.x = (float)s.k + dt; (void)parent; return base; }
static int p6(void) { PlainS s; s.k = 2; VecR b, o; VecR r = FromPlain(s, nullptr, b, 0.5f, o); return o.x == 2.5f && r.x == 0 ? 0 : 1; }
int entry(void) {
  int r = p1(); if (r) return 10 + r;
  r = p2(); if (r) return 20 + r;
  r = p3(); if (r) return 30 + r;
  r = p4(); if (r) return 40 + r;
  r = p5(); if (r) return 50 + r;
  r = p6(); if (r) return 60 + r;
  return 0;
}
)OKMS", 0);
}

// steady_clock::time_point and system_clock::time_point, a brace-initialized time_point member, duration<double, milli>.
static void r26_clock_types(Tally& tally) {
    std::printf("r26_clock_types\n");
    okayms_test::run_both(kRunClock, tally, "clock_types", std::string(kAlloc) + R"OKMS(
#include <chrono>
struct P { std::chrono::steady_clock::time_point m_start{}, m_epoch{}; };
static double Ms(std::chrono::steady_clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }
int entry(void) {
  P p;
  p.m_start = std::chrono::steady_clock::now();
  std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();
  std::chrono::system_clock::time_point s = std::chrono::system_clock::now();
  (void)s;
  return Ms(t - p.m_start) >= 0.0 ? 0 : 1;
}
)OKMS", 0);
}

// dynamic_cast<T*> over a single-inheritance chain (the type information objects point to their base's), to a base
// without a check, of a null pointer; std::max({...}) and std::min({...}) with T deduced from the braced list.
static void r26_dynamic_cast(Tally& tally) {
    std::printf("r26_dynamic_cast\n");
    okayms_test::run_both(kRunRtti, tally, "dynamic_cast", std::string(kAlloc) + R"OKMS(
#include <algorithm>
#include <typeinfo>
struct A { virtual ~A() {} int a; };
struct B : A { int b; };
struct C : B { int c; };
struct D : A { int d; };
static int check(A* p) {
  int r = 0;
  if (B* b = dynamic_cast<B*>(p)) r += 1 + 0 * b->b;
  if (dynamic_cast<C*>(p) != nullptr) r += 10;
  if (dynamic_cast<D*>(p) != nullptr) r += 100;
  return r;
}
static int prog_a(void) {
  A a; B b; C c; D d;
  if (check(&a) != 0) return 1;
  if (check(&b) != 1) return 2;
  if (check(&c) != 11) return 3;
  if (check(&d) != 100) return 4;
  A* none = nullptr;
  if (dynamic_cast<B*>(none) != nullptr) return 5;
  B* up = &c;
  if (dynamic_cast<A*>(up) != &c) return 6;
  return 0;
}

static int prog_b(void) { float a = 2.0f, b = 5.0f; float m = std::max({a, b, 1e-3f}); float n = std::min({a, b, 3.0f}); float k = std::max(a, b); return m == 5.0f && n == 2.0f && k == 5.0f ? 0 : 1; }

int entry(void) {
  int r = prog_a();
  if (r != 0) return r;
  return prog_b() == 0 ? 0 : 20;
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
    r26_containers(tally);
    r26_locale(tally);
    r26_csignal(tally);
    r26_threads(tally);
    r26_fixes2(tally);
    r26_fixes3(tally);
    r26_clock_types(tally);
    r26_dynamic_cast(tally);
    return tally.finish(16);
}
