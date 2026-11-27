// Round 34: the next blockers of the Windows survey. Inherited constructors (`using Base::Base;`, okayscript's
// RuntimeError, 16 engine files). Each program returns 0 when all of its checks hold, or the number of the first check
// that failed; it is built by okay-cl at /O0, /O1 and /O2 and run under Wine, and MinGW g++ builds the same text with
// its own headers and must agree (program_check.hpp). Written from ISO/IEC 14882 ([namespace.udecl],
// [class.inhctor.init], [over.match.funcs]). No code from other compilers or libraries.

#include "program_check.hpp"

#include <string>

namespace {
using okayms_test::Tally;
const okayms_test::ProgramRun kRun{"r34", {}};
const okayms_test::ProgramRun kRunRtti{"r34", {}, true};  // g++ builds the reference with typeid
}  // namespace

static const char* kAlloc = okayms_test::bump_alloc();

// `using Base::Base;` ([namespace.udecl] 3, [class.inhctor.init]): the base's constructors make objects of the derived
// class, through two levels, with the base's default arguments; the derived class's own constructor of the same
// parameters hides the base's; the other members get their default member initializers. Before: "no matching
// constructor in RuntimeError" (okayscript/detail/Support.hpp:60) or "an integer argument for a pointer parameter".
static void r34_inherit_ctors(Tally& tally) {
    std::printf("r34_inherit_ctors\n");
    okayms_test::run_both(kRun, tally, "inherit_ctors", std::string(kAlloc) + R"OKMS(
#include <string>
struct A { int x; A(int v) : x(v) {} A(int v, int w) : x(v * w) {} };
struct B : A { using A::A; int y = 3; B(const char*) : A(9) {} };
struct C : B { using B::B; };
struct Err { std::string msg; int line; Err(const std::string& m, int ln, int col = 0) : msg(m), line(ln + col) {} };
struct Base2 : Err { using Err::Err; int kind = 4; };
struct Leaf : Base2 { using Base2::Base2; explicit Leaf(const Err& e) : Base2(e.msg, e.line) { kind = 9; } };
template <class T> struct Box { T v; Box(T a) : v(a) {} };
struct IntBox : Box<int> { using Box<int>::Box; };
struct Hide : A { using A::A; Hide(int v) : A(v + 100) {} };
int entry(void) {
  B b(4);
  CK(1, b.x == 4 && b.y == 3);
  C c(2, 5);
  CK(2, c.x == 10 && c.y == 3);
  C d(7);
  CK(3, d.x == 7);
  B e("x");
  CK(4, e.x == 9);
  Leaf l(std::string("bad"), 5);
  CK(5, l.msg == "bad" && l.line == 5 && l.kind == 4);
  Leaf m("abc", 6, 2);
  CK(6, m.line == 8 && m.msg.size() == 3);
  Leaf n(Err("q", 1));
  CK(7, n.kind == 9 && n.line == 1);
  IntBox ib(42);
  CK(8, ib.v == 42);
  Hide h(1);
  CK(9, h.x == 101);
  Hide h2(2, 3);
  CK(10, h2.x == 6);
  return 0;
}
)OKMS", 0);
}

// The constructs behind okayscript's Runtime.hpp and the engine's OkayScriptVMInternal.hpp (Round 34), each a blocker
// once RuntimeError compiled: `decltype(C::m)` of a data member outside a member function ([expr.prim.id] 3,
// [dcl.type.decltype] 1.3; before "expected '('"), `m{a, b}` and `m{x}` in a constructor's initializer list for a member
// of an aggregate class ([class.base.init] 7, [dcl.init.aggr] 4; before "member m takes one initializer", and for `m{x}`
// "not an address: a unary operator or cast"), a constant of the function around a lambda read without a
// capture ([basic.def.odr] 5, [expr.prim.lambda.capture] 8; before "unknown name"), and `std::vector<E> v{1};` as a
// default member initializer where 1 cannot become an E ([over.match.list] 1: vector(1); before "not an address: a
// constant").
static void r34_runtime_members(Tally& tally) {
    std::printf("r34_runtime_members\n");
    okayms_test::run_both(kRun, tally, "runtime_members", std::string(kAlloc) + R"OKMS(
#include <vector>
struct Env { int v; Env() : v(7) {} };
struct Runtime { int table[3]; double scale; std::vector<Env> scopes{1}; };
using Table = decltype(Runtime::table);
using Scale = decltype(Runtime::scale);
struct HostFn { Runtime* rt; int bias; int get() const { return rt->table[1] + bias; } };
struct SceneFn { Runtime* rt; };
struct PairFn { Runtime* rt; SceneFn scene; int k; };
struct Ctx {
  explicit Ctx(Runtime& r) : tf{&r}, both{&r, SceneFn{&r}}, three{&r, SceneFn{&r}, 9}, n{5} {}
  HostFn tf;
  PairFn both;
  PairFn three;
  int n;
};
int run(int x) {
  constexpr int kMax = 1 << 4;
  const int kMin = 3;
  auto clamp = [](int v) { return v > kMax ? kMax : v < kMin ? kMin : v; };
  return clamp(x);
}
int entry(void) {
  Runtime r;
  r.table[1] = 40;
  CK(1, sizeof(Table) == 12 && sizeof(Scale) == 8);
  Table t = {1, 2, 3};
  CK(2, t[2] == 3);
  Ctx c(r);
  CK(3, c.tf.rt == &r && c.tf.bias == 0 && c.tf.get() == 40);
  CK(4, c.both.rt == &r && c.both.scene.rt == &r && c.both.k == 0);
  CK(5, c.three.k == 9 && c.n == 5);
  CK(6, run(100) == 16 && run(-4) == 3 && run(8) == 8);
  CK(7, r.scopes.size() == 1 && r.scopes[0].v == 7);
  return 0;
}
)OKMS", 0);
}

// An array of an aggregate class whose members have constructors, made from a braced list (UIGallery.hpp's local
// `struct Blob { const char* name; Color col; Vec2 pos, size; }` array; [dcl.init.aggr] 4; before "bad expression"), and
// an item made by a call that returns a small class in a register (before "not an address: a call"). Members without
// an item keep their default member initializers.
static void r34_aggregate_arrays(Tally& tally) {
    std::printf("r34_aggregate_arrays\n");
    okayms_test::run_both(kRun, tally, "aggregate_arrays", R"OKMS(
struct Vec2 { float x = 0, y = 0; Vec2() = default; Vec2(float a, float b) : x(a), y(b) {} };
struct Color { float r = 1, g = 1, b = 1, a = 1; Color() = default; Color(float r0, float g0, float b0, float a0 = 1) : r(r0), g(g0), b(b0), a(a0) {}
  static Color FromBytes(int r0, int g0, int b0) { return {r0 / 255.0f, g0 / 255.0f, b0 / 255.0f}; } };
struct Small { int v; static Small Make(int k) { Small s; s.v = k * 3; return s; } };
struct Blob { const char* name; Color col; Vec2 pos, size; int tag = 6; };
struct Pair { Small a; Small b; };
int f(bool portrait) {
  const Blob blobs[] = {
      {"A", Color::FromBytes(255, 0, 51), portrait ? Vec2{20, 160} : Vec2{150, 150}, {320, 330}},
      {"B", Color::FromBytes(0, 255, 0), Vec2{1, 2}, {3, 4}},
      {"C"}};
  int n = 0;
  for (const Blob& b : blobs) n += (int)b.pos.x + (int)b.size.y + b.tag;
  if (blobs[0].col.r != 1.0f || blobs[0].col.b != 0.2f || blobs[0].col.a != 1.0f) return -1;
  if (blobs[2].col.g != 1.0f || blobs[2].pos.x != 0.0f || blobs[2].name[0] != 'C') return -2;
  return n;
}
int entry(void) {
  CK(1, f(true) == 20 + 330 + 6 + 1 + 4 + 6 + 0 + 0 + 6);
  CK(2, f(false) == 150 + 330 + 6 + 1 + 4 + 6 + 6);
  Pair p = {Small::Make(2), Small::Make(5)};
  CK(3, p.a.v == 6 && p.b.v == 15);
  Small arr[2] = {Small::Make(1), Small::Make(4)};
  CK(4, arr[0].v == 3 && arr[1].v == 12);
  return 0;
}
)OKMS", 0);
}

// std::function's target<T>() and target_type() ([func.wrap.func.targ]; OkayScriptVMInternal.hpp's
// `it->second.target<SharedApiCall>()`; before "expected an expression"), and a namespace-qualified class as the result
// of a function type in a template argument when another namespace has an enumeration of that name
// (Transport.hpp's `std::function<okay::http::Result(...)>` beside ScriptBindings.hpp's `enum class Result`; before the
// enumeration was taken: "not an address: a call of function__fn<W4Result@@...>").
static void r34_function_target(Tally& tally) {
    std::printf("r34_function_target\n");
    okayms_test::run_both(kRunRtti, tally, "function_target", std::string(kAlloc) + R"OKMS(
#include <functional>
namespace okay { namespace http { struct Result { int a; int b; int c; int d; int e; }; } }
namespace okay { namespace bind { enum class Result { NotFound, Ok }; } }
namespace okay { namespace account { struct Result { int z; }; } }
namespace okay { namespace backend {
struct HT { using Hook = std::function<okay::http::Result(int)>; Hook h; void Set(Hook x) { h = x; } int Call() { return h(3).e; } };
} }
namespace okay { namespace account {
struct S { okay::backend::HT t; void SetT(std::function<okay::http::Result(int)> fn) { t.Set(fn); } };
} }
struct Api { int id; int operator()(int k) const { return k + id; } };
struct Other { int operator()(int k) const { return k; } };
int entry(void) {
  okay::account::S s;
  s.SetT([](int k) { okay::http::Result r{}; r.e = k * 2; return r; });
  CK(1, s.t.Call() == 6);
  std::function<int(int)> f = Api{40};
  CK(2, f.target<Api>() != nullptr && f.target<Api>()->id == 40);
  CK(3, f.target<Other>() == nullptr);
  CK(4, f.target_type() == typeid(Api));
  std::function<int(int)> e;
  CK(5, e.target<Api>() == nullptr && e.target_type() == typeid(void));
  CK(6, f(2) == 42);
  return 0;
}
)OKMS", 0);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    Tally tally;
    r34_inherit_ctors(tally);
    r34_runtime_members(tally);
    r34_aggregate_arrays(tally);
    r34_function_target(tally);
    return tally.finish(4);
}
