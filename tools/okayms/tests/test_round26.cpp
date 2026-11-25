// Round 26: return type deduction (auto, decltype(auto), trailing return types), and the headers and compiler fixes
// the engine files stopped at after Round 25. Each program returns 0 when all of its checks hold, or the number of the
// first check that failed; it is built by okay-cl at /O0, /O1 and /O2 and run under Wine, and MinGW g++ builds the
// same text with its own headers and must agree (program_check.hpp). Written from ISO/IEC 14882 [dcl.spec.auto],
// [dcl.type.decltype], [dcl.fct]. No code from other compilers or libraries.

#include "program_check.hpp"

#include <string>

namespace {
using okayms_test::Tally;
const okayms_test::ProgramRun kRun{"r26", {}};
}  // namespace

// The bump allocator of program_check.hpp (no C runtime is linked on the okay-ms side).
static const char* kAlloc = okayms_test::bump_alloc();

// auto and decltype(auto) return types: free functions, members, member templates, function templates, a trailing
// return type, auto&, a class result returned in memory, a method that calls an auto method.
static void r26_auto_return(Tally& tally) {
    std::printf("r26_auto_return\n");
    okayms_test::run_both(kRun, tally, "auto_return", std::string(kAlloc) + R"OKMS(
#include <string>
#include <utility>
auto twice(int x) { return x * 2; }
auto wide(int x) -> long long { return (long long)x << 33; }
auto none(int* p) { *p = 9; }
auto name() { return std::string("abc"); }
struct S {
  int v;
  std::string s;
  auto sooner() const { return v; }
  auto later() const { return sooner() + 1; }
  auto& ref() { return v; }
  decltype(auto) dref() { return ref(); }
  decltype(auto) dval() { return v + 0; }
  template <class T> auto add(T t) const { return t + v; }
  template <class... A> decltype(auto) app(A&&... a) { return s.append(std::forward<A>(a)...); }
};
template <class T> auto ident(T a) { return a; }
template <class T> auto ptr(T* p) { return p; }
int entry(void) {
  CK(1, twice(4) == 8);
  CK(2, wide(1) == 8589934592LL);
  int k = 0;
  none(&k);
  CK(3, k == 9);
  std::string n = name();
  CK(4, n == "abc" && n.size() == 3);
  S s;
  s.v = 3;
  CK(5, s.sooner() == 3 && s.later() == 4);
  s.ref() = 7;
  CK(6, s.v == 7);
  s.dref() = 11;
  CK(7, s.v == 11);
  int dv = s.dval();
  dv = 1;
  CK(8, s.v == 11 && dv == 1);
  CK(9, s.add(2) == 13 && s.add(1.5) > 12.4 && s.add(1.5) < 12.6);
  s.app("xy").append("z");
  CK(10, s.s == "xyz");
  CK(11, ident(5) == 5 && ident('c') == 'c' && ident(std::string("q")) == "q");
  int arr[2] = {1, 2};
  CK(12, ptr(arr + 1) == &arr[1]);
  CK(13, sizeof(twice(1)) == 4 && sizeof(wide(1)) == 8);
  return 0;
}
)OKMS", 0);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    Tally tally;
    r26_auto_return(tally);
    return tally.finish(1);
}
