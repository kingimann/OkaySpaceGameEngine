// Round 28: a conditional whose branches are std::string objects (or a string and a C string) as
// an operand of + ([expr.cond] p4-p6), string::append(n, c) and string::erase(first, last)
// (Core/Backup.cpp:202, Core/CommandLine.cpp:55, Core/Console.cpp:62, Core/Profiler.hpp:262,
// Core/DisplayModes.cpp:175).
#include <string>
#include <cstdio>
#include <algorithm>
#include <cctype>
struct Opt { std::string name; std::string usage; };
static std::string Fail(const std::string& s) { return "E:" + s; }
int main() {
  Opt opt{"out", "u"};
  std::string c = "";
  int n = 2;
  std::string a = std::string("x") + (c.empty() ? "clip" : c);
  std::string b = "k" + (n ? "-" + std::to_string(n) : std::string()) + ".bak";
  std::string d = Fail("--" + opt.name + " needs a value" + "!");
  std::string e = a + (opt.usage.empty() ? "" : "  usage: " + opt.usage) + ";";
  std::string out; out.append((std::size_t)2 * 2 + 2, ' '); out += "|";
  std::string m = "a b\tc";
  m.erase(std::remove_if(m.begin(), m.end(), [](char ch) { return std::isspace((unsigned char)ch) != 0; }), m.end());
  std::string s = "a", t = "b";
  std::string f = (n ? s : t) + "k" + (n > 5 ? s + "1" : t + "2");
  std::string g = "k" + (n ? s : std::string()) + (n ? "-" + s : t);
  printf("%s %s
", f.c_str(), g.c_str());
  printf("[%s] [%s] [%s] [%s] [%s] [%s]\n", a.c_str(), b.c_str(), d.c_str(), e.c_str(), out.c_str(), m.c_str());
}
