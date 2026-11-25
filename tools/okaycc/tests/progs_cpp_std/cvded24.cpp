// Round 24: const T& given a const object deduces T without the const ([temp.deduct.call] p4);
// round 23 kept it (f(a) printed 1 for a const int, g++ 0).
#include <cstdio>
template <class T> struct Is { static const int c = 0; };
template <class T> struct Is<const T> { static const int c = 1; };
template <class V> int f(const V &) { return Is<V>::c; }
template <class V> int g(V &) { return Is<V>::c; }
int main() { const int a = 1; int b = 2; printf("%d %d %d %d\n", f(a), f(b), g(a), g(b)); }
