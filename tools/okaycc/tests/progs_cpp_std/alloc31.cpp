// std::vector<T, Alloc> (the allocator is accepted and not used) and std::allocator
// ([default.allocator]; Render/FbxReader.hpp's ByteBuffer).
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>
template <class T> struct UninitAllocator : std::allocator<T> {
    template <class U> struct rebind { using other = UninitAllocator<U>; };
    UninitAllocator() noexcept = default;
    template <class U> UninitAllocator(const UninitAllocator<U>&) noexcept {}
    template <class U> void construct(U* p) noexcept { ::new ((void*)p) U; }
    template <class U, class... A> void construct(U* p, A&&... a) { ::new ((void*)p) U(static_cast<A&&>(a)...); }
};
using ByteBuffer = std::vector<std::uint8_t, UninitAllocator<std::uint8_t>>;
using NumberBuffer = std::vector<double, UninitAllocator<double>>;
static int sum(const ByteBuffer& b) { int s = 0; for (auto c : b) s += c; return s; }
int main() {
    ByteBuffer b;
    b.resize(4);
    for (int i = 0; i < 4; ++i) b[i] = (std::uint8_t)(i * 3);
    b.push_back(10);
    NumberBuffer n(3);
    n[1] = 2.5;
    std::vector<int, std::allocator<int>> v{1, 2, 3};
    std::allocator<long> al;
    long* p = al.allocate(2);
    p[0] = 5; p[1] = 6;
    std::printf("%d %zu %.1f %zu %d %ld\n", sum(b), b.size(), n[1], n.size(), v[2], p[0] + p[1]);
    al.deallocate(p, 2);
    return 0;
}
