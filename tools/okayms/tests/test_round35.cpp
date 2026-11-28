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

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    Tally tally;
    r35_posix_headers(tally);
    r35_preprocessor_library(tally);
    r35_thread_local(tally);
    r35_wrong_code(tally);
    return tally.finish(4);
}
