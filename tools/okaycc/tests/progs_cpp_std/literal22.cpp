// Literal types in C++: true and false are bool ([lex.bool]) and 'c' is a char ([lex.ccon] p2), so
// overloads on bool and char are chosen. Before round 22 f(false) and g(true) took the int overload.
#include <cstdio>
struct S { void f(int) { std::printf("int\n"); } void f(bool) { std::printf("bool\n"); } void f(char) { std::printf("char\n"); } };
void g(int) { std::printf("g int\n"); }
void g(bool) { std::printf("g bool\n"); }
int main() { S s; s.f(false); s.f(1); s.f('c'); g(true); g(3); bool b = true; s.f(b); return 0; }
