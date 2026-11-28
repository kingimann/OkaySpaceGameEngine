// Round 35: the next blockers of the two engine surveys. Each program returns 0 when all of its checks hold, or the
// number of the first check that failed; it is built by okay-cl at /O0, /O1 and /O2 and run under Wine, and MinGW g++
// builds the same text with its own headers and must agree (program_check.hpp). Written from ISO/IEC 14882, ISO/IEC 9899
// and POSIX.1-2017 (the sections are named at each test). No code from other compilers or libraries.

#include "program_check.hpp"

#include <string>

namespace {
using okayms_test::Tally;
const okayms_test::ProgramRun kRun{"r35", {}};
}  // namespace

static const char* kAlloc = okayms_test::bump_alloc();

// Our own C and POSIX headers for the plain survey (_WIN32 not defined): <time.h> (33 files, through AccountDetail.hpp
// and AccountService.hpp), <sys/socket.h>, <netinet/in.h>, <arpa/inet.h>, <netdb.h>, <sys/select.h>, <poll.h>,
// <netinet/tcp.h>, <dlfcn.h>, <dirent.h> and <errno.h>. Compile-only: the Microsoft C runtime has none of the POSIX
// functions, so nothing that calls them links on Windows (the same as the Round 30 POSIX headers). Before: "cannot open
// include time.h".
static void r35_posix_headers(Tally& tally) {
    std::printf("r35_posix_headers\n");
    okayms::CompileInput in;
    in.source = R"OKMS(
#include <time.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/select.h>
#include <poll.h>
#include <dlfcn.h>
#include <dirent.h>
#include <unistd.h>
int check(void) {
  if (sizeof(struct sockaddr_in) != 16 || sizeof(struct sockaddr) != 16) return 1;
  if (sizeof(struct in_addr) != 4 || AF_INET != 2 || SOCK_DGRAM != 2 || IPPROTO_UDP != 17) return 2;
  fd_set set;
  FD_ZERO(&set);
  FD_SET(5, &set);
  FD_SET(70, &set);
  if (!FD_ISSET(5, &set) || !FD_ISSET(70, &set) || FD_ISSET(6, &set)) return 3;
  FD_CLR(5, &set);
  if (FD_ISSET(5, &set)) return 4;
  struct tm t;
  t.tm_year = 70;
  if (errno == EAGAIN) return 5;
  return 0;
}
long long now(void) { return (long long)::time(nullptr) + (long long)clock(); }
int open_udp(unsigned short port) {
  int s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(port);
  socklen_t len = sizeof(addr);
  ::bind(s, reinterpret_cast<sockaddr*>(&addr), len);
  addrinfo hints{};
  addrinfo* res = nullptr;
  if (::getaddrinfo("localhost", nullptr, &hints, &res) == 0) ::freeaddrinfo(res);
  pollfd p{s, POLLIN, 0};
  ::poll(&p, 1, 0);
  void* lib = dlopen("libasound.so.2", RTLD_NOW);
  if (lib) dlsym(lib, "snd_pcm_open");
  DIR* d = opendir("/dev/input");
  if (d) { dirent* e = readdir(d); (void)e; closedir(d); }
  usleep((useconds_t)1000);
  return s;
}
)OKMS";
    in.include_paths.push_back("stdinc");
    for (const char* u : {"_WIN32", "_WIN64", "_M_X64", "_M_AMD64", "_MSC_VER", "_MSC_FULL_VER", "_MSVC_LANG"})
        in.undefines.push_back(u);
    const auto compiled = okayms::compile_c(in);
    CHECK(tally, compiled.ok());
    if (!compiled.ok()) {
        std::fprintf(stderr, "  posix headers: %s\n", compiled.error.c_str());
        return;
    }
    CHECK(tally, okayms_test::has_symbol(compiled.value.object, "getaddrinfo"));
    CHECK(tally, okayms_test::has_symbol(compiled.value.object, "dlopen"));
    CHECK(tally, okayms_test::has_symbol(compiled.value.object, "htons"));
}

// The preprocessor and two library pieces behind the plain survey's Linux files (Round 35):
// - `__has_include(<h>)` and `__has_include("h")` ([cpp.cond] 1, 5), and `#ifdef __has_include` ([cpp.cond] 6);
// - an #if or #elif in a group that is skipped is not evaluated ([cpp.cond] 13): OwnLinuxX11.cpp has
//   `#if __has_include(<X11/...>)` inside an `#else` that is skipped (before: "bad preprocessor expression");
// - `deque::insert(pos, first, last)` ([sequence.reqmts]; OwnLinuxAudio.cpp's byte queue; before "no member function
//   insert with 3 arguments");
// - `2 * period` and `d / n` on a duration ([time.duration.nonmember]), and `t - d` of a time point and a duration is a
//   time point ([time.point.nonmember]). Before, `t0 - period` took the time point's member `operator-(time_point)`
//   (the duration made into a time point through its explicit constructor) and gave a duration.
static void r35_preprocessor_library(Tally& tally) {
    std::printf("r35_preprocessor_library\n");
    okayms_test::run_both(kRun, tally, "pp_library", std::string(kAlloc) + R"OKMS(
#include <chrono>
#include <deque>
#ifndef __has_include
#error no __has_include
#endif
#if __has_include(<vector>) && !__has_include(<no/such/header_r35.h>)
#define HAS_OK 1
#else
#define HAS_OK 0
#endif
#if 0
#if this is not an expression (
#elif neither is this ((
#endif
#elif 1
#define SKIP_OK 1
#elif also not (( an expression
#endif
struct Ticks { using Clock = std::chrono::steady_clock; };
int entry(void) {
  CK(1, HAS_OK == 1);
  CK(2, SKIP_OK == 1);
  std::deque<unsigned char> q;
  q.push_back(1);
  q.push_back(9);
  const unsigned char bytes[4] = {2, 3, 4, 5};
  q.insert(q.end() - 1, bytes, bytes + 4);
  CK(3, q.size() == 6 && q[0] == 1 && q[1] == 2 && q[4] == 5 && q[5] == 9);
  q.insert(q.begin(), bytes + 2, bytes + 4);
  CK(4, q.size() == 8 && q[0] == 4 && q[1] == 5 && q[2] == 1 && q[7] == 9);
  const auto period = std::chrono::nanoseconds(250);
  CK(5, (2 * period).count() == 500 && (period * 3).count() == 750 && (period / 5).count() == 50);
  CK(6, std::chrono::nanoseconds(1000) / period == 4);
  Ticks::Clock::time_point t0{};
  const auto due = t0 + std::chrono::nanoseconds(7000);
  const auto back = due - 2 * period;
  CK(7, back.time_since_epoch().count() == 6500 && (due - t0).count() == 7000);
  CK(8, (back - t0).count() == 6500);
  return 0;
}
)OKMS", 0);
}

// thread_local objects of class type at namespace scope ([basic.stc.thread], [basic.start.dynamic] 7, [basic.start.term]
// 1; 6 engine files: SceneSerializer.cpp's `static thread_local std::string g_lastSceneDir`, GameObject.cpp's
// `thread_local std::vector<std::type_index>`, Guided.cpp's thread_local std::map). Each thread gets its own object,
// made before the thread uses it and destroyed when the thread ends. Here: main's thread and a thread of CreateThread.
// okay-ms makes them in the TLS callback of the image (reason 1 for main's thread, 2 for a new thread) and destroys them
// at reason 3; g++ makes them on first use. Before: "a thread_local object with a constructor or destructor at namespace
// scope is not supported yet".
static void r35_thread_local(Tally& tally) {
    std::printf("r35_thread_local\n");
    const okayms_test::ProgramRun run{"r35", {"CreateThread", "WaitForSingleObject", "CloseHandle"}};
    okayms_test::run_both(run, tally, "thread_local", std::string(kAlloc) + R"OKMS(
#include <windows.h>
#include <string>
#include <vector>
#include <map>
struct Counter {
  static int made;
  static int last_gone;
  int id;
  int alive;
  Counter() : id(++made), alive(42) {}
  ~Counter() { last_gone = id; alive = 0; }
};
int Counter::made = 0;
int Counter::last_gone = 0;
thread_local Counter tc;
static thread_local std::string ts;
thread_local std::vector<int> tv;
static thread_local std::map<std::string, int> tm;
thread_local std::string tn("seed");
static unsigned long __stdcall worker(void* p) {
  int* out = (int*)p;
  out[0] = tc.id;
  out[1] = tc.alive;
  out[2] = (int)ts.size() + (int)tv.size() + (int)tm.size();
  ts = "worker";
  tv.push_back(5);
  tm["w"] = 1;
  out[3] = (int)ts.size() * 10 + (int)tv.size() + (int)tm.size();
  out[4] = tn == "seed";
  return 0;
}
int entry(void) {
  const int mine = tc.id;
  CK(1, tc.alive == 42 && mine >= 1);
  ts += "abc";
  tv.push_back(1);
  tv.push_back(2);
  tm["a"] = 7;
  CK(2, ts == "abc" && tv.size() == 2 && tm["a"] == 7 && tn == "seed");
  int out[5] = {0, 0, 0, 0, 0};
  HANDLE h = CreateThread(0, 0, worker, out, 0, 0);
  CK(3, h != 0);
  WaitForSingleObject(h, INFINITE);
  CloseHandle(h);
  CK(4, out[0] != mine && out[1] == 42);
  CK(5, out[2] == 0 && out[3] == 62 && out[4] == 1);
  CK(6, Counter::last_gone == out[0]);
  CK(7, tc.id == mine && tc.alive == 42 && ts == "abc" && tv.size() == 2 && tm.size() == 1);
  return 0;
}
)OKMS", 0);
}

// Silent wrong code found while linking engine/src/Scene/ProjectLists.cpp (Round 35; the Round 34 compiler's result in
// brackets):
// - a member that is an array of objects (`std::string e[4]`, std::array's elems_) had only its first element made by
//   the implicit constructor and destroyed by the destructor ([class.base.init] 9, [class.dtor] 13): the others held
//   what the stack held, and a store to e[2] freed that [access violation, exit 5];
// - `c ? f() : s` with a class prvalue and an lvalue s took s's bytes without its copy constructor ([expr.cond] 7): the
//   result shared s's buffer [a change to the copy changed s, exit 3];
// - an `explicit` constructor made implicit conversions ([class.conv.ctor] 1) [the overload taking the class was
//   chosen for an int, exit 5].
static void r35_wrong_code(Tally& tally) {
    std::printf("r35_wrong_code\n");
    okayms_test::run_both(kRun, tally, "wrong_code", std::string(kAlloc) + R"OKMS(
#include <array>
#include <string>
static int made = 0;
static int gone = 0;
struct Tracked { int v; Tracked() : v(7) { ++made; } ~Tracked() { ++gone; } };
struct Holder { Tracked t[3]; Tracked grid[2][2]; int n = 5; };
static int Dirty() { volatile char buf[2048]; for (int i = 0; i < 2048; ++i) buf[i] = (char)0x5A; return buf[7]; }
static int Use() {
  std::array<std::string, 4> a{};
  int r = (int)a[2].size() + (int)a[3].size();
  a[2] = "Ignore Raycast";
  a[3] = "x";
  return r + (int)a[2].size() + (int)a[3].size();
}
static std::string g_name = "Default";
static std::string Name(int i) {
  const std::string& s = g_name;
  return i ? ("Layer " + std::to_string(i)) : s;
}
static int Len(const std::string& t) { return (int)t.size(); }
struct Wrapped { int v; explicit Wrapped(int x) : v(x) {} };
static int Pick(const Wrapped&) { return 1; }
static int Pick(double) { return 2; }
int entry(void) {
  Dirty();
  CK(1, Use() == 15);
  {
    Holder h;
    CK(2, made == 7 && h.t[2].v == 7 && h.grid[1][1].v == 7 && h.n == 5);
  }
  CK(3, gone == 7);
  CK(4, Len(Name(0)) == 7 && g_name == "Default");
  { std::string a = Name(0); a[0] = 'X'; }
  CK(5, g_name == "Default" && Name(3) == "Layer 3");
  CK(6, Pick(3) == 2 && Pick(Wrapped(3)) == 1);
  return 0;
}
)OKMS", 0);
}

// Round 35 language items behind the Windows survey's next blockers:
// - a lambda with default arguments called without them ([expr.prim.lambda.closure] 4; VehicleBuilder.hpp's
//   `[](int r, int g, int b, int a = 255)`, 3 files; before "wrong number of arguments to rgb");
// - arrays of objects of more than one dimension: local, static local and global ([dcl.array] 6, [class.base.init];
//   NavMeshBake.cpp's `Vec3 buf[2][9]`; before "an array of objects needs one dimension");
// - `T (&a)[N]` and `T (&a)[M][N]` parameters, with T deduced from an array argument ([dcl.ref], [temp.deduct.call] 3;
//   MdlReader.cpp, UVUnwrap.cpp; before "expected ')'").
static void r35_language(Tally& tally) {
    std::printf("r35_language\n");
    okayms_test::run_both(kRun, tally, "language", std::string(kAlloc) + R"OKMS(
#include <cstdint>
#include <string>
struct Color { int r, g, b, a; static Color FromBytes(int r, int g, int b, int a) { return Color{r, g, b, a}; } };
struct Vec3 { float x, y, z; Vec3() : x(1), y(2), z(3) {} Vec3(float a, float b, float c) : x(a), y(b), z(c) {} };
static int alive = 0;
struct Obj { int v; Obj() : v(4) { ++alive; } ~Obj() { --alive; } };
static Obj g_grid[2][3];
static float Slab(float z0) {
  Vec3 buf[2][9];
  buf[0][0] = Vec3(0, 0, z0);
  buf[1][8] = Vec3(5, 6, 7);
  const Vec3* in = buf[1];
  return buf[0][0].z + in[8].x + buf[1][3].y;
}
template <class T> bool CornersIn(const T (&v)[3], std::int32_t n) { return v[0] >= 0 && v[1] >= 0 && v[2] >= 0 && v[0] < n && v[1] < n && v[2] < n; }
static int Sum3(const int (&v)[3]) { int s = 0; for (int i = 0; i < 3; ++i) s += v[i]; return s; }
static void Bump(int (&v)[3]) { v[1] += 10; }
int entry(void) {
  auto rgb = [](int r, int g, int b, int a = 255) { return Color::FromBytes(r, g, b, a); };
  const Color dark = rgb(38, 38, 42), glass = rgb(150, 205, 235, 70);
  CK(1, dark.a == 255 && glass.a == 70 && dark.b == 42);
  int base = 10;
  auto add = [&](int x, int y = 2) { return base + x + y; };
  CK(2, add(1) == 13 && add(1, 5) == 16);
  CK(3, Slab(10) == 17);
  CK(4, alive == 6 && g_grid[1][2].v == 4);
  {
    Obj local[3][2];
    CK(5, alive == 12 && local[2][1].v == 4);
    static std::string names[2][2];
    names[1][1] = "x";
    CK(6, names[1][1] == "x" && names[0][1].empty());
  }
  CK(7, alive == 6);
  int tri[3] = {0, 4, 2};
  CK(8, CornersIn(tri, 5) && !CornersIn(tri, 4) && Sum3(tri) == 6);
  Bump(tri);
  CK(9, tri[1] == 14);
  double c[3][3] = {{1, 0, 0}, {0, 2, 0}, {0, 0, 3}};
  auto power = [&](const double (&m)[3][3], double v[3]) { double t = 0; for (int i = 0; i < 3; ++i) t += m[i][i] * v[i]; return t; };
  double v[3] = {1, 1, 1};
  CK(10, power(c, v) == 6);
  return 0;
}
)OKMS", 0);
}

// `signed char` and `long` as types of their own ([basic.fundamental] 1, 2; Round 35): both are the size of char and
// int on Windows x64 (LLP64), but an overload or a specialization of one is not taken for the other, and the decorated
// names use C (signed char) and J/K (long, unsigned long) as MSVC does. Before: the overloads Kind(int) and Kind(long)
// were one function, and Tag<long> took Tag<int>'s specialization.
static void r35_distinct_types(Tally& tally) {
    std::printf("r35_distinct_types\n");
    okayms::ObjectFile object;
    okayms_test::run_both(kRun, tally, "distinct_types", std::string(kAlloc) + R"OKMS(
#include <limits>
#include <vector>
#include <functional>
static int Kind(int) { return 1; }
static int Kind(long) { return 2; }
static int Kind(signed char) { return 3; }
static int Kind(char) { return 4; }
static int Kind(unsigned char) { return 5; }
template <class T> struct Tag { static int id() { return 0; } };
template <> struct Tag<long> { static int id() { return 7; } };
template <> struct Tag<int> { static int id() { return 8; } };
template <> struct Tag<signed char> { static int id() { return 9; } };
int entry(void) {
  long l = 3;
  int i = 4;
  signed char sc = -1;
  char c = 'a';
  unsigned char uc = 200;
  CK(1, Kind(l) == 2 && Kind(i) == 1);
  CK(2, Kind(sc) == 3 && Kind(c) == 4 && Kind(uc) == 5);
  CK(3, Tag<long>::id() == 7 && Tag<int>::id() == 8 && Tag<signed char>::id() == 9 && Tag<char>::id() == 0);
  CK(4, std::numeric_limits<long>::max() == 2147483647 && std::numeric_limits<unsigned long>::max() == 4294967295u);
  std::vector<long> v{1, 2, 3};
  std::vector<int> w{4};
  CK(5, v.size() == 3 && v[2] == 3 && w[0] == 4);
  CK(6, sizeof(long) == 4 && sizeof(signed char) == 1 && (int)sc == -1);
  return 0;
}
)OKMS", 0, &object);
    CHECK(tally, okayms_test::has_symbol(object, "?Kind@@YAHJ@Z"));
    CHECK(tally, okayms_test::has_symbol(object, "?Kind@@YAHC@Z"));
    CHECK(tally, okayms_test::has_symbol(object, "?Kind@@YAHH@Z"));
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    Tally tally;
    r35_posix_headers(tally);
    r35_preprocessor_library(tally);
    r35_thread_local(tally);
    r35_wrong_code(tally);
    r35_language(tally);
    r35_distinct_types(tally);
    return tally.finish(6);
}
