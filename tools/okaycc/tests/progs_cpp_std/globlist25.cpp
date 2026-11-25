// Global objects of classes with constructors initialized by a braced list ([dcl.init.list] p3):
// the initializer-list constructor or the constructor the values fit. Round 24 stored the list
// as bytes, so std::vector<int> g = {1, 2, 3} crashed at its first use (silent wrong code).
#include <cstdio>
#include <map>
#include <string>
#include <vector>
struct P {
    int a, b;
    P(int x, int y) : a(x), b(y) {}
};
static std::vector<int> g = {1, 2, 3};
std::vector<std::string> names{"ab", "cd"};
std::map<int, int> m = {{1, 10}, {2, 20}};
P p{4, 5};
static P q = {6, 7};
std::string s{"text"};
int main() {
    g.push_back(4);
    std::printf("%d %d %d\n", (int)g.size(), g[0], g[3]);
    std::printf("%s %s %d %d\n", names[0].c_str(), names[1].c_str(), m[1], m[2]);
    std::printf("%d %d %d %d %s\n", p.a, p.b, q.a, q.b, s.c_str());
}
