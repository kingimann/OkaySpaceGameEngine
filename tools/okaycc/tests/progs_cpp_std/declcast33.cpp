// Round 33: a closure object called through a pointer made by a cast to decltype of it
// ([dcl.type.decltype], [expr.static.cast], [expr.call]); decltype in a lambda that captures
// nothing names the local, it does not capture it ([expr.prim.lambda.capture] p7);
// engine/include/okay/Core/Jobs.hpp's ParallelFor.
#include <cstddef>
#include <cstdio>
struct RangeFn {
    void* obj;
    void (*call)(void*, std::size_t, std::size_t);
};
template <class F>
void ParallelFor(std::size_t n, F&& fn) {
    auto perChunk = [&fn](std::size_t b, std::size_t e) {
        for (std::size_t i = b; i < e; ++i) fn(i);
    };
    RangeFn r;
    r.obj = &perChunk;
    r.call = [](void* o, std::size_t b, std::size_t e) { (*static_cast<decltype(perChunk)*>(o))(b, e); };
    r.call(r.obj, 0, n / 2);
    r.call(r.obj, n / 2, n);
}
int main() {
    int sum = 0;
    ParallelFor(7, [&](std::size_t i) { sum += (int)i * 2; });
    auto l = [](int a) { return a + 1; };
    void* v = &l;
    auto q = static_cast<decltype(l)*>(v);
    using L = decltype(l);
    std::printf("%d %d %d\n", sum, (*q)(4), (*static_cast<L*>(v))(5));
    return 0;
}
