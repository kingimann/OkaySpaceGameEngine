// Round 28: the implicit copy constructor takes a const reference ([class.copy.ctor] p7), so a const
// object of a class with constructors is copied (Net/ServerQuery.cpp:176, a const time_point).
#include <string>
#include <chrono>
#include <cstdio>
struct S { std::string s; int n = 1; };
int main() {
  const S c{"x", 2}; S d; d = c; S e(c);
  using Clock = std::chrono::steady_clock;
  const Clock::time_point start; Clock::time_point last = start; last = start;
  printf("%s %d %s %d %d\n", d.s.c_str(), d.n, e.s.c_str(), e.n, (int)(last == start));
}
