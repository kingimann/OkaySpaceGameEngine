// Round 28: std::string + char ([string.op.plus]; Core/Profiler.hpp:232).
#include <string>
#include <cstdio>
int main() {
  std::string out;
  int fi = 3;
  out += std::to_string(fi) + ',';
  out += 'x' + std::string("y");
  std::string a = std::string("q") + 'c';
  printf("%s %s\n", out.c_str(), a.c_str());
}
