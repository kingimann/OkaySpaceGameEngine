// std::is_invocable ([meta.rel]) in if constexpr, as engine/include/okay/Core/Jobs.hpp uses it: a
// body taking a range (begin, end) or one index is called the way it takes.
#include <cstddef>
#include <cstdio>
#include <type_traits>
template <class Fn>
void forEach(std::size_t n, Fn fn) {
    if constexpr (std::is_invocable_v<Fn &, std::size_t, std::size_t>) {
        fn((std::size_t)0, n);
    } else {
        for (std::size_t i = 0; i < n; ++i) fn(i);
    }
}
struct Two {
    int operator()(int a, int b) const { return a + b; }
};

int main() {
    long sum = 0;
    forEach(5, [&](std::size_t i) { sum += (long)i; });
    forEach(4, [&](std::size_t b, std::size_t e) { sum += (long)(e - b) * 100; });
    std::printf("%ld\n", sum);
    std::printf("%d %d %d %d\n", (int)std::is_invocable<Two, int, int>::value, (int)std::is_invocable_v<Two, int>,
                (int)std::is_invocable_v<int(int), int>, (int)std::is_invocable_v<int (*)(int), int, int>);
}
