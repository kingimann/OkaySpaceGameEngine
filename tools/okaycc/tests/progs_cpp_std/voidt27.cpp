// void_t detection in class template partial specializations ([temp.spec.partial.match] p2, [temp.deduct]
// p8): a pattern whose decltype has no type for the arguments does not match (Core/UndoCommands.hpp:26),
// and a base named through its namespace (std::true_type) before its class template has an instance.
#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
template <class T, class = void> struct HasEq : std::false_type {};
template <class T>
struct HasEq<T, std::void_t<decltype(std::declval<const T&>() == std::declval<const T&>())>> : std::true_type {};
template <class T, class = void> struct HasSize : std::false_type {};
template <class T> struct HasSize<T, std::void_t<decltype(std::declval<T&>().size())>> : std::true_type {};
struct NoEq { int a; };
struct WithEq { int a; bool operator==(const WithEq &o) const { return a == o.a; } };
template <class T> bool Same(const T& a, const T& b) {
    if constexpr (HasEq<T>::value) return a == b; else { (void)a; (void)b; return false; }
}
int main() {
    std::printf("%d %d %d %d\n", (int)HasEq<int>::value, (int)HasEq<NoEq>::value, (int)HasEq<WithEq>::value,
                (int)HasEq<std::string>::value);
    std::printf("%d %d %d\n", (int)HasSize<int>::value, (int)HasSize<std::vector<int>>::value, (int)HasSize<std::string>::value);
    std::printf("%d %d %d\n", (int)Same(3, 3), (int)Same(NoEq{1}, NoEq{1}), (int)Same(WithEq{2}, WithEq{2}));
}
