// Round 30: the next blockers of the Windows and plain surveys. Each program returns 0 when all of its checks hold, or
// the number of the first check that failed; it is built by okay-cl at /O0, /O1 and /O2 and run under Wine, and MinGW
// g++ builds the same text with its own headers and must agree (program_check.hpp). One unit test writes and reads a
// COFF object with more than 65535 relocations in one section. Written from ISO/IEC 14882 ([stmt.ranged],
// [basic.lookup.unqual], [class.member.lookup], [dcl.fct.default], [temp.deduct], [dcl.enum], [class.nest],
// [dcl.mptr], [expr.unary.op], [expr.mptr.oper], [conv.mem], [expr.static.cast], [expr.prim.paren], [basic.scope.block],
// [fs.filesystem.syn], [time.point]), POSIX.1-2017 and Microsoft's "PE Format" (Section Flags,
// IMAGE_SCN_LNK_NRELOC_OVFL; COFF Relocations). No code from other compilers or libraries.

#include "program_check.hpp"

#include <algorithm>
#include <string>

namespace {
using okayms_test::Tally;
const okayms_test::ProgramRun kRun{"r30", {}};
const okayms_test::ProgramRun kRunClock{"r30", {"GetSystemTimeAsFileTime"}};
}  // namespace

static const char* kAlloc = okayms_test::bump_alloc();

// A range for over a class with no member begin or end: the free functions begin(r) and end(r) of its namespace
// ([stmt.ranged] 1.3; std::filesystem::directory_iterator in UIStyleLib.hpp:409).
static void r30_free_begin_end(Tally& tally) {
    std::printf("r30_free_begin_end\n");
    okayms_test::run_both(kRun, tally, "free_begin_end", std::string(kAlloc) + R"OKMS(
namespace lst {
struct Walk {
  int at;
  int stop;
  int operator*() const { return at * 10; }
  Walk& operator++() { ++at; return *this; }
  bool operator!=(const Walk& o) const { return at != o.at; }
};
inline Walk begin(Walk w) { return w; }
inline Walk end(const Walk& w) { Walk e; e.at = w.stop; e.stop = w.stop; return e; }
}
lst::Walk make(int a, int b) { lst::Walk w; w.at = a; w.stop = b; return w; }
int entry(void) {
  int sum = 0;
  int n = 0;
  for (const auto& v : make(2, 5)) { sum += v; ++n; }
  CK(1, sum == 90 && n == 3);
  lst::Walk w = make(0, 4);
  sum = 0;
  for (int v : w) sum += v;
  CK(2, sum == 60);
  return 0;
}
)OKMS", 0);
}

// A member function of the class hides a namespace function of the same name in an unqualified call, also when the
// member is declared further down the class ([basic.lookup.unqual], [class.member.lookup]; ScriptUI.hpp:32's
// Text(s), Platformer25DController.hpp:288's TryStepUp beside StepUp.hpp's free TryStepUp). Before Round 30 the call
// went to the free function: "too few arguments", or the wrong function when the counts matched.
static void r30_member_hides_free(Tally& tally) {
    std::printf("r30_member_hides_free\n");
    okayms_test::run_both(kRun, tally, "member_hides_free", std::string(kAlloc) + R"OKMS(
namespace ui {
inline int Text(const char* s, int a, int b) { return s[0] + a + b; }
inline int Step(int a, int b) { return a - b; }
struct Host {
  int last = 0;
  int Heading(const char* s) { return Text(s); }
  int Text(const char* s) { last = s[0]; return 1000 + s[0]; }
  int Go(int x) { return Step(x, 2); }
  int Step(int a, int b) { return a * b; }
  int Free(int x) { return ui::Step(x, 2); }
};
}
int entry(void) {
  ui::Host h;
  CK(1, h.Heading("A") == 1065 && h.last == 65);
  CK(2, h.Go(7) == 14);
  CK(3, h.Free(7) == 5 && ui::Text("A", 1, 2) == 68);
  return 0;
}
)OKMS", 0);
}

// Default arguments of function templates ([dcl.fct.default]; ControllerFeel.hpp's FindInTree<T>(go, depth = 0)) and a
// braced default argument, which ended the template's declaration at its '{' (DynamicTree.hpp:83's
// `const V3& grow = V3{}`).
static void r30_template_defaults(Tally& tally) {
    std::printf("r30_template_defaults\n");
    okayms_test::run_both(kRun, tally, "template_defaults", std::string(kAlloc) + R"OKMS(
struct V3 { float x, y, z; };
struct Node { int v; Node* child; };
template <class T> T* deepest(T* n, int depth = 0) {
  if (depth > 10) return nullptr;
  return n->child ? deepest<T>(n->child, depth + 1) : n;
}
template <class T> int depth_of(T* n, int depth = 0) { return n->child ? depth_of(n->child, depth + 1) : depth; }
template <class F> float ray(const V3& o, F&& cb, const V3& grow = V3{}) { return cb(o.x + grow.x + grow.y); }
template <class F> float ray2(F cb, V3 grow = V3{1.0f, 2.0f, 3.0f}, int k = 2) { return cb(grow.z * k); }
struct Tree {
  int base = 4;
  template <class F> float cast(const V3& o, F&& cb, const V3& grow = V3{}) const { return cb(o.y + grow.z) + base; }
};
int entry(void) {
  Node c{3, nullptr}, b{2, &c}, a{1, &b};
  CK(1, deepest<Node>(&a)->v == 3 && deepest(&a, 0)->v == 3 && deepest(&a, 11) == nullptr);
  CK(2, depth_of(&a) == 2 && depth_of(&b, 5) == 6);
  float seen = 0;
  CK(3, ray(V3{1, 2, 3}, [&](float v) { seen = v; return v * 2; }) == 2.0f && seen == 1.0f);
  CK(4, ray(V3{1, 2, 3}, [](float v) { return v; }, V3{1, 1, 0}) == 3.0f);
  CK(5, ray2([](float v) { return v + 1; }) == 7.0f && ray2([](float v) { return v; }, V3{0, 0, 1}) == 2.0f &&
            ray2([](float v) { return v; }, V3{0, 0, 1}, 5) == 5.0f);
  Tree t;
  CK(6, t.cast(V3{0, 3, 0}, [](float v) { return v; }) == 7.0f && t.cast(V3{0, 3, 0}, [](float v) { return v; }, V3{0, 0, 2}) == 9.0f);
  return 0;
}
)OKMS", 0);
}

// An enumeration declared in a class is a member type of the class ([dcl.enum], [class.nest]): HealthWatch::Event
// finds it as the return type of a member defined outside the class, and Event in that member's body too, though a
// class Event exists in the namespace (RagdollShared.cpp:119 with okay::Event of Platform.hpp).
static void r30_class_enum(Tally& tally) {
    std::printf("r30_class_enum\n");
    okayms_test::run_both(kRun, tally, "class_enum", std::string(kAlloc) + R"OKMS(
namespace okay {
struct Event { int type; int extra; };
struct HealthWatch {
  enum Event { None, Died, Revived, Hurt };
  bool wasDead = false;
  Event Poll(int hp, float* damage = nullptr);
};
HealthWatch::Event HealthWatch::Poll(int hp, float* damage) {
  Event e = None;
  if (hp <= 0 && !wasDead) e = Died;
  else if (hp > 0 && wasDead) e = Revived;
  else if (damage && *damage > 0) e = Hurt;
  wasDead = hp <= 0;
  return e;
}
}
int entry(void) {
  okay::HealthWatch w;
  float d = 2;
  CK(1, w.Poll(5) == okay::HealthWatch::None && w.Poll(5, &d) == okay::HealthWatch::Hurt);
  CK(2, w.Poll(0) == okay::HealthWatch::Died && w.Poll(3) == okay::HealthWatch::Revived);
  okay::Event ev{7, 8};
  CK(3, ev.type + ev.extra == 15 && sizeof(okay::Event) == 8 && sizeof(okay::HealthWatch::Event) == 4);
  return 0;
}
)OKMS", 0);
}

// Pointers to data members ([dcl.mptr], [expr.unary.op] 4, [expr.mptr.oper], [conv.mem]): `T C::*` as a variable, a
// parameter (also of a lambda), a struct member set to nullptr; &C::m; obj.*pm and p->*pm read and written; a pointer
// to a member of a base converted to the derived class with static_cast (VehicleTuning.hpp:118 and
// BuiltinPropertiesGameplay.cpp, UINavigation.hpp:292's `const std::string UIFocusHint::*link`), and a function
// template that deduces the class and the member type (PropertyBuilder.hpp's `I Owner::*m`).
static void r30_member_pointers(Tally& tally) {
    std::printf("r30_member_pointers\n");
    okayms_test::run_both(kRun, tally, "member_pointers", std::string(kAlloc) + R"OKMS(
#include <string>
#include <vector>
struct Behaviour { virtual ~Behaviour() {} int id = 1; };
struct Tuning {
  float mass = 2.5f;
  int gears = 5;
  bool abs = true;
  char key = 'k';
  enum Kind { KFloat, KInt, KBool, KKey };
  struct Desc {
    const char* name; Kind kind;
    float Tuning::*f; int Tuning::*i; bool Tuning::*b; char Tuning::*k;
  };
  static std::vector<Desc> Table() {
    std::vector<Desc> v;
    auto F = [&](const char* n, float Tuning::*m) { v.push_back({n, KFloat, m, nullptr, nullptr, nullptr}); };
    auto I = [&](const char* n, int Tuning::*m) { v.push_back({n, KInt, nullptr, m, nullptr, nullptr}); };
    auto B = [&](const char* n, bool Tuning::*m) { v.push_back({n, KBool, nullptr, nullptr, m, nullptr}); };
    auto K = [&](const char* n, char Tuning::*m) { v.push_back({n, KKey, nullptr, nullptr, nullptr, m}); };
    F("mass", &Tuning::mass);
    I("gears", &Tuning::gears);
    B("abs", &Tuning::abs);
    K("key", &Tuning::key);
    return v;
  }
  double Get(const Desc& d) const {
    switch (d.kind) {
      case KFloat: return this->*(d.f);
      case KInt: return this->*(d.i);
      case KBool: return (this->*(d.b)) ? 1.0 : 0.0;
      case KKey: return (unsigned char)(this->*(d.k));
    }
    return -1;
  }
  void Set(const Desc* d, double value) {
    if (d->kind == KFloat) this->*(d->f) = (float)value;
    else if (d->kind == KInt) this->*(d->i) = (int)value;
    else if (d->kind == KBool) this->*(d->b) = value != 0.0;
    else this->*(d->k) = (char)(int)value;
  }
};
struct Vehicle : public Behaviour, public Tuning { int speed = 3; };
struct Hint { std::string up, down; int n = 0; };
int pick(Hint* h, const std::string Hint::*link) {
  if (!h || (h->*link).empty()) return -2;
  if ((h->*link) == "-") return -1;
  return (int)(h->*link).size();
}
template <class Owner, class I> int read_int(const Owner& o, I Owner::*m) { return (int)(o.*m) + (int)sizeof(I); }
int entry(void) {
  Tuning t;
  float Tuning::*pm = &Tuning::mass;
  CK(1, t.*pm == 2.5f && pm != nullptr);
  t.*pm = 4.0f;
  int Tuning::*none = nullptr;
  CK(2, t.mass == 4.0f && !none && none == nullptr);
  std::vector<Tuning::Desc> table = Tuning::Table();
  CK(3, table.size() == 4 && table[1].f == nullptr && table[1].i != nullptr && table[0].f != nullptr);
  CK(4, t.Get(table[0]) == 4.0 && t.Get(table[1]) == 5.0 && t.Get(table[2]) == 1.0 && t.Get(table[3]) == 'k');
  t.Set(&table[1], 7);
  t.Set(&table[2], 0);
  t.Set(&table[3], 'z');
  CK(5, t.gears == 7 && !t.abs && t.key == 'z');
  Vehicle v;
  int Vehicle::*vg = static_cast<int Vehicle::*>(table[1].i);
  v.*vg = 9;
  CK(6, v.gears == 9 && v.speed == 3 && v.id == 1);
  Tuning* tp = &v;
  CK(7, tp->*(table[1].i) == 9 && v.*(&Vehicle::speed) == 3);
  Hint h;
  h.up = "abc";
  h.down = "-";
  auto target = [&](const std::string Hint::*link) -> int { return pick(&h, link) * 10; };
  CK(8, target(&Hint::up) == 30 && target(&Hint::down) == -10 && pick(nullptr, &Hint::up) == -2);
  h.up.clear();
  CK(9, pick(&h, &Hint::up) == -2);
  CK(10, read_int(t, &Tuning::gears) == 11 && read_int(v, &Vehicle::speed) == 7 && read_int(t, &Tuning::key) == 123);
  return 0;
}
)OKMS", 0);
}

// (std::max)(a, b): a function name in parentheses (written so a min or max macro is not expanded; OfficialGame.hpp's
// CompareVersions) is the same function ([expr.prim.paren]).
static void r30_paren_name(Tally& tally) {
    std::printf("r30_paren_name\n");
    okayms_test::run_both(kRun, tally, "paren_name", std::string(kAlloc) + R"OKMS(
#include <algorithm>
#include <vector>
int cmp(const std::vector<int>& a, const std::vector<int>& b) {
  for (unsigned long long i = 0; i < (std::max)(a.size(), b.size()); ++i) {
    const int x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0;
    if (x != y) return x < y ? -1 : 1;
  }
  return 0;
}
int entry(void) {
  std::vector<int> a{1, 10}, b{1, 9, 0};
  CK(1, cmp(a, b) == 1 && cmp(b, a) == -1 && cmp(a, a) == 0);
  int lo = (std::min)(4, 2);
  int k = 3;
  int p = (k) * (2);
  CK(2, lo == 2 && p == 6 && (std::max)(lo, k) == 3);
  return 0;
}
)OKMS", 0);
}

// std::filesystem::file_time_type is a std::chrono::time_point of a file clock ([fs.filesystem.syn]): its count,
// clock::now(), clock::duration and duration_cast (UIDataGlue.cpp, FileStamp.hpp, AssetDatabase.cpp,
// ImportSidecar.cpp). okay-ms's clock counts 100 ns steps since 1601, as Windows file times (libstdc++'s has another
// epoch, so the checks do not look at the absolute value).
static void r30_file_time(Tally& tally) {
    std::printf("r30_file_time\n");
    okayms_test::run_both(kRunClock, tally, "file_time", std::string(kAlloc) + R"OKMS(
#include <chrono>
#include <filesystem>
namespace fs = std::filesystem;
long long ticks(fs::file_time_type t) { return (long long)t.time_since_epoch().count(); }
int entry(void) {
  using clock = fs::file_time_type::clock;
  const fs::file_time_type now = fs::file_time_type::clock::now();
  CK(1, ticks(now) != 0);
  const long long settle = (long long)std::chrono::duration_cast<clock::duration>(std::chrono::seconds(2)).count();
  CK(2, settle == 2 * (long long)clock::duration::period::den / (long long)clock::duration::period::num);
  const fs::file_time_type later = now + std::chrono::duration_cast<clock::duration>(std::chrono::seconds(3));
  const auto d = std::chrono::duration_cast<std::chrono::seconds>(later - now);
  CK(3, d.count() == 3 && later > now && !(later == now));
  const long long a = (long long)clock::now().time_since_epoch().count();
  CK(4, a >= ticks(now));
  return 0;
}
)OKMS", 0);
}

// A name used in a block before an inner declaration of the same name in that block is the outer one
// ([basic.scope.block], [basic.scope.pdecl]): `int d = 1; { int x = d; int d = 5; ... }` reads the outer d into x.
// Before Round 30 the inner d (still unset) was read: silent wrong code.
static void r30_local_before_shadow(Tally& tally) {
    std::printf("r30_local_before_shadow\n");
    okayms_test::run_both(kRun, tally, "local_before_shadow", std::string(kAlloc) + R"OKMS(
int use(int v) { return v + 1; }
// Sibling loops with their own i in a constructor (HmacSha256's constructor in Sha256.hpp): the constructor's body had
// scope numbers one apart from the code generator's, so the later rule picked the wrong i (found by the engine crypto
// link test while this round's rule was written).
struct Pads {
  unsigned char a[8], b[8];
  Pads(unsigned long long n) {
    unsigned char k[8] = {0};
    if (n > 8) k[0] = 9;
    else for (unsigned long long i = 0; i < n; ++i) k[i] = (unsigned char)(i + 1);
    unsigned char pad[8];
    for (int i = 0; i < 8; ++i) pad[i] = k[i] ^ 0x36;
    for (int i = 0; i < 8; ++i) a[i] = pad[i];
    for (int i = 0; i < 8; ++i) pad[i] = k[i] ^ 0x5c;
    for (int i = 0; i < 8; ++i) b[i] = pad[i];
  }
};
int entry(void) {
  int d = 1;
  int x = 0;
  {
    x = use(d);
    int d = 50;
    x += d;
  }
  CK(1, x == 52 && d == 1);
  int total = 0;
  for (int i = 0; i < 3; ++i) {
    total += d;
    int d = 100;
    total += d;
  }
  CK(2, total == 303);
  int k = 7;
  {
    int y = k * 2;
    int k = y + 1;
    CK(3, k == 15 && y == 14);
  }
  CK(4, k == 7);
  Pads p(3);
  CK(5, p.a[0] == (1 ^ 0x36) && p.a[3] == 0x36 && p.b[2] == (3 ^ 0x5c) && p.b[7] == 0x5c);
  return 0;
}
)OKMS", 0);
}

// A call through a reference to a function pointer (`Fn& f`, or `F&& f` deduced for a lambda without captures, which
// is a function pointer here): before Round 30 the call went to the address of the pointer instead of the function it
// holds (a crash; DynamicTree.hpp's RayCast(o, d, maxT, F&& cb)). Its own test: silent wrong code.
static void r30_fp_reference(Tally& tally) {
    std::printf("r30_fp_reference\n");
    okayms_test::run_both(kRun, tally, "fp_reference", std::string(kAlloc) + R"OKMS(
typedef float (*Fn)(float);
float twice(float v) { return v * 2; }
float callref(Fn& f, float x) { return f(x); }
template <class F> float callt(F&& f, float x) { return f(x); }
template <class F> float callv(F f, float x) { return f(x); }
int entry(void) {
  Fn p = twice;
  CK(1, callref(p, 3.0f) == 6.0f);
  CK(2, callt(p, 4.0f) == 8.0f && callv(p, 1.0f) == 2.0f);
  CK(3, callt([](float v) { return v + 1; }, 4.0f) == 5.0f);
  Fn& r = p;
  CK(4, r(5.0f) == 10.0f);
  return 0;
}
)OKMS", 0);
}

// Two lambdas of other signatures given to one function template: each needs its own instance. Before Round 30 a
// lambda without captures (a function pointer here) was keyed by the type code PEAH whatever its signature, so the
// second call reused the first instance and called its lambda with the wrong argument type (silent wrong code).
static void r30_lambda_instances(Tally& tally) {
    std::printf("r30_lambda_instances\n");
    okayms_test::run_both(kRun, tally, "lambda_instances", std::string(kAlloc) + R"OKMS(
template <class F> double apply(F f, int x) { return f(x); }
int entry(void) {
  double a = apply([](int v) { return v * 3; }, 2);
  double b = apply([](double v) { return v / 4; }, 2);
  long long c = (long long)apply([](long long v) { return v << 40; }, 1);
  CK(1, a == 6.0);
  CK(2, b == 0.5);
  CK(3, c == (1LL << 40));
  return 0;
}
)OKMS", 0);
}

// Smaller pieces found on the way: "255.f" and "2." are floating literals ([lex.fcon]; "255.f" was read as 255 . f);
// `c ? "text" : std::string(...)` is a std::string ([expr.cond]); a member function template with a braced default
// argument of a class-template type (MatchResults.hpp's `const std::vector<std::uint32_t>& winners = {}`); an
// enumeration of one class no longer hides a nested class of the same name in another class (ModelAnimator's
// `enum Slot` against Inventory::Slot{}).
static void r30_pieces(Tally& tally) {
    std::printf("r30_pieces\n");
    okayms_test::run_both(kRun, tally, "pieces", std::string(kAlloc) + R"OKMS(
#include <string>
#include <vector>
#include <cstdint>
float clampf(float v) { return v < 0.f ? 0.f : (v > 255.f ? 255.f : v); }
int pick_len(bool kb, bool any, int c) {
  std::string j = kb ? "keyboard" : any ? "any" : "pad" + std::to_string(c);
  return (int)j.size() * 10 + j[0] - 'a';
}
struct Report {
  int n = 0;
  template <class Roster>
  static Report From(const Roster& roster, std::uint32_t host, const std::vector<std::uint32_t>& winners = {}) {
    Report r;
    r.n = (int)roster.size() * 100 + (int)winners.size() * 10 + (int)host;
    return r;
  }
};
struct Animator { enum Slot { SlotIdle, SlotWalk, SlotCount }; int s = SlotWalk; };
struct Inventory {
  struct Slot { std::string item; int count = 0; };
  std::vector<Slot> slots;
  int Remove(const std::string& item, int n) {
    for (auto& s : slots) {
      if (n <= 0) break;
      if (s.item != item) continue;
      int take = s.count < n ? s.count : n;
      s.count -= take; n -= take;
      if (s.count <= 0) s = Slot{};
    }
    return n;
  }
};
int entry(void) {
  auto cl = [](float v) { return v < 0.f ? 0.f : (v > 255.f ? 255.f : v); };
  CK(1, cl(-3.0f) == 0.0f && cl(300.0f) == 255.0f && cl(7.0f) == 7.0f && clampf(300.f) == 255.f);
  double d = 5.;
  CK(2, d == 5.0 && 1.e2 == 100.0 && 3.F == 3.0f && sizeof(2.f) == 4 && sizeof(2.) == 8);
  CK(3, pick_len(true, false, 3) == 80 + 'k' - 'a' && pick_len(false, true, 3) == 30 &&
            pick_len(false, false, 12) == 50 + 'p' - 'a');
  std::vector<int> roster{1, 2};
  std::vector<std::uint32_t> w{7, 8, 9};
  CK(4, Report::From(roster, 4).n == 204 && Report::From(roster, 1, w).n == 231);
  Inventory inv;
  Inventory::Slot a;
  a.item = "x";
  a.count = 3;
  inv.slots.push_back(a);
  Animator an;
  CK(5, inv.Remove("x", 5) == 2 && inv.slots[0].count == 0 && inv.slots[0].item.empty() && an.s == 1);
  return 0;
}
)OKMS", 0);
}

// More of the survey's next blockers: a call with 36 arguments (ThirdPersonController.hpp:906 passes 35; 16 values was
// the limit of the spill area), members of a nested class defined outside it (`void RoomReverb::Ring::Resize(int)`),
// a local class naming its function's static local ([class.local]), a std::function initialized with a lambda that
// calls it ([basic.scope.pdecl]), `(V{0, 0, 0} - v)` in parentheses, `case K::Key:` through `using K = Def::Kind;`,
// std::set_union and std::set_difference, deque::erase(first, last), and EOF after <sstream>.
static void r30_pieces2(Tally& tally) {
    std::printf("r30_pieces2\n");
    okayms_test::run_both(kRun, tally, "pieces2", std::string(kAlloc) + R"OKMS(
#include <algorithm>
#include <deque>
#include <functional>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
float many(float a0, float a1, float a2, float a3, float a4, float a5, float a6, float a7, float a8, float a9, float a10, float a11, float a12, float a13, float a14, float a15, float a16, float a17, float a18, float a19, float a20, float a21, float a22, float a23, float a24, float a25, float a26, float a27, float a28, float a29, float a30, float a31, float a32, float a33, float a34, float a35) { return a0 * 1 + a1 * 2 + a2 * 3 + a3 * 4 + a4 * 5 + a5 * 6 + a6 * 7 + a7 * 8 + a8 * 9 + a9 * 10 + a10 * 11 + a11 * 12 + a12 * 13 + a13 * 14 + a14 * 15 + a15 * 16 + a16 * 17 + a17 * 18 + a18 * 19 + a19 * 20 + a20 * 21 + a21 * 22 + a22 * 23 + a23 * 24 + a24 * 25 + a25 * 26 + a26 * 27 + a27 * 28 + a28 * 29 + a29 * 30 + a30 * 31 + a31 * 32 + a32 * 33 + a33 * 34 + a34 * 35 + a35 * 36; }
struct Room {
  struct Ring { int pos; Ring(int p); ~Ring(); void Resize(int n); int Read() const; };
};
int ring_dtors;
Room::Ring::Ring(int p) : pos(p) {}
Room::Ring::~Ring() { ++ring_dtors; }
void Room::Ring::Resize(int n) { pos = n; }
int Room::Ring::Read() const { return pos * 2; }
int fire(int n) {
  static int depth = 0;
  if (depth >= 3) return 100;
  struct Depth { Depth() { ++depth; } ~Depth() { --depth; } } guard;
  return n > 0 ? fire(n - 1) + depth : depth;
}
struct N { int v; std::vector<N*> kids; };
int sum_tree(N* root) {
  int total = 0;
  std::function<void(N*)> walk = [&](N* t) {
    if (!t) return;
    total += t->v;
    for (N* c : t->kids) walk(c);
  };
  walk(root);
  return total;
}
struct V {
  float x, y, z;
  V operator-(const V& o) const { return V{x - o.x, y - o.y, z - o.z}; }
  V operator*(float s) const { return V{x * s, y * s, z * s}; }
};
struct Def { enum Kind { Number, Key, Text }; Kind kind = Key; };
using K = Def::Kind;
int what(const Def& d) {
  switch (d.kind) {
    case K::Number: return 1;
    case K::Key: return 2;
    default: return 3;
  }
}
int peek_end(const std::string& text) {
  std::istringstream ls(text);
  int n = 0;
  while (ls && ls.peek() != EOF) { ls.get(); ++n; }
  return n;
}
int entry(void) {
  int k = 1;
  CK(1, many((float)(k + 0), (float)(k + 1), (float)(k + 2), (float)(k + 3), (float)(k + 4), (float)(k + 5), (float)(k + 6), (float)(k + 7), (float)(k + 8), (float)(k + 9), (float)(k + 10), (float)(k + 11), (float)(k + 12), (float)(k + 13), (float)(k + 14), (float)(k + 15), (float)(k + 16), (float)(k + 17), (float)(k + 18), (float)(k + 19), (float)(k + 20), (float)(k + 21), (float)(k + 22), (float)(k + 23), (float)(k + 24), (float)(k + 25), (float)(k + 26), (float)(k + 27), (float)(k + 28), (float)(k + 29), (float)(k + 30), (float)(k + 31), (float)(k + 32), (float)(k + 33), (float)(k + 34), (float)(k + 35)) == 16206.0f);
  {
    Room::Ring r(3);
    r.Resize(4);
    CK(2, r.Read() == 8);
  }
  CK(3, ring_dtors == 1 && fire(1) == 3 && fire(9) == 106);
  N a{1, {}}, b{2, {}}, c{4, {}};
  b.kids.push_back(&c);
  a.kids.push_back(&b);
  CK(4, sum_tree(&a) == 7);
  V m{2, 4, 6};
  V r = (V{0, 0, 0} - m) * 0.5f;
  CK(5, r.x == -1.0f && r.z == -3.0f);
  Def d;
  CK(6, what(d) == 2);
  d.kind = K::Text;
  CK(7, what(d) == 3);
  std::vector<int> x{1, 3, 5, 7}, y{3, 4, 7, 9}, u, df;
  std::set_union(x.begin(), x.end(), y.begin(), y.end(), std::back_inserter(u));
  std::set_difference(x.begin(), x.end(), y.begin(), y.end(), std::back_inserter(df));
  CK(8, u.size() == 6 && u[0] == 1 && u[2] == 4 && u[5] == 9 && df.size() == 2 && df[0] == 1 && df[1] == 5);
  std::deque<int> q;
  for (int i = 0; i < 6; ++i) q.push_back(i);
  q.erase(std::remove_if(q.begin(), q.end(), [](int v) { return v % 2 == 1; }), q.end());
  CK(9, q.size() == 3 && q[0] == 0 && q[1] == 2 && q[2] == 4);
  CK(10, peek_end("abc") == 3);
  return 0;
}
)OKMS", 0);
}

// IMAGE_SCN_LNK_NRELOC_OVFL (PE Format, Section Flags): a section with more relocations than NumberOfRelocations can
// hold. write_coff writes 0xFFFF there, sets the flag and puts the real count (the records plus the one that holds the
// count) in the VirtualAddress field of a first record; read_coff reads it back to the same relocations.
static void r30_reloc_overflow(Tally& tally) {
    std::printf("r30_reloc_overflow\n");
    okayms::ObjectFile obj;
    obj.machine = 0x8664;
    okayms::Section data;
    data.name = ".data";
    data.characteristics = 0xC0500040u;  // initialized data, read, write, 16-byte alignment
    const uint32_t count = 70000;
    data.data.assign(static_cast<size_t>(count) * 8u, 0);
    okayms::Symbol target;
    target.name = "target";
    target.value = 0;
    target.section = 1;
    target.storage = 2;
    obj.symbols.push_back(target);
    for (uint32_t i = 0; i < count; ++i) {
        okayms::Relocation r;
        r.offset = i * 8u;
        r.symbol = 0;
        r.type = 1;  // IMAGE_REL_AMD64_ADDR64
        data.relocations.push_back(r);
    }
    obj.sections.push_back(data);
    const auto written = okayms::write_coff(obj);
    CHECK(tally, written.ok());
    if (!written.ok()) {
        std::fprintf(stderr, "  %s\n", written.error.c_str());
        return;
    }
    const std::vector<uint8_t>& bytes = written.value;
    // The section header: NumberOfRelocations at 20 + 32, Characteristics at 20 + 36, PointerToRelocations at 20 + 24.
    const uint16_t nrel = static_cast<uint16_t>(bytes[52] | (bytes[53] << 8));
    const uint32_t flags = static_cast<uint32_t>(bytes[56]) | (static_cast<uint32_t>(bytes[57]) << 8) |
                           (static_cast<uint32_t>(bytes[58]) << 16) | (static_cast<uint32_t>(bytes[59]) << 24);
    const uint32_t relptr = static_cast<uint32_t>(bytes[44]) | (static_cast<uint32_t>(bytes[45]) << 8) |
                            (static_cast<uint32_t>(bytes[46]) << 16) | (static_cast<uint32_t>(bytes[47]) << 24);
    const uint32_t first = static_cast<uint32_t>(bytes[relptr]) | (static_cast<uint32_t>(bytes[relptr + 1]) << 8) |
                           (static_cast<uint32_t>(bytes[relptr + 2]) << 16) |
                           (static_cast<uint32_t>(bytes[relptr + 3]) << 24);
    CHECK(tally, nrel == 0xFFFF);
    CHECK(tally, (flags & 0x01000000u) != 0);
    CHECK(tally, first == count + 1);
    const auto back = okayms::read_coff(bytes.data(), bytes.size());
    CHECK(tally, back.ok());
    if (!back.ok()) {
        std::fprintf(stderr, "  %s\n", back.error.c_str());
        return;
    }
    CHECK(tally, back.value.sections.size() == 1 && back.value.sections[0].relocations.size() == count);
    CHECK(tally, (back.value.sections[0].characteristics & 0x01000000u) == 0);
    bool same = back.value.sections.size() == 1 && back.value.sections[0].relocations.size() == count;
    for (uint32_t i = 0; same && i < count; ++i) {
        const okayms::Relocation& r = back.value.sections[0].relocations[i];
        same = r.offset == i * 8u && r.symbol == 0 && r.type == 1;
    }
    CHECK(tally, same);
    // A section with 65535 relocations exactly also takes the overflow form (0xFFFF is the mark).
    obj.sections[0].relocations.resize(65535);
    const auto edge = okayms::write_coff(obj);
    CHECK(tally, edge.ok());
    if (edge.ok()) {
        const auto edge_back = okayms::read_coff(edge.value.data(), edge.value.size());
        CHECK(tally, edge_back.ok() && edge_back.value.sections[0].relocations.size() == 65535);
    }
}

// A program whose data section has more than 65535 relocations (a table of 70000 function addresses), compiled by
// okay-cl, written as a COFF object (the overflow form), read back, linked by okay-link and run (the QuestLog.cpp class of
// survey failures: okay-cl refused to write such an object).
static void r30_big_reloc_program(Tally& tally) {
    std::printf("r30_big_reloc_program\n");
    std::string src = std::string(kAlloc) + "int one(void) { return 1; }\nint two(void) { return 2; }\n"
                                            "typedef int (*Fn)(void);\nFn table[70000] = {";
    for (int i = 0; i < 70000; ++i) src += (i % 7 == 0) ? "two," : "one,";
    src += "};\nint entry(void) {\n  long long sum = 0;\n  for (int i = 0; i < 70000; ++i) sum += table[i]();\n"
           "  CK(1, sum == 80000);\n  CK(2, table[69999] == one && table[69993] == two);\n  return 0;\n}\n";
    okayms::ObjectFile object;
    okayms_test::run_both(kRun, tally, "big_reloc_program", src, 0, &object);
    // The /O0 object through the file form, as okay-cl writes it and okay-link reads it.
    size_t most = 0;
    for (const okayms::Section& sec : object.sections) most = std::max(most, sec.relocations.size());
    CHECK(tally, most > 65535);
    const auto bytes = okayms::write_coff(object);
    CHECK(tally, bytes.ok());
    if (!bytes.ok()) {
        std::fprintf(stderr, "  %s\n", bytes.error.c_str());
        return;
    }
    const auto back = okayms::read_coff(bytes.value.data(), bytes.value.size());
    CHECK(tally, back.ok());
    if (!back.ok()) return;
    const auto linked = okayms::link_executable({back.value}, okayms::LinkOptions{});
    CHECK(tally, linked.ok());
    if (!linked.ok()) return;
    const std::string exe = "/tmp/okayms-r30/big_reloc_file.exe";
    CHECK(tally, okayms_test::write_bytes(exe, linked.value));
    CHECK(tally, okayms_test::wine_exit(exe) == 0);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    Tally tally;
    r30_free_begin_end(tally);
    r30_member_hides_free(tally);
    r30_template_defaults(tally);
    r30_class_enum(tally);
    r30_member_pointers(tally);
    r30_paren_name(tally);
    r30_file_time(tally);
    r30_local_before_shadow(tally);
    r30_fp_reference(tally);
    r30_lambda_instances(tally);
    r30_pieces(tally);
    r30_pieces2(tally);
    r30_reloc_overflow(tally);
    r30_big_reloc_program(tally);
    return tally.finish(14);
}
