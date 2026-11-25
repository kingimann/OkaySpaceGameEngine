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
// `std::string()` of a by-value parameter (SaveFramework.hpp's SaveWriter, "not an address: a call of string").
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
  int Size() const { return (int)m_prefix.size() + m_block->n; }
  W Scope(const std::string& name) const { return W(*m_block, m_prefix + name + "."); }
 private:
  Block* m_block;
  std::string m_prefix;
};
struct Doc { Block b; Block& Get(const std::string&) { return b; } W Writer(const std::string& name) { return W(Get(name)); } };
int g(int b, std::string prefix = std::string()) { return b + (int)prefix.size(); }
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
  CK(4, w.Size() == 2 && w.Scope("ab").Size() == 5 && g(3) == 3 && g(3, "xy") == 5);
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
    return tally.finish(8);
}
