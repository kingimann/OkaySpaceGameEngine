// dynamic_cast with multiple inheritance ([expr.dynamic.cast] p7-p9; Itanium C++ ABI 2.5.2): down from
// and across a base after the primary one moves the address (round 26 returned it unmoved: wrong
// code), to void * gives the whole object, to a reference; failed casts give null.
#include <cstdio>
struct A { virtual ~A() {} int a = 1; };
struct B { virtual ~B() {} virtual int f() { return 2; } int b = 2; };
struct C : A, B { int f() override { return 30; } int c = 3; };
struct D : C { int d = 4; };
struct E : A { int e = 5; };
int main() {
    D d;
    B *pb = &d;
    A *pa = &d;
    C *pc = dynamic_cast<C *>(pb);        // down from the second base
    D *pd = dynamic_cast<D *>(pb);
    A *across = dynamic_cast<A *>(pb);    // across: B -> A
    B *back = dynamic_cast<B *>(pa);      // across: A -> B
    E e;
    A *pe = &e;
    B *none = dynamic_cast<B *>(pe);
    C *none2 = dynamic_cast<C *>(pe);
    std::printf("%d %d %d %d\n", pc ? pc->c : -1, pd ? pd->d : -1, across ? across->a : -1, back ? back->f() : -1);
    std::printf("%d %d %d %d\n", none == nullptr, none2 == nullptr, pc == static_cast<C *>(&d), back == static_cast<B *>(&d));
    B &rb = d;
    C &rc = dynamic_cast<C &>(rb);
    std::printf("%d %d\n", rc.c, (void *)dynamic_cast<void *>(pb) == (void *)&d);
}
