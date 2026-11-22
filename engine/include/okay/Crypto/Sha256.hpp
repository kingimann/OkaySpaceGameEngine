#pragma once
// okay::crypto SHA-256, HMAC-SHA256 and PBKDF2-HMAC-SHA256: the ONE copy of SHA-256 in the engine.
// Header-only on purpose: the launcher, editor, player, updater and anti-cheat headers use it without linking
// anything, and the compiler can inline the block function into the PBKDF2 loop. No <mutex>/<thread>, no
// static mutable state (every object is independent, so any number of threads may hash at once), no exceptions,
// no allocation (peak memory is the object itself: about 112 bytes plus a 64-byte message schedule ring on the stack).
// The only static is the once-decided choice of block function (a function-local constant, see below).
//
// Written from FIPS 180-4 (SHA-256), RFC 2104 / RFC 4231 (HMAC) and RFC 8018 section 5.2 (PBKDF2). The round
// constants are the first 32 bits of the fractional parts of the cube roots of the first 64 primes, the initial
// hash value those of the square roots of the first 8 (FIPS 180-4 4.2.2, 5.3.3). See docs/platform/sha256_comparison.md for
// how this one was chosen over two older copies and how it was checked.
//
// UNREVIEWED like the rest of okay::crypto (see Crypto.hpp), but SHA-256, HMAC and PBKDF2 here are NOT behind the
// OKAY_OWN_CRYPTO switch: they have always been the engine's own code (file checksums, account password hashes,
// the anti-cheat tag). Only the libsodium-replacement paths are switched.
//
// SHA-256 has no secret-dependent branch or table index: the only data-dependent control flow is the message
// length, which is public.
//
// Speed (2026-11-16, branch claude2/own-http-sha-fast): two block functions give the same bytes.
//  - Portable C++ (every compiler and CPU): the 64 rounds are written out in full (macros below, removed again at
//    the end of the header), the message schedule is computed on the fly in a sixteen-word ring (FIPS 180-4 6.2.2
//    step 1, W[t] for t >= 16, as each round needs it), and many blocks go through one call so the hash state stays
//    in registers. Ch and Maj use equivalent forms: Ch = g ^ (e & (f ^ g)), Maj = b ^ ((a ^ b) & (b ^ c)), where
//    b ^ c of a round is a ^ b of the round before (the variables rotate), so it is carried over instead of recomputed.
//  - x86 SHA extensions (SHA256RNDS2, SHA256MSG1, SHA256MSG2), written from Intel's public description of the three
//    instructions (Intel 64 and IA-32 Architectures Software Developer's Manual, volume 2, and Intel's "Intel SHA
//    Extensions" paper): only with GCC or Clang on x86, compiled through a function target attribute (no build flag),
//    and used only when the CPU says it has them (CPUID leaf 7 EBX bit 29, plus SSSE3 and SSE4.1 from leaf 1). The
//    check runs once, into a function-local constant. OKAY_SHA256_PORTABLE=1 in the environment forces the portable
//    function (for tests and for comparing the two). MSVC and other CPUs always use the portable function.
// Only the speed changed: the digest of every message is the same as before (tests/sha256_compare gb and
// scripts/sha256_fast_check.py check it against Python hashlib; tests/test_sha256_unify compares the two functions).
#include <cstddef>
#include <cstdint>
#include <cstring>

#if (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__)) && !defined(OKAY_SHA256_NO_SHANI)
#define OKAY_SHA256_SHANI 1
#include <cstdlib>   // (no <cpuid.h>: it clashes with MinGW's <intrin.h> __cpuid, which <immintrin.h> pulls in)
#include <immintrin.h>
#endif

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

inline std::uint32_t LoadBe32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) << 24 | static_cast<std::uint32_t>(p[1]) << 16 |
           static_cast<std::uint32_t>(p[2]) << 8 | static_cast<std::uint32_t>(p[3]);
}

// ---- portable block function -------------------------------------------------------------------------------------
// FIPS 180-4 4.1.2: S0/S1 are the "big sigma" functions of the rounds, s0/s1 the "small sigma" ones of the schedule.
#define OKAY_SHA256_S0(x) (Rotr((x), 2) ^ Rotr((x), 13) ^ Rotr((x), 22))
#define OKAY_SHA256_S1(x) (Rotr((x), 6) ^ Rotr((x), 11) ^ Rotr((x), 25))
#define OKAY_SHA256_s0(x) (Rotr((x), 7) ^ Rotr((x), 18) ^ ((x) >> 3))
#define OKAY_SHA256_s1(x) (Rotr((x), 17) ^ Rotr((x), 19) ^ ((x) >> 10))
// One round t (FIPS 180-4 6.2.2 step 3). The eight working variables are named in rotated order instead of being moved:
// after the round, the new "a" is the old h slot and the new "e" is the old d slot. `bc` holds b ^ c on entry and a ^ b
// (the next round's b ^ c) on exit.
#define OKAY_SHA256_RND(a, b, c, d, e, f, g, h, t)                                                       \
    do {                                                                                                 \
        const std::uint32_t t1 = h + OKAY_SHA256_S1(e) + (g ^ (e & (f ^ g))) + kK[t] + w[(t) & 15];    \
        const std::uint32_t ab = a ^ b;                                                                  \
        const std::uint32_t t2 = OKAY_SHA256_S0(a) + (b ^ (ab & bc));                                    \
        bc = ab;                                                                                         \
        d += t1;                                                                                         \
        h = t1 + t2;                                                                                     \
    } while (0)
// The same round for t >= 16, first replacing W[t-16] in the ring with W[t] = s1(W[t-2]) + W[t-7] + s0(W[t-15]) + W[t-16].
#define OKAY_SHA256_RNDX(a, b, c, d, e, f, g, h, t)                                                      \
    do {                                                                                                 \
        w[(t) & 15] += OKAY_SHA256_s1(w[((t) - 2) & 15]) + w[((t) - 7) & 15] + OKAY_SHA256_s0(w[((t) - 15) & 15]); \
        OKAY_SHA256_RND(a, b, c, d, e, f, g, h, t);                                                      \
    } while (0)
#define OKAY_SHA256_EIGHT(R, t)                                                                           \
    R(a, b, c, d, e, f, g, h, (t) + 0); R(h, a, b, c, d, e, f, g, (t) + 1);                              \
    R(g, h, a, b, c, d, e, f, (t) + 2); R(f, g, h, a, b, c, d, e, (t) + 3);                              \
    R(e, f, g, h, a, b, c, d, (t) + 4); R(d, e, f, g, h, a, b, c, (t) + 5);                              \
    R(c, d, e, f, g, h, a, b, (t) + 6); R(b, c, d, e, f, g, h, a, (t) + 7)

/// The 64 rounds over one block whose sixteen message words are in `w` (overwritten: it becomes the schedule ring),
/// added into `hs`.
inline void CompressRing(std::uint32_t hs[8], std::uint32_t w[16]) {
    std::uint32_t a = hs[0], b = hs[1], c = hs[2], d = hs[3], e = hs[4], f = hs[5], g = hs[6], h = hs[7];
    std::uint32_t bc = b ^ c;
    OKAY_SHA256_EIGHT(OKAY_SHA256_RND, 0);
    OKAY_SHA256_EIGHT(OKAY_SHA256_RND, 8);
    OKAY_SHA256_EIGHT(OKAY_SHA256_RNDX, 16);
    OKAY_SHA256_EIGHT(OKAY_SHA256_RNDX, 24);
    OKAY_SHA256_EIGHT(OKAY_SHA256_RNDX, 32);
    OKAY_SHA256_EIGHT(OKAY_SHA256_RNDX, 40);
    OKAY_SHA256_EIGHT(OKAY_SHA256_RNDX, 48);
    OKAY_SHA256_EIGHT(OKAY_SHA256_RNDX, 56);
    hs[0] += a; hs[1] += b; hs[2] += c; hs[3] += d; hs[4] += e; hs[5] += f; hs[6] += g; hs[7] += h;
}

#undef OKAY_SHA256_EIGHT
#undef OKAY_SHA256_RNDX
#undef OKAY_SHA256_RND
#undef OKAY_SHA256_s1
#undef OKAY_SHA256_s0
#undef OKAY_SHA256_S1
#undef OKAY_SHA256_S0

/// Portable: `n` whole 64-byte blocks at `p` into `hs`.
inline void BlocksPortable(std::uint32_t hs[8], const std::uint8_t* p, std::size_t n) {
    std::uint32_t w[16];
    for (; n > 0; --n, p += 64) {
        for (int i = 0; i < 16; ++i) w[i] = LoadBe32(p + 4 * i);
        CompressRing(hs, w);
    }
}

/// Portable: one block given as sixteen message words (host order).
inline void WordsPortable(std::uint32_t hs[8], const std::uint32_t msg[16]) {
    std::uint32_t w[16];
    for (int i = 0; i < 16; ++i) w[i] = msg[i];
    CompressRing(hs, w);
}

#if defined(OKAY_SHA256_SHANI)
// ---- x86 SHA extensions ------------------------------------------------------------------------------------------
// From Intel's description of the instructions: SHA256RNDS2 does two rounds; its first operand holds the state words
// C, D, G, H and its second A, B, E, F, each with the first-named word in the highest 32-bit lane, and the two
// (K[t] + W[t]) values sit in the lowest two lanes of the third. It returns the new A, B, E, F; after two rounds the
// old A, B, E, F are exactly the new C, D, G, H, so the two registers swap roles every two rounds. SHA256MSG1 adds
// s0(W[t+1]) to each of four words W[t]; SHA256MSG2 adds the s1 terms and finishes W[t+16..t+19] once W[t+9..t+12]
// has been added. The lane shuffles below were worked out by hand from that layout (and are checked by the tests).

/// `n` blocks into `hs`. `bytes` = big-endian message bytes (a normal block); otherwise `p` is sixteen host-order
/// message words (FinalWith32Words), one block.
__attribute__((target("sha,sse4.1,ssse3"))) inline void ShaNiCompress(std::uint32_t hs[8], const void* p, std::size_t n, bool bytes) {
    const __m128i swap = _mm_set_epi64x(0x0c0d0e0f08090a0bLL, 0x0405060700010203LL);   // reverse the bytes of each word
    // [A B C D] and [E F G H] (lane 0 first) -> ABEF = lanes [F E B A], CDGH = lanes [H G D C].
    __m128i abcd = _mm_loadu_si128(reinterpret_cast<const __m128i*>(hs));
    __m128i efgh = _mm_loadu_si128(reinterpret_cast<const __m128i*>(hs + 4));
    abcd = _mm_shuffle_epi32(abcd, 0xB1);                      // [B A D C]
    efgh = _mm_shuffle_epi32(efgh, 0x1B);                      // [H G F E]
    __m128i abef = _mm_alignr_epi8(abcd, efgh, 8);             // [F E B A]
    __m128i cdgh = _mm_blend_epi16(efgh, abcd, 0xF0);          // [H G D C]
    const std::uint8_t* q = static_cast<const std::uint8_t*>(p);
    for (; n > 0; --n, q += 64) {
        const __m128i abefSave = abef, cdghSave = cdgh;
        __m128i m[4];
        for (int i = 0; i < 4; ++i) {
            m[i] = _mm_loadu_si128(reinterpret_cast<const __m128i*>(q + 16 * i));
            if (bytes) m[i] = _mm_shuffle_epi8(m[i], swap);    // lanes now W[4i] .. W[4i+3]
        }
        for (int g = 0; g < 16; ++g) {                         // four rounds per group; m[g & 3] holds W[4g .. 4g+3]
            const __m128i wk = _mm_add_epi32(m[g & 3], _mm_loadu_si128(reinterpret_cast<const __m128i*>(kK + 4 * g)));
            cdgh = _mm_sha256rnds2_epu32(cdgh, abef, wk);      // rounds 4g, 4g+1: cdgh now holds the new A B E F
            abef = _mm_sha256rnds2_epu32(abef, cdgh, _mm_shuffle_epi32(wk, 0x0E));   // rounds 4g+2, 4g+3
            if (g < 12) {                                      // W[4g+16 .. 4g+19] from groups g, g+1, g+2, g+3
                const __m128i w9 = _mm_alignr_epi8(m[(g + 3) & 3], m[(g + 2) & 3], 4);   // W[4g+9 .. 4g+12]
                m[g & 3] = _mm_sha256msg2_epu32(_mm_add_epi32(_mm_sha256msg1_epu32(m[g & 3], m[(g + 1) & 3]), w9), m[(g + 3) & 3]);
            }
        }
        abef = _mm_add_epi32(abef, abefSave);
        cdgh = _mm_add_epi32(cdgh, cdghSave);
    }
    // Back: ABEF [F E B A] -> [A B E F]; CDGH [H G D C] -> [G H C D]; then [A B C D] and [E F G H].
    const __m128i t = _mm_shuffle_epi32(abef, 0x1B);
    cdgh = _mm_shuffle_epi32(cdgh, 0xB1);
    _mm_storeu_si128(reinterpret_cast<__m128i*>(hs), _mm_blend_epi16(t, cdgh, 0xF0));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(hs + 4), _mm_alignr_epi8(cdgh, t, 8));
}

/// True when this CPU has the SHA extensions, SSSE3 and SSE4.1.
inline void CpuidLeaf(unsigned leaf, unsigned sub, unsigned& a, unsigned& b, unsigned& c, unsigned& d) {
    __asm__ __volatile__("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "0"(leaf), "2"(sub));
}

inline bool CpuHasShaNi() {
    unsigned a = 0, b = 0, c = 0, d = 0;
    CpuidLeaf(0, 0, a, b, c, d);
    if (a < 7) return false;                                   // highest basic leaf
    CpuidLeaf(1, 0, a, b, c, d);
    const bool ssse3 = (c >> 9) & 1u, sse41 = (c >> 19) & 1u;
    CpuidLeaf(7, 0, a, b, c, d);
    return ssse3 && sse41 && ((b >> 29) & 1u);
}
#endif

/// Which block function runs: the SHA extensions when the CPU has them and OKAY_SHA256_PORTABLE is not "1". Decided once.
inline bool UseShaNi() {
#if defined(OKAY_SHA256_SHANI)
    static const bool use = [] {
        const char* env = std::getenv("OKAY_SHA256_PORTABLE");
        return CpuHasShaNi() && !(env && env[0] == '1' && env[1] == 0);
    }();
    return use;
#else
    return false;
#endif
}

/// Name of the block function in use ("sha-ni" or "portable"), for logs and benchmarks.
inline const char* BlockFunctionName() { return UseShaNi() ? "sha-ni" : "portable"; }

/// `n` whole 64-byte blocks at `p` into `hs`, with the block function in use.
inline void Blocks(std::uint32_t hs[8], const std::uint8_t* p, std::size_t n) {
#if defined(OKAY_SHA256_SHANI)
    if (UseShaNi()) { ShaNiCompress(hs, p, n, true); return; }
#endif
    BlocksPortable(hs, p, n);
}

/// One block of sixteen host-order message words into `hs`, with the block function in use.
inline void Words(std::uint32_t hs[8], const std::uint32_t msg[16]) {
#if defined(OKAY_SHA256_SHANI)
    if (UseShaNi()) { ShaNiCompress(hs, msg, 1, false); return; }
#endif
    WordsPortable(hs, msg);
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
            std::memcpy(m_buf + m_fill, m, take);
            m_fill += take; m += take; n -= take;
            if (m_fill < 64) return;
            sha256_detail::Blocks(m_h, m_buf, 1); m_fill = 0;
        }
        if (n >= 64) {                                      // all whole blocks straight from the caller's memory, in one call
            const std::size_t blocks = n / 64;
            sha256_detail::Blocks(m_h, m, blocks);
            m += blocks * 64; n -= blocks * 64;
        }
        if (n != 0) std::memcpy(m_buf, m, n);
        m_fill = n;
    }

    /// Writes the 32-byte digest and wipes the state and the buffer.
    void Final(std::uint8_t out[32]) {
        const std::uint64_t bits = m_len * 8;
        m_buf[m_fill++] = 0x80;
        if (m_fill > 56) {
            while (m_fill < 64) m_buf[m_fill++] = 0;
            sha256_detail::Blocks(m_h, m_buf, 1); m_fill = 0;
        }
        while (m_fill < 56) m_buf[m_fill++] = 0;
        for (int i = 0; i < 8; ++i) m_buf[56 + i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
        sha256_detail::Blocks(m_h, m_buf, 1);
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
        sha256_detail::Words(out, w);
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
