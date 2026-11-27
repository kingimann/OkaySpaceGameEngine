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

// Declarations and decorated names: `struct Impl;` in a class declares that class's own Impl ([dcl.type.elab] 2; it
// took another class's Impl, so unique_ptr<Impl> was the other class's instance: JsInternal.hpp's `rt.vm->moreIntrinsics`
// was "unknown field"), a declared class keeps its decorated name when defined (JavaScriptVM.cpp: variant<...,
// shared_ptr<ArrayObj>> named ArrayObj two ways), `new Outer::Inner()` ([expr.new]), an unnamed reference parameter in a
// function returning a class (`std::string Text(js::Runtime&, js::HostObj* h)`, [dcl.ambig.res] 1: it was read as an
// object with constructor arguments), and a member of a class template instance compiled later with its own parameter
// pack ([temp.variadic] 5: std::function<Value(Runtime&, const std::string&)> expanded three arguments).
static void r33_decls(Tally& tally) {
    std::printf("r33_decls\n");
    okayms_test::run_both(kRun, tally, "decls", std::string(kAlloc) + R"OKMS(
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace okay {
class VM { public: VM(); ~VM(); struct Impl; std::unique_ptr<Impl> p; };
namespace js {
struct Runtime { struct Impl; std::unique_ptr<Impl> vm; Runtime(); ~Runtime(); };
struct Runtime::Impl { std::vector<int> more; int k = 4; };
inline int f(Runtime& r) { return r.vm->k + (int)r.vm->more.size(); }
Runtime::Runtime() : vm(new Impl()) {}
Runtime::~Runtime() {}
struct H { double v[3]; };
}
struct VM::Impl { js::Runtime rt; double d = 2; };
namespace {
using Impl = VM::Impl;
int g(Impl& i) { return (int)i.d; }
std::string Text(js::Runtime&, js::H* h) {
  auto t = [](double d) { return std::to_string((int)d); };
  return "(" + t(h->v[0]) + ", " + t(h->v[1]) + ")";
}
}
VM::VM() : p(new VM::Impl()) {}
VM::~VM() {}
int text_size(js::Runtime& r, js::H* h) { return (int)Text(r, h).size(); }
}
struct Value { int t; double d; };
struct Hooks {
  int base = 4;
  std::function<Value(Hooks&, const std::string& spec)> importHook;
  struct Mods { std::function<void(Hooks& rt, const std::string& key, int* meta)> initMeta; } modules;
};
int run_hook(Hooks& h) { std::string s = "abc"; Value r = h.importHook(h, s); return r.t; }
int entry(void) {
  okay::VM vm;
  okay::js::Runtime r;
  CK(1, okay::js::f(r) == 4 && okay::g(*vm.p) == 2 && okay::js::f(vm.p->rt) == 4);
  okay::js::H h;
  h.v[0] = 2; h.v[1] = 3; h.v[2] = 0;
  CK(2, okay::text_size(r, &h) == 6);
  Hooks hooks;
  hooks.importHook = [](Hooks& x, const std::string& spec) { Value v; v.t = x.base + (int)spec.size(); v.d = 0; return v; };
  int m = 0;
  hooks.modules.initMeta = [&m](Hooks& x, const std::string& key, int* meta) { m = x.base * 10 + (int)key.size() + *meta; };
  CK(3, run_hook(hooks) == 7);
  int one = 1;
  hooks.modules.initMeta(hooks, "xy", &one);
  CK(4, m == 43);
  return 0;
}
)OKMS", 0);
}

// Braced lists and our headers, second part: a braced list is given to the initializer-list constructor only when its
// items can become elements ([over.match.list] 1: `return {names.begin(), names.end()};` of a set into a
// vector<string>, and `{istream_iterator<string>(in), {}}`, took the list constructor: "no conversion to string", or a
// crash), std::initializer_list<T>{...} as an expression, istream_iterator ([istream.iterator]), u16string insert,
// erase and compare(pos, n, s, pos2, n2), and the double <cmath> functions (before, std::sqrt(double) had float
// precision and std::acosh did not exist; fmod is exact).
static void r33_lists_math(Tally& tally) {
    std::printf("r33_lists_math\n");
    okayms_test::run_both(kRun, tally, "lists_math", std::string(kAlloc) + R"OKMS(
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>
std::vector<std::string> All(const std::set<std::string>& names) { return {names.begin(), names.end()}; }
std::vector<std::string> Words(const char* list) {
  std::istringstream in(list);
  return {std::istream_iterator<std::string>(in), std::istream_iterator<std::string>()};
}
int pick(int argc) {
  int n = 0;
  for (const char* c : argc > 5 ? std::initializer_list<const char*>{"few", "one", "two"} : std::initializer_list<const char*>{"one", "other"}) n += c[0];
  return n;
}
int entry(void) {
  std::set<std::string> s; s.insert("b"); s.insert("a"); s.insert("c");
  std::vector<std::string> v = All(s);
  CK(1, v.size() == 3 && v[0] == "a" && v[2] == "c");
  std::vector<std::string> w = {"x", "y"};
  std::vector<int> n{3, 4};
  CK(2, w.size() == 2 && w[1] == "y" && n.size() == 2 && n[0] == 3);
  std::vector<std::string> words = Words("  abs  floor max ");
  CK(3, words.size() == 3 && words[0] == "abs" && words[2] == "max");
  CK(4, pick(1) == 'o' + 'o' && pick(9) == 'f' + 'o' + 't');
  std::u16string u = u"1234567";
  for (size_t i = u.size(); i > 3; i -= 3) u.insert(i - 3, 1, u',');
  CK(5, u == u"1,234,567");
  u.erase(1, 1);
  CK(6, u == u"1234,567" && u.compare(0, 2, std::u16string(u"x12"), 1, 2) == 0);
  volatile double two = 2.0, x = 5.5, y = 2.0;  // (round(0.49999999999999994) is 0 here; MinGW's reference gives 1)
  double r = std::sqrt(two);
  CK(7, r * r - 2.0 < 1e-15 && r * r - 2.0 > -1e-15 && r != (double)(float)r);
  CK(8, std::fmod(x, y) == 1.5 && std::fmod(-x, y) == -1.5 && std::fmod(1e300, 7.0) == 1.0);
  CK(9, std::round(2.5) == 3.0 && std::round(-2.5) == -3.0 && std::round(-0.4) == 0.0);
  CK(10, std::fabs(std::acosh(1.0)) < 1e-15 && std::fabs(std::asinh(1.0) - 0.881373587019543) < 1e-14);
  CK(11, std::fabs(std::atanh(0.5) - 0.5493061443340549) < 1e-14 && std::fabs(std::cbrt(27.0) - 3.0) < 1e-14);
  int e = 0;
  double m = std::frexp(12.0, &e);
  CK(12, m == 0.75 && e == 4);
  CK(13, std::fabs(std::sin(1.0) - 0.8414709848078965) < 1e-15 && std::fabs(std::exp(1.0) - 2.718281828459045) < 1e-14);
  CK(14, std::fabs(std::expm1(1e-10) - 1.00000000005e-10) < 1e-24 && std::fabs(std::log1p(1e-10) - 9.9999999995e-11) < 1e-24);
  return 0;
}
)OKMS", 0);
}

// The Supabase and asset headers (15 and 6 survey files): an init-capture `[d = DirectSend(...)]`
// ([expr.prim.lambda.capture] 6; before, "an init-capture is not supported"), including a moved unique_ptr; a member
// function template whose result is std::function<R(A, B)> (the '(' inside the template arguments was taken for the
// parameter list); `ns::C::f()` and `ns::C(args)` for a class whose name another namespace uses too ([namespace.qual]),
// also inside a member function named like the class; `const T& k = f();` (the temporary lives as long as the
// reference, [class.temporary] 6; before, "not an address"); `ok ? p : nullptr` with a shared_ptr p ([expr.cond] 4); a
// default argument naming a member type of its class at a call from outside ([dcl.fct.default] 5); and map::extract,
// vector's < and std::stable_partition.
static void r33_backend(Tally& tally) {
    std::printf("r33_backend\n");
    okayms_test::run_both(kRun, tally, "backend", std::string(kAlloc) + R"OKMS(
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
struct Direct { std::string base; std::string operator()(const std::string& p) const { return base + p; } };
Direct Make(std::string b) { Direct d; d.base = std::move(b); return d; }
struct Funcs {
  std::function<std::string(const std::string&)> send_;
  explicit Funcs(std::string url) { send_ = [d = Make(std::move(url))](const std::string& path) { return d(path); }; }
};
template <class T> struct Handle { T v; };
struct Cache {
  template <class T>
  static std::function<int(const std::string&, int)> Wrap(std::function<int(Handle<T>, const std::string& error)> done) {
    return [done](const std::string& e, int k) { Handle<T> h; h.v = (T)k; return done(h, e); };
  }
  int run() { auto w = Wrap<int>([](Handle<int> h, const std::string& e) { return h.v + (int)e.size(); }); return w("ab", 5); }
};
namespace okay { namespace backend {
class Database { public: Database() {} virtual ~Database() {} virtual int Kind() const { return 1; } };
namespace supabase {
class Database final : public okay::backend::Database { public: explicit Database(int s) : s_(s) {} int Kind() const override { return s_; } int s_; static int Recover(int b) { return b + 40; } };
}
} namespace account {
struct Service {
  okay::backend::supabase::Database Database() const { return okay::backend::supabase::Database(7); }
  int Two() const { return okay::backend::supabase::Database{8}.Kind(); }
  static int Recover(int b) { return okay::backend::supabase::Database::Recover(b) + 1; }
};
} }
int live = 0;
struct C { int v; C(int x) : v(x) { ++live; } C(const C& o) : v(o.v) { ++live; } ~C() { --live; } };
C make(int x) { return C(x); }
int loop(int n) { int s = 0; for (int i = 0; i < n; ++i) { const C& r = make(i); s += r.v + live * 100; } return s; }
struct Image { int w = 0; bool Load(const std::string& p) { w = (int)p.size(); return w > 2; } };
std::shared_ptr<Image> Get(const std::string& p) { auto a = std::make_shared<Image>(); return a->Load(p) ? a : nullptr; }
namespace m {
struct POpt { int k = 4; };
struct P { using Opt = POpt; int v = 0; static P From(int x, const Opt& o = Opt()) { P p; p.v = x + o.k; return p; } };
int from(int x) { return P::From(x).v; }
}
struct Entry { int v = 0; };
bool Rename(std::map<std::string, Entry>& mm, const std::string& from, const std::string& to) {
  if (to.empty() || from == to || mm.count(to)) return false;
  auto node = mm.extract(from);
  if (node.empty()) return false;
  node.key() = to;
  mm.insert(std::move(node));
  return true;
}
int entry(void) {
  Funcs f("http://x/");
  CK(1, f.send_("a") == "http://x/a");
  int base = 5;
  auto add = [n = base * 2, k = 1](int x) { return n + k + x; };
  std::unique_ptr<int> up(new int(7));
  auto own = [p = std::move(up)]() { return *p; };
  CK(2, add(3) == 14 && own() == 7 && !up);
  Cache cache;
  CK(3, cache.run() == 7);
  okay::account::Service svc;
  CK(4, svc.Database().Kind() == 7 && svc.Two() == 8 && okay::account::Service::Recover(1) == 42);
  {
    const C& r = make(5);
    CK(5, live == 1 && r.v == 5);
    const C& q = C(6);
    CK(6, live == 2 && q.v == 6);
  }
  CK(7, live == 0 && loop(3) == 303 && live == 0);
  CK(8, Get("abcd") && Get("abcd")->w == 4 && !Get("a"));
  CK(9, m::from(3) == 7);
  std::map<std::string, Entry> mm; mm["a"].v = 1; mm["b"].v = 2;
  CK(10, Rename(mm, "a", "c") && mm.size() == 2 && mm.count("a") == 0 && mm["c"].v == 1 && !Rename(mm, "zz", "q"));
  std::vector<std::pair<int, int>> k1 = {{1, 2}}, k2 = {{1, 3}};
  std::vector<int> s1 = {1, 2}, s2 = {1, 2, 0};
  CK(11, k1 < k2 && !(k2 < k1) && k1 <= k2 && s1 < s2 && s2 >= s1);
  std::vector<int> v = {1, 2, 3, 4, 5, 6};
  auto mid = std::stable_partition(v.begin(), v.end(), [](int x) { return x % 2 == 0; });
  CK(12, mid - v.begin() == 3 && v[0] == 2 && v[1] == 4 && v[2] == 6 && v[3] == 1 && v[5] == 5);
  return 0;
}
)OKMS", 0);
}

// `return [a](int k) { ... };` in a function whose result is std::function ([stmt.return] 2, [dcl.init] 17.6.3: the
// result is copy-initialized from the closure by std::function's constructor). Silent wrong code before: the closure's
// bytes were copied into the std::function object, so the call read its holder pointer from the captured value (the
// first program returned 5 instead of 6; with a captured std::function it returned 10 instead of 11 or crashed).
static void r33_return_convert(Tally& tally) {
    std::printf("r33_return_convert\n");
    okayms_test::run_both(kRun, tally, "return_convert", std::string(kAlloc) + R"OKMS(
#include <functional>
#include <string>
std::function<int(int)> AddA(int a) { return [a](int k) { return k + a; }; }
std::function<int(int)> PlusOne(std::function<int(int)> d) { return [d](int k) { return d(k) + 1; }; }
struct Named { std::string s; };
std::function<std::string()> Greet(Named n) { return [n]() { return "hi " + n.s; }; }
int entry(void) {
  auto w = AddA(1);
  CK(1, w(5) == 6);
  std::function<int(int)> base = [](int x) { return x * 2; };
  CK(2, PlusOne(base)(5) == 11);
  Named n;
  n.s = "you";
  CK(3, Greet(n)() == "hi you");
  return 0;
}
)OKMS", 0);
}

// Aggregates (Layout.cpp's `Builder builder{sheets, &ctx, {}, {false, ...}, {}}`): an array member of scalars from a
// nested list, a reference member bound in the nested-list path ([dcl.init.aggr] 4.2), and the implicit copy
// constructor of a class with an array member and a member with its own copy constructor ([class.copy.ctor] 14).
// Silent wrong code before: the copy constructor stored nothing for the array (the copy read stack garbage: 205 for 111),
// and the reference member was stored as a value; deque::emplace_back did not exist.
static void r33_aggregates(Tally& tally) {
    std::printf("r33_aggregates\n");
    okayms_test::run_both(kRun, tally, "aggregates", std::string(kAlloc) + R"OKMS(
#include <deque>
#include <string>
#include <utility>
#include <vector>
struct Ctx { int k = 3; };
struct Builder {
  const std::vector<int>& sheets;
  const Ctx* ctx;
  std::deque<int> synth;
  bool anyPseudo[6] = {false, false, false, false, false, false};
  std::vector<std::pair<std::string, int>> counters;
  int count() const { int n = 0; for (bool b : anyPseudo) n += b ? 1 : 0; return n + (int)sheets.size() + ctx->k; }
};
struct S { int a; bool f[3] = {false, false, false}; std::vector<int> v; };
int entry(void) {
  S t;
  t.a = 1; t.f[0] = true; t.f[2] = true;
  S s(t);
  CK(1, s.a * 100 + s.f[0] * 10 + s.f[2] == 111);
  S u{1, {true, false, true}, {}};
  CK(2, u.a * 100 + u.f[0] * 10 + u.f[2] == 111 && u.v.empty());
  std::vector<int> sheets = {1, 2};
  Ctx c;
  Builder builder{sheets, &c, {}, {false, true, false, true, false, false}, {}};
  CK(3, builder.count() == 7 && builder.synth.empty() && builder.counters.empty());
  Builder b2{sheets, &c, {}, {true}, {}};
  CK(4, b2.count() == 6 && b2.anyPseudo[0] && !b2.anyPseudo[1]);
  builder.synth.emplace_back(4);
  builder.synth.emplace_front(2);
  builder.synth.emplace_back();
  CK(5, builder.synth.size() == 3 && builder.synth.front() == 2 && builder.synth[1] == 4 && builder.synth.back() == 0);
  return 0;
}
)OKMS", 0);
}

// A std::function local in a nested block of a file that uses exceptions, left by `return` (ActionListOps.cpp): the
// holder's scalar deleting destructor was made there, and its parameters took the block's scope ("unknown name this").
static void r33_eh_block(Tally& tally) {
    std::printf("r33_eh_block\n");
    okayms_test::run_both(kRunEh, tally, "eh_block", R"OKMS(
#include <functional>
struct N { N* c; int tag; };
int find_tag(N* r, int op, int want) {
  int b = 0;
  if (op == 1) {
    try { b += 1; } catch (...) { b = 0; }
  } else {
    std::function<N*(N*)> g = [&](N* k) -> N* { if (!k) return nullptr; if (k->tag == want) return k; if (N* d = g(k->c)) return d; return nullptr; };
    N* hit = g(r);
    return hit ? hit->tag : -1;
  }
  return b;
}
int entry(void) {
  N c{nullptr, 7};
  N b{&c, 5};
  N a{&b, 3};
  CK(1, find_tag(&a, 0, 7) == 7 && find_tag(&a, 0, 9) == -1 && find_tag(&a, 1, 0) == 1);
  return 0;
}
)OKMS", 0);
}

// Raw string literals ([lex.string] 1, [lex.pptoken] 3.1; SceneSerializerInternal.hpp, Browser/Script.cpp,
// InputAsset.cpp, NetBuildSync.cpp; before, "expected ';'"): R"delim(...)delim" with quotes, backslashes, newlines and a
// macro name inside (not expanded), the u8 prefix, and the line numbers after one; and a call through a function pointer
// held by a variable at namespace scope ([expr.call] 1; BehaviorTreeRunner.cpp's g_pauseHook; before, "unknown
// function"), also from a member function with a reference argument.
static void r33_raw_strings(Tally& tally) {
    std::printf("r33_raw_strings\n");
    okayms_test::run_both(kRun, tally, "raw_strings", R"OKMS(
#define JS 1
static int len(const char* s) { int n = 0; while (s[n]) ++n; return n; }
int first_line = __LINE__;
const char* k1 = R"JS(a'b "q" \n
JS // not a macro here
)JS";
const char* k2 = R"(x)";
const char* k3 = u8R"z(()")z";
int after_line = __LINE__;
namespace okay {
struct Runner { using PauseHook = void (*)(Runner&, int); int last = 0; void Note(int id); static void Set(PauseHook h); };
namespace { Runner::PauseHook g_pauseHook = nullptr; }
void Runner::Set(PauseHook h) { g_pauseHook = h; }
void Runner::Note(int id) { if (g_pauseHook) g_pauseHook(*this, id); }
}
typedef int (*Fn)(int);
Fn g_f = nullptr;
int twice(int x) { return 2 * x; }
void hook(okay::Runner& r, int id) { r.last = id; }
int entry(void) {
  CK(1, len(k1) == 34 && k1[1] == '\'' && k1[4] == '"' && k1[8] == '\\' && k1[10] == '\n');
  CK(2, len(k2) == 1 && k2[0] == 'x');
  CK(3, len(k3) == 3 && k3[2] == '"');
  CK(4, after_line - first_line == 6);
  g_f = twice;
  CK(5, g_f(3) == 6);
  okay::Runner r;
  okay::Runner::Set(hook);
  r.Note(7);
  CK(6, r.last == 7);
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
    r33_decls(tally);
    r33_lists_math(tally);
    r33_backend(tally);
    r33_return_convert(tally);
    r33_aggregates(tally);
    r33_eh_block(tally);
    r33_raw_strings(tally);
    return tally.finish(15);
}
