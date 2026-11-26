// Function template instances made while the file is checked (round 31): chains of deduced
// return types, enable_if pairs, member templates, lambdas and static locals inside instances.
#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

template <class T>
auto twice(T v) { return v + v; }
template <class T>
auto quad(T v) { return twice(twice(v)); }
template <class T, class U>
auto sum2(T a, U b) { return quad(a) + b; }

template <class T>
typename std::enable_if<std::is_integral<T>::value, int>::type kind(T) { return 1; }
template <class T>
typename std::enable_if<!std::is_integral<T>::value, int>::type kind(T) { return 2; }

template <class F, class T>
auto applyTo(F f, T v) { return f(v); }

template <class T>
int counter(T) {
    static int n = 0;
    return ++n;
}
template <class T>
std::string named(const T& v) {
    static std::string last;
    last = std::to_string(v);
    return last + "!";
}

struct Box {
    std::vector<int> v;
    template <class T>
    auto add(T x) { v.push_back((int)x); return v.size(); }
    template <class T>
    T first() const { return (T)v[0]; }
};

template <class T>
T sumAll(T a) { return a; }
template <class T, class... R>
T sumAll(T a, R... r) { return a + sumAll(r...); }

int main() {
    std::printf("%d %.1f %ld\n", quad(3), quad(1.25), sum2(2L, 5));
    std::printf("%d %d %d\n", kind(3), kind(2.5), kind('c'));
    std::printf("%d %.2f\n", applyTo([](int x) { return x * 7; }, 6), applyTo([](double d) { return d / 4; }, 3.0));
    counter(1);
    counter(2);
    std::printf("%d %d %s\n", counter(3), counter(1.5), named(42).c_str());
    Box b;
    b.add(4);
    b.add(2.9);
    std::printf("%zu %d %.1f\n", b.add('a'), b.first<int>(), b.first<double>());
    std::printf("%d %.1f\n", sumAll(1, 2, 3, 4), sumAll(0.5, 1.5, 2.0));
    return 0;
}
