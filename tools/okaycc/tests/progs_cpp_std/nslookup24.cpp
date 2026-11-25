// Round 24: names of a namespace used without qualification inside its class and function
// templates (lib::Box<int>::f reads lib::npos; lib::h calls lib::helper): instances are looked up
// in their template's namespace ([temp.res], [basic.lookup.unqual]).
#include <cstdio>
namespace lib {
static const unsigned long npos = 99;
inline int helper() { return 5; }
template <class T> struct Box {
    T v;
    unsigned long f() const { return v < 0 ? npos : (unsigned long)v; }
    int g() const { return helper(); }
};
template <class T> int h(T t) { return helper() + (int)t; }
}
int main() { lib::Box<int> b; b.v = -1; printf("%lu %d %d\n", b.f(), b.g(), lib::h(2)); }
