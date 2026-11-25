// Round 24: primary class templates with a parameter pack ([temp.variadic], [temp.class]): a body of
// its own (sizeof...), a declaration given meaning by partial specializations (a recursive list),
// a fixed parameter before the pack, and a function template deduced from such a class.
#include <cstdio>
template <class... T> struct Count { static const int n = sizeof...(T); };
template <class R, class... A> struct Sig { static const int args = sizeof...(A); R last; };
template <class... T> struct List;
template <> struct List<> { int sum() const { return 0; } };
template <class H, class... T> struct List<H, T...> {
    H head;
    List<T...> tail;
    int sum() const { return (int)head + tail.sum(); }
};
template <class... T> int lengthOf(const List<T...> &) { return (int)sizeof...(T); }
int main() {
    printf("%d %d %d\n", Count<>::n, Count<int>::n, Count<int, char, double>::n);
    List<int, char, long> l;
    l.head = 1;
    l.tail.head = 2;
    l.tail.tail.head = 3;
    printf("%d %d\n", l.sum(), lengthOf(l));
    Sig<long, int, int> s;
    s.last = 40;
    printf("%d %ld\n", Sig<int>::args + s.args, s.last);
}
