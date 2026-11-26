// A base named by a typedef in a namespace (struct X : std::true_type), also in templates and in
// partial specializations, before the class the typedef names was made ([class.derived] p2).
#include <cstdio>
#include <type_traits>
template <class T, class = void> struct A : std::false_type {};
template <class T> struct A<T, void> : std::true_type {};
template <class T, class U> struct B : std::false_type {};
template <class T> struct B<T, T> : std::true_type {};
struct X : std::true_type {};
int main() {
    std::printf("%d\n", (int)X::value); std::printf("%d %d %d\n", (int)A<int>::value, (int)B<int, int>::value, (int)B<int, char>::value); }
