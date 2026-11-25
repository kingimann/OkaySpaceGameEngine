// Round 29: multiple inheritance (several bases, this-pointer adjustment, one table per base, adjustor thunks,
// dynamic_cast across bases), then the next engine blockers of the survey. Each program returns 0 when all of its
// checks hold, or the number of the first check that failed; it is built by okay-cl at /O0, /O1 and /O2 and run under
// Wine, and MinGW g++ builds the same text with its own headers and must agree (program_check.hpp). Written from
// ISO/IEC 14882 ([class.derived], [class.mi], [class.virtual], [conv.ptr], [expr.static.cast], [expr.dynamic.cast],
// [class.base.init], [class.dtor], [over.ics.list], [lex.ext], [char.traits], [meta.trans.other]) and Microsoft's
// public documentation of the x64 C++ object layout and name decoration. No code from other compilers or libraries.

#include "program_check.hpp"

#include <string>

namespace {
using okayms_test::Tally;
const okayms_test::ProgramRun kRun{"r29", {}};
const okayms_test::ProgramRun kRunRtti{"r29", {}, true};
const okayms_test::ProgramRun kRunTime{"r29", {"msvcrt.dll:_gmtime64_s", "msvcrt.dll:_localtime64_s"}};
}  // namespace

static const char* kAlloc = okayms_test::bump_alloc();

// Two polymorphic bases and a plain one (ModelAnimator : Behaviour, AnimBackend; VehicleController : Behaviour,
// VehicleTuning): calls through each base pointer reach the derived overrides (the second base's table holds adjustor
// thunks), conversions to the second and third base move the pointer and keep null as null, a cast back moves it
// back, members of every base are reached from the derived class, construction runs the bases in declaration order and
// a delete through the second base runs every destructor once.
static void r29_multiple_bases(Tally& tally) {
    std::printf("r29_multiple_bases\n");
    okayms_test::run_both(kRun, tally, "multiple_bases", std::string(kAlloc) + R"OKMS(
int trace[16];
int ntrace;
void note(int v) { trace[ntrace++] = v; }
struct Big { long long a, b, c; };
struct Named {
  int id;
  Named(int i) : id(i) { note(1); }
  virtual ~Named() { note(-1); }
  virtual int Kind() const { return 1; }
  virtual int Tag() const { return 1; }
  int Id() const { return id; }
};
struct Anim {
  int clips = 3;
  Anim() { note(2); }
  virtual ~Anim() { note(-2); }
  virtual int Count() const = 0;
  virtual int Play(int k) { return k + clips; }
  virtual int Tag() const { return 2; }
  virtual Big Info(int v) { Big b; b.a = v; b.b = 0; b.c = 0; return b; }
  int Twice() { return Count() * 2; }
};
struct Tuning {
  float mass = 2.5f;
  int gears = 5;
  int Gears() const { return gears; }
};
struct Model : public Named, public Anim, public Tuning {
  int extra;
  Model(int i) : Named(i), extra(40) { note(3); }
  ~Model() override { note(-3); }
  int Kind() const override { return 7; }
  int Count() const override { return extra + clips; }
  int Play(int k) override { return Anim::Play(k) * 10; }
  int Tag() const override { return 9; }
  Big Info(int v) override { Big b; b.a = v; b.b = v + id; b.c = extra + gears; return b; }
  int Sum() { return Id() + Twice() + Gears(); }
};
struct Leaf : Model {
  Leaf() : Model(2) {}
  int Count() const override { return 1; }
};
struct Plain : Named, Anim {
  Plain() : Named(4) {}
  int Count() const override { return 6; }
};
int use(Anim* p) { return p ? p->Count() : -1; }
int gears_of(const Tuning& t) { return t.Gears() + (int)t.mass; }
Anim* as_anim(Model* m) { return m; }
int entry(void) {
  ntrace = 0;
  Model m(5);
  CK(1, ntrace == 3 && trace[0] == 1 && trace[1] == 2 && trace[2] == 3);
  CK(2, m.Kind() == 7 && m.Count() == 43 && m.Id() == 5 && m.clips == 3 && m.gears == 5);
  Anim* a = &m;
  CK(3, a->Count() == 43 && a->Play(2) == 50 && a->Twice() == 86 && a->clips == 3);
  Named* n = &m;
  CK(4, n->Kind() == 7 && (void*)n == (void*)&m && (void*)a != (void*)&m);
  CK(5, n->Tag() == 9 && a->Tag() == 9 && m.Tag() == 9);
  CK(6, static_cast<Model*>(a) == &m && (Model*)a == &m && a == as_anim(&m));
  Tuning* t = &m;
  CK(7, t->gears == 5 && t->mass == 2.5f && static_cast<Model*>(t) == &m && (void*)t != (void*)a);
  Model* z = nullptr;
  Anim* az = z;
  CK(8, az == nullptr && use(z) == -1 && use(&m) == 43 && as_anim(z) == nullptr);
  Anim& ar = m;
  CK(9, ar.Count() == 43 && gears_of(m) == 7);
  Big b = a->Info(4);
  CK(10, b.a == 4 && b.b == 9 && b.c == 45);
  CK(11, m.Sum() == 96);
  Leaf leaf;
  Anim* la = &leaf;
  CK(12, la->Count() == 1 && leaf.Count() == 1 && la->Twice() == 2 && la->Play(1) == 40);
  Plain p;
  Plain* pp = &p;
  CK(13, pp->Play(1) == 4 && pp->Twice() == 12 && ((Anim*)pp)->Tag() == 2 && pp->Kind() == 1);
  Model* h = new Model(9);
  Anim* ha = h;
  ntrace = 0;
  delete ha;
  CK(14, ntrace == 3 && trace[0] == -3 && trace[1] == -2 && trace[2] == -1);
  Model c = m;
  Anim* ca = &c;
  CK(15, ca->Count() == 43 && c.Id() == 5 && c.gears == 5);
  return 0;
}
)OKMS", 0);
}

// dynamic_cast and typeid across bases (PlayerInput's dynamic_cast<IPlayerSim*>(component), SaveFrameworkScene's
// dynamic_cast<ISaveable*>): a cast from one base to another finds the object's whole type through the table's
// offset slot, then the base inside it; a class the object does not have gives null.
static void r29_cross_cast(Tally& tally) {
    std::printf("r29_cross_cast\n");
    okayms_test::run_both(kRunRtti, tally, "cross_cast", std::string(kAlloc) + R"OKMS(
#include <typeinfo>
struct Component {
  int enabled = 1;
  virtual ~Component() {}
  virtual int Update() { return 0; }
};
struct ISaveable {
  virtual ~ISaveable() {}
  virtual int Save() const = 0;
};
struct IPlayerSim {
  virtual int Simulate(int in) = 0;
};
struct Saveable : Component, ISaveable {
  int value = 11;
  int Save() const override { return value; }
};
struct Controller : Component, IPlayerSim {
  int speed = 3;
  int Simulate(int in) override { return in * speed; }
  int Update() override { return 5; }
};
struct Fancy : Saveable, IPlayerSim {
  int Simulate(int in) override { return in + value; }
};
int entry(void) {
  Saveable s;
  Controller c;
  Fancy f;
  Component* list[3] = {&s, &c, &f};
  int saved = 0, simmed = 0;
  for (int i = 0; i < 3; ++i) {
    if (ISaveable* sv = dynamic_cast<ISaveable*>(list[i])) saved += sv->Save();
    if (IPlayerSim* ps = dynamic_cast<IPlayerSim*>(list[i])) simmed += ps->Simulate(2);
  }
  CK(1, saved == 22 && simmed == 6 + 13);
  ISaveable* is = &f;
  CK(2, dynamic_cast<Fancy*>(is) == &f && dynamic_cast<Component*>(is) == (Component*)&f);
  CK(3, dynamic_cast<IPlayerSim*>(is) == (IPlayerSim*)&f && dynamic_cast<Controller*>(is) == nullptr);
  CK(4, typeid(*is) == typeid(Fancy) && typeid(*list[1]) == typeid(Controller));
  IPlayerSim* ps = &c;
  CK(5, dynamic_cast<Controller*>(ps) == &c && dynamic_cast<ISaveable*>(ps) == nullptr);
  ISaveable* none = nullptr;
  CK(6, dynamic_cast<Component*>(none) == nullptr && list[1]->Update() == 5);
  return 0;
}
)OKMS", 0);
}

// The library pieces the next engine files stopped at: reverse iterators (ModelAnimator.hpp's ch.rbegin()), map::swap
// (Inventory::NotifyChanged), string_view::compare(pos, n, s) (XmlLite.hpp), std::char_traits<char>::length
// (VsRefs.hpp, ObjectPath.cpp), a user-defined literal in an inline namespace (StringId.hpp's operator""_sid), a class
// nested in a class named past an enumeration of the same name elsewhere (Character::Bone beside HumanoidMotion::Bone),
// std::max(-x, y) in a lambda (WaterShading.hpp), and a function of a namespace that calls itself.
static void r29_library(Tally& tally) {
    std::printf("r29_library\n");
    okayms_test::run_both(kRun, tally, "library", std::string(kAlloc) + R"OKMS(
#include <vector>
#include <string>
#include <string_view>
#include <map>
#include <set>
#include <array>
#include <algorithm>
#include <iterator>
#include <unordered_map>
#include <cstddef>
namespace motion { enum Bone { B_HIPS, B_TORSO }; }
namespace game {
class Character {
public:
  struct Bone { int parent; };
  std::vector<Bone> Skeleton() const { std::vector<Bone> b; b.push_back(Bone{3}); b.push_back(Bone{5}); return b; }
};
struct Id { unsigned v; };
inline namespace literals {
constexpr Id operator""_id(const char* s, std::size_t n) { return Id{(unsigned)n * 100u + (unsigned)s[0]}; }
}
long long sum_to(int n) { return n <= 0 ? 0 : n + sum_to(n - 1); }
}
using namespace game;
int clamp_in(float limit, bool on) {
  auto vid = [&](int i) {
    float px = i * 1.5f;
    if (on) px = std::min(limit, std::max(-limit, px));
    return (int)px;
  };
  return vid(4) * 10 - vid(-4);
}
int entry(void) {
  std::vector<int> v{1, 2, 3, 4};
  int acc = 0;
  for (auto it = v.rbegin(); it != v.rend(); ++it) acc = acc * 10 + *it;
  CK(1, acc == 4321);
  const std::vector<int>& cv = v;
  std::vector<int> back(cv.rbegin(), cv.rend());
  std::string s = "abc";
  std::string r(s.rbegin(), s.rend());
  CK(2, back[0] == 4 && back[3] == 1 && r == "cba");
  std::map<int, int> m;
  m[3] = 30; m[1] = 10; m[7] = 70;
  std::set<int> st{5, 2, 9};
  std::array<int, 3> a{{7, 8, 9}};
  auto ri = a.rbegin();
  ri += 1;
  CK(3, m.rbegin()->first == 7 && *st.rbegin() == 9 && *ri == 8 && ri[1] == 7 && (a.rend() - a.rbegin()) == 3);
  std::map<std::string, long long> now, counts;
  now["a"] = 2;
  counts["b"] = 1; counts["c"] = 4;
  counts.swap(now);
  std::unordered_map<int, int> h1, h2;
  h1[1] = 2;
  h1.swap(h2);
  CK(4, counts.size() == 1 && counts["a"] == 2 && now.size() == 2 && h1.empty() && h2[1] == 2);
  const std::string_view an("xmlns:svg");
  CK(5, an.compare(0, 6, "xmlns:") == 0 && an.compare(0, 5, "xmlnt") < 0 && an.compare(6, 3, "svg") == 0);
  const char* p = "prefix/";
  std::string name = "prefix/rest";
  CK(6, name.compare(0, std::char_traits<char>::length(p), p) == 0 && std::char_traits<char>::length("") == 0);
  CK(7, std::char_traits<char>::compare("ab", "ac", 2) < 0 && std::char_traits<char>::lt('a', (char)0xE0));
  Id id = "abc"_id;
  CK(8, id.v == 300u + 'a');
  Character ch;
  const std::vector<Character::Bone> sk = ch.Skeleton();
  CK(9, sk.size() == 2 && sk[1].parent == 5 && motion::B_TORSO == 1);
  CK(10, clamp_in(3.0f, true) == 33 && clamp_in(3.0f, false) == 66 && clamp_in(10.0f, true) == 66);
  CK(11, game::sum_to(4) == 10);
  return 0;
}
)OKMS", 0);
}

// localtime_r and gmtime_r (Log::Stamp's POSIX path, CrashHandler.hpp's ::localtime_r) on the Microsoft runtime's
// _localtime64_s and _gmtime64_s, the <fcntl.h> flags, and a function template whose result is written with `struct`.
static void r29_posix_time(Tally& tally) {
    std::printf("r29_posix_time\n");
    okayms_test::run_both(kRunTime, tally, "posix_time", R"OKMS(
#ifdef __GNUC__
#define _POSIX_THREAD_SAFE_FUNCTIONS 1
#endif
#include <ctime>
#include <fcntl.h>
struct cell { int a; };
template <typename T> struct cell *fill(const T *t, struct cell *out) { out->a = (int)*t; return out; }
int entry(void) {
  time_t t = 86400 * 2 + 3600 * 5 + 61;
  struct tm out;
  struct tm* r = ::gmtime_r(&t, &out);
  CK(1, r == &out && out.tm_hour == 5 && out.tm_mday == 3 && out.tm_min == 1 && out.tm_sec == 1 && out.tm_year == 70);
  struct tm loc;
  CK(2, localtime_r(&t, &loc) == &loc && loc.tm_year == 70);
  CK(3, (O_CREAT | O_WRONLY | O_TRUNC) == (_O_CREAT | _O_WRONLY | _O_TRUNC) && O_RDONLY == 0);
  long long v = 9;
  cell c;
  CK(4, fill(&v, &c)->a == 9);
  return 0;
}
)OKMS", 0);
}

// A braced list argument chooses the overload that can take it (Mesh.hpp:385 `out.erase({min, max})` on a set of
// pairs): a class parameter by its constructor or as an aggregate, never a pointer for a list of two. Round 28 chose
// the pointer overload and passed the first element as an address.
static void r29_braced_overload(Tally& tally) {
    std::printf("r29_braced_overload\n");
    okayms_test::run_both(kRun, tally, "braced_overload", std::string(kAlloc) + R"OKMS(
#include <set>
#include <utility>
#include <algorithm>
#include <vector>
struct P { int x, y; };
struct S {
  int last = 0;
  int erase(const P& p) { last = p.x * 10 + p.y; return 1; }
  int erase(const P* pos) { last = pos ? 1000 : 2000; return 2; }
};
int take(const P& p) { return p.x * 10 + p.y; }
int take(const int* p) { return p ? 1000 : 2000; }
int entry(void) {
  S s;
  int a = 4, b = 3;
  CK(1, s.erase({a, b}) == 1 && s.last == 43);
  CK(2, take({4, 2}) == 42);
  std::set<std::pair<int, int>> out;
  out.insert({1, 2}); out.insert({3, 4}); out.insert({2, 5});
  out.erase({std::min(a, b), std::max(a, b)});
  CK(3, out.size() == 2 && out.count({3, 4}) == 0 && out.count({1, 2}) == 1);
  std::vector<int> v;
  v.push_back(7);
  v.insert(v.end(), {8, 9});
  CK(4, v.size() == 3 && v[2] == 9);
  return 0;
}
)OKMS", 0);
}

// Smaller fixes the survey showed after the blockers above: static_cast<T*>(p)->m (the rewritten cast was applied to
// p->m), a cast to an enumeration declared in a class (UIBind::Mode), a member of a derived class hiding a base member
// of the same name (UIDamageNumbers::m_live beside Component::m_live), a local of an enclosing block declared after an
// inner block that has its own local of that name (UITouchScroll::Step), an aggregate with a std::string member made
// from a list (its text was lost), and string::rfind of a string.
static void r29_pieces(Tally& tally) {
    std::printf("r29_pieces\n");
    okayms_test::run_both(kRun, tally, "pieces", std::string(kAlloc) + R"OKMS(
#include <string>
#include <vector>
struct C { float a; };
struct E { const void* id; float alpha; };
struct Base {
  virtual ~Base() {}
  bool m_live = false;
  int BaseLive() const { return m_live ? 1 : 0; }
};
class UIBind : public Base {
public:
  enum class Mode { TwoWay = 0, ToVariable = 1, FromVariable = 2 };
  Mode mode = Mode::TwoWay;
  std::vector<int> m_live;
  int Count() { m_live.push_back(4); m_live.clear(); m_live.push_back(9); return (int)m_live.size(); }
};
int shadow(int on) {
  int r = 0;
  if (on) {
    int d = 5;
    d = d + 1;
    r += d;
  }
  const int d = 100;
  return r + d;
}
struct G { std::string name; int n; };
int entry(void) {
  C c{1.0f};
  E e{&c, 2.5f};
  static_cast<C*>(const_cast<void*>(e.id))->a = e.alpha;
  CK(1, c.a == 2.5f);
  UIBind b;
  int mode = 2;
  b.mode = (UIBind::Mode)mode;
  CK(2, b.mode == UIBind::Mode::FromVariable && static_cast<UIBind::Mode>(1) == UIBind::Mode::ToVariable);
  CK(3, b.Count() == 1 && b.m_live[0] == 9 && b.BaseLive() == 0);
  CK(4, shadow(1) == 106 && shadow(0) == 100);
  G g{"obj", 3};
  CK(5, g.name.size() == 3 && g.name == "obj" && g.n == 3);
  const std::string name = "obj_x_obj_y";
  CK(6, name.rfind(g.name + "_", 0) == 0 && name.rfind("obj") == 6 && name.rfind(std::string("x_")) == 4 && name.rfind("zz") == std::string::npos);
  return 0;
}
)OKMS", 0);
}

// Structured bindings ([dcl.struct.bind]; 32 engine files: `for (const auto& [k, v] : map)`, `const auto [id, parent] =
// stack.back();`): by reference into a container's elements, by value as a copy, from a call result, of an array, and
// over a braced list of pairs.
static void r29_bindings(Tally& tally) {
    std::printf("r29_bindings\n");
    okayms_test::run_both(kRun, tally, "bindings", std::string(kAlloc) + R"OKMS(
#include <map>
#include <string>
#include <utility>
#include <vector>
struct Pt { int x, y; };
std::pair<int, int> mk() { return {3, 4}; }
int entry(void) {
  std::map<std::string, int> m;
  m["a"] = 1; m["bb"] = 2;
  int total = 0;
  for (const auto& [k, v] : m) total += (int)k.size() * 10 + v;
  CK(1, total == 10 + 1 + 20 + 2);
  for (auto& [k, v] : m) v *= 2;
  CK(2, m["a"] == 2 && m["bb"] == 4);
  Pt p{5, 6};
  auto [px, py] = p;
  px = 50;
  CK(3, px == 50 && p.x == 5 && py == 6);
  auto& [rx, ry] = p;
  rx = 7;
  CK(4, p.x == 7 && ry == 6);
  const auto [a, b] = mk();
  CK(5, a == 3 && b == 4);
  std::vector<std::pair<int, int>> stack{{1, 2}, {3, 4}};
  const auto [id, parent] = stack.back();
  CK(6, id == 3 && parent == 4);
  int arr[2] = {8, 9};
  auto& [e0, e1] = arr;
  e1 = 10;
  CK(7, e0 + arr[1] == 18);
  int named = 0;
  for (const auto& [label, n] : {std::pair<const char*, int>{"x", 1}, std::pair<const char*, int>{"yy", 2}})
    named += n * (label[1] == 0 ? 1 : 10);
  CK(8, named == 21);
  return 0;
}
)OKMS", 0);
}

// More of what the survey stopped at once the blockers above were gone: std::string(s, pos) (a constructor with a
// default argument beats the iterator-pair template), C::~C() = default; outside the class, an alias template declared
// in a class (EventBus::Handler), explicit template arguments with a lambda for a std::function<...T...> parameter
// (AssetManager::RegisterLoader), lower_bound and upper_bound with a comparison, string::replace, Outer::Inner{...} for
// a nested class whose plain name another class also has, a nested class named past a scoped enumeration of the same
// name elsewhere (AnimStateMachine::State::IkKey beside Countdown::State), and a template instance calling itself
// (FindInTree<T>).
static void r29_more(Tally& tally) {
    std::printf("r29_more\n");
    okayms_test::run_both(kRun, tally, "more", std::string(kAlloc) + R"OKMS(
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <algorithm>
struct Impl { int v = 3; };
class Clock {
public:
  Clock();
  ~Clock();
  int Get() const { return m_impl->v; }
private:
  std::unique_ptr<Impl> m_impl;
};
Clock::Clock() { m_impl = std::make_unique<Impl>(); }
Clock::~Clock() = default;
class Bus {
public:
  template <typename E>
  using Handler = std::function<void(const E&)>;
  template <typename E> int Add(Handler<E> h) { E e{}; h(e); return 1; }
};
struct Ev { int x = 4; };
struct Loader {
  template <class T>
  int Reg(std::string name, std::function<std::shared_ptr<T>(const std::string& path, std::string& error)> load) {
    std::string err;
    std::shared_ptr<T> p = load(name, err);
    return *p;
  }
};
struct Key { float t; int v; };
class Countdown { public: enum class State { Idle, Running, Done }; State s = State::Idle; };
namespace other { class Log { public: struct State { int a = 0; }; }; }
class Machine {
public:
  struct State {
    int x = 1;
    struct IkKey { float t = 0.0f; float w = 1.0f; };
    std::vector<IkKey> ik;
  };
  static float Sample(const std::vector<State::IkKey>& keys, float scale) { return keys.empty() ? 0.0f : keys[0].w * scale; }
};
struct N { int v; N* kid; };
template <class T> T* deepest(N* n, int depth) {
  if (!n->kid || depth > 5) return (T*)n;
  if (auto* c = deepest<T>(n->kid, depth + 1)) return c;
  return nullptr;
}
int entry(void) {
  const std::string design = "user:ab";
  std::map<std::string, int> parts;
  parts["ab"] = 4;
  auto it = parts.find(std::string(design, 5));
  CK(1, it != parts.end() && it->second == 4 && std::string(design, 1, 3) == "ser");
  Clock c;
  CK(2, c.Get() == 3);
  Bus b;
  int got = 0;
  b.Add<Ev>([&](const Ev& e) { got = e.x; });
  CK(3, got == 4);
  Loader l;
  CK(4, l.Reg<int>("x", [](const std::string& p, std::string& e) { return std::make_shared<int>((int)p.size() + 4); }) == 5);
  std::vector<Key> keys{{0.0f, 1}, {1.0f, 2}, {2.0f, 3}};
  auto lo = std::lower_bound(keys.begin(), keys.end(), 1.0f, [](const Key& k, float t) { return k.t < t; });
  auto hi = std::upper_bound(keys.begin(), keys.end(), 1.0f, [](float t, const Key& k) { return t < k.t; });
  CK(5, lo->v == 2 && hi->v == 3);
  std::string s = "a_X_b_X";
  for (size_t p = 0; (p = s.find("X", p)) != std::string::npos; p += 3) s.replace(p, 1, "YYY");
  std::string t = "abc";
  t.replace(1, 1, 3, 'z');
  CK(6, s == "a_YYY_b_YYY" && t == "azzzc");
  Machine::State st;
  st.ik.push_back(Machine::State::IkKey{0.5f, 2.0f});
  Countdown cd;
  CK(7, Machine::Sample(st.ik, 3.0f) == 6.0f && cd.s == Countdown::State::Idle);
  N n3{3, nullptr}, n2{2, &n3}, n1{1, &n2};
  CK(8, deepest<N>(&n1, 0)->v == 3);
  return 0;
}
)OKMS", 0);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    Tally tally;
    r29_multiple_bases(tally);
    r29_cross_cast(tally);
    r29_library(tally);
    r29_posix_time(tally);
    r29_braced_overload(tally);
    r29_pieces(tally);
    r29_bindings(tally);
    r29_more(tally);
    return tally.finish(8);
}
