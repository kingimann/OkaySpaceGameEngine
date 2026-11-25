// Round 24: a function template with an explicit value argument and the rest deduced
// ([temp.arg.explicit] p6), whose return type depends on both (typename At<I, T>::type).
#include <cstdio>
template <unsigned long I, class T> struct At;
template <class T> struct At<0, T> { typedef T type; };
template <class T> struct At<1, T> { typedef long type; };
template <unsigned long I, class T> struct Elem { typedef typename At<I, T>::type type; };
template <unsigned long I, class T> typename At<I, T>::type pick(T v) { return (typename At<I, T>::type)v; }
int main() {
    Elem<0, int>::type a = 3;
    Elem<1, int>::type b = 4;
    printf("%d %ld %d\n", a, b, (int)sizeof(pick<1>(2)));
}
