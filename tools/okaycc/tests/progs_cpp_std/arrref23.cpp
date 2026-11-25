// Round 23: arrays through references. A T&& or const T& parameter given a string literal or an
// array keeps the array type ([temp.deduct.call] p2-p3), and the reference's value is the
// array's address (it decays to a pointer when used). Before: a crash and "cannot bind".
#include <cstdio>
template <typename T>
void P(T&& a) { std::printf("%s %d\n", a, (int)sizeof(a)); }
template <typename T>
void Q(const T& a) { std::printf("%s %d\n", a, (int)sizeof(a)); }
template <typename T>
int N(T& a) { return (int)sizeof(a); }
void R(const char (&a)[4]) { std::printf("%s %d %c\n", a, (int)sizeof(a), a[1]); }
int main() {
    P("hey");
    Q("abc");
    R("xyz");
    const char* s = "ptr";
    P(s);
    int arr[5] = {1, 2, 3, 4, 5};
    std::printf("%d\n", N(arr));
    return 0;
}
