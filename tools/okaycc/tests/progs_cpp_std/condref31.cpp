// T &r = c ? x : v[i]; with two lvalues of one scalar type ([expr.cond] p4; Render/FbxLegacy.hpp).
#include <cstdint>
#include <cstdio>
#include <vector>
struct Elem { std::uint64_t links = 0; };
int main() {
    std::vector<std::uint32_t> lastChild = {7, 8, 9};
    std::uint32_t rootLast = 1;
    const std::uint32_t kNone = 0xFFFFFFFFu;
    for (std::uint32_t parent : {kNone, 1u}) {
        std::uint32_t& last = parent == kNone ? rootLast : lastChild[parent];
        last += 10;
    }
    std::vector<Elem> elems(2);
    Elem root;
    for (int k = 0; k < 2; ++k) {
        Elem& p = k == 0 ? root : elems[1];
        p.links = (p.links & 0xFFFFFFFFull) | ((std::uint64_t)(k + 3) << 32);
    }
    std::printf("%u %u %llu %llu\n", rootLast, lastChild[1], (unsigned long long)(root.links >> 32),
                (unsigned long long)(elems[1].links >> 32));
    return 0;
}
