// Round 36: the next blockers of the Windows engine survey. Each program returns 0 when all of its checks hold, or the
// number of the first check that failed; it is built by okay-cl at /O0, /O1 and /O2 and run under Wine, and MinGW g++
// builds the same text with its own headers and must agree (program_check.hpp). Written from ISO/IEC 14882 (the
// sections are named at each test). No code from other compilers or libraries.

#include "program_check.hpp"

#include <string>

namespace {
using okayms_test::Tally;
const okayms_test::ProgramRun kRun{"r36", {}};
const okayms_test::ProgramRun kRunEh{"r36", {}, false, true};  // own heap and exception runtime
}  // namespace

static const char* kAlloc = okayms_test::bump_alloc();

// The result of `!x`, of a comparison and of `&&` and `||` is a bool ([expr.unary.op] 9, [expr.rel] 6, [expr.eq] 6,
// [expr.log.and] 1, [expr.log.or] 1). Before Round 36 okay-ms gave `!x` no type at all, so `return !x;` in a function
// that returns a class was not converted by the class's constructor ("not an address: a unary operator or cast" in
// NotNode::Eval, 4 engine files), and it typed comparisons as int, so `return a == b;` and `Kind(a == b)` chose the
// int overload over the bool one (silent wrong code: VsValue's `return a == b;` made a number, not a bool).
static void r36_bool_results(Tally& tally) {
    std::printf("r36_bool_results\n");
    okayms_test::run_both(kRun, tally, "bool_results", std::string(kAlloc) + R"OKMS(
#include <string>
struct V { int k; std::string s; V(bool) : k(2) {} V(int) : k(3) {} V(float) : k(1) {} };
static V f1(bool x) { return !x; }
static V f2(int x) { return x == 1; }
static V f3(int a, int b) { return a < b && b > 0; }
static V f4(int a) { return a != 0 || a > 9; }
static int Kind(bool) { return 1; }
static int Kind(int) { return 2; }
template <class T> struct Is { static int id() { return 0; } };
template <> struct Is<bool> { static int id() { return 5; } };
template <class T> static int Deduce(T) { return Is<T>::id(); }
int entry(void) {
  CK(1, f1(true).k == 2);
  CK(2, f2(1).k == 2);
  CK(3, f3(1, 2).k == 2 && f4(0).k == 2);
  int a = 3, b = 3;
  CK(4, Kind(a == b) == 1 && Kind(!a) == 1 && Kind(a < b || a > b) == 1 && Kind(a) == 2);
  auto r = a == b;
  auto n = !a;
  CK(5, Kind(r) == 1 && Kind(n) == 1 && sizeof(r) == 1 && r && !n);
  CK(6, Deduce(a >= b) == 5 && Deduce(!b) == 5 && Deduce(a) == 0);
  V v = !a;
  CK(7, v.k == 2);
  return 0;
}
)OKMS", 0);
}

// `obj = value` where the class has no operator= taking the value: the value is converted to a temporary of the class
// by a converting constructor and the class's implicit copy assignment runs ([over.match.oper] 3, [class.copy.assign],
// [over.ics.user]). Nodes.hpp's ToggleNode: `ctx.variables[name] = !cur;` (VsValue has a bool constructor). Before
// Round 36: "not an address" for `!x`, a comparison, a constant or a cast.
static void r36_assign_converted(Tally& tally) {
    std::printf("r36_assign_converted\n");
    okayms_test::run_both(kRun, tally, "assign_converted", std::string(kAlloc) + R"OKMS(
#include <string>
#include <map>
static int g_live = 0;
struct V {
  int k;
  std::string s;
  V() : k(0) { ++g_live; }
  V(bool) : k(2), s("bool") { ++g_live; }
  V(int i) : k(3), s("int") { ++g_live; if (i == 7) s = "seven, a longer text that is on the heap"; }
  V(const char* t) : k(4), s(t) { ++g_live; }
  V(const V& o) : k(o.k), s(o.s) { ++g_live; }
  ~V() { --g_live; }
};
int entry(void) {
  {
    V v;
    bool b = false;
    int i = 7;
    v = !b;
    CK(1, v.k == 2 && v.s == "bool");
    v = i;
    CK(2, v.k == 3 && v.s == "seven, a longer text that is on the heap");
    v = i == 7;
    CK(3, v.k == 2);
    v = 5;
    CK(4, v.k == 3 && v.s == "int");
    v = true;
    CK(5, v.k == 2);
    v = "text";
    CK(6, v.k == 4 && v.s == "text");
    v = (bool)i;
    CK(7, v.k == 2);
    std::map<std::string, V> m;
    m["a"] = !b;
    m["b"] = i;
    CK(8, m["a"].k == 2 && m["b"].k == 3 && m["b"].s.size() > 20);
  }
  CK(9, g_live == 0);
  return 0;
}
)OKMS", 0);
}

// A default argument that calls a static member function of the class, written in the class (`const std::string& file
// = DefaultFile()`), and called from outside the class: the name is looked up in the class ([dcl.fct.default] 5,
// [basic.scope.class]) and the default is evaluated at each call ([dcl.fct.default] 9). SaveData.hpp's Save::SetFloat
// from NodesWave4.hpp. Before Round 36: "unknown function DefaultFile".
static void r36_member_defaults(Tally& tally) {
    std::printf("r36_member_defaults\n");
    okayms_test::run_both(kRun, tally, "member_defaults", std::string(kAlloc) + R"OKMS(
#include <string>
static int Helper() { return 40; }
struct Save {
  static const std::string& DefaultFile() { static std::string f = "save.okaysave"; return f; }
  static int Get(const std::string& k, const std::string& file = DefaultFile()) { return (int)(k.size() * 100 + file.size()); }
  static int Next(int step = Bump()) { return step; }
  static int Bump() { static int c = 0; return ++c; }
  static int Free(int v = Helper()) { return v + 2; }
};
struct Node {
  int Exec() { return Save::Get("ab"); }
};
int entry(void) {
  CK(1, Save::Get("abc") == 313);
  Node n;
  CK(2, n.Exec() == 213);
  CK(3, Save::Get("a", "xy") == 102);
  const int a = Save::Next();
  const int b = Save::Next();
  CK(4, a == 1 && b == 2 && Save::Next(9) == 9);
  CK(5, Save::Free() == 42 && Save::Free(1) == 3);
  return 0;
}
)OKMS", 0);
}

// unique_ptr<Derived> to unique_ptr<Base> ([unique.ptr.single.ctor] 18, [unique.ptr.single.asgn] 6): NodeGraph::Add's
// `AddNode(std::make_unique<T>(...))` with AddNode taking unique_ptr<VsNode>. Before Round 36 our <memory> had no such
// constructor: "not an address: a call of make_unique".
static void r36_unique_ptr_convert(Tally& tally) {
    std::printf("r36_unique_ptr_convert\n");
    okayms_test::run_both(kRun, tally, "unique_ptr_convert", std::string(kAlloc) + R"OKMS(
#include <memory>
#include <utility>
#include <vector>
static int g_gone = 0;
struct B { virtual ~B() { ++g_gone; } virtual int Id() const { return 1; } };
struct D : B { int j; explicit D(int x) : j(x) {} ~D() override { g_gone += 10; } int Id() const override { return j; } };
struct G {
  std::vector<std::unique_ptr<B>> v;
  B* AddNode(std::unique_ptr<B> n) { v.push_back(std::move(n)); return v.back().get(); }
  template <typename T, typename... Args> T* Add(Args&&... args) {
    return static_cast<T*>(AddNode(std::make_unique<T>(std::forward<Args>(args)...)));
  }
};
int entry(void) {
  {
    G g;
    D* d = g.Add<D>(5);
    CK(1, d->j == 5 && g.v.size() == 1 && g.v[0]->Id() == 5);
    std::unique_ptr<D> own = std::make_unique<D>(7);
    std::unique_ptr<B> base;
    base = std::move(own);
    CK(2, !own && base && base->Id() == 7);
    std::unique_ptr<B> moved(std::make_unique<D>(8));
    CK(3, moved->Id() == 8 && g_gone == 0);
  }
  CK(4, g_gone == 33);
  return 0;
}
)OKMS", 0);
}

// By-value parameters of a class with a destructor are destroyed by the callee, once ([expr.call] 7 leaves the place to
// the implementation; the Microsoft x64 convention has the callee destroy them). Before Round 36 the caller made a copy,
// passed the callee a byte copy of that, and destroyed its own copy after the call: what the callee moved out of its
// parameter was destroyed a second time (a `std::unique_ptr<VsNode>` parameter pushed into a vector deleted its node;
// `explicit ToggleNode(std::string n) : name(std::move(n))` freed the member's text), silent wrong code. Also: placement
// new of a class with a move constructor and no copy constructor copied its bytes instead of moving ([expr.new] 22;
// vector<unique_ptr>::push_back). Linked with okay-ms's own heap (mem.c), so a second free is not hidden by the bump
// allocator. g++ (Itanium convention) destroys parameters in the caller at the end of the full-expression, so each
// check reads the counts in a later statement.
static void r36_by_value_params(Tally& tally) {
    std::printf("r36_by_value_params\n");
    okayms_test::run_both(kRunEh, tally, "by_value_params", R"OKMS(
#include <string>
#include <utility>
static int g_live = 0, g_made = 0;
struct C {
  int v;
  C(int x) : v(x) { ++g_live; ++g_made; }
  C(const C& o) : v(o.v) { ++g_live; ++g_made; }
  C(C&& o) : v(o.v) { o.v = -1; ++g_live; ++g_made; }
  ~C() { --g_live; }
};
struct S { C c; int k; };               // implicit copy constructor and destructor
struct Big { C a; C b; long long pad[4]; Big(int x) : a(x), b(x + 1) {} };
static int byval(C c) { return c.v; }
static int byS(S s) { return s.c.v + s.k; }
static int byBig(Big b) { return b.a.v + b.b.v; }
static int unnamed(C, int k) { return k; }
static C pass(C c) { return c; }
static std::string keep;
static void takeStr(std::string s) { keep = std::move(s); }
struct Holder { std::string name; explicit Holder(std::string n) : name(std::move(n)) {} };
static int thrower(C c, int k) { C inner(k); if (k > 0) throw k; return c.v; }
static int early(C c, int k) { if (k > 0) return c.v + k; return -c.v; }
int entry(void) {
  {
    C a(5);
    int r = byval(a);
    CK(1, r == 5 && g_live == 1 && a.v == 5);
    r = byval(C(6));
    CK(2, r == 6 && g_live == 1);
    S s{C(7), 1};
    r = byS(s);
    CK(3, r == 8 && g_live == 2 && s.c.v == 7);
    Big big(10);
    r = byBig(big);
    CK(4, r == 21 && g_live == 4);
    r = unnamed(a, 3);
    CK(5, r == 3 && g_live == 4);
    C c2 = pass(a);
    CK(6, c2.v == 5 && g_live == 5);
    r = early(a, 1);
    int r2 = early(a, 0);
    CK(7, r == 6 && r2 == -5 && g_live == 5);
    std::string t = "a text long enough to be on the heap for sure";
    takeStr(t);
    CK(8, keep == t && t.size() > 20);
    takeStr(std::string("second text, also long enough for the heap"));
    CK(9, keep == "second text, also long enough for the heap");
    Holder h(std::string("holder name that is long enough to be on the heap"));
    CK(10, h.name == "holder name that is long enough to be on the heap");
    C moved(9);
    r = byval(std::move(moved));
    CK(11, r == 9 && moved.v == -1 && g_live == 6);
    int caught = 0;
    C four(4);
    try { thrower(four, 2); } catch (int e) { caught = e; }
    CK(13, caught == 2 && g_live == 7);
    try { thrower(a, 3); } catch (int e) { caught += e; }
    CK(14, caught == 5 && g_live == 7);
  }
  CK(12, g_live == 0);
  return 0;
}

)OKMS", 0);
}

// Single-file blockers of the Windows survey (Round 36), each from ISO/IEC 14882:
// - `.00074f` and `.5`: a floating literal that starts with its point ([lex.fcon] 1; VorbisDecoder.cpp);
// - `auto dead = it++;`: the type of x++ and ++x is the operand's ([expr.post.incr] 1; Undo.cpp, map erase loops);
// - `auto* tp = fp ? nullptr : p;`: the pointer's type ([expr.cond] 7.3; HumanoidActionsRuntime.cpp, before an int);
// - `int f(Mono)` with a plain struct: an unnamed parameter of class type ([dcl.fct] 3; Interpreter.cpp's
//   `operator()(std::monostate)`);
// - `[&](int) -> CellInfo { ... }` with a local class as the trailing result ([expr.prim.lambda] 1; NavGrid3D.hpp);
// - `template struct R<int>;` an explicit instantiation ([temp.explicit] 1; OkayScriptVM.cpp);
// - `C::C(Fence f)` with Fence a member type of C ([basic.scope.class]; GpuTimer.cpp);
// - `W& W::operator=(W&&) noexcept = default;` outside the class ([dcl.fct.def.default] 5; WavDecoder.cpp);
// - std::deque's rbegin and rend ([sequence.reqmts]) and std::addressof ([specialized.addressof]; Jobs.hpp).
static void r36_single_file_blockers(Tally& tally) {
    std::printf("r36_single_file_blockers\n");
    okayms_test::run_both(kRun, tally, "single_file", std::string(kAlloc) + R"OKMS(
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <variant>
struct Mono {};
static int Take(Mono) { return 7; }
static int Take2(int a, Mono) { return a; }
struct Visit {
  int operator()(std::monostate) const { return 1; }
  int operator()(int v) const { return v; }
};
template <class T> struct R { T v; T get() const { return v + 1; } };
template struct R<int>;
struct G {
  using Fence = std::function<int()>;
  Fence f;
  explicit G(Fence fence);
};
G::G(Fence fence) : f(fence) {}
struct W {
  int a;
  int b;
  W() : a(0), b(0) {}
  W& operator=(W&&) noexcept;
};
W& W::operator=(W&&) noexcept = default;
struct Node { int k; };
static Node* Find(Node* n, int k) { return n->k == k ? n : nullptr; }
int entry(void) {
  float x = 1000.0f;
  CK(1, .5 == 0.5 && .25f * 4.0f == 1.0f && .00074f * x > 0.73f && .00074f * x < 0.75f);
  int arr[3] = {4, 5, 6};
  int* it = arr;
  auto dead = it++;
  CK(2, *dead == 4 && *it == 5);
  auto back = it--;
  CK(3, *back == 5 && *it == 4);
  Node n{3};
  Node* fp = nullptr;
  auto* tp = fp ? nullptr : Find(&n, 3);
  CK(4, tp != nullptr && tp->k == 3);
  Mono m;
  CK(5, Take(m) == 7 && Take2(9, m) == 9);
  Visit vis;
  CK(6, vis(std::monostate{}) == 1 && vis(4) == 4);
  struct CellInfo { bool ok; float surf; };
  auto probe = [&](int ix) -> CellInfo { return CellInfo{ix > 0, 2.5f}; };
  CK(7, probe(2).ok && !probe(-1).ok && probe(1).surf == 2.5f);
  R<int> r{4};
  CK(8, r.get() == 5);
  G g([] { return 11; });
  CK(9, g.f() == 11);
  W w1;
  W w2;
  w2.a = 3;
  w2.b = 4;
  w1 = static_cast<W&&>(w2);
  CK(10, w1.a == 3 && w1.b == 4);
  std::deque<int> d;
  d.push_back(1);
  d.push_back(2);
  d.push_back(3);
  int order = 0;
  for (auto rit = d.rbegin(); rit != d.rend(); ++rit) order = order * 10 + *rit;
  CK(11, order == 321);
  std::map<int, int> mp;
  mp[1] = 10;
  mp[2] = 20;
  int sum = 0;
  for (auto mit = mp.begin(); mit != mp.end();) {
    auto cur = mit++;
    sum += cur->second;
  }
  CK(12, sum == 30);
  CK(13, std::addressof(n) == &n);
  return 0;
}
)OKMS", 0);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    Tally tally;
    r36_bool_results(tally);
    r36_assign_converted(tally);
    r36_member_defaults(tally);
    r36_unique_ptr_convert(tally);
    r36_by_value_params(tally);
    r36_single_file_blockers(tally);
    return tally.finish(6);
}
