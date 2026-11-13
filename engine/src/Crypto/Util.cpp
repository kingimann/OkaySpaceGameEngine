// okay::crypto utilities: MemZero and ConstantTimeEqual. UNREVIEWED (see Crypto.hpp).
#include <okay/Crypto/Crypto.hpp>
#include "CtAnnotate.hpp"

namespace okay::crypto {

void MemZero(void* p, std::size_t n) {
    volatile std::uint8_t* v = static_cast<volatile std::uint8_t*>(p);
    for (std::size_t i = 0; i < n; ++i) v[i] = 0;
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__("" : : "r"(p) : "memory");   // the compiler may not assume the stores are dead
#endif
}

bool ConstantTimeEqual(const void* a, const void* b, std::size_t n) {
    const std::uint8_t* x = static_cast<const std::uint8_t*>(a);
    const std::uint8_t* y = static_cast<const std::uint8_t*>(b);
    std::uint32_t d = 0;
    for (std::size_t i = 0; i < n; ++i) d |= static_cast<std::uint32_t>(x[i] ^ y[i]);
    // d == 0 -> 1, otherwise 0, without a branch on d.
    const bool equal = ((d - 1u) >> 31) == 1u;
    OKAY_CT_PUBLIC(equal);   // by design: whether a tag/signature matched is public
    return equal;
}

} // namespace okay::crypto
