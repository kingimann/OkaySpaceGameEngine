// Round 33: an enumerator of a base class named through the derived class, a class template
// instance or an alias of one ([class.member.lookup], [class.qual]);
// engine/include/okay/Core/Inflate.hpp's Decoder::Role::LitLen.
#include <cstdio>
constexpr int kFastBits = 10;
struct DecoderBase {
    enum class Role { Plain, LitLen, Dist };
    enum : int { kBadCode = -1, kStarved = -2 };
};
struct Extra {
    enum Mode { Off, On = 5 };
};
template <int FastBits>
struct DecoderT : DecoderBase {
    int quick[1 << 3];
    int Build(int n, Role r) { return n * 10 + (int)r; }
};
struct Both : DecoderBase, Extra {
    int z = 0;
};
using Decoder = DecoderT<kFastBits>;
int main() {
    Decoder d;
    int a = d.Build(3, Decoder::Role::LitLen);
    int b = d.Build(4, DecoderT<7>::Role::Dist);
    int c = Decoder::kStarved + DecoderT<3>::kBadCode;
    int e = (int)Both::Role::Dist + Both::On;
    std::printf("%d %d %d %d\n", a, b, c, e);
    return 0;
}
