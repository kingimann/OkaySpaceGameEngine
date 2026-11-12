// BLAKE2b (RFC 7693), sequential mode. UNREVIEWED (see Crypto.hpp).
// The data-dependent work is adds, xors and rotates; the message schedule indexes
// with the public round constants only.
#include <okay/Crypto/Crypto.hpp>

namespace okay::crypto {
namespace {

constexpr std::uint64_t kIv[8] = {
    0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
    0x510e527fade682d1ull, 0x9b05688c2b3e6c1full, 0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull};

constexpr std::uint8_t kSigma[12][16] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3},
    {11, 8, 12, 0, 5, 2, 15, 13, 10, 14, 3, 6, 7, 1, 9, 4},
    {7, 9, 3, 1, 13, 12, 11, 14, 2, 6, 5, 10, 4, 0, 15, 8},
    {9, 0, 5, 7, 2, 4, 10, 15, 14, 1, 11, 12, 6, 8, 3, 13},
    {2, 12, 6, 10, 0, 11, 8, 3, 4, 13, 7, 5, 15, 14, 1, 9},
    {12, 5, 1, 15, 14, 13, 4, 10, 0, 7, 6, 3, 9, 2, 8, 11},
    {13, 11, 7, 14, 12, 1, 3, 9, 5, 0, 15, 4, 8, 6, 2, 10},
    {6, 15, 14, 9, 11, 3, 0, 8, 12, 2, 13, 7, 1, 4, 10, 5},
    {10, 2, 8, 4, 7, 6, 1, 5, 15, 11, 9, 14, 3, 12, 13, 0},
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
    {14, 10, 4, 8, 9, 15, 13, 6, 1, 12, 0, 2, 11, 7, 5, 3}};

inline std::uint64_t Rotr(std::uint64_t x, int c) { return (x >> c) | (x << (64 - c)); }
inline std::uint64_t Load64(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
    return v;
}

inline void G(std::uint64_t v[16], int a, int b, int c, int d, std::uint64_t x, std::uint64_t y) {
    v[a] = v[a] + v[b] + x; v[d] = Rotr(v[d] ^ v[a], 32);
    v[c] = v[c] + v[d];     v[b] = Rotr(v[b] ^ v[c], 24);
    v[a] = v[a] + v[b] + y; v[d] = Rotr(v[d] ^ v[a], 16);
    v[c] = v[c] + v[d];     v[b] = Rotr(v[b] ^ v[c], 63);
}

} // namespace

Blake2b::Blake2b(std::size_t outLen, const std::uint8_t* key, std::size_t keyLen) : m_fill(0), m_outLen(outLen) {
    if (m_outLen < 1) m_outLen = 1;
    if (m_outLen > 64) m_outLen = 64;
    if (keyLen > 64) keyLen = 64;
    for (int i = 0; i < 8; ++i) m_h[i] = kIv[i];
    m_h[0] ^= 0x01010000ull ^ (static_cast<std::uint64_t>(keyLen) << 8) ^ static_cast<std::uint64_t>(m_outLen);
    m_t[0] = m_t[1] = 0;
    for (int i = 0; i < 128; ++i) m_buf[i] = 0;
    if (keyLen > 0) {                                    // the key is the first block, zero padded
        for (std::size_t i = 0; i < keyLen; ++i) m_buf[i] = key[i];
        m_fill = 128;
    }
}

Blake2b::~Blake2b() {
    MemZero(m_h, sizeof m_h); MemZero(m_buf, sizeof m_buf);
}

void Blake2b::Compress(bool last) {
    std::uint64_t m[16], v[16];
    for (int i = 0; i < 16; ++i) m[i] = Load64(m_buf + 8 * i);
    for (int i = 0; i < 8; ++i) { v[i] = m_h[i]; v[i + 8] = kIv[i]; }
    v[12] ^= m_t[0];
    v[13] ^= m_t[1];
    if (last) v[14] = ~v[14];
    for (int r = 0; r < 12; ++r) {
        const std::uint8_t* s = kSigma[r];
        G(v, 0, 4, 8, 12, m[s[0]], m[s[1]]);
        G(v, 1, 5, 9, 13, m[s[2]], m[s[3]]);
        G(v, 2, 6, 10, 14, m[s[4]], m[s[5]]);
        G(v, 3, 7, 11, 15, m[s[6]], m[s[7]]);
        G(v, 0, 5, 10, 15, m[s[8]], m[s[9]]);
        G(v, 1, 6, 11, 12, m[s[10]], m[s[11]]);
        G(v, 2, 7, 8, 13, m[s[12]], m[s[13]]);
        G(v, 3, 4, 9, 14, m[s[14]], m[s[15]]);
    }
    for (int i = 0; i < 8; ++i) m_h[i] ^= v[i] ^ v[i + 8];
    MemZero(m, sizeof m); MemZero(v, sizeof v);
}

void Blake2b::Update(const std::uint8_t* in, std::size_t n) {
    // The last block is only compressed in Final, so a full buffer waits for more input.
    while (n > 0) {
        if (m_fill == 128) {
            m_t[0] += 128; if (m_t[0] < 128) ++m_t[1];
            Compress(false);
            m_fill = 0;
        }
        std::size_t take = 128 - m_fill;
        if (take > n) take = n;
        for (std::size_t i = 0; i < take; ++i) m_buf[m_fill + i] = in[i];
        m_fill += take; in += take; n -= take;
    }
}

void Blake2b::Final(std::uint8_t* out) {
    m_t[0] += m_fill; if (m_t[0] < m_fill) ++m_t[1];
    for (std::size_t i = m_fill; i < 128; ++i) m_buf[i] = 0;
    Compress(true);
    for (std::size_t i = 0; i < m_outLen; ++i) out[i] = static_cast<std::uint8_t>(m_h[i / 8] >> (8 * (i % 8)));
    MemZero(m_h, sizeof m_h); MemZero(m_buf, sizeof m_buf);
    m_fill = 0;
}

void Blake2bHash(std::uint8_t* out, std::size_t outLen, const std::uint8_t* m, std::size_t n,
                 const std::uint8_t* key, std::size_t keyLen) {
    Blake2b b(outLen, key, keyLen);
    if (n) b.Update(m, n);
    b.Final(out);
}

} // namespace okay::crypto
