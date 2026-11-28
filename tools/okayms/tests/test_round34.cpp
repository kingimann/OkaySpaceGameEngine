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

// A qualified name looked up from the namespace being read ([namespace.qual] 1, [basic.lookup.qual]): okayofficial's
// `json::Value` inside namespace okayofficial is okayofficial::json::Value, not another namespace's json::Value
// (OfficialGame.hpp:331, 8 files; before, "a range for needs an array of known length" as the other class had no str).
// A using-declaration through a namespace alias (`namespace og = okayofficial; using og::json::Escape;`,
// AntiCheatIntegrity.hpp; before "unknown function Escape"). An if with an init statement whose condition makes a
// temporary ([stmt.if] 3; AntiCheatIntegrity.hpp:333; before "unknown name __bt..."), destroyed once.
static void r34_lookup(Tally& tally) {
    std::printf("r34_lookup\n");
    okayms_test::run_both(kRun, tally, "lookup", std::string(kAlloc) + R"OKMS(
#include <string>
namespace okay { namespace net { namespace json { struct Value { int other; }; } } }
namespace okay { namespace js { using Value = int; } }
namespace okayofficial {
namespace json {
struct Value { std::string str; int n = 3; };
inline std::string Escape(const std::string& s) { return "<" + s + ">"; }
}
inline int Digits(const json::Value* v) {
  int n = 0;
  for (char c : v->str) n = n * 10 + (c - '0');
  return n;
}
}
namespace okayac {
namespace og = okayofficial;
inline std::string Wrap(const std::string& s) {
  using og::json::Escape;
  return Escape(s);
}
}
static int g_dtors = 0;
struct Tmp { std::string s; Tmp(const char* p) : s(p) {} ~Tmp() { ++g_dtors; } bool empty() const { return s.empty(); } };
inline Tmp Trim(const char* p) { return Tmp(p); }
int pick(const char* (*env)(const char*)) {
  if (const char* e = env("K"); e && !Trim(e).empty()) return 1;
  return 2;
}
const char* give(const char* k) { return k; }
const char* none(const char*) { return ""; }
int entry(void) {
  okayofficial::json::Value v;
  v.str = "42";
  CK(1, okayofficial::Digits(&v) == 42 && v.n == 3);
  CK(2, okayac::Wrap("a") == "<a>");
  CK(3, pick(give) == 1 && g_dtors == 1);
  CK(4, pick(none) == 2 && g_dtors == 2);
  return 0;
}
)OKMS", 0);
}

// Silent wrong code (Round 33 and before, /O1 and /O2): a `char` or `short` local kept in a register was spilled around a
// call with a 32-bit store, three (or two) bytes past its slot, over the next local. A range for over a reference
// (`for (char c : *s) total = total + 1;`, OfficialGame.hpp's `for (char c : v->str)`) lost the end pointer it keeps
// beside the element and ran off the string: an access violation (exit 5). Each spill now stores and loads the slot's own
// width (Intel SDM vol. 2, MOV 88 /r, MOVSX, MOVZX). Also silent before: `unsigned char c = p[i];` kept in a register was
// not cut to 8 bits (0xFF stayed -1, check 5), nor `short h = 65535;` to 16 (check 6).
static void r34_narrow_spill(Tally& tally) {
    std::printf("r34_narrow_spill\n");
    okayms_test::run_both(kRun, tally, "narrow_spill", R"OKMS(
struct S { const char* p; int n; const char* begin() const { return p; } const char* end() const { return p + n; } };
struct W { const short* p; int n; const short* begin() const { return p; } const short* end() const { return p + n; } };
int Count(const S* s) { int total = 0; for (char c : *s) total = total + 1; return total; }
int Sum(const S& s) { int total = 0; for (char c : s) total = total + (c - 48); return total; }
int Shorts(const W* w) { int total = 0; for (short v : *w) total = total + v; return total; }
int UnsignedChars(const S* s) { int total = 0; for (unsigned char c : *s) total = total + c; return total; }
int Bytes(const char* p, int n) { int total = 0; for (int i = 0; i < n; i++) { unsigned char c = p[i]; total = total + c; } return total; }
int Halves(const int* p, int n) { int total = 0; for (int i = 0; i < n; i++) { short h = p[i]; total = total + h; } return total; }
int entry(void) {
  S s;
  s.p = "4271"; s.n = 4;
  CK(1, Count(&s) == 4);
  CK(2, Sum(s) == 14);
  short xs[3] = {-5, 300, 7};
  W w;
  w.p = xs; w.n = 3;
  CK(3, Shorts(&w) == 302);
  S h;
  h.p = "\xff\x01"; h.n = 2;
  CK(4, UnsignedChars(&h) == 256);
  CK(5, Bytes("\xff\x01", 2) == 256);
  int wide[2] = {65535, 65538};
  CK(6, Halves(wide, 2) == 1);
  return 0;
}
)OKMS", 0);
}

// Silent wrong code (every round before 34, /O1 and /O2): the register allocator took a variable's live range from its
// first to its last name in the tree, so a variable read only in a loop's condition (`i < n`) was free once the walk
// passed the condition, and a variable made in the body was given its register: the next pass compared with the body's
// value. `Twice({1, 2, 3}, 3)` returned 2 instead of 12 with the Round 33 compiler. A variable named in a loop is now live
// for the whole loop ([stmt.iter]); a label (goto) makes every variable live for the whole function.
static void r34_loop_registers(Tally& tally) {
    std::printf("r34_loop_registers\n");
    okayms_test::run_both(kRun, tally, "loop_registers", R"OKMS(
int Twice(const int* p, int n) { int total = 0; for (int i = 0; i < n; i++) { int h = p[i] * 2; total = total + h; } return total; }
int Count(int n) { int k = 0; int i = 0; while (i < n) { int sq = i * i; k = k + sq; i = i + 1; } return k; }
int Down(int n) { int k = 0; do { int t = n * 3; k = k + t; n = n - 1; } while (n > 0); return k; }
int Nested(int a, int b) { int s = 0; for (int i = 0; i < a; i++) { for (int j = 0; j < b; j++) { int x = i * 10 + j; s = s + x; } int y = i; s = s + y; } return s; }
int Jumps(int n) { int k = 0; int i = 0; again: { int z = i + 100; k = k + z; } i = i + 1; if (i < n) goto again; return k; }
int entry(void) {
  int v[3] = {1, 2, 3};
  CK(1, Twice(v, 3) == 12);
  CK(2, Count(4) == 14);
  CK(3, Down(3) == 18);
  CK(4, Nested(3, 2) == 0 + 1 + 10 + 11 + 20 + 21 + 0 + 1 + 2);
  CK(5, Jumps(3) == 303);
  return 0;
}
)OKMS", 0);
}

// Names of the engine's GL and RHI files (Round 34): two functions each with a local `struct Cand` (ShadowSetup.hpp and
// Mesh.hpp; [class.local]: two classes; before, the lambda of the second named the first's, "unknown field key", and
// std::vector<Cand> was one instance for both), `table::TextureSize(...)` called from a member function of that name
// (GLDevice.cpp; [namespace.qual]; before "no member function TextureSize with 4 arguments"), and a function of a
// namespace defined with its qualified name (`int gl_render::Cat() {...}`, GLRendererResources.cpp:118;
// [namespace.memdef] 2; before "expected ';'").
static void r34_names(Tally& tally) {
    std::printf("r34_names\n");
    okayms_test::run_both(kRun, tally, "names", std::string(kAlloc) + R"OKMS(
#include <algorithm>
#include <utility>
#include <vector>
inline int g() {
  struct Cand { double a; double b; int t1; };
  std::vector<Cand> v;
  v.push_back({1.0, 2.0, 4});
  v.push_back({0.5, 2.0, 9});
  std::sort(v.begin(), v.end(), [](const Cand& x, const Cand& y) { return x.a < y.a; });
  return v[0].t1;
}
int f() {
  struct Cand { int index; float key; };
  std::vector<Cand> cands;
  for (int i = 0; i < 4; ++i) cands.push_back({i, (float)(10 - i * 3 % 7)});
  std::stable_sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.key < b.key; });
  int r = 0;
  for (const Cand& c : cands) r = r * 10 + c.index;
  return r;
}
namespace rhi { namespace table {
template <class Map> bool TextureSize(const Map& m, int id, int& w, int& h) { w = m.w + id; h = m.h; return true; }
} }
namespace rhi {
struct M { int w; int h; };
struct Base { virtual bool TextureSize(int id, int& w, int& h) const = 0; };
struct Dev : Base {
  M m_textures{3, 4};
  bool TextureSize(int id, int& w, int& hh) const override { return table::TextureSize(m_textures, id, w, hh); }
};
}
namespace gl_render { int Cat(); int Two(int a); }
int gl_render::Cat() { static const int id = 7; return id; }
int gl_render::Two(int a) { return a * 2 + Cat(); }
namespace okay { namespace r { int Three(); } }
int okay::r::Three() { return 3; }
int entry(void) {
  CK(1, g() == 9);
  CK(2, f() == 2130);
  rhi::Dev d;
  int w = 0, h = 0;
  CK(3, d.TextureSize(10, w, h) && w == 13 && h == 4);
  CK(4, gl_render::Cat() == 7 && gl_render::Two(5) == 17 && okay::r::Three() == 3);
  return 0;
}
)OKMS", 0);
}

// Our own <GL/gl.h> (OpenGL 1.1 types, enumerants and the opengl32.dll functions, from the OpenGL 1.1 specification):
// a file that uses it compiles, the functions are imports (__imp_ names), and the enumerants have the specification's
// values. Compile only (no GL context under Wine here).
static void r34_gl_header(Tally& tally) {
    std::printf("r34_gl_header\n");
    okayms::CompileInput in;
    in.source = R"OKMS(
#include <GL/gl.h>
int check(void) {
  if (GL_TEXTURE_2D != 0x0DE1 || GL_RGBA != 0x1908 || GL_UNSIGNED_BYTE != 0x1401 || GL_COLOR_BUFFER_BIT != 0x4000) return 1;
  if (sizeof(GLenum) != 4 || sizeof(GLboolean) != 1 || sizeof(GLclampd) != 8) return 2;
  return 0;
}
void frame(GLuint tex) {
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glBindTexture(GL_TEXTURE_2D, tex);
  const GLubyte* v = glGetString(GL_VERSION);
  (void)v;
}
)OKMS";
    in.include_paths.push_back("stdinc");
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  gl.h: %s\n", compiled.error.c_str());
        return;
    }
    CHECK(tally, okayms_test::has_symbol(compiled.value.object, "__imp_glClear"));
    CHECK(tally, okayms_test::has_symbol(compiled.value.object, "__imp_glGetString"));
}

// Our own <list> ([list]; ThumbnailCache.cpp's LRU list with splice, SceneMerge.cpp's list with insert, erase and
// std::next; before "cannot open include list"): a doubly linked list whose iterators stay valid while other elements
// are added, moved (splice) or erased.
static void r34_list(Tally& tally) {
    std::printf("r34_list\n");
    okayms_test::run_both(kRun, tally, "list", std::string(kAlloc) + R"OKMS(
#include <list>
#include <string>
#include <vector>
#include <map>
#include <iterator>
struct Entry { int key; std::string name; };
int entry(void) {
  std::list<Entry> lru;
  std::map<int, std::list<Entry>::iterator> index;
  for (int i = 0; i < 4; ++i) { lru.push_front(Entry{i, std::string(1, (char)('a' + i))}); index[i] = lru.begin(); }
  CK(1, lru.size() == 4 && lru.front().key == 3 && lru.back().key == 0);
  lru.splice(lru.begin(), lru, index[1]);
  CK(2, lru.front().key == 1 && index[1]->name == "b" && lru.size() == 4);
  lru.pop_back();
  CK(3, lru.back().key == 2 && lru.size() == 3);
  int sum = 0;
  for (auto it = lru.begin(); it != lru.end();) { if (it->key == 3) it = lru.erase(it); else { sum = sum * 10 + it->key; ++it; } }
  CK(4, sum == 12 && lru.size() == 2);
  std::vector<std::string> bs = {"x", "y", "z"};
  std::list<std::string> r(bs.begin(), bs.end());
  auto it = std::next(r.begin());
  auto ni = r.insert(it, "q");
  CK(5, *ni == "q" && r.size() == 4);
  std::string all;
  for (const std::string& s : r) all += s;
  CK(6, all == "xqyz");
  r.erase(ni);
  CK(7, r.size() == 3 && *std::next(r.begin()) == "y");
  std::list<int> nums = {5, 6, 5, 7};
  nums.remove(5);
  CK(8, nums.size() == 2 && nums.front() == 6);
  lru.clear();
  CK(9, lru.empty());
  return 0;
}
)OKMS", 0);
}

// Our own <winsock2.h> and <ws2tcpip.h> (Net/UdpSocket.cpp and Net/ServerQuery.cpp; before "cannot open include
// winsock2.h"): the documented record sizes (MinGW's headers agree), byte order, fd_set, and a UDP datagram sent to
// itself over the loopback address through ws2_32.dll under Wine.
static void r34_winsock(Tally& tally) {
    std::printf("r34_winsock\n");
    const okayms_test::ProgramRun run{"r34", {"ws2_32.dll:WSAStartup", "ws2_32.dll:WSACleanup", "ws2_32.dll:socket",
                                              "ws2_32.dll:bind", "ws2_32.dll:getsockname", "ws2_32.dll:sendto",
                                              "ws2_32.dll:recvfrom", "ws2_32.dll:closesocket", "ws2_32.dll:htons",
                                              "ws2_32.dll:htonl", "ws2_32.dll:ntohs", "ws2_32.dll:ntohl",
                                              "ws2_32.dll:select", "ws2_32.dll:ioctlsocket"}};
    okayms_test::run_both(run, tally, "winsock", R"OKMS(
#include <winsock2.h>
#include <ws2tcpip.h>
int entry(void) {
  CK(1, sizeof(sockaddr_in) == 16 && sizeof(in_addr) == 4 && sizeof(WSADATA) == 408 && sizeof(fd_set) == 520);
  CK(2, sizeof(addrinfo) == 48 && sizeof(timeval) == 8 && sizeof(SOCKET) == 8);
  CK(3, htons(0x1234) == 0x3412 && htonl(0x01020304u) == 0x04030201u && ntohs(htons(77)) == 77);
  WSADATA d;
  CK(4, WSAStartup(MAKEWORD(2, 2), &d) == 0);
  SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  CK(5, s != INVALID_SOCKET);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  CK(6, bind(s, (sockaddr*)&addr, sizeof(addr)) == 0);
  int len = sizeof(addr);
  CK(7, getsockname(s, (sockaddr*)&addr, &len) == 0 && ntohs(addr.sin_port) != 0);
  const char msg[4] = {'o', 'k', 'm', 's'};
  CK(8, sendto(s, msg, 4, 0, (sockaddr*)&addr, sizeof(addr)) == 4);
  fd_set set;
  FD_ZERO(&set);
  FD_SET(s, &set);
  FD_SET(s, &set);
  CK(9, set.fd_count == 1);
  timeval tv{};
  tv.tv_sec = 2;
  CK(10, select(0, &set, nullptr, nullptr, &tv) == 1);
  char got[8] = {};
  sockaddr_in from{};
  int from_len = sizeof(from);
  CK(11, recvfrom(s, got, 8, 0, (sockaddr*)&from, &from_len) == 4 && got[3] == 's');
  CK(12, ntohl(from.sin_addr.s_addr) == INADDR_LOOPBACK);
  u_long on = 1;
  CK(13, ioctlsocket(s, FIONBIO, &on) == 0);
  closesocket(s);
  WSACleanup();
  return 0;
}
)OKMS", 0);
}

// Templates behind PropertyBuilder.hpp, EngineApi_*.cpp and <limits> (Round 34). A specialization's own type matches only
// that type ([temp.class.spec.match] 2): numeric_limits<unsigned>::max() was numeric_limits<int>'s (2147483647, silent
// wrong value with the Round 33 compiler) and N<unsigned char> was N<char>; numeric_limits for char, unsigned char,
// short, unsigned short and bool (std::uint8_t fields); a member template returning the class template's injected name
// (`B& Add(...)`, [temp.local] 1; before "expected a type"); a static member template called with explicit arguments
// from a lambda (`As<Owner>(c)`, [class.static] 2; before "unknown name As"); a braced list for a parameter that is not
// deduced ([temp.deduct.call] 1; before "cannot deduce template arguments"); a class with `operator bool` making the
// bool alternative of a variant and counting as convertible ([class.conv.fct], [meta.rel]; before "typename names no
// member type").
static void r34_templates(Tally& tally) {
    std::printf("r34_templates\n");
    okayms_test::run_both(kRun, tally, "templates", std::string(kAlloc) + R"OKMS(
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>
template <class T> struct N;
template <> struct N<char> { static int m() { return 1; } };
template <> struct N<unsigned char> { static int m() { return 3; } };
template <> struct N<short> { static int m() { return 4; } };
template <> struct N<unsigned short> { static int m() { return 5; } };
template <> struct N<int> { static int m() { return 7; } };
template <> struct N<unsigned> { static int m() { return 8; } };
template <> struct N<long long> { static int m() { return 9; } };
struct Component { int kind = 0; };
struct GameObject : Component { int id = 1; float speed = 2.5f; std::string name = "go"; };
template <class C> class Builder {
public:
  int n = 0;
  template <class Owner> Builder& AddFloat(const char* label, float Owner::*m) {
    (void)label;
    return Prop([m](const Component& c) { return (int)(As<Owner>(c).*m * 2); });
  }
  template <class Owner> Builder& AddEnum(const char* label, int Owner::*m, std::vector<std::string> options) {
    (void)label;
    n += (int)options.size() * 1000;
    return Prop([m](const Component& c) { return As<Owner>(c).*m; });
  }
  Builder& Tooltip(const char*) { n += 100; return *this; }
private:
  template <class T> static const T& As(const Component& c) { return static_cast<const T&>(c); }
  Builder& Prop(std::function<int(const Component&)> get) { GameObject g; n += get(g); return *this; }
};
class Flag { public: explicit Flag(bool on = true) : m_on(on) {} operator bool() const { return m_on; } private: bool m_on; };
using Value = std::variant<std::monostate, bool, std::int32_t, float, std::string>;
int entry(void) {
  CK(1, N<char>::m() == 1 && N<unsigned char>::m() == 3 && N<short>::m() == 4 && N<unsigned short>::m() == 5);
  CK(2, N<int>::m() == 7 && N<unsigned>::m() == 8 && N<long long>::m() == 9);
  CK(3, std::numeric_limits<unsigned>::max() == 4294967295u && std::numeric_limits<std::uint8_t>::max() == 255);
  CK(4, std::numeric_limits<std::uint16_t>::max() == 65535 && std::numeric_limits<short>::lowest() == -32768);
  CK(5, std::numeric_limits<char>::max() == 127 && std::numeric_limits<bool>::max());
  Builder<GameObject> b;
  b.AddFloat("speed", &GameObject::speed).Tooltip("x");
  CK(6, b.n == 105);
  b.AddEnum("id", &GameObject::id, {"A", "B", "C"});
  CK(7, b.n == 105 + 3000 + 1);
  CK(8, (std::is_convertible<Flag, bool>::value) && std::is_class<Flag>::value);
  Flag on(true);
  Value v{on};
  CK(9, v.index() == 1 && std::get<bool>(v));
  Value k{7};
  CK(10, k.index() == 2);
  return 0;
}
)OKMS", 0);
}

// Member names that hide namespace functions ([basic.lookup.unqual], [class.member.lookup]): a static member function
// called from a lambda in a member function (UITheme.hpp's `Contrast(fg, panel)` beside okay::Contrast(); before "wrong
// number of arguments to okay::Contrast"), and a static member function template called from another one (Log.hpp's
// Error calling Emit beside a namespace okay::Emit; before "wrong number of arguments to okay::Emit").
static void r34_hiding(Tally& tally) {
    std::printf("r34_hiding\n");
    okayms_test::run_both(kRun, tally, "hiding", std::string(kAlloc) + R"OKMS(
#include <string>
namespace okay {
inline float& Contrast() { static float v = 1.0f; return v; }
inline int Emit(const std::string& body, int k) { return (int)body.size() + k; }
struct Theme {
  float a = 2, b = 5;
  static float Contrast(float x, float y) { return x > y ? x / y : y / x; }
  float Worst() { auto w = [&](float f) { return Contrast(f, a) + Contrast(f, b); }; return w(10.0f); }
};
struct Log {
  static int last;
  template <typename... Args> static void Error(Args&&... args) { Emit(3, "ERROR", nullptr, args...); }
  template <typename... Args> static void Emit(int level, const char* tag, const char* cat, Args&&... args) {
    (void)tag; (void)cat; last = level * 10 + (int)sizeof...(args);
  }
};
int Log::last = 0;
}
int entry(void) {
  okay::Theme t;
  CK(1, t.Worst() == 7.0f);
  okay::Log::Error(std::string("x"), 2);
  CK(2, okay::Log::last == 32);
  CK(3, okay::Contrast() == 1.0f && okay::Emit("ab", 1) == 3);
  return 0;
}
)OKMS", 0);
}

// An enumerator named through a class's alias of a scoped enumeration ([class.qual], [dcl.typedef] 2): `Builder::K::Bool`
// and `B::K::Int` with `using K = ParamDef::Kind;` in Builder (GuidedArchitecture.cpp:514 and three more Modeling
// files; before "expected '('").
static void r34_enum_alias(Tally& tally) {
    std::printf("r34_enum_alias\n");
    okayms_test::run_both(kRun, tally, "enum_alias", R"OKMS(
namespace g { struct ParamDef { enum class Kind { Float, Int, Bool, Choice }; Kind kind = Kind::Float; }; }
using namespace g;
struct Builder {
  using K = ParamDef::Kind;
  int last = -1;
  void P(const char* id, K k, double d) { (void)id; (void)d; last = (int)k; }
  void Q() { P("q", K::Choice, 0); }
};
int entry(void) {
  Builder b;
  b.P("w", Builder::K::Bool, 1.0);
  CK(1, b.last == 2);
  using B = Builder;
  b.P("s", B::K::Int, 2.0);
  CK(2, b.last == 1);
  b.Q();
  CK(3, b.last == 3);
  return 0;
}
)OKMS", 0);
}

// Conversions (Round 34): a class with `operator int` given for an int parameter (silent wrong value before: the object's
// bytes were passed; [over.ics.user]); an `explicit operator bool` is not used for an implicit conversion
// ([class.conv.fct] 2: shared_ptr's made `be = std::make_shared<Q>()` pick operator=(int), "large struct return has no
// destination", Matchmaker.cpp:254) but is for a condition and a cast.
static void r34_conversions(Tally& tally) {
    std::printf("r34_conversions\n");
    okayms_test::run_both(kRun, tally, "conversions", std::string(kAlloc) + R"OKMS(
#include <memory>
struct H { int v; explicit operator bool() const { return v != 0; } operator int() const { return v * 2; } };
struct G { int v; operator int() const { return v * 2; } };
int take(int k) { return k + 1; }
static int conv_a(void) {
  G g{3};
  CK(1, take(g) == 7);
  H h{3};
  int k = h;
  CK(2, k == 6);
  CK(3, take(h) == 7);
  return 0;
}
struct H2 { int v; explicit operator bool() const { return v != 0; } operator int() const { return v * 2; } };
int take2(int k) { return k + 1; }
static int conv_b(void) {
  H2 h{3};
  CK(1, take2(h) == 7);
  CK(2, (bool)h && static_cast<bool>(h));
  H2 z{0};
  if (z) return 3;
  CK(4, !z);
  return 0;
}
struct Backend { virtual ~Backend() {} virtual int Id() const { return 1; } };
struct Queue : Backend { int Id() const override { return 7; } };
static int conv_c(void) {
  std::shared_ptr<Backend> be;
  if (!be) be = std::make_shared<Queue>();
  CK(1, be && be->Id() == 7);
  std::shared_ptr<Backend> b2 = std::make_shared<Queue>();
  CK(2, b2->Id() == 7 && be.use_count() == 1);
  return 0;
}
int entry(void) {
  int r = conv_a();
  if (r) return r;
  r = conv_b();
  if (r) return 10 + r;
  r = conv_c();
  if (r) return 20 + r;
  return 0;
}
)OKMS", 0);
}

// `new Agg{...}` for an aggregate with class members (Matchmaker.cpp's `new RunArgs{...}`; [expr.new] 22; before "no
// matching constructor in RunArgs"), and `Clock::time_point{}`, a member typedef of the named class made from braces
// (Matchmaker.cpp:171; before "expected '('").
static void r34_new_aggregate(Tally& tally) {
    std::printf("r34_new_aggregate\n");
    const okayms_test::ProgramRun run{"r34", {"QueryPerformanceCounter", "QueryPerformanceFrequency"}};
    okayms_test::run_both(run, tally, "new_aggregate", std::string(kAlloc) + R"OKMS(
#include <chrono>
#include <memory>
#include <string>
struct Req { std::string game; int n = 1; };
struct RunArgs { std::shared_ptr<int> impl; unsigned gen; Req req; float poll, wait; };
static int agg_part(void) {
  auto sp = std::make_shared<int>(5);
  Req r; r.game = "g"; r.n = 3;
  RunArgs local{sp, 7u, r, 0.5f, 2.0f};
  CK(1, *local.impl == 5 && local.gen == 7 && local.req.game == "g" && local.wait == 2.0f);
  RunArgs* a = new RunArgs{sp, 8u, r, 0.25f, 3.0f};
  CK(2, *a->impl == 5 && a->gen == 8 && a->req.n == 3 && a->poll == 0.25f && a->wait == 3.0f);
  CK(3, sp.use_count() == 3);
  delete a;
  CK(4, sp.use_count() == 2);
  return 0;
}
using Clock = std::chrono::steady_clock;
static int tp_part(void) {
  Clock::time_point matchedAt{};
  int n = 0;
  if (matchedAt == Clock::time_point{}) { matchedAt = Clock::now(); n = 1; }
  if (matchedAt != Clock::time_point{}) n += 10;
  return n == 11 ? 0 : 1;
}
int entry(void) {
  int r = agg_part();
  if (r) return r;
  return tp_part() ? 10 : 0;
}
)OKMS", 0);
}

// Silent wrong code (Round 33 and before): a decimal floating literal with more than 19 digits wrapped the 64-bit
// numbers its digits were gathered in, so 0.91893853320467274178 was about 0.73 and 3.14159265358979323846 was wrong
// ([lex.fcon]); now up to 38 significant digits are kept and rounded once (IEEE 754 round to nearest even).
static void r34_long_literals(Tally& tally) {
    std::printf("r34_long_literals\n");
    okayms_test::run_both(kRun, tally, "long_literals", R"OKMS(
static bool near(double a, double b, double t) { double d = a - b; if (d < 0) d = -d; return d < t; }
int entry(void) {
  CK(1, near(0.9189385332046727, 0.9189385332, 1e-9));
  CK(2, near(0.91893853320467274, 0.9189385332, 1e-9));
  CK(3, near(0.918938533204672741, 0.9189385332, 1e-9));
  CK(4, near(0.9189385332046727417, 0.9189385332, 1e-9));
  CK(5, near(0.91893853320467274178, 0.9189385332, 1e-9));
  CK(6, near(3.14159265358979323846, 3.1415926535, 1e-9));
  CK(7, near(123456789012345678901234.0, 1.2345678901234568e23, 1e9));
  return 0;
}
)OKMS", 0);
}

// Library pieces the next engine files use (Round 34): std::rint, nearbyint, lrint, llrint (ISO/IEC 9899 7.12.9; ties to
// even), std::lgamma (7.12.8.3; Stirling's series with the Bernoulli numbers, the recurrence and the reflection
// formula), the range insert of set and unordered_set, string::shrink_to_fit, and std::quoted ([quoted.manip]).
static void r34_library(Tally& tally) {
    std::printf("r34_library\n");
    okayms_test::run_both(kRun, tally, "library", std::string(kAlloc) + R"OKMS(
#include <cmath>
#include <set>
#include <unordered_set>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
static bool near(double a, double b) { double d = a - b; if (d < 0) d = -d; return d < 1e-9 * (1 + (b < 0 ? -b : b)); }
int entry(void) {
  CK(1, std::lrint(2.5) == 2 && std::lrint(3.5) == 4 && std::lrint(-2.5) == -2 && std::lrint(2.4f) == 2 && std::lrint(0.6f) == 1);
  CK(2, std::llrint(-3.7) == -4 && std::rint(1.5) == 2.0 && std::nearbyint(0.5) == 0.0);
  CK(3, near(std::lgamma(1.0), 0.0) && near(std::lgamma(2.0), 0.0) && near(std::lgamma(5.0), 3.17805383034794561964));
  CK(4, near(std::lgamma(0.5), 0.57236494292470008707) && near(std::lgamma(-0.5), 1.26551212348464539649));
  CK(5, near(std::lgamma(100.0), 359.13420536957539878));
  std::vector<int> v = {3, 1, 3, 2};
  std::set<int> s;
  s.insert(v.begin(), v.end());
  std::unordered_set<int> u;
  u.insert(v.begin(), v.end());
  CK(6, s.size() == 3 && u.size() == 3 && u.count(2) == 1);
  std::string t = "abc";
  t.reserve(100);
  t.shrink_to_fit();
  CK(7, t == "abc");
  std::ostringstream os;
  os << std::quoted(std::string("a \"b\" c")) << ' ' << std::quoted(std::string("-"));
  CK(8, os.str() == "\"a \\\"b\\\" c\" \"-\"");
  std::istringstream is(os.str() + " plain");
  std::string r1, r2, r3;
  is >> std::quoted(r1) >> std::quoted(r2) >> std::quoted(r3);
  CK(9, r1 == "a \"b\" c" && r2 == "-" && r3 == "plain");
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
    r34_lookup(tally);
    r34_narrow_spill(tally);
    r34_loop_registers(tally);
    r34_names(tally);
    r34_gl_header(tally);
    r34_list(tally);
    r34_winsock(tally);
    r34_templates(tally);
    r34_hiding(tally);
    r34_enum_alias(tally);
    r34_conversions(tally);
    r34_new_aggregate(tally);
    r34_long_literals(tally);
    r34_library(tally);
    return tally.finish(18);
}
