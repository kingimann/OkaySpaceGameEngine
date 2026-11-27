// Round 33: the JavaScript engine's files (engine/src/JavaScript, the survey's biggest blocker) and the language rules
// behind them: a type name declared in several namespaces (typedefs were one flat table), a reference member in an
// aggregate with default member initializers, a plain struct used as the scope of its nested class (`M::E{...}`,
// `sizeof(M::E)`), a function template-id used as a pointer (`Add<0>`), a nested braced list given to a constructor
// (`pair{name, {line, col}}`), a range `for` over a string literal, u"..." starting with a quote, a class nested in
// the class around a lambda, and `std::initializer_list<T> x = {...}` (silent wrong code: the braces were read as the
// list's two members). Our own headers gain u16string_view, multimap and unordered_multimap, the key-equality argument
// of the unordered containers, hash<T*>, std::mismatch, double versions of isnan/isinf/isfinite/signbit/copysign
// (std::isinf(1e300) went through float and was true), std::calloc and std::free, string::insert(it, c), and more of
// u16string. Each program returns 0 when all of its checks hold, or the number of the first check that failed; it is
// built by okay-cl at /O0, /O1 and /O2 and run under Wine, and MinGW g++ builds the same text with its own headers and
// must agree (program_check.hpp). Written from ISO/IEC 14882 ([basic.lookup.unqual], [namespace.qual],
// [namespace.udecl], [dcl.init.aggr], [class.default.ctor], [class.nest], [expr.sizeof], [temp.arg.explicit],
// [over.over], [conv.func], [over.ics.list], [stmt.ranged], [lex.string], [expr.prim.lambda.closure],
// [dcl.init.list], [support.initlist], [string.view.template], [multimap], [unord.multimap], [unord.req], [unord.hash],
// [mismatch], [string.insert], [string.compare], [string.append], [string.capacity], [cstdlib.syn]) and ISO/IEC 9899
// (7.12.3, 7.22.3.2). No code from other compilers or libraries.

#include "program_check.hpp"

#include <cstring>
#include <string>

namespace {
using okayms_test::Tally;
const okayms_test::ProgramRun kRun{"r33", {}};
const okayms_test::ProgramRun kRunEh{"r33", {}, false, true};  // links okay-ms's own heap (calloc)
}  // namespace

static const char* kAlloc = okayms_test::bump_alloc();

// A type name declared in several namespaces ([basic.lookup.unqual], [namespace.qual], [namespace.udecl]):
// okay::script's `using Value = std::variant<...>` replaced okay::js's class Value everywhere after it, so
// JavaScriptVM.cpp's `using js::Value; Value::Obj(h)` and js::Value's own `Value v;` failed. Before: "no member
// function Obj" and "unknown field t".
static void r33_ns_types(Tally& tally) {
    std::printf("r33_ns_types\n");
    okayms_test::run_both(kRun, tally, "ns_types", R"OKMS(
namespace app { namespace script { struct V { int a; }; using Value = V; typedef int Count; } }
namespace app { namespace ui { struct Value { int x; int y; }; typedef double Count; } }
namespace app { namespace js {
struct O { int k; };
struct H : O { int c; };
struct Value { int t; O* p; static Value Obj(O* o) { Value v; v.t = 7; v.p = o; return v; } };
} }
namespace app { namespace ui { Count half() { return 0.5; } int count_size() { return (int)sizeof(Count); } } }
namespace app { namespace script { int value_size() { return (int)sizeof(Value); } int count_size() { return (int)sizeof(Count); } } }
namespace app {
using js::Value;
int get(Value v) { return v.t; }
int make(js::H* h) { return get(Value::Obj(h)); }
int qualified() { script::Value s; s.a = 5; ui::Value u; u.x = 1; u.y = 2; return s.a + u.x + u.y + (int)sizeof(ui::Count); }
int braced() { script::Value s = script::Value{4}; return s.a; }
}
int entry(void) {
  app::js::H h;
  CK(1, app::make(&h) == 7);
  CK(2, app::script::value_size() == 4);
  CK(3, app::script::count_size() == 4 && app::ui::count_size() == 8);
  CK(4, app::ui::half() == 0.5);
  CK(5, app::qualified() == 16);
  CK(6, sizeof(app::ui::Value) == 8 && sizeof(app::js::Value) == 16);
  CK(7, app::braced() == 4);
  return 0;
}
)OKMS", 0);
}

// An aggregate with a reference member and default member initializers, made from a braced list (JsLibJson.cpp's
// `struct Reader { Runtime& rt; const std::u16string& s; std::size_t i = 0; int depth = 0; }` and `Reader r{rt, text};`,
// [dcl.init.aggr] 4, [class.default.ctor] 2). Before: "the reference member rt needs a member initializer"; and when it
// compiled (the defaults made the class need a constructor) the items were stored through the unset references.
static void r33_ref_aggregate(Tally& tally) {
    std::printf("r33_ref_aggregate\n");
    okayms_test::run_both(kRun, tally, "ref_aggregate", R"OKMS(
struct Rt { int v = 5; };
struct Reader { Rt& rt; const int& s; int i = 0; int depth = 2; int get() const { return rt.v + s + i + depth; } };
int entry(void) {
  Rt rt;
  int t = 10;
  Reader r{rt, t};
  CK(1, r.get() == 17);
  r.rt.v = 6;
  CK(2, rt.v == 6);
  t = 20;
  CK(3, r.s == 20 && r.depth == 2);
  return 0;
}
)OKMS", 0);
}

// A struct with no member functions is the scope of the class nested in it ([class.nest], [expr.sizeof],
// [expr.type.conv]): JsInternal.hpp's `MapObj::Entry{k, v, true}` and `sizeof(MapObj::Entry)`. Before: "expected ';'"
// and "sizeof needs a type or a name". A name that is not a member stays an error (test_round23's B::Size).
static void r33_nested_scope(Tally& tally) {
    std::printf("r33_nested_scope\n");
    okayms_test::run_both(kRun, tally, "nested_scope", std::string(kAlloc) + R"OKMS(
#include <vector>
struct MapObj { struct Entry { int k; double v; bool live; }; int n; };
namespace ns { struct Outer { struct In { char c[24]; }; }; }
int first(MapObj::Entry e) { return e.k; }
int entry(void) {
  CK(1, sizeof(MapObj::Entry) + 48 == 72);
  CK(2, sizeof(ns::Outer::In) == 24);
  MapObj::Entry e = MapObj::Entry{1, 2.0, true};
  CK(3, e.live && e.k == 1);
  e = MapObj::Entry{3, 4.0, false};
  CK(4, e.k == 3 && !e.live);
  CK(5, first(MapObj::Entry{6, 0.0, true}) == 6);
  std::vector<MapObj::Entry> entries;
  entries.push_back(MapObj::Entry{8, 1.5, true});
  CK(6, entries[0].k == 8 && entries[0].v == 1.5);
  return 0;
}
)OKMS", 0);
}

// A function template-id without a call is a pointer to the instance ([temp.arg.explicit] 4, [over.over],
// [conv.func]): JsLibArray.cpp's `const NativeFn kTACtors[] = {TACtor<0>, ...}` and JsLibObject.cpp's
// `ErrorCtor<ErrorType::Error>`. Before: "expected '('".
static void r33_template_fn_values(Tally& tally) {
    std::printf("r33_template_fn_values\n");
    okayms_test::run_both(kRun, tally, "template_fn_values", R"OKMS(
enum class Kind { A, B, C };
typedef int (*Fn)(int);
template <int T> int Add(int x) { return x + T; }
template <Kind K> int Tag(int x) { return x * 10 + (int)K; }
const Fn kAdds[3] = {Add<0>, Add<1>, Add<2>};
int entry(void) {
  Fn tags[] = {Tag<Kind::A>, Tag<Kind::B>, Tag<Kind::C>};
  Fn one = &Add<7>;
  CK(1, kAdds[2](5) == 7 && kAdds[0](5) == 5);
  CK(2, tags[1](3) == 31 && tags[2](0) == 2);
  CK(3, one(1) == 8);
  CK(4, one == &Add<7>);
  return 0;
}
)OKMS", 0);
}

// Braces, literals and lambdas: a nested braced list given to a constructor (JsParser.cpp's
// `refs.push_back({name, {at.line, at.col}})` into a vector of pair<u16string, pair<int, int>>, [over.ics.list]);
// a range `for` over a string literal ([stmt.ranged], [lex.string] 6: JsRegExp.cpp's `for (char16_t c : u"dgimsuy")`);
// u"..." whose first character is a quote (JsCompiler.cpp: the lexer took `u"'` for a character literal); and a class
// nested in the class around a lambda ([expr.prim.lambda.closure] 13: JsRegExp.cpp's `Entry e = stack.back();` found
// another class's Entry). Before: "expected an expression", "a range for needs an array of known length",
// "unterminated character", "unknown field kind".
static void r33_braces_literals(Tally& tally) {
    std::printf("r33_braces_literals\n");
    okayms_test::run_both(kRun, tally, "braces_literals", std::string(kAlloc) + R"OKMS(
#include <vector>
#include <string>
#include <utility>
struct Tok { int line; int col; };
struct Scope { std::vector<std::pair<std::string, std::pair<int, int>>> refs; };
struct Other { struct Entry { double a; double b; double c; }; };
struct Matcher {
  struct Entry { int kind; int idx; };
  int caps[4] = {0, 0, 0, 0};
  int Run() {
    std::vector<Entry> stack;
    stack.push_back(Entry{1, 2});
    stack.push_back(Entry{2, 3});
    auto fail = [&]() -> int {
      int n = 0;
      while (!stack.empty()) { Entry e = stack.back(); stack.pop_back(); if (e.kind == 1) caps[e.idx] = 9; n += (int)sizeof(Entry); }
      return n;
    };
    return fail();
  }
};
int entry(void) {
  std::vector<Scope> s(1);
  Tok at{3, 4};
  std::string name = "ab";
  s.back().refs.push_back({name, {at.line, at.col}});
  std::pair<int, std::pair<int, int>> p = {1, {2, 3}};
  CK(1, s[0].refs.size() == 1 && s[0].refs[0].second.second == 4 && s[0].refs[0].first.size() == 2);
  CK(2, p.second.first == 2);
  int sum = 0;
  for (char16_t c : u"dgi") sum += c ? 1 : 0;
  CK(3, sum == 3);
  for (char c : "xy") sum += c;
  CK(4, sum == 3 + 'x' + 'y');
  const char16_t* q = u"'a";
  std::u16string quoted = u"the constant '" + std::u16string(u"x") + u"'";
  CK(5, q[0] == 39 && q[1] == 97 && quoted.size() == 16 && quoted[13] == 39);
  Matcher m;
  CK(6, m.Run() == 16);
  CK(7, m.caps[2] == 9 && m.caps[3] == 0);
  return 0;
}
)OKMS", 0);
}

// std::initializer_list<T> x = {a, b, c} ([dcl.init.list] 3.10, [support.initlist]) and a static one (JsLibNumber.cpp's
// `static const std::initializer_list<const char*> text = {...}`). Silent wrong code before: the braces were read as
// the list's two members, so x.size() was 5 for {4, 5, 6} (the second element); the static one did not compile.
static void r33_init_list(Tally& tally) {
    std::printf("r33_init_list\n");
    okayms_test::run_both(kRun, tally, "init_list", R"OKMS(
#include <initializer_list>
int count_long(const char* w) {
  static const std::initializer_list<const char*> text = {"long", "short", "narrow"};
  int n = 0;
  for (const char* t : text) {
    const char* a = t;
    const char* b = w;
    while (*a && *a == *b) { ++a; ++b; }
    if (*a == *b) n += 10;
    ++n;
  }
  return n;
}
int entry(void) {
  std::initializer_list<int> three = {4, 5, 6};
  CK(1, three.size() == 3);
  const std::initializer_list<int> local = {4, 5, 6};
  int s = 0;
  for (int v : local) s += v;
  CK(2, s == 15);
  CK(3, count_long("short") == 13 && count_long("x") == 3);
  return 0;
}
)OKMS", 0);
}

// Our own headers: u16string_view as a map key (JavaScript.hpp's atom table), u16string's capacity, compare(pos, n, s),
// append(s, pos, n) and >, string::insert(it, c), hash<T*>, multimap and unordered_multimap with equal_range and
// erase(first, last), unordered_set(first, last), the key-equality argument of unordered_map (JsInternal.hpp's KeyEq:
// before, keys were compared with == and a class without == did not compile), std::mismatch over reverse iterators,
// and the double classification functions (before, std::isinf(1e300) converted the double to float and was true).
static void r33_library(Tally& tally) {
    std::printf("r33_library\n");
    okayms_test::run_both(kRun, tally, "library", std::string(kAlloc) + R"OKMS(
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
struct Key { int v; };
struct KeyHash { size_t operator()(const Key& k) const { return (size_t)(k.v & 7); } };
struct KeyEq { bool operator()(const Key& a, const Key& b) const { return (a.v & 0xFF) == (b.v & 0xFF); } };
int entry(void) {
  std::u16string a = u"hello";
  std::u16string b = u"help";
  std::unordered_map<std::u16string_view, int> atoms;
  atoms.emplace(std::u16string_view(a), 1);
  atoms.emplace(std::u16string_view(b), 2);
  CK(1, atoms.find(std::u16string_view(b)) != atoms.end() && atoms.find(std::u16string_view(b))->second == 2);
  CK(2, atoms.find(std::u16string_view(u"hel")) == atoms.end() && atoms.size() == 2);
  CK(3, a.capacity() >= a.size() && a.compare(1, 3, u"ell") == 0 && a.compare(0, 2, u"hz") < 0 && b > a);
  std::u16string part;
  part.append(a, 1, 3);
  CK(4, part == u"ell");
  std::string d = "234";
  d.insert(d.begin(), '1');
  d.insert(d.begin() + 2, 'x');
  CK(5, d == "12x34");
  int x = 0;
  const void* px = &x;
  CK(6, std::hash<const void*>()(px) == (size_t)px);
  std::multimap<int, int> mm;
  mm.emplace(2, 20); mm.emplace(1, 10); mm.emplace(2, 21); mm.emplace(3, 30);
  auto range = mm.equal_range(2);
  int sum = 0;
  for (auto it = range.first; it != range.second; ++it) sum = sum * 100 + it->second;
  CK(7, sum == 2021 && mm.count(2) == 2 && mm.count(5) == 0);
  std::unordered_multimap<const int*, int> pending;
  int k1 = 0, k2 = 0;
  pending.emplace(&k1, 1); pending.emplace(&k2, 2); pending.emplace(&k1, 3);
  auto r1 = pending.equal_range(&k1);
  int found = 0;
  for (auto it = r1.first; it != r1.second; ++it) found += it->second;
  pending.erase(r1.first, r1.second);
  CK(8, found == 4 && pending.size() == 1 && pending.begin()->second == 2);
  std::vector<int> v = {3, 1, 3, 2};
  std::unordered_set<int> us(v.begin(), v.end());
  CK(9, us.size() == 3 && us.count(2) == 1);
  std::unordered_map<Key, int, KeyHash, KeyEq> by;
  by[Key{1}] = 5;
  by[Key{0x101}] += 2;
  CK(10, by.size() == 1 && by[Key{1}] == 7);
  std::vector<unsigned> l1 = {1, 2, 3, 4};
  std::vector<unsigned> l2 = {9, 2, 3, 4};
  auto diff = std::mismatch(l1.rbegin(), l1.rend(), l2.rbegin());
  CK(11, diff.first != l1.rend() && *diff.first == 1 && *diff.second == 9);
  volatile double big = 1e300;
  double nz = -0.0;
  CK(12, !std::isinf(big) && std::isfinite(big) && std::isinf(big * 1e10) && !std::isnan(big));
  CK(13, std::signbit(nz) && !std::signbit(0.0) && std::signbit(-2.5f) && std::copysign(3.0, -1.0) == -3.0);
  return 0;
}
)OKMS", 0);
}

// std::calloc and std::free ([cstdlib.syn], ISO/IEC 9899 7.22.3.2) with okay-ms's own heap (runtime/mem.c): zeroed
// storage, and null when count * size does not fit in 64 bits (it wrapped before: the size was also cut to 32 bits).
static void r33_calloc(Tally& tally) {
    std::printf("r33_calloc\n");
    okayms_test::run_both(kRunEh, tally, "calloc", R"OKMS(
#include <cstdlib>
int entry(void) {
  int* p = static_cast<int*>(std::calloc(64, sizeof(int)));
  CK(1, p != nullptr);
  int sum = 0;
  for (int i = 0; i < 64; ++i) sum += p[i];
  CK(2, sum == 0);
  std::free(p);
  unsigned long long huge = 1ull << 62;
  CK(3, std::calloc(huge, 16) == nullptr);
  return 0;
}
)OKMS", 0);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    Tally tally;
    r33_ns_types(tally);
    r33_ref_aggregate(tally);
    r33_nested_scope(tally);
    r33_template_fn_values(tally);
    r33_braces_literals(tally);
    r33_init_list(tally);
    r33_library(tally);
    r33_calloc(tally);
    return tally.finish(8);
}
