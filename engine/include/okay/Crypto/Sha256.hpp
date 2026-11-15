#pragma once
// okay::crypto SHA-256, HMAC-SHA256 and PBKDF2-HMAC-SHA256: the ONE copy of SHA-256 in the engine.
// Header-only on purpose: the launcher, editor, player, updater and anti-cheat headers use it without linking
// anything, and the compiler can inline the block function into the PBKDF2 loop. No <mutex>/<thread>, no
// static mutable state (every object is independent, so any number of threads may hash at once), no exceptions,
// no allocation (peak memory is the object itself: about 112 bytes plus a 256-byte message schedule on the stack).
//
// Written from FIPS 180-4 (SHA-256), RFC 2104 / RFC 4231 (HMAC) and RFC 8018 section 5.2 (PBKDF2). The round
// constants are the first 32 bits of the fractional parts of the cube roots of the first 64 primes, the initial
// hash value those of the square roots of the first 8 (FIPS 180-4 4.2.2, 5.3.3). See docs/sha256_comparison.md for
// how this one was chosen over two older copies and how it was checked.
//
// UNREVIEWED like the rest of okay::crypto (see Crypto.hpp), but SHA-256, HMAC and PBKDF2 here are NOT behind the
// OKAY_OWN_CRYPTO switch: they have always been the engine's own code (file checksums, account password hashes,
// the anti-cheat tag). Only the libsodium-replacement paths are switched.
//
// SHA-256 has no secret-dependent branch or table index: the only data-dependent control flow is the message
// length, which is public.
#include <cstddef>
#include <cstdint>

namespace okay::crypto {
namespace sha256_detail {

constexpr std::uint32_t kK[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

inline std::uint32_t Rotr(std::uint32_t x, int c) { return (x >> c) | (x << (32 - c)); }

/// Zero memory in a way the optimiser may not remove (this header cannot call okay::crypto::MemZero: no link needed).
inline void Wipe(void* p, std::size_t n) {
    volatile std::uint8_t* q = static_cast<volatile std::uint8_t*>(p);
    while (n--) *q++ = 0;
}

} // namespace sha256_detail

/// Streaming SHA-256. Update() any number of times, then Final() once (it wipes the state; start a new object for the
/// next message). The byte counter is 64 bits, so the length field wraps only after 2^61 bytes, as FIPS 180-4 allows.
class Sha256 {
public:
    Sha256() : m_len(0), m_fill(0) {
        static constexpr std::uint32_t kIv[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                                 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
        for (int i = 0; i < 8; ++i) m_h[i] = kIv[i];
        for (int i = 0; i < 64; ++i) m_buf[i] = 0;
    }

    void Update(const std::uint8_t* m, std::size_t n) {
        if (n == 0) return;
        m_len += n;
        if (m_fill != 0) {                                  // top up a partly filled block first
            std::size_t take = 64 - m_fill;
            if (take > n) take = n;
            for (std::size_t i = 0; i < take; ++i) m_buf[m_fill + i] = m[i];
            m_fill += take; m += take; n -= take;
            if (m_fill < 64) return;
            Block(m_buf); m_fill = 0;
        }
        while (n >= 64) { Block(m); m += 64; n -= 64; }     // whole blocks straight from the caller's memory: no copy
        for (std::size_t i = 0; i < n; ++i) m_buf[i] = m[i];
        m_fill = n;
    }

    /// Writes the 32-byte digest and wipes the state and the buffer.
    void Final(std::uint8_t out[32]) {
        const std::uint64_t bits = m_len * 8;
        m_buf[m_fill++] = 0x80;
        if (m_fill > 56) {
            while (m_fill < 64) m_buf[m_fill++] = 0;
            Block(m_buf); m_fill = 0;
        }
        while (m_fill < 56) m_buf[m_fill++] = 0;
        for (int i = 0; i < 8; ++i) m_buf[56 + i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
        Block(m_buf);
        Store(out, m_h);
        Wipe();
    }

    /// PBKDF2/HMAC helper: this object has absorbed a whole number of 64-byte blocks (an HMAC key pad). Writes the digest
    /// of everything absorbed followed by the 32 bytes at `m`, in ONE compression, and leaves this object unchanged.
    void FinalWith32(const std::uint8_t m[32], std::uint8_t out[32]) const {
        std::uint32_t in[8], h[8];
        for (int i = 0; i < 8; ++i)
            in[i] = static_cast<std::uint32_t>(m[4 * i]) << 24 | static_cast<std::uint32_t>(m[4 * i + 1]) << 16 |
                    static_cast<std::uint32_t>(m[4 * i + 2]) << 8 | static_cast<std::uint32_t>(m[4 * i + 3]);
        FinalWith32Words(in, h);
        Store(out, h);
        sha256_detail::Wipe(in, sizeof in); sha256_detail::Wipe(h, sizeof h);
    }

    /// Same as FinalWith32, with the 32 input bytes and the 32 output bytes as eight big-endian words each (no byte shuffling:
    /// this is what the PBKDF2 loop calls). Fast path: the object holds whole blocks only (m_fill == 0, as after an HMAC pad);
    /// the 64-bit bit count is written in full. If a partial block is pending it still gives the right answer, by hashing a copy
    /// the ordinary way (slower, never used by HMAC/PBKDF2).
    void FinalWith32Words(const std::uint32_t in[8], std::uint32_t out[8]) const {
        if (m_fill != 0) {
            std::uint8_t b[32], d[32];
            for (int i = 0; i < 8; ++i) {
                b[4 * i] = static_cast<std::uint8_t>(in[i] >> 24); b[4 * i + 1] = static_cast<std::uint8_t>(in[i] >> 16);
                b[4 * i + 2] = static_cast<std::uint8_t>(in[i] >> 8); b[4 * i + 3] = static_cast<std::uint8_t>(in[i]);
            }
            Sha256 t = *this;
            t.Update(b, 32);
            t.Final(d);
            for (int i = 0; i < 8; ++i)
                out[i] = static_cast<std::uint32_t>(d[4 * i]) << 24 | static_cast<std::uint32_t>(d[4 * i + 1]) << 16 |
                         static_cast<std::uint32_t>(d[4 * i + 2]) << 8 | static_cast<std::uint32_t>(d[4 * i + 3]);
            sha256_detail::Wipe(b, sizeof b); sha256_detail::Wipe(d, sizeof d);
            return;
        }
        std::uint32_t w[16];
        for (int i = 0; i < 8; ++i) w[i] = in[i];
        w[8] = 0x80000000u;
        for (int i = 9; i < 14; ++i) w[i] = 0;
        const std::uint64_t bits = (m_len + 32) * 8;
        w[14] = static_cast<std::uint32_t>(bits >> 32);
        w[15] = static_cast<std::uint32_t>(bits);
        for (int i = 0; i < 8; ++i) out[i] = m_h[i];
        CompressWords(out, w);
    }

    /// Zero the state and buffer now (an abandoned object holding a secret). The object must not be used afterwards.
    void Wipe() {
        sha256_detail::Wipe(m_h, sizeof m_h); sha256_detail::Wipe(m_buf, sizeof m_buf);
        m_len = 0; m_fill = 0;
    }

private:
    std::uint32_t m_h[8];
    std::uint64_t m_len;
    std::uint8_t m_buf[64];
    std::size_t m_fill;

    static void Store(std::uint8_t out[32], const std::uint32_t h[8]) {
        for (int i = 0; i < 8; ++i) {
            out[4 * i] = static_cast<std::uint8_t>(h[i] >> 24); out[4 * i + 1] = static_cast<std::uint8_t>(h[i] >> 16);
            out[4 * i + 2] = static_cast<std::uint8_t>(h[i] >> 8); out[4 * i + 3] = static_cast<std::uint8_t>(h[i]);
        }
    }

    void Block(const std::uint8_t* p) {
        std::uint32_t w[16];
        for (int i = 0; i < 16; ++i)
            w[i] = static_cast<std::uint32_t>(p[4 * i]) << 24 | static_cast<std::uint32_t>(p[4 * i + 1]) << 16 |
                   static_cast<std::uint32_t>(p[4 * i + 2]) << 8 | static_cast<std::uint32_t>(p[4 * i + 3]);
        CompressWords(m_h, w);
    }

    // One round of FIPS 180-4 6.2.2 step 3; the eight working variables are passed in rotated order instead of being shifted.
    static void Round(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t& d, std::uint32_t e, std::uint32_t f,
                      std::uint32_t g, std::uint32_t& h, std::uint32_t kw) {
        using sha256_detail::Rotr;
        const std::uint32_t t1 = h + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + kw;
        const std::uint32_t t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        d += t1;
        h = t1 + t2;
    }

    /// The compression of one block (sixteen big-endian message words) into `hs`; the message schedule is kept in a
    /// sixteen-word ring, and the rounds are unrolled eight at a time.
    static void CompressWords(std::uint32_t hs[8], const std::uint32_t msg[16]) {
        using sha256_detail::Rotr;
        using sha256_detail::kK;
        std::uint32_t w[16];
        for (int i = 0; i < 16; ++i) w[i] = msg[i];
        std::uint32_t a = hs[0], b = hs[1], c = hs[2], d = hs[3], e = hs[4], f = hs[5], g = hs[6], h = hs[7];
        for (int i = 0; i < 64; i += 16) {
            if (i != 0) {                                           // w[t] = w[t-16] + s0(w[t-15]) + w[t-7] + s1(w[t-2]), in the ring
                for (int j = 0; j < 16; ++j) {
                    const std::uint32_t w15 = w[(j + 1) & 15], w2 = w[(j + 14) & 15];
                    const std::uint32_t s0 = Rotr(w15, 7) ^ Rotr(w15, 18) ^ (w15 >> 3);
                    const std::uint32_t s1 = Rotr(w2, 17) ^ Rotr(w2, 19) ^ (w2 >> 10);
                    w[j] = w[j] + s0 + w[(j + 9) & 15] + s1;
                }
            }
            Round(a, b, c, d, e, f, g, h, kK[i + 0] + w[0]);
            Round(h, a, b, c, d, e, f, g, kK[i + 1] + w[1]);
            Round(g, h, a, b, c, d, e, f, kK[i + 2] + w[2]);
            Round(f, g, h, a, b, c, d, e, kK[i + 3] + w[3]);
            Round(e, f, g, h, a, b, c, d, kK[i + 4] + w[4]);
            Round(d, e, f, g, h, a, b, c, kK[i + 5] + w[5]);
            Round(c, d, e, f, g, h, a, b, kK[i + 6] + w[6]);
            Round(b, c, d, e, f, g, h, a, kK[i + 7] + w[7]);
            Round(a, b, c, d, e, f, g, h, kK[i + 8] + w[8]);
            Round(h, a, b, c, d, e, f, g, kK[i + 9] + w[9]);
            Round(g, h, a, b, c, d, e, f, kK[i + 10] + w[10]);
            Round(f, g, h, a, b, c, d, e, kK[i + 11] + w[11]);
            Round(e, f, g, h, a, b, c, d, kK[i + 12] + w[12]);
            Round(d, e, f, g, h, a, b, c, kK[i + 13] + w[13]);
            Round(c, d, e, f, g, h, a, b, kK[i + 14] + w[14]);
            Round(b, c, d, e, f, g, h, a, kK[i + 15] + w[15]);
        }
        hs[0] += a; hs[1] += b; hs[2] += c; hs[3] += d; hs[4] += e; hs[5] += f; hs[6] += g; hs[7] += h;
    }
};

/// One-shot SHA-256 of `n` bytes at `m` (m may be null when n is 0).
inline void Sha256Hash(std::uint8_t out[32], const std::uint8_t* m, std::size_t n) {
    Sha256 s;
    s.Update(m, n);
    s.Final(out);
}

/// HMAC-SHA256 (RFC 2104, test vectors RFC 4231). The key is set up once; Update/Final may then be used for one message,
/// or Final(out) after Update-ing nothing gives the MAC of the empty message. A key longer than 64 bytes is hashed first.
/// Wipes its pads when it ends.
class HmacSha256 {
public:
    HmacSha256(const std::uint8_t* key, std::size_t keyLen) {
        std::uint8_t k[64] = {0};
        if (keyLen > 64) Sha256Hash(k, key, keyLen);
        else for (std::size_t i = 0; i < keyLen; ++i) k[i] = key[i];
        std::uint8_t pad[64];
        for (int i = 0; i < 64; ++i) pad[i] = k[i] ^ 0x36;
        m_inner.Update(pad, 64);
        for (int i = 0; i < 64; ++i) pad[i] = k[i] ^ 0x5c;
        m_outer.Update(pad, 64);
        sha256_detail::Wipe(k, sizeof k); sha256_detail::Wipe(pad, sizeof pad);
    }
    ~HmacSha256() { m_inner.Wipe(); m_outer.Wipe(); }
    HmacSha256(const HmacSha256&) = default;
    HmacSha256& operator=(const HmacSha256&) = default;

    void Update(const std::uint8_t* m, std::size_t n) { m_inner.Update(m, n); }

    /// Writes the 32-byte MAC. The object is spent afterwards.
    void Final(std::uint8_t out[32]) {
        std::uint8_t in[32];
        m_inner.Final(in);
        m_outer.FinalWith32(in, out);
        sha256_detail::Wipe(in, sizeof in);
        m_outer.Wipe();
    }

    /// The two pad-absorbed states, for a caller (PBKDF2) that runs many HMACs under one key.
    const Sha256& InnerState() const { return m_inner; }
    const Sha256& OuterState() const { return m_outer; }

private:
    Sha256 m_inner, m_outer;
};

/// One-shot HMAC-SHA256.
inline void HmacSha256Mac(std::uint8_t out[32], const std::uint8_t* key, std::size_t keyLen,
                          const std::uint8_t* m, std::size_t n) {
    HmacSha256 h(key, keyLen);
    h.Update(m, n);
    h.Final(out);
}

/// PBKDF2-HMAC-SHA256 (RFC 8018 section 5.2): writes `dkLen` bytes (any length up to 2^32-1 blocks of 32 bytes; 0 writes
/// nothing; returns false and writes nothing when dkLen needs more than 2^32-1 blocks, which the 32-bit block counter cannot
/// number: RFC 8018 caps dkLen at (2^32-1) * 32 bytes). `iterations` below 1 is treated as 1. The two pad states are computed once, so each iteration costs exactly two
/// SHA-256 compressions. Everything secret on the stack is wiped on return.
inline bool Pbkdf2HmacSha256(std::uint8_t* out, std::size_t dkLen, const std::uint8_t* password, std::size_t passwordLen,
                             const std::uint8_t* salt, std::size_t saltLen, std::uint32_t iterations) {
    if (static_cast<unsigned long long>(dkLen) > 32ull * 0xFFFFFFFFull) return false;
    if (iterations < 1) iterations = 1;
    const HmacSha256 keyed(password, passwordLen);
    for (std::uint32_t block = 1; dkLen > 0; ++block) {
        // U1 = HMAC(P, salt || INT_32_BE(block))
        Sha256 first = keyed.InnerState();
        first.Update(salt, saltLen);
        const std::uint8_t idx[4] = {static_cast<std::uint8_t>(block >> 24), static_cast<std::uint8_t>(block >> 16),
                                     static_cast<std::uint8_t>(block >> 8), static_cast<std::uint8_t>(block)};
        first.Update(idx, 4);
        std::uint8_t inB[32], uB[32];
        first.Final(inB);
        keyed.OuterState().FinalWith32(inB, uB);
        std::uint32_t u[8], t[8], inD[8];
        for (int k = 0; k < 8; ++k)
            u[k] = static_cast<std::uint32_t>(uB[4 * k]) << 24 | static_cast<std::uint32_t>(uB[4 * k + 1]) << 16 |
                   static_cast<std::uint32_t>(uB[4 * k + 2]) << 8 | static_cast<std::uint32_t>(uB[4 * k + 3]);
        for (int k = 0; k < 8; ++k) t[k] = u[k];
        for (std::uint32_t i = 1; i < iterations; ++i) {            // U(i) = HMAC(P, U(i-1)); T = U1 ^ U2 ^ ...
            keyed.InnerState().FinalWith32Words(u, inD);
            keyed.OuterState().FinalWith32Words(inD, u);
            for (int k = 0; k < 8; ++k) t[k] ^= u[k];
        }
        std::uint8_t tB[32];
        for (int k = 0; k < 8; ++k) {
            tB[4 * k] = static_cast<std::uint8_t>(t[k] >> 24); tB[4 * k + 1] = static_cast<std::uint8_t>(t[k] >> 16);
            tB[4 * k + 2] = static_cast<std::uint8_t>(t[k] >> 8); tB[4 * k + 3] = static_cast<std::uint8_t>(t[k]);
        }
        const std::size_t take = dkLen < 32 ? dkLen : 32;
        for (std::size_t k = 0; k < take; ++k) out[k] = tB[k];
        sha256_detail::Wipe(inB, sizeof inB); sha256_detail::Wipe(uB, sizeof uB); sha256_detail::Wipe(u, sizeof u);
        sha256_detail::Wipe(t, sizeof t); sha256_detail::Wipe(inD, sizeof inD); sha256_detail::Wipe(tB, sizeof tB);
        out += take; dkLen -= take;
    }
    return true;
}

} // namespace okay::crypto
