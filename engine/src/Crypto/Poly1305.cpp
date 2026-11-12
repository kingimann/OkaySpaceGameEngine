// Poly1305 (RFC 8439 section 2.5). UNREVIEWED (see Crypto.hpp).
// Arithmetic mod 2^130 - 5 on five 26-bit limbs; every product fits in 64 bits.
// Only adds, multiplies, shifts and masks: no secret-dependent branches or indexes.
#include <okay/Crypto/Crypto.hpp>

namespace okay::crypto {
namespace {

constexpr std::uint32_t kMask26 = 0x3ffffffu;

inline std::uint32_t Load32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}
inline void Store32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v); p[1] = static_cast<std::uint8_t>(v >> 8);
    p[2] = static_cast<std::uint8_t>(v >> 16); p[3] = static_cast<std::uint8_t>(v >> 24);
}

// Split 16 little-endian bytes into five 26-bit limbs (limb 4 holds bits 104..127).
inline void Split(std::uint32_t l[5], const std::uint8_t* p) {
    l[0] = Load32(p) & kMask26;
    l[1] = (Load32(p + 3) >> 2) & kMask26;
    l[2] = (Load32(p + 6) >> 4) & kMask26;
    l[3] = (Load32(p + 9) >> 6) & kMask26;
    l[4] = Load32(p + 12) >> 8;
}

} // namespace

Poly1305::Poly1305(const std::uint8_t key[32]) : m_fill(0) {
    std::uint8_t r[16];
    for (int i = 0; i < 16; ++i) r[i] = key[i];
    // Clamp r (RFC 8439 section 2.5).
    r[3] &= 15; r[7] &= 15; r[11] &= 15; r[15] &= 15;
    r[4] &= 252; r[8] &= 252; r[12] &= 252;
    Split(m_r, r);
    for (int i = 0; i < 5; ++i) m_h[i] = 0;
    for (int i = 0; i < 4; ++i) m_pad[i] = Load32(key + 16 + 4 * i);
    for (int i = 0; i < 16; ++i) m_buf[i] = 0;
    MemZero(r, sizeof r);
}

Poly1305::~Poly1305() {
    MemZero(m_r, sizeof m_r); MemZero(m_h, sizeof m_h); MemZero(m_pad, sizeof m_pad); MemZero(m_buf, sizeof m_buf);
}

// Process whole 16-byte blocks; `hibit` is the 2^128 bit (1<<24 in limb 4) for full blocks.
void Poly1305::Blocks(const std::uint8_t* m, std::size_t n, std::uint32_t hibit) {
    const std::uint64_t r0 = m_r[0], r1 = m_r[1], r2 = m_r[2], r3 = m_r[3], r4 = m_r[4];
    const std::uint64_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
    std::uint64_t h0 = m_h[0], h1 = m_h[1], h2 = m_h[2], h3 = m_h[3], h4 = m_h[4];
    while (n >= 16) {
        std::uint32_t t[5];
        Split(t, m);
        h0 += t[0]; h1 += t[1]; h2 += t[2]; h3 += t[3]; h4 += t[4] + hibit;

        std::uint64_t d0 = h0 * r0 + h1 * s4 + h2 * s3 + h3 * s2 + h4 * s1;
        std::uint64_t d1 = h0 * r1 + h1 * r0 + h2 * s4 + h3 * s3 + h4 * s2;
        std::uint64_t d2 = h0 * r2 + h1 * r1 + h2 * r0 + h3 * s4 + h4 * s3;
        std::uint64_t d3 = h0 * r3 + h1 * r2 + h2 * r1 + h3 * r0 + h4 * s4;
        std::uint64_t d4 = h0 * r4 + h1 * r3 + h2 * r2 + h3 * r1 + h4 * r0;

        std::uint64_t c = d0 >> 26; h0 = d0 & kMask26; d1 += c;
        c = d1 >> 26; h1 = d1 & kMask26; d2 += c;
        c = d2 >> 26; h2 = d2 & kMask26; d3 += c;
        c = d3 >> 26; h3 = d3 & kMask26; d4 += c;
        c = d4 >> 26; h4 = d4 & kMask26; h0 += c * 5;
        c = h0 >> 26; h0 &= kMask26; h1 += c;
        m += 16; n -= 16;
    }
    m_h[0] = static_cast<std::uint32_t>(h0); m_h[1] = static_cast<std::uint32_t>(h1);
    m_h[2] = static_cast<std::uint32_t>(h2); m_h[3] = static_cast<std::uint32_t>(h3);
    m_h[4] = static_cast<std::uint32_t>(h4);
}

void Poly1305::Update(const std::uint8_t* m, std::size_t n) {
    if (m_fill > 0) {
        while (n > 0 && m_fill < 16) { m_buf[m_fill++] = *m++; --n; }
        if (m_fill < 16) return;
        Blocks(m_buf, 16, 1u << 24);
        m_fill = 0;
    }
    const std::size_t whole = n & ~static_cast<std::size_t>(15);
    if (whole) { Blocks(m, whole, 1u << 24); m += whole; n -= whole; }
    while (n > 0) { m_buf[m_fill++] = *m++; --n; }
}

void Poly1305::Final(std::uint8_t tag[16]) {
    if (m_fill > 0) {
        m_buf[m_fill] = 1;                               // the padding bit sits inside the block,
        for (std::size_t i = m_fill + 1; i < 16; ++i) m_buf[i] = 0;
        Blocks(m_buf, 16, 0);                            // so no 2^128 bit for the short block
        m_fill = 0;
    }
    std::uint32_t h0 = m_h[0], h1 = m_h[1], h2 = m_h[2], h3 = m_h[3], h4 = m_h[4];
    // Fully carry.
    std::uint32_t c = h1 >> 26; h1 &= kMask26; h2 += c;
    c = h2 >> 26; h2 &= kMask26; h3 += c;
    c = h3 >> 26; h3 &= kMask26; h4 += c;
    c = h4 >> 26; h4 &= kMask26; h0 += c * 5;
    c = h0 >> 26; h0 &= kMask26; h1 += c;
    c = h1 >> 26; h1 &= kMask26; h2 += c;
    c = h2 >> 26; h2 &= kMask26; h3 += c;
    c = h3 >> 26; h3 &= kMask26; h4 += c;
    c = h4 >> 26; h4 &= kMask26; h0 += c * 5;
    c = h0 >> 26; h0 &= kMask26; h1 += c;
    // g = h + 5 - 2^130; keep g if h >= 2^130 - 5 (g did not go negative), else h.
    std::uint32_t g0 = h0 + 5; c = g0 >> 26; g0 &= kMask26;
    std::uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= kMask26;
    std::uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= kMask26;
    std::uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= kMask26;
    std::uint32_t g4 = h4 + c - (1u << 26);
    const std::uint32_t keepG = (g4 >> 31) - 1u;         // all ones when g4 did not borrow
    h0 = (h0 & ~keepG) | (g0 & keepG);
    h1 = (h1 & ~keepG) | (g1 & keepG);
    h2 = (h2 & ~keepG) | (g2 & keepG);
    h3 = (h3 & ~keepG) | (g3 & keepG);
    h4 = (h4 & ~keepG) | (g4 & keepG);
    // h mod 2^128, as four 32-bit words, plus the pad, mod 2^128.
    const std::uint32_t w0 = h0 | (h1 << 26);
    const std::uint32_t w1 = (h1 >> 6) | (h2 << 20);
    const std::uint32_t w2 = (h2 >> 12) | (h3 << 14);
    const std::uint32_t w3 = (h3 >> 18) | (h4 << 8);
    std::uint64_t f = static_cast<std::uint64_t>(w0) + m_pad[0];
    Store32(tag, static_cast<std::uint32_t>(f));
    f = static_cast<std::uint64_t>(w1) + m_pad[1] + (f >> 32);
    Store32(tag + 4, static_cast<std::uint32_t>(f));
    f = static_cast<std::uint64_t>(w2) + m_pad[2] + (f >> 32);
    Store32(tag + 8, static_cast<std::uint32_t>(f));
    f = static_cast<std::uint64_t>(w3) + m_pad[3] + (f >> 32);
    Store32(tag + 12, static_cast<std::uint32_t>(f));
    MemZero(m_r, sizeof m_r); MemZero(m_h, sizeof m_h); MemZero(m_pad, sizeof m_pad); MemZero(m_buf, sizeof m_buf);
}

void Poly1305Mac(std::uint8_t tag[16], const std::uint8_t* m, std::size_t n, const std::uint8_t key[32]) {
    Poly1305 p(key);
    p.Update(m, n);
    p.Final(tag);
}

} // namespace okay::crypto
