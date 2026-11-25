// Substitution failure is not an error ([temp.deduct] p8): a function template whose return type
// or template parameter cannot be formed for the deduced arguments (std::enable_if<false>::type)
// leaves the overload set, and the next template is used. Also -> decltype(f()) with a lambda
// taken by a forwarding reference (Core/SaveData.hpp: Locked(F &&f) -> decltype(f())).
#include <cstdio>
#include <type_traits>
template <class T>
typename std::enable_if<std::is_integral<T>::value, const char *>::type kind(T) { return "integral"; }
template <class T>
typename std::enable_if<std::is_floating_point<T>::value, const char *>::type kind(T) { return "floating"; }
template <class T, class = typename std::enable_if<std::is_pointer<T>::value>::type>
int deref(T p) { return *p; }
template <class T, class = typename std::enable_if<!std::is_pointer<T>::value>::type, class = void>
int deref(T v) { return (int)v + 100; }
template <class T>
std::enable_if_t<(sizeof(T) > 4), int> wide(T) { return 8; }
template <class T>
std::enable_if_t<(sizeof(T) <= 4), int> wide(T) { return 4; }
struct Guard {
    template <class F> static auto Locked(F &&f) -> decltype(f()) { return f(); }
};
template <class T> auto twice(T x) -> decltype(x * 2) { return x * 2; }
int main() {
    int x = 5;
    const char *a = kind(3);
    const char *b = kind(2.5);
    std::printf("%s %s\n", a, b);
    std::printf("%d %d\n", deref(&x), deref(7));
    std::printf("%d %d\n", wide(1), wide(1.0));
    int r = Guard::Locked([&] { return x + 1; });
    std::printf("%d %g\n", r, twice(1.5));
}
