// Round 32: the next blockers of the Windows survey and the language rules behind them: `final` and `override` as plain
// names, std::void_t in a class template specialization (a real deduction failure, not always true), a function
// template declared without a body (std::declval), a reference returned by a lambda or through a function pointer,
// `static T& r = f();`, a function type with a qualified result in a template argument, a temporary at the start of a
// statement, several class objects in one namespace-scope declaration, `T x{v};` at namespace scope, `T a[N] = {};` as
// a default member initializer, std::allocator and vector's allocator argument, map of a value type with no default
// constructor, <cstdint>'s limit macros, _mkgmtime and wcslen. Each program returns 0 when all of its checks hold, or
// the number of the first check that failed; it is built by okay-cl at /O0, /O1 and /O2 and run under Wine, and MinGW
// g++ builds the same text with its own headers and must agree (program_check.hpp). Written from ISO/IEC 14882
// ([lex.name], [class.mem], [temp.alias], [temp.deduct], [meta.trans.other], [declval], [expr.call], [dcl.init.ref],
// [stmt.dcl], [dcl.fct], [stmt.ambig], [expr.type.conv], [dcl.decl], [dcl.init.list], [default.allocator],
// [vector.capacity], [map.access], [cstdint.syn]) and Microsoft's public C runtime documentation (_mkgmtime, wcslen).
// No code from other compilers or libraries.

#include "program_check.hpp"

#include <cstring>
#include <string>

namespace {
using okayms_test::Tally;
const okayms_test::ProgramRun kRun{"r32", {}};
const okayms_test::ProgramRun kRunRtti{"r32", {}, true};  // the g++ reference needs typeid
}  // namespace

static const char* kAlloc = okayms_test::bump_alloc();

// `override` and `final` are identifiers with a special meaning only after a function declarator or a class name
// ([lex.name] 2); anywhere else they are names (Inflate.hpp: `bool ok = true, final = false;`). Round 31 dropped the
// words everywhere, so `final = false` was a syntax error and `!final` read `!` of nothing.
static void r32_final_name(Tally& tally) {
    std::printf("r32_final_name\n");
    okayms_test::run_both(kRun, tally, "final_name", std::string(kAlloc) + R"OKMS(
struct Base { virtual int f() const { return 1; } virtual ~Base() {} };
struct Leaf final : Base { int f() const override final { return 2; } };
struct Other final { int v = 3; };
template <class T> struct Wrap final : Base { int f() const override { return 4; } };
int count_down(int n) {
  bool ok = true, final = false;
  int override = 0;
  while (ok && !final) { ++override; final = override >= n; }
  return override;
}
int entry(void) {
  Leaf leaf;
  const Base& b = leaf;
  Wrap<int> w;
  const Base& bw = w;
  Other o;
  CK(1, count_down(5) == 5);
  CK(2, b.f() == 2 && bw.f() == 4 && o.v == 3);
  return 0;
}
)OKMS", 0);
}

// std::void_t<...> in a partial specialization ([temp.alias] 2, [temp.deduct] 8): the specialization is used only when
// the types in it can be formed. okayscript's HasInt<V, void_t<decltype(declval<const V&>().IntPtr())>>. Round 31 could
// not read the pattern; with `typename V::type` it took the specialization for every V (void_t was just void).
static void r32_void_t(Tally& tally) {
    std::printf("r32_void_t\n");
    okayms_test::run_both(kRun, tally, "void_t", std::string(kAlloc) + R"OKMS(
#include <type_traits>
#include <utility>
template <class V, class = void> struct HasInt : std::false_type {};
template <class V> struct HasInt<V, std::void_t<decltype(std::declval<const V&>().IntPtr())>> : std::true_type {};
template <class V, class = void> struct HasType : std::false_type {};
template <class V> struct HasType<V, std::void_t<typename V::type>> : std::true_type {};
struct WithInt { long long n = 7; const long long* IntPtr() const { return &n; } };
struct NoInt { float f = 1.5f; };
struct Typed { typedef int type; };
template <class V> long long IntOr(const V& v, long long other) {
  if constexpr (HasInt<V>::value) return *v.IntPtr();
  else { (void)v; return other; }
}
int entry(void) {
  CK(1, HasInt<WithInt>::value);
  CK(2, !HasInt<NoInt>::value);
  CK(3, HasType<Typed>::value && !HasType<NoInt>::value);
  CK(4, IntOr(WithInt{}, 1) == 7 && IntOr(NoInt{}, 9) == 9);
  return 0;
}
)OKMS", 0);
}

// A reference result through a function pointer or a lambda ([expr.call] 14: an lvalue of the referred type), and a
// static local reference bound to one the first time ([dcl.init.ref], [stmt.dcl] 3; CameraStack.hpp's
// `static std::unordered_map<std::string, Image>& images = []() -> auto& { ... }();`). Round 31: "not an address" and
// "global initializer must be a constant".
static void r32_ref_results(Tally& tally) {
    std::printf("r32_ref_results\n");
    okayms_test::run_both(kRun, tally, "ref_results", std::string(kAlloc) + R"OKMS(
int g_slot = 1;
int& slot() { return g_slot; }
int calls = 0;
int& counter() {
  static int& r = []() -> int& { static int m = 10; ++calls; return m; }();
  return r;
}
int& later() {
  static int& r = slot();
  return r;
}
int entry(void) {
  int& (*p)() = slot;
  p() = 4;
  CK(1, g_slot == 4);
  auto pick = [](int& a) -> int& { return a; };
  int x = 0;
  pick(x) = 9;
  CK(2, x == 9);
  counter() += 5;
  counter() += 1;
  CK(3, counter() == 16 && calls == 1);
  later() = 21;
  CK(4, g_slot == 21 && &later() == &g_slot);
  auto deduced = []() -> auto& { return g_slot; };
  deduced() = 3;
  CK(5, g_slot == 3);
  return 0;
}
)OKMS", 0);
}

// Statements and declarations the engine writes: a temporary at the start of an expression statement ([stmt.ambig] 1:
// `std::vector<T>().swap(v);` is not a declaration), several class objects in one namespace-scope declaration
// ([dcl.decl] 3: `std::atomic<int> a{0}, b{0};`), `T g{v};` of an aggregate or a scalar at namespace scope
// ([dcl.init.list] 3), and `T a[N] = {};` as a default member initializer (value-initialized elements).
static void r32_decls(Tally& tally) {
    std::printf("r32_decls\n");
    okayms_test::run_both(kRun, tally, "decls", std::string(kAlloc) + R"OKMS(
#include <atomic>
#include <vector>
struct Pt { int x; int y; };
struct Counter { int n; Counter(int v) : n(v) {} int get() const { return n; } };
std::atomic<int> g_pending{3}, g_finished{4};
Counter c1{5}, c2(6), c3{7};
Pt g_pt{8, 9};
int g_scalar{10};
int g_zero{};
struct Live {
  std::atomic<int> slots[4] = {};
  int plain[3] = {};
};
struct Box { int v = 2; int twice() const { return v * 2; } };
int entry(void) {
  CK(1, g_pending.load() == 3 && g_finished.load() == 4);
  CK(2, c1.get() == 5 && c2.get() == 6 && c3.get() == 7);
  CK(3, g_pt.x == 8 && g_pt.y == 9 && g_scalar == 10 && g_zero == 0);
  std::vector<int> v;
  v.push_back(1);
  v.push_back(2);
  std::vector<int>().swap(v);
  CK(4, v.size() == 0);
  CK(5, Box().twice() == 4);
  Live l;
  l.slots[2].store(5);
  CK(6, l.slots[0].load() == 0 && l.slots[2].load() == 5 && l.plain[1] == 0);
  return 0;
}
)OKMS", 0);
}

// Library pieces: std::allocator and vector's allocator argument (FbxReader.hpp's vector<uint8_t, UninitAllocator<...>>,
// with a member class template `rebind` that okay-ms steps over), shrink_to_fit, a map and an unordered_map of a type
// with no default constructor (EventBus.hpp's unordered_map<int, type_index>; map::at no longer needs one), <cstdint>'s
// limit macros, a function template declared without a body (declval in decltype), a parameter that points to an array
// (`const uint8_t (*pal)[3]`) or an array parameter of two dimensions (`int rows[][3]`: Round 31 read the rows from
// the parameter's own slot, silent wrong values), and a function type with a qualified result as a template argument (AccountService.hpp's
// std::function<okay::http::Result(...)>).
static void r32_library(Tally& tally) {
    std::printf("r32_library\n");
    okayms_test::run_both(kRunRtti, tally, "library", std::string(kAlloc) + R"OKMS(
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>
template <class T> struct Uninit : std::allocator<T> {
  template <class U> struct rebind { using other = Uninit<U>; };
  Uninit() noexcept = default;
  template <class U> Uninit(const Uninit<U>&) noexcept {}
};
using Bytes = std::vector<std::uint8_t, Uninit<std::uint8_t>>;
struct A {};
struct B {};
namespace web { struct Result { int status = 0; }; }
template <class T> T make_one();
int sum_pal(const std::uint8_t (*pal)[3], int n) {
  int s = 0;
  for (int i = 0; i < n; ++i) s += pal[i][0] + pal[i][2];
  return s;
}
int sum_rows(const int rows[][3], int n) { return rows[n - 1][0] + rows[0][2]; }
int corner(int grid[2][3]) { grid[1][2] = 40; return grid[0][1]; }
int entry(void) {
  Bytes bytes;
  bytes.resize(3);
  bytes[0] = 1; bytes[1] = 2; bytes[2] = 3;
  bytes.reserve(64);
  CK(1, bytes.size() == 3);
  CK(9, bytes.capacity() >= 64);
  bytes.shrink_to_fit();  // a non-binding request ([vector.capacity] 8): only the contents are checked
  CK(8, bytes.size() == 3 && bytes[0] == 1 && bytes[2] == 3);
  std::unordered_map<int, std::type_index> tokens;
  tokens.emplace(1, std::type_index(typeid(A)));
  tokens.emplace(2, std::type_index(typeid(B)));
  CK(2, tokens.find(1)->second == std::type_index(typeid(A)) && tokens.find(2)->second != std::type_index(typeid(A)));
  std::map<int, int> m;
  m[3] = 4;
  CK(3, m.at(3) == 4 && m.size() == 1);
  CK(4, SIZE_MAX == 18446744073709551615ull && INT32_MIN == -2147483647 - 1 && UINT16_MAX == 65535 &&
        INT64_MAX == 9223372036854775807ll && UINT64_C(5) == 5ull);
  using Made = decltype(make_one<web::Result>());
  Made r;
  r.status = 200;
  CK(5, r.status == 200);
  std::uint8_t pal[2][3] = {{1, 2, 3}, {4, 5, 6}};
  CK(6, sum_pal(pal, 2) == 14);
  int rows[2][3] = {{1, 2, 3}, {4, 5, 6}};
  CK(10, sum_rows(rows, 2) == 7 && corner(rows) == 2 && rows[1][2] == 40);
  std::function<web::Result(const std::string&)> send = [](const std::string& s) { web::Result w; w.status = (int)s.size(); return w; };
  CK(7, send("abcd").status == 4);
  return 0;
}
)OKMS", 0);
}

// The C runtime pieces declared for the engine's Windows paths: _mkgmtime (MatchResults.hpp; Microsoft's C runtime
// documentation: the UTC form of mktime, the 64-bit _mkgmtime64 for a 64-bit time_t) and wcslen through <string>
// (NativeHttp.hpp). Compiled only: the decorated, imported names are checked, without running.
static void r32_crt_names(Tally& tally) {
    std::printf("r32_crt_names\n");
    okayms::CompileInput in;
    in.source = "#include <ctime>\n#include <string>\n"
                "long long utc(struct tm* t) { return (long long)_mkgmtime(t); }\n"
                "unsigned long long len(const std::wstring& w) { return wcslen(w.c_str()) + std::wcslen(L\"ab\"); }\n";
    in.include_paths.push_back("stdinc");
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        return;
    }
    CHECK(tally, okayms_test::has_symbol(compiled.value.object, "_mkgmtime64"));
    CHECK(tally, okayms_test::has_symbol(compiled.value.object, "wcslen"));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    Tally tally;
    r32_final_name(tally);
    r32_void_t(tally);
    r32_ref_results(tally);
    r32_decls(tally);
    r32_library(tally);
    r32_crt_names(tally);
    return tally.finish(6);
}
