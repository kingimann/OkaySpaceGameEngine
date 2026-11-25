// Non-member operator templates ([over.match.oper] p3 with [temp.deduct.call]), and function
// templates whose other parameters name no template parameter: such a parameter must take its
// argument ([temp.deduct.call] p4). Round 24 took the first template that deduced: which(x, 5)
// stopped with "cannot assign argument".
#include <cstdio>
namespace q {
struct N {
    explicit N(int) {}
};
static const N none(0);
template <typename T>
struct O {
    bool h = false;
    T v{};
};
template <typename T>
bool operator==(const O<T> &a, N) {
    return !a.h;
}
template <typename T>
bool operator==(const O<T> &a, const O<T> &b) {
    return a.h == b.h && (!a.h || a.v == b.v);
}
template <typename T>
bool operator==(const O<T> &a, const T &v) {
    return a.h && a.v == v;
}
template <typename T>
O<T> operator+(const O<T> &a, const O<T> &b) {
    O<T> r;
    r.h = a.h && b.h;
    r.v = a.v + b.v;
    return r;
}
}  // namespace q
template <typename T>
int which(const T &, const char *) {
    return 1;
}
template <typename T>
int which(const T &, int) {
    return 2;
}
int main() {
    q::O<int> o, p;
    p.h = true;
    p.v = 7;
    std::printf("%d %d %d %d %d\n", (int)(o == q::none), (int)(p == q::none), (int)(o == o), (int)(p == 7), (int)(p == o));
    q::O<int> s = p + p;
    std::printf("%d %d\n", s.v, (int)s.h);
    std::printf("%d %d\n", which(o, 5), which(o, "x"));
}
