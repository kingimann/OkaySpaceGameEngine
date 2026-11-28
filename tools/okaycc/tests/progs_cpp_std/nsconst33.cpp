// Round 33: a namespace-scope constant whose initializer is a constant expression, not only a
// literal, used in a later enumerator ([expr.const], [dcl.enum] p2);
// engine/src/JavaScript/JsInternal.hpp's kTATypes = (int)TAType::Count.
#include <cstdint>
#include <cstdio>
namespace js {
enum class TAType : std::uint8_t { I8, U8, U8C, I16, Count };
constexpr int kTATypes = (int)TAType::Count;
constexpr int kTwice = static_cast<int>(TAType::U8C) * 2 + 1;
const long kBig = 1L << 40;
constexpr bool kOn = kTATypes > 3;
enum Slot : int {
    kCtor,
    kProto,
    kProtos,
    kCtors = kProtos + kTATypes,
    kSlots = kCtors + kTATypes,
    kAfter = kSlots + kTwice + (kOn ? 100 : 0),
};
}  // namespace js
int table[js::kSlots];
int main() {
    std::printf("%d %d %d %d %ld %d\n", js::kCtors, js::kSlots, js::kAfter, (int)sizeof(table) / 4, js::kBig >> 38,
                (int)js::kOn);
    return 0;
}
