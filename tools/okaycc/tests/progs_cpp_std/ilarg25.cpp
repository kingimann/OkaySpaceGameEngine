// A braced list for a std::initializer_list parameter of a member or free function, beside an
// overload taking a scalar ([dcl.init.list] p5, [over.ics.list]).
#include <initializer_list>
#include <cstdio>
struct V {
    int s = 0;
    void add(int x) { s += x; }
    void add(std::initializer_list<int> il) { for (int x : il) s += x; }
};
void tot(std::initializer_list<int> il) { int s = 0; for (int x : il) s += x; std::printf("%d\n", s); }
int main() { V v; v.add({5, 6}); v.add(1); std::printf("%d\n", v.s); tot({1, 2, 3}); }
