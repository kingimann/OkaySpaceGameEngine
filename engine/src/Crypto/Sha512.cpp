// SHA-512 (FIPS 180-4). UNREVIEWED (see Crypto.hpp). Ed25519 hashes with it. The round
// constants are the first 64 bits of the fractional parts of the cube roots of the first
// 80 primes (the initial hash, of the square roots of the first 8).
#include <okay/Crypto/Crypto.hpp>

namespace okay::crypto {
namespace {

constexpr std::uint64_t kK[80] = {
    0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull,
    0x3956c25bf348b538ull, 0x59f111f1b605d019ull, 0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull,
    0xd807aa98a3030242ull, 0x12835b0145706fbeull, 0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull,
    0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull, 0xc19bf174cf692694ull,
    0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull, 0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull,
    0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull,
    0x983e5152ee66dfabull, 0xa831c66d2db43210ull, 0xb00327c898fb213full, 0xbf597fc7beef0ee4ull,
    0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull, 0x06ca6351e003826full, 0x142929670a0e6e70ull,
    0x27b70a8546d22ffcull, 0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull, 0x53380d139d95b3dfull,
    0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull, 0x92722c851482353bull,
    0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull, 0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull,
    0xd192e819d6ef5218ull, 0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull,
    0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull, 0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull,
    0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull, 0x5b9cca4f7763e373ull, 0x682e6ff3d6b2b8a3ull,
    0x748f82ee5defb2fcull, 0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
    0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull,
    0xca273eceea26619cull, 0xd186b8c721c0c207ull, 0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull,
    0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull, 0x113f9804bef90daeull, 0x1b710b35131c471bull,
    0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull, 0x431d67c49c100d4cull,
    0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull, 0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull};
constexpr std::uint64_t kIv[8] = {
    0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
    0x510e527fade682d1ull, 0x9b05688c2b3e6c1full, 0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull};

inline std::uint64_t Rotr(std::uint64_t x, int c) { return (x >> c) | (x << (64 - c)); }

} // namespace

Sha512::Sha512() : m_len(0), m_fill(0) {
    for (int i = 0; i < 8; ++i) m_h[i] = kIv[i];
    for (int i = 0; i < 128; ++i) m_buf[i] = 0;
}

void Sha512::Block(const std::uint8_t* p) {
    std::uint64_t w[80];
    for (int i = 0; i < 16; ++i) {
        std::uint64_t v = 0;
        for (int j = 0; j < 8; ++j) v = (v << 8) | p[8 * i + j];
        w[i] = v;
    }
    for (int i = 16; i < 80; ++i) {
        const std::uint64_t s0 = Rotr(w[i - 15], 1) ^ Rotr(w[i - 15], 8) ^ (w[i - 15] >> 7);
        const std::uint64_t s1 = Rotr(w[i - 2], 19) ^ Rotr(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint64_t a = m_h[0], b = m_h[1], c = m_h[2], d = m_h[3], e = m_h[4], f = m_h[5], g = m_h[6], h = m_h[7];
    for (int i = 0; i < 80; ++i) {
        const std::uint64_t S1 = Rotr(e, 14) ^ Rotr(e, 18) ^ Rotr(e, 41);
        const std::uint64_t ch = (e & f) ^ (~e & g);
        const std::uint64_t t1 = h + S1 + ch + kK[i] + w[i];
        const std::uint64_t S0 = Rotr(a, 28) ^ Rotr(a, 34) ^ Rotr(a, 39);
        const std::uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint64_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    m_h[0] += a; m_h[1] += b; m_h[2] += c; m_h[3] += d; m_h[4] += e; m_h[5] += f; m_h[6] += g; m_h[7] += h;
    MemZero(w, sizeof w);
}

void Sha512::Update(const std::uint8_t* m, std::size_t n) {
    m_len += n;
    while (n > 0) {
        std::size_t take = 128 - m_fill;
        if (take > n) take = n;
        for (std::size_t i = 0; i < take; ++i) m_buf[m_fill + i] = m[i];
        m_fill += take; m += take; n -= take;
        if (m_fill == 128) { Block(m_buf); m_fill = 0; }
    }
}

void Sha512::Final(std::uint8_t out[64]) {
    const std::uint64_t bits = m_len * 8;    // (messages under 2^61 bytes; the upper 64 length bits are 0)
    const std::uint8_t one = 0x80, zero = 0;
    Update(&one, 1);
    while (m_fill != 112) Update(&zero, 1);
    std::uint8_t lenBytes[16] = {0};
    for (int i = 0; i < 8; ++i) lenBytes[8 + i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
    Update(lenBytes, 16);
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j) out[8 * i + j] = static_cast<std::uint8_t>(m_h[i] >> (56 - 8 * j));
    MemZero(m_h, sizeof m_h); MemZero(m_buf, sizeof m_buf);
}

void Sha512Hash(std::uint8_t out[64], const std::uint8_t* m, std::size_t n) {
    Sha512 s;
    if (n) s.Update(m, n);
    s.Final(out);
}

} // namespace okay::crypto
