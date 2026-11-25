// Round 24: array lengths that are constant expressions with sizeof (a class member double
// s[sizeof(P) / 4 + 1] had no elements; a local one was "not a constant").
#include <cstdio>
struct P { int a, b, c; };
struct Q { double s[sizeof(P) / 4 + 1]; int t[sizeof(int) * 2]; };
int main() { int loc[sizeof(P)]; printf("%d %d\n", (int)sizeof(Q), (int)sizeof(loc)); }
