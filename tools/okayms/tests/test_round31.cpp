// Round 31: the real rules behind three of Round 30's narrow ones (a parenthesized function name, the conditional
// operator with a class operand, the point of declaration), Microsoft's representation of pointers to data members,
// pointers to member functions, and the next blockers of the Windows and plain surveys. Each program returns 0 when all
// of its checks hold, or the number of the first check that failed; it is built by okay-cl at /O0, /O1 and /O2 and run
// under Wine, and MinGW g++ builds the same text with its own headers and must agree (program_check.hpp). Written from
// ISO/IEC 14882 ([cpp.replace], [expr.prim.paren], [expr.cond], [basic.scope.pdecl], [conv.mem], [dcl.mptr],
// [expr.mptr.oper], [temp.variadic], [dcl.fct.default], [class.local], [expr.comma], [dcl.enum], [time.duration],
// [dcl.struct.bind], [string.conversions]) and Microsoft's public x64 name-decoration rules. No code from other
// compilers or libraries.

#include "program_check.hpp"

#include <cstring>
#include <string>

namespace {
using okayms_test::Tally;
const okayms_test::ProgramRun kRun{"r31", {}};
const okayms_test::ProgramRun kRunEh{"r31", {}, false, true};
}  // namespace

static const char* kAlloc = okayms_test::bump_alloc();

// A function-like macro is expanded only when '(' follows its name ([cpp.replace] 10), so `(name)(args)` calls the
// function even where a macro of that name exists (windows.h's min and max). A parenthesized name is the same
// function, object or overload set ([expr.prim.paren]), for any name: Round 30 did this only for names ending in min
// or max. A parenthesized type is still a cast, also when a postfix operator follows its operand.
static void r31_paren_name(Tally& tally) {
    std::printf("r31_paren_name\n");
    okayms_test::run_both(kRun, tally, "paren_name", std::string(kAlloc) + R"OKMS(
#include <algorithm>
#define max(a, b) ((a) > (b) ? 100 : 200)
#define clamp3(v, lo, hi) 999
namespace ns {
inline int (clamp3)(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
inline int twice(int v) { return v * 2; }
template <class T> T pick(T a, T b) { return a < b ? b : a; }
}
struct Doubler { int k; int operator()(int v) const { return v * k; } };
typedef int I;
int entry(void) {
  int a = 3, b = 9;
  CK(1, (std::max)(a, b) == 9 && max(a, b) == 200);
  CK(2, (ns::clamp3)(42, 0, 10) == 10 && clamp3(1, 2, 3) == 999);
  CK(3, (ns::twice)(4) == 8 && (ns::pick<int>)(5, 2) == 5 && (ns::pick)(1.5, 2.5) == 2.5);
  int (*fp)(int) = ns::twice;
  Doubler d{3};
  CK(4, (fp)(6) == 12 && (d)(5) == 15 && (std::max<int>)(7, 1) == 7);
  int arr[3] = {10, 20, 30};
  CK(5, (I)(arr)[1] == 20 && (long)(arr[2]) == 30);
  return 0;
}
)OKMS", 0);
}

// c ? x : y where one operand is an object of a class and the other is not, or of another class ([expr.cond] 4): the
// operand that converts to the other's type (a converting constructor, a conversion function, or a derived-to-base
// lvalue) is converted, for any class (Round 30 did only std::string from a string literal).
static void r31_ternary_class(Tally& tally) {
    std::printf("r31_ternary_class\n");
    okayms_test::run_both(kRun, tally, "ternary_class", std::string(kAlloc) + R"OKMS(
#include <string>
struct Name {
  char text[8];
  int len;
  Name(const char* s) : len(0) { while (s[len] && len < 7) { text[len] = s[len]; ++len; } text[len] = 0; }
};
struct Meters {
  int v;
  operator int() const { return v * 100; }
};
struct Base { int x = 1; virtual ~Base() {} };
struct Pad { int p = 7; };
struct Derived : Pad, Base { int y = 2; };
struct Label { int n; Label(const Name& nm) : n(nm.len * 10) {} };
Name make(int i) { return Name(i > 0 ? "plus" : "minus"); }
int entry(void) {
  bool t = true, f = false;
  Name n1 = t ? "abc" : make(1);
  Name n2 = f ? "abc" : make(-1);
  CK(1, n1.len == 3 && n2.len == 5 && n2.text[0] == 'm');
  Meters m{3};
  int v1 = t ? m : 7;
  int v2 = f ? m : 7;
  CK(2, v1 == 300 && v2 == 7);
  Derived d;
  Base b;
  b.x = 5;
  (t ? d : b).x = 40;
  (f ? d : b).x = 50;
  CK(3, d.x == 40 && b.x == 50 && d.y == 2 && d.p == 7);
  Label l1 = t ? Label(Name("xy")) : Name("hello");
  Label l2 = f ? Label(Name("xy")) : Name("hello");
  CK(4, l1.n == 20 && l2.n == 50);
  std::string s = f ? "lit" : std::string("str") + "ing";
  CK(5, s == "string");
  return 0;
}
)OKMS", 0);
}

// The point of declaration of a local is after its declarator, before its initializer ([basic.scope.pdecl] 1), for
// every variable: before Round 31 only a lambda initializer saw the new name, and `long k = sizeof(k);` with an outer
// `int k` silently took the outer one (4 instead of 8).
static void r31_decl_point(Tally& tally) {
    std::printf("r31_decl_point\n");
    okayms_test::run_both(kRun, tally, "decl_point", std::string(kAlloc) + R"OKMS(
#include <functional>
struct Node { Node* self; int v; };
int entry(void) {
  int k = 7;
  {
    long long k = sizeof(k);
    CK(1, k == 8);
  }
  CK(2, k == 7);
  void* p = &p;
  CK(3, p == (void*)&p);
  Node n = {&n, 3};
  CK(4, n.self == &n && n.self->v == 3);
  Node m{&m, 4};
  CK(5, m.self == &m);
  const int c = 5;
  int* q = nullptr;
  {
    int* q = (int*)&q;
    CK(6, (void*)q == (void*)&q);
  }
  CK(8, q == nullptr);
  std::function<int(int)> fact = [&](int x) { return x <= 1 ? 1 : x * fact(x - 1); };
  CK(7, fact(5) == 120 && c == 5);
  return 0;
}
)OKMS", 0);
}

// A pack expansion in a braced list ([temp.variadic] 5): Localization.hpp's
// `std::vector<LocArg>{LocArg(std::forward<A>(args))...}` (HoldPrompt.hpp:98, 11 files), and `{args...}`.
static void r31_pack_braced(Tally& tally) {
    std::printf("r31_pack_braced\n");
    okayms_test::run_both(kRun, tally, "pack_braced", std::string(kAlloc) + R"OKMS(
#include <string>
#include <vector>
#include <utility>
struct LocArg {
  std::string text;
  LocArg(const std::string& v) : text(v) {}
  LocArg(const char* v) : text(v) {}
  LocArg(int v) : text(v < 10 ? std::string(1, (char)('0' + v)) : std::string("big")) {}
};
std::string Args(const std::string& key, const std::vector<LocArg>& args) {
  std::string r = key;
  for (const LocArg& a : args) r += "|" + a.text;
  return r;
}
template <class... A> std::string Localize(const std::string& key, A&&... args) {
  return Args(key, std::vector<LocArg>{LocArg(std::forward<A>(args))...});
}
template <class... A> int Sum(A... args) {
  std::vector<int> v{args...};
  int s = 0;
  for (int x : v) s += x;
  return s + (int)v.size() * 100;
}
int entry(void) {
  std::string keys = "E";
  const std::string alt = "F";
  bool localized = true;
  keys = localized ? Localize("either", keys, alt) : keys + " or " + alt;
  CK(1, keys == "either|E|F");
  std::string k2 = !localized ? Localize("x", keys) : std::string("E") + " or " + alt;
  CK(2, k2 == "E or F");
  CK(3, Localize("n", 3, "q") == "n|3|q" && Localize("none") == "none");
  CK(4, Sum(1, 2, 3) == 306 && Sum(5) == 105);
  return 0;
}
)OKMS", 0);
}

// A scoped enumerator named through its class (`IKJointLimit::Type::Hinge`) when a class of the enumeration's plain
// name exists in another namespace (ScriptInternal.hpp's `struct Type`): JointLimit.hpp:114, 10 files. The enumerator
// was taken as a member function of that class ("expected '('").
static void r31_scoped_enum_name(Tally& tally) {
    std::printf("r31_scoped_enum_name\n");
    okayms_test::run_both(kRun, tally, "scoped_enum_name", std::string(kAlloc) + R"OKMS(
namespace sc { struct Type { int kind; int id; bool operator==(const Type& o) const { return kind == o.kind; } }; }
namespace okay {
struct L {
  enum class Type { None = 0, Hinge = 1, Ball = 2 };
  Type type = Type::None;
  bool Active() const { return type != Type::None; }
};
inline int f(const L& limit) { if (limit.type == L::Type::Hinge) return 1; return limit.type == L::Type::Ball ? 2 : 3; }
}
int entry(void) {
  okay::L l;
  CK(1, !l.Active() && okay::f(l) == 3);
  l.type = okay::L::Type::Hinge;
  CK(2, l.Active() && okay::f(l) == 1 && (int)l.type == 1);
  l.type = okay::L::Type::Ball;
  sc::Type a{1, 2}, b{1, 3};
  CK(3, okay::f(l) == 2 && a == b);
  return 0;
}
)OKMS", 0);
}

// Pieces: the comma operator with floating-point operands (SoftwareRenderer.hpp:302, `++x, w0 += dw0, w1 += dw1`); a
// class in a member function naming the function's static local (VisualScriptComponent.hpp:106); a default argument
// `std::string()` of a by-value parameter (SaveFramework.hpp's SaveWriter, "not an address: a call of string"); a member
// of an anonymous union named in a member function (Lua.hpp:83, "unknown name b").
static void r31_pieces(Tally& tally) {
    std::printf("r31_pieces\n");
    okayms_test::run_both(kRun, tally, "pieces", std::string(kAlloc) + R"OKMS(
#include <string>
#include <utility>
int raster(int minX, int maxX) {
  double w0 = 0.5, w1 = 0.25;
  const double dw0 = -0.125, dw1 = 0.0625;
  int n = 0;
  for (int x = minX; x <= maxX; ++x, w0 += dw0, w1 += dw1) {
    double w2 = 1.0 - w0 - w1;
    if (w0 >= 0 && w2 >= 0) ++n;
  }
  double last = (n, w0);
  return n * 1000 + (int)(last * 100);
}
struct C {
  int seen = 0;
  void Send(int n) {
    static int depth = 0;
    if (depth >= 3) return;
    struct Depth { Depth() { ++depth; } ~Depth() { --depth; } } guard;
    seen += depth;
    if (n > 0) Send(n - 1);
  }
};
struct Block { int n = 0; };
class W {
 public:
  explicit W(Block& b, std::string prefix = std::string()) : m_block(&b), m_prefix(std::move(prefix)) {}
  int Length() const { return (int)m_prefix.size() + m_block->n; }
  W Scope(const std::string& name) const { return W(*m_block, m_prefix + name + "."); }
 private:
  Block* m_block;
  std::string m_prefix;
};
struct Doc { Block b; Block& Get(const std::string&) { return b; } W Writer(const std::string& name) { return W(Get(name)); } };
int g(int b, std::string prefix = std::string()) { return b + (int)prefix.size(); }
enum class VT { Nil, Bool, Int, Float };
struct Value {
  VT t = VT::Nil;
  union { bool b; long long i; double n; };
  Value() : i(0) {}
  bool Truthy() const { return !(t == VT::Nil || (t == VT::Bool && !b)); }
  double AsNum() const { return t == VT::Int ? (double)i : n; }
  void SetInt(long long v) { t = VT::Int; i = v; }
};
int entry(void) {
  CK(1, raster(0, 7) == 4950);
  C c;
  c.Send(5);
  CK(2, c.seen == 6);
  c.Send(1);
  CK(3, c.seen == 9);
  Doc d;
  d.b.n = 2;
  W w = d.Writer("x");
  CK(4, w.Length() == 2 && w.Scope("ab").Length() == 5 && g(3) == 3 && g(3, "xy") == 5);
  Value v;
  CK(5, !v.Truthy());
  v.t = VT::Bool;
  v.b = false;
  CK(6, !v.Truthy());
  v.SetInt(41);
  CK(7, v.Truthy() && v.AsNum() == 41.0 && v.i == 41);
  return 0;
}
)OKMS", 0);
}

// Pointers to data members with Microsoft's representation ([dcl.mptr], [conv.mem]; Round 31): the member's offset,
// and -1 for null, so a pointer to the member at offset 0 is not null. Round 30 kept the offset plus one and 0 for null.
// The first four bytes of the value are compared (g++'s is the same offset and -1 in eight bytes): null given as
// nullptr, as 0, left out of a braced list, left out of a constant table, and as a default member initializer; tests,
// `!pm`, `pm &&`, `bool b = pm`, and a pointer to a base's member converted to the derived class.
static void r31_memptr_repr(Tally& tally) {
    std::printf("r31_memptr_repr\n");
    okayms_test::run_both(kRun, tally, "memptr_repr", std::string(kAlloc) + R"OKMS(
struct T { int first; int second; float f; };
struct Desc { const char* name; int T::*i; float T::*f; };
static const Desc kTable[] = {{"first", &T::first, nullptr}, {"f", nullptr, &T::f}, {"none"}};
struct Holder { int T::*pm = nullptr; int k = 4; };
struct Pre { char pad[12]; };
struct D : Pre, T { int extra; };
int bytes4(const void* p) {
  const unsigned char* b = (const unsigned char*)p;
  return (int)((unsigned)b[0] | (unsigned)b[1] << 8 | (unsigned)b[2] << 16 | (unsigned)b[3] << 24);
}
int raw(int T::*p) { return bytes4(&p); }
int raw_d(int D::*p) { return bytes4(&p); }
int entry(void) {
  int T::*p0 = &T::first;
  int T::*p1 = &T::second;
  int T::*z = nullptr;
  int T::*z0 = 0;
  CK(1, raw(p0) == 0 && raw(p1) == 4 && raw(z) == -1 && raw(z0) == -1);
  CK(2, p0 != nullptr && p0 && !z && z == nullptr && !(z != 0) && p0 != p1);
  bool b0 = p0, bz = z;
  CK(3, b0 && !bz && (p0 && p1) && !(z || !p1) && (p0 ? 1 : 2) == 1 && (z ? 1 : 2) == 2);
  CK(4, raw(kTable[0].i) == 0 && raw(kTable[1].i) == -1 && raw(kTable[2].i) == -1 && kTable[2].f == nullptr &&
            kTable[1].f != nullptr);
  Desc local = {"x", &T::first};
  CK(5, raw(local.i) == 0 && local.f == nullptr && local.i != nullptr);
  Holder h;
  CK(6, raw(h.pm) == -1 && !h.pm && h.k == 4);
  T t{1, 2, 3.5f};
  CK(7, t.*p0 == 1 && t.*p1 == 2 && t.*(kTable[1].f) == 3.5f);
  int D::*dp = p0;
  int D::*dz = z;
  D d;
  d.first = 11;
  CK(8, raw_d(dp) == 12 && raw_d(dz) == -1 && d.*dp == 11 && !dz);
  return 0;
}
)OKMS", 0);
}

// The decorated names of functions taking pointers to data members (Microsoft's public x64 rules: P, E, then Q for a
// member pointer, R when the member type is const, the class's name and @@, then the member's type).
// Two objects of a class with no operator for them and no conversion: an error, where Round 30 compared their first
// bytes ([over.match.oper] 9).
static void r31_class_operand_error(Tally& tally) {
    std::printf("r31_class_operand_error\n");
    okayms::CompileInput in;
    in.source = "struct A { long long n; };\nint less(A a, A b) { return a < b; }\n";
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, !compiled.ok());
    CHECK(tally, !compiled.ok() && compiled.error.find("no operator <") != std::string::npos);
}

// std::filesystem::filesystem_error ([fs.class.filesystem.error]) and std::system_error exist and can be thrown
// (FileCopy.hpp:46, 4 files: "a thrown value of unknown type"); the decorated-name level, without running.
static void r31_filesystem_error(Tally& tally) {
    std::printf("r31_filesystem_error\n");
    okayms::CompileInput in;
    in.source = "#include <filesystem>\n#include <system_error>\n"
                "void fail(const std::filesystem::path& a, const std::filesystem::path& b, std::error_code ec) {\n"
                "  if (ec) throw std::filesystem::filesystem_error(\"copy\", a, b, ec);\n}\n"
                "int code_of(const std::system_error& e) { return e.code().value() + (int)e.code().category().name()[0]; }\n";
    in.include_paths.push_back("stdinc");
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) std::fprintf(stderr, "  %s\n", compiled.error.c_str());
}

static void r31_memptr_names(Tally& tally) {
    std::printf("r31_memptr_names\n");
    okayms::CompileInput in;
    // Overloaded, so the names are decorated (a function that is not overloaded keeps its plain name in okay-ms).
    in.source = "struct S { int a; const int c; };\n"
                "int take(int S::*p) { return p ? 1 : 0; }\n"
                "int take(int v) { return v; }\n"
                "int takec(const int S::*p) { return p ? 1 : 0; }\n"
                "int takec(int v) { return v; }\n";
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        return;
    }
    CHECK(tally, okayms_test::has_symbol(compiled.value.object, "?take@@YAHPEQS@@H@Z"));
    CHECK(tally, okayms_test::has_symbol(compiled.value.object, "?takec@@YAHPERS@@H@Z"));
}

// Pointers to member functions ([dcl.mptr], [expr.unary.op] 4, [expr.mptr.oper] 6; Round 31), as Physics2D.cpp,
// Physics3D.cpp and Scene.cpp use them: `void (Component::*fn)(const Collision2D&)` as a parameter, `&Component::OnHit`
// of a virtual function (the call reaches the derived class's override through a thunk), a non-virtual one, a const
// one with a result, `(c->*fn)(info)` and `(obj.*fn)(...)`, null, comparison, a lambda parameter and a local.
static void r31_memfn(Tally& tally) {
    std::printf("r31_memfn\n");
    okayms_test::run_both(kRun, tally, "memfn", std::string(kAlloc) + R"OKMS(
struct Collision { int force; };
struct Component {
  int hits = 0;
  int awake = 0;
  virtual ~Component() {}
  virtual void OnHit(const Collision& c) { hits += c.force; }
  virtual void Awake() { awake += 1; }
  void Plain(int k) { hits += k * 1000; }
  int Twice(int k) const { return k * 2 + hits; }
};
struct Player : Component {
  int special = 0;
  void OnHit(const Collision& c) override { special += c.force * 10; }
  void Awake() override { awake += 100; }
};
void Dispatch(Component** list, int n, void (Component::*fn)(const Collision&), const Collision& info) {
  for (int i = 0; i < n; ++i) {
    Component* c = list[i];
    if (c) (c->*fn)(info);
  }
}
int entry(void) {
  Component base;
  Player player;
  Component* list[3] = {&base, nullptr, &player};
  Collision hit{3};
  Dispatch(list, 3, &Component::OnHit, hit);
  CK(1, base.hits == 3 && player.hits == 0 && player.special == 30);
  void (Component::*plain)(int) = &Component::Plain;
  (base.*plain)(2);
  (player.*plain)(1);
  CK(2, base.hits == 2003 && player.hits == 1000);
  int (Component::*twice)(int) const = &Component::Twice;
  const Component& cref = base;
  CK(3, (cref.*twice)(5) == 2013 && (list[2]->*twice)(1) == 1002);
  auto send = [](Component* c, void (Component::*fn)()) { (c->*fn)(); };
  send(&base, &Component::Awake);
  send(&player, &Component::Awake);
  CK(4, base.awake == 1 && player.awake == 100);
  void (Component::*none)() = nullptr;
  void (Component::*awake)() = &Component::Awake;
  CK(5, !none && none == nullptr && awake != nullptr && awake == &Component::Awake && awake != none);
  if (awake) (player.*awake)();
  CK(6, player.awake == 200);
  return 0;
}
)OKMS", 0);
}

// The decorated name of a function taking a pointer to a member function (P8, the class, E, A or B for the this
// qualifier, A, the result, the parameters, Z).
static void r31_memfn_names(Tally& tally) {
    std::printf("r31_memfn_names\n");
    okayms::CompileInput in;
    in.source = "struct S { int a; void f(int) {} int g() const { return a; } };\n"
                "int take(void (S::*p)(int)) { return p ? 1 : 0; }\n"
                "int take(int (S::*p)() const) { return p ? 2 : 0; }\n"
                "int take(int v) { return v; }\n";
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  %s\n", compiled.error.c_str());
        return;
    }
    CHECK(tally, okayms_test::has_symbol(compiled.value.object, "?take@@YAHP8S@@EAAXH@Z@Z"));
    CHECK(tally, okayms_test::has_symbol(compiled.value.object, "?take@@YAHP8S@@EBAHXZ@Z"));
}

// A name that is an enumeration in one namespace and a class in another ([namespace.qual], [basic.lookup.unqual]):
// okay::scripting's `enum class Result` (ScriptBindings.hpp) and okay::http's `struct Result` (NativeHttp.hpp). Written
// `okay::http::Result`, or `Result` inside okay::http, it is the class; inside okay::scripting the enumeration
// (AccountService.hpp:1250, 16 files: "cannot deduce template arguments of move" on the class's member).
static void r31_enum_class_names(Tally& tally) {
    std::printf("r31_enum_class_names\n");
    okayms_test::run_both(kRun, tally, "enum_class_names", std::string(kAlloc) + R"OKMS(
#include <string>
#include <utility>
namespace okay { namespace scripting {
enum class Result { NotFound, Ok, Error };
inline Result Get(int a) { return a > 0 ? Result::Ok : a < 0 ? Result::Error : Result::NotFound; }
} }
namespace okay { namespace http {
struct Result { bool reached = false; long status = 0; std::string body; };
inline Result Send(int a) { Result r; r.reached = a > 0; r.status = a; r.body = "xy"; return r; }
} }
namespace okay { namespace account {
struct ApiResponse { bool ok = false; long status = 0; std::string body; };
struct Api {
  ApiResponse Call(int a) const {
    okay::http::Result nr = okay::http::Send(a);
    ApiResponse out;
    if (!nr.reached) return out;
    out.status = nr.status;
    out.body = std::move(nr.body);
    out.ok = true;
    return out;
  }
};
} }
int entry(void) {
  okay::account::Api api;
  okay::account::ApiResponse r = api.Call(3);
  CK(1, r.ok && r.status == 3 && r.body == "xy" && !api.Call(0).ok);
  CK(2, okay::scripting::Get(1) == okay::scripting::Result::Ok && (int)okay::scripting::Get(-1) == 2);
  okay::http::Result h;
  CK(3, !h.reached && h.body.empty());
  return 0;
}
)OKMS", 0);
}

// A time point moved by a duration of another unit ([time.point.nonmember]; NativeFetch.hpp's `now() - milliseconds(1000)`,
// which subtracted bytes). Durations of different units compared ([time.duration.comparisons]: in their common unit), as SceneSerializer.cpp's
// `(std::filesystem::file_time_type::clock::now() - mtime) > std::chrono::seconds(2)`, whose parenthesized start, a
// type's nested name followed by `::now()`, was read as a cast ("expected ')'"). Structured bindings of std::tuple, a
// tuple-like type ([dcl.struct.bind] 4).
static void r31_chrono_tuple(Tally& tally) {
    std::printf("r31_chrono_tuple\n");
    okayms_test::run_both(kRun, tally, "chrono_tuple", std::string(kAlloc) + R"OKMS(
#include <chrono>
#include <tuple>
#include <string>
struct Clock { static long long now() { return 42; } };
int entry(void) {
  std::chrono::milliseconds ms(2500);
  std::chrono::seconds s(2);
  int r = 0;
  if (ms > s) r += 1;
  if (s < ms) r += 2;
  if (std::chrono::milliseconds(2000) == s) r += 4;
  if (ms != s) r += 8;
  if (std::chrono::seconds(3) >= ms) r += 16;
  if (s <= std::chrono::milliseconds(1999)) r += 32;
  if (std::chrono::nanoseconds(1) < std::chrono::hours(1)) r += 64;
  CK(1, r == 1 + 2 + 4 + 8 + 16 + 64);
  CK(2, (Clock::now() - 2) == 40);
  typedef std::chrono::time_point<std::chrono::steady_clock, std::chrono::nanoseconds> TP;
  TP t0(std::chrono::nanoseconds(5000000000LL));
  TP t1 = t0 - std::chrono::milliseconds(1000);
  TP t2 = t0 + std::chrono::seconds(2);
  CK(5, t1.time_since_epoch().count() == 4000000000LL && t2.time_since_epoch().count() == 7000000000LL &&
            (t0 - t1) == std::chrono::seconds(1));
  std::tuple<int, std::string, double> t(7, "ab", 2.5);
  auto [a, b, c] = t;
  CK(3, a == 7 && b == "ab" && c == 2.5);
  auto& [x, y, z] = t;
  x = 9;
  y += "c";
  CK(4, std::get<0>(t) == 9 && std::get<1>(t) == "abc" && z == 2.5);
  return 0;
}
)OKMS", 0);
}

// std::stoi and its siblings throw invalid_argument for text that is not a number and out_of_range for a value outside
// int ([string.conversions]), in a file that uses exceptions (Round 30's had no exceptions and gave 0). And
// std::filesystem::filesystem_error, a std::system_error with paths ([fs.class.filesystem.error]; FileCopy.hpp:46
// throws one), caught as itself and as its base.
static void r31_stoi_throws(Tally& tally) {
    std::printf("r31_stoi_throws\n");
    okayms_test::run_both(kRunEh, tally, "stoi_throws", R"OKMS(
#include <string>
#include <stdexcept>
#include <filesystem>
#include <system_error>
int parse(const std::string& s, int fallback) {
  try {
    return std::stoi(s);
  } catch (const std::invalid_argument&) {
    return fallback;
  } catch (const std::out_of_range&) {
    return fallback * 2;
  }
}
int entry(void) {
  CK(1, parse("42", -1) == 42 && parse(" -7x", -1) == -7);
  CK(2, parse("abc", -1) == -1 && parse("", -3) == -3);
  CK(3, parse("99999999999", 5) == 10);
  bool caught = false;
  try {
    (void)std::stod(std::string("x1"));
  } catch (const std::exception& e) {
    caught = std::string(e.what()) == "stod";
  }
  CK(4, caught && std::stod(std::string("1.5")) == 1.5);
  int code = 0;
  std::string first, second;
  try {
    throw std::filesystem::filesystem_error("copy", std::filesystem::path("a/b.txt"), std::filesystem::path("c"),
                                            std::error_code(5, std::generic_category()));
  } catch (const std::filesystem::filesystem_error& e) {
    code = e.code().value();
    first = e.path1().string();
    second = e.path2().string();
  }
  CK(5, code == 5 && first == "a/b.txt" && second == "c");
  try {
    throw std::filesystem::filesystem_error("x", std::error_code(7, std::generic_category()));
  } catch (const std::system_error& e) {
    code = e.code().value();
  }
  CK(6, code == 7);
  return 0;
}
)OKMS", 0);
}

// `return s;` of a class object that is not the function's own local (a reference parameter, a member, a global, *p):
// the result is made by the copy constructor ([stmt.return] 2). Before Round 31 its bytes were copied, so a
// std::string result shared the source's heap buffer and freed it when it was destroyed (path::string()'s
// `return text_;`). Run with okay-ms's real heap (runtime/mem.c), where a freed buffer is reused: the bump allocator of
// the other programs never reuses memory and hid it.
static void r31_return_copy(Tally& tally) {
    std::printf("r31_return_copy\n");
    okayms_test::run_both(kRunEh, tally, "return_copy", R"OKMS(
#include <string>
#include <utility>
std::string g_text("a/b.txt");
std::string byref(const std::string& s) { return s; }
std::string glob() { return g_text; }
std::string byval(std::string s) { return s; }
struct P {
  std::string text_;
  std::string list[2];
  std::string str() const { return text_; }
  std::string other() const { return this->list[1]; }
  std::string take() { return std::move(text_); }
};
std::string deref(const P* p) { return p->text_; }
int entry(void) {
  std::string a("a/b.txt");
  CK(1, byref(a) == "a/b.txt");
  std::string fill1(100, 'x');
  CK(2, a == "a/b.txt");
  CK(3, glob() == "a/b.txt");
  std::string fill2(100, 'y');
  CK(4, g_text == "a/b.txt" && byval(a) == "a/b.txt" && a == "a/b.txt");
  P p;
  p.text_ = "a/b.txt";
  p.list[1] = "second item";
  CK(5, p.str() == "a/b.txt" && p.other() == "second item" && deref(&p) == "a/b.txt");
  std::string fill3(100, 'z');
  CK(6, p.text_ == "a/b.txt" && p.list[1] == "second item");
  std::string moved = p.take();
  std::string fill4(100, 'w');
  CK(7, moved == "a/b.txt");
  return 0;
}
)OKMS", 0);
}

// Operators that used to compare bytes: std::pair's and std::array's comparisons ([pairs.spec], [array.syn]; Mesh.hpp
// sorts pair<float, int>, SkinnedMesh.hpp compares array<float, 4>), the containers' == ([container.reqmts],
// [unord.req]; SaveFramework.hpp compares two std::map), and a
// hidden friend operator!= that calls the operator== defined before it in the class ([class.friend] 6: the friends
// were moved out of the class in the wrong order, so the call found no operator== and compared the bytes).
static void r31_pair_friend(Tally& tally) {
    std::printf("r31_pair_friend\n");
    okayms_test::run_both(kRun, tally, "pair_friend", std::string(kAlloc) + R"OKMS(
#include <algorithm>
#include <array>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
namespace n {
struct Id {
  int v;
  int pad;
  friend bool operator==(Id a, Id b) { return a.v == b.v; }
  friend bool operator!=(Id a, Id b) { return !(a == b); }
};
}
int entry(void) {
  std::vector<std::pair<float, int> > got;
  got.push_back(std::make_pair(2.5f, 1));
  got.push_back(std::make_pair(0.25f, 2));
  got.push_back(std::make_pair(-1.0f, 3));
  got.push_back(std::make_pair(0.25f, 0));
  std::sort(got.begin(), got.end());
  CK(1, got[0].second == 3 && got[1].second == 0 && got[2].second == 2 && got[3].second == 1);
  std::array<float, 3> p{{1.0f, 2.0f, 3.0f}}, q{{1.0f, 2.0f, -1.0f}};
  CK(4, p != q && q < p && !(p < q) && p == p && q <= p);
  std::pair<int, int> a(1, 2), b(1, 3);
  CK(2, a < b && !(b < a) && a != b && a == std::make_pair(1, 2) && b > a && a <= a && b >= a);
  n::Id x{1, 7}, y{1, 9}, z{2, 7};
  CK(3, x == y && !(x != y) && x != z);
  std::wstring w = L"ab" + std::wstring(L"cd");
  w = w + L"e";
  CK(5, w.size() == 5 && w == std::wstring(L"abcde"));
  std::map<std::string, std::string> m1, m2;
  m1["a"] = "x";
  m1["b"] = "y";
  m2["b"] = "y";
  m2["a"] = "x";
  CK(6, m1 == m2 && !(m1 != m2));
  m2["a"] = "z";
  CK(7, m1 != m2);
  std::unordered_map<int, int> u1, u2;
  u1[1] = 10;
  u1[2] = 20;
  u2[2] = 20;
  u2[1] = 10;
  std::set<int> s1, s2;
  s1.insert(3);
  s2.insert(3);
  std::deque<int> d1, d2;
  d1.push_back(1);
  d2.push_back(1);
  d2.push_back(2);
  CK(8, u1 == u2 && s1 == s2 && d1 != d2);
  return 0;
}
)OKMS", 0);
}

// A base or member constructor whose argument needs a temporary ([class.base.init] 7): `m(w + "!")` and a class
// argument passed by value to a base constructor. Before Round 31 the temporary was made after the constructor calls,
// so the member was made from storage not yet written (an empty string, or a wrong error code in a derived exception).
static void r31_ctor_init_temps(Tally& tally) {
    std::printf("r31_ctor_init_temps\n");
    okayms_test::run_both(kRunEh, tally, "ctor_init_temps", R"OKMS(
#include <string>
struct EC { int v; const char* c; };
struct Base {
  std::string m;
  EC code;
  Base(EC ec, const std::string& w) : m(w + "!"), code(ec) {}
  virtual ~Base() {}
};
struct Derived : Base {
  std::string a, b;
  Derived(const std::string& w, EC ec) : Base(ec, w), a(w + w), b(std::string("x") + w) {}
};
struct Plain { std::string m; explicit Plain(const std::string& w) : m(std::string(w) + "?") {} };
int entry(void) {
  std::string w("copy");
  Plain p(w);
  CK(1, p.m == "copy?");
  EC ec{5, "g"};
  Base b(ec, w);
  CK(2, b.m == "copy!" && b.code.v == 5);
  Derived d("ab", ec);
  CK(3, d.m == "ab!" && d.code.v == 5 && d.a == "abab" && d.b == "xab");
  return 0;
}
)OKMS", 0);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    Tally tally;
    r31_paren_name(tally);
    r31_ternary_class(tally);
    r31_decl_point(tally);
    r31_pack_braced(tally);
    r31_scoped_enum_name(tally);
    r31_pieces(tally);
    r31_memptr_repr(tally);
    r31_memptr_names(tally);
    r31_memfn(tally);
    r31_memfn_names(tally);
    r31_enum_class_names(tally);
    r31_chrono_tuple(tally);
    r31_stoi_throws(tally);
    r31_class_operand_error(tally);
    r31_filesystem_error(tally);
    r31_return_copy(tally);
    r31_pair_friend(tally);
    r31_ctor_init_temps(tally);
    return tally.finish(18);
}
