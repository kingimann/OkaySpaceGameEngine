// ChaCha20 (RFC 8439 section 2.3/2.4) and HChaCha20 (draft-irtf-cfrg-xchacha).
// UNREVIEWED (see Crypto.hpp). Only adds, xors and rotates on state words: no
// secret-dependent branches or memory indexes.
#include <okay/Crypto/Crypto.hpp>

namespace okay::crypto {
namespace {

inline std::uint32_t Rotl(std::uint32_t x, int c) { return (x << c) | (x >> (32 - c)); }
inline std::uint32_t Load32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}
inline void Store32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v); p[1] = static_cast<std::uint8_t>(v >> 8);
    p[2] = static_cast<std::uint8_t>(v >> 16); p[3] = static_cast<std::uint8_t>(v >> 24);
}

inline void QuarterRound(std::uint32_t& a, std::uint32_t& b, std::uint32_t& c, std::uint32_t& d) {
    a += b; d ^= a; d = Rotl(d, 16);
    c += d; b ^= c; b = Rotl(b, 12);
    a += b; d ^= a; d = Rotl(d, 8);
    c += d; b ^= c; b = Rotl(b, 7);
}

// Twenty rounds (ten column + diagonal double rounds) over the 16-word state.
void Rounds(std::uint32_t x[16]) {
    for (int i = 0; i < 10; ++i) {
        QuarterRound(x[0], x[4], x[8], x[12]);
        QuarterRound(x[1], x[5], x[9], x[13]);
        QuarterRound(x[2], x[6], x[10], x[14]);
        QuarterRound(x[3], x[7], x[11], x[15]);
        QuarterRound(x[0], x[5], x[10], x[15]);
        QuarterRound(x[1], x[6], x[11], x[12]);
        QuarterRound(x[2], x[7], x[8], x[13]);
        QuarterRound(x[3], x[4], x[9], x[14]);
    }
}

constexpr std::uint32_t kSigma[4] = {0x61707865u, 0x3320646eu, 0x79622d32u, 0x6b206574u};   // "expand 32-byte k"

} // namespace

bool ChaCha20Xor(std::uint8_t* out, const std::uint8_t* in, std::size_t n,
                 const std::uint8_t key[32], const std::uint8_t nonce[12], std::uint32_t counter) {
    // RFC 8439 has a 32-bit block counter. Wrapping it would reuse keystream (found by the Wycheproof/sodium_compat counter
    // vectors), so a request that needs more blocks than are left refuses and touches nothing.
    // blocks needed = ceil(n / 64) must be <= 2^32 - counter (compared in 64 bits; also safe for 32-bit size_t).
    const std::uint64_t blocks = (static_cast<std::uint64_t>(n) >> 6) + ((n & 63) ? 1u : 0u);
    if (blocks > (std::uint64_t(1) << 32) - counter) return false;
    std::uint32_t st[16];
    for (int i = 0; i < 4; ++i) st[i] = kSigma[i];
    for (int i = 0; i < 8; ++i) st[4 + i] = Load32(key + 4 * i);
    st[12] = counter;
    for (int i = 0; i < 3; ++i) st[13 + i] = Load32(nonce + 4 * i);

    while (n > 0) {
        std::uint32_t x[16];
        for (int i = 0; i < 16; ++i) x[i] = st[i];
        Rounds(x);
        std::uint8_t block[64];
        for (int i = 0; i < 16; ++i) Store32(block + 4 * i, x[i] + st[i]);
        const std::size_t take = n < 64 ? n : 64;
        for (std::size_t i = 0; i < take; ++i) out[i] = static_cast<std::uint8_t>(in[i] ^ block[i]);
        out += take; in += take; n -= take;
        st[12] += 1;   // cannot wrap into reuse: refused above (the last block's increment may wrap, and is never used)
        MemZero(block, sizeof block);
        MemZero(x, sizeof x);
    }
    MemZero(st, sizeof st);
    return true;
}

void HChaCha20(std::uint8_t out[32], const std::uint8_t key[32], const std::uint8_t in[16]) {
    std::uint32_t x[16];
    for (int i = 0; i < 4; ++i) x[i] = kSigma[i];
    for (int i = 0; i < 8; ++i) x[4 + i] = Load32(key + 4 * i);
    for (int i = 0; i < 4; ++i) x[12 + i] = Load32(in + 4 * i);
    Rounds(x);
    // No final addition; the output is words 0..3 and 12..15.
    for (int i = 0; i < 4; ++i) Store32(out + 4 * i, x[i]);
    for (int i = 0; i < 4; ++i) Store32(out + 16 + 4 * i, x[12 + i]);
    MemZero(x, sizeof x);
}

} // namespace okay::crypto
