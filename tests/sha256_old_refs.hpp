#pragma once
// The OLD SHA-256 copies, kept verbatim as REFERENCE implementations for the differential tests
// (tests/test_sha256_unify.cpp, tests/sha256_compare.cpp). Nothing in the engine includes this file.
// They are the code that shipped before the three copies were merged into okay/Crypto/Sha256.hpp
// (docs/platform/sha256_comparison.md): copy 2 (AccountService.hpp), its PBKDF2 and the HMAC from AntiCheatChecks.hpp,
// and copy 3 (launcher/VoiceChat.hpp). Only the namespaces were renamed (okay_old::...).
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace okay_old {
namespace crypto {   // copy 1 as it was in engine/src/Crypto/Sha256.cpp (MemZero calls dropped)
class Sha256 {
public:
    Sha256();
    void Update(const std::uint8_t* m, std::size_t n);
    void Final(std::uint8_t out[32]);
private:
    std::uint32_t m_h[8];
    std::uint64_t m_len;
    std::uint8_t m_buf[64];
    std::size_t m_fill;
    void Block(const std::uint8_t* p);
};
constexpr std::uint32_t kK[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};
constexpr std::uint32_t kIv[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au, 0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

inline std::uint32_t Rotr(std::uint32_t x, int c) { return (x >> c) | (x << (32 - c)); }


inline Sha256::Sha256() : m_len(0), m_fill(0) {
    for (int i = 0; i < 8; ++i) m_h[i] = kIv[i];
    for (int i = 0; i < 64; ++i) m_buf[i] = 0;
}

inline void Sha256::Block(const std::uint8_t* p) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i)
        w[i] = static_cast<std::uint32_t>(p[4 * i]) << 24 | static_cast<std::uint32_t>(p[4 * i + 1]) << 16 |
               static_cast<std::uint32_t>(p[4 * i + 2]) << 8 | static_cast<std::uint32_t>(p[4 * i + 3]);
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = m_h[0], b = m_h[1], c = m_h[2], d = m_h[3], e = m_h[4], f = m_h[5], g = m_h[6], h = m_h[7];
    for (int i = 0; i < 64; ++i) {
        const std::uint32_t S1 = Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + S1 + ch + kK[i] + w[i];
        const std::uint32_t S0 = Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = S0 + maj;
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    m_h[0] += a; m_h[1] += b; m_h[2] += c; m_h[3] += d; m_h[4] += e; m_h[5] += f; m_h[6] += g; m_h[7] += h;
}

inline void Sha256::Update(const std::uint8_t* m, std::size_t n) {
    m_len += n;
    while (n > 0) {
        std::size_t take = 64 - m_fill;
        if (take > n) take = n;
        for (std::size_t i = 0; i < take; ++i) m_buf[m_fill + i] = m[i];
        m_fill += take; m += take; n -= take;
        if (m_fill == 64) { Block(m_buf); m_fill = 0; }
    }
}

inline void Sha256::Final(std::uint8_t out[32]) {
    const std::uint64_t bits = m_len * 8;
    const std::uint8_t one = 0x80, zero = 0;
    Update(&one, 1);
    while (m_fill != 56) Update(&zero, 1);
    std::uint8_t lenBytes[8];
    for (int i = 0; i < 8; ++i) lenBytes[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
    Update(lenBytes, 8);
    for (int i = 0; i < 8; ++i) {
        out[4 * i] = static_cast<std::uint8_t>(m_h[i] >> 24); out[4 * i + 1] = static_cast<std::uint8_t>(m_h[i] >> 16);
        out[4 * i + 2] = static_cast<std::uint8_t>(m_h[i] >> 8); out[4 * i + 3] = static_cast<std::uint8_t>(m_h[i]);
    }
    
}

inline void Sha256Hash(std::uint8_t out[32], const std::uint8_t* m, std::size_t n) {
    Sha256 s;
    if (n) s.Update(m, n);
    s.Final(out);
}

} // namespace crypto
namespace account {
class Sha256 {
public:
    Sha256() { Reset(); }

    void Update(const std::uint8_t* data, std::size_t len) {
        for (std::size_t i = 0; i < len; ++i) {
            buffer_[bufLen_++] = data[i];
            if (bufLen_ == 64) { Block(buffer_.data()); bitLen_ += 512; bufLen_ = 0; }
        }
    }
    void Update(const std::string& s) {
        Update(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
    }

    std::array<std::uint8_t, 32> Digest() {
        std::uint64_t totalBits = bitLen_ + std::uint64_t(bufLen_) * 8;
        std::uint8_t pad = 0x80;
        Update(&pad, 1);
        std::uint8_t zero = 0x00;
        while (bufLen_ != 56) Update(&zero, 1);
        for (int i = 7; i >= 0; --i) {
            std::uint8_t b = std::uint8_t((totalBits >> (i * 8)) & 0xff);
            Update(&b, 1);
        }
        std::array<std::uint8_t, 32> out{};
        for (int i = 0; i < 8; ++i) {
            out[i * 4 + 0] = std::uint8_t((h_[i] >> 24) & 0xff);
            out[i * 4 + 1] = std::uint8_t((h_[i] >> 16) & 0xff);
            out[i * 4 + 2] = std::uint8_t((h_[i] >> 8) & 0xff);
            out[i * 4 + 3] = std::uint8_t(h_[i] & 0xff);
        }
        Reset();
        return out;
    }

    /// PBKDF2 helper: `*this` has absorbed exactly one 64-byte block (an HMAC key pad). The digest of that block followed by the 32 bytes
    /// at `m`, in one compression, without touching `*this`.
    std::array<std::uint8_t, 32> FinishWith32(const std::uint8_t* m) const {
        std::uint8_t blk[64] = {0};
        for (int i = 0; i < 32; ++i) blk[i] = m[i];
        blk[32] = 0x80;
        blk[62] = 0x03; blk[63] = 0x00;                  // (64 + 32) bytes = 768 bits
        Sha256 t = *this;
        t.Block(blk);
        std::array<std::uint8_t, 32> out{};
        for (int i = 0; i < 8; ++i) {
            out[i * 4 + 0] = std::uint8_t((t.h_[i] >> 24) & 0xff);
            out[i * 4 + 1] = std::uint8_t((t.h_[i] >> 16) & 0xff);
            out[i * 4 + 2] = std::uint8_t((t.h_[i] >> 8) & 0xff);
            out[i * 4 + 3] = std::uint8_t(t.h_[i] & 0xff);
        }
        return out;
    }

private:
    std::array<std::uint32_t, 8> h_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t   bufLen_ = 0;
    std::uint64_t bitLen_ = 0;

    void Reset() {
        h_ = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
              0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
        bufLen_ = 0;
        bitLen_ = 0;
    }

    static std::uint32_t Ror(std::uint32_t x, std::uint32_t n) {
        return (x >> n) | (x << (32 - n));
    }

    void Block(const std::uint8_t* p) {
        static const std::uint32_t k[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t(p[i * 4]) << 24) | (std::uint32_t(p[i * 4 + 1]) << 16) |
                   (std::uint32_t(p[i * 4 + 2]) << 8) | std::uint32_t(p[i * 4 + 3]);
        for (int i = 16; i < 64; ++i) {
            std::uint32_t s0 = Ror(w[i - 15], 7) ^ Ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            std::uint32_t s1 = Ror(w[i - 2], 17) ^ Ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
        std::uint32_t e = h_[4], f = h_[5], g = h_[6], hh = h_[7];
        for (int i = 0; i < 64; ++i) {
            std::uint32_t S1 = Ror(e, 6) ^ Ror(e, 11) ^ Ror(e, 25);
            std::uint32_t ch = (e & f) ^ (~e & g);
            std::uint32_t t1 = hh + S1 + ch + k[i] + w[i];
            std::uint32_t S0 = Ror(a, 2) ^ Ror(a, 13) ^ Ror(a, 22);
            std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            std::uint32_t t2 = S0 + maj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
        h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += hh;
    }
};

inline std::string ToHex(const std::uint8_t* data, std::size_t len) {
    static const char* hx = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        out.push_back(hx[data[i] >> 4]);
        out.push_back(hx[data[i] & 0xf]);
    }
    return out;
}

inline std::string Sha256Hex(const std::string& s) {
    Sha256 h;
    h.Update(s);
    auto d = h.Digest();
    return ToHex(d.data(), d.size());
}

// Salt + iterate so a leaked database can't be reversed with a quick lookup.
inline std::string HashPassword(const std::string& password, const std::string& saltHex) {
    std::string acc = saltHex + ":" + password;
    for (int i = 0; i < 50000; ++i) acc = Sha256Hex(acc);
    return acc;
}

inline std::array<std::uint8_t, 32> Pbkdf2Sha256(const std::string& password, const std::vector<std::uint8_t>& salt, std::uint32_t iterations) {
    std::array<std::uint8_t, 64> key{};
    if (password.size() > 64) {
        Sha256 h; h.Update(password);
        const auto d = h.Digest();
        for (std::size_t i = 0; i < 32; ++i) key[i] = d[i];
    } else {
        for (std::size_t i = 0; i < password.size(); ++i) key[i] = std::uint8_t(password[i]);
    }
    std::array<std::uint8_t, 64> ipad{}, opad{};
    for (std::size_t i = 0; i < 64; ++i) { ipad[i] = key[i] ^ 0x36; opad[i] = key[i] ^ 0x5c; }
    Sha256 inner, outer;
    inner.Update(ipad.data(), 64);
    outer.Update(opad.data(), 64);
    // U1 = HMAC(P, salt || INT_32_BE(1))
    Sha256 first = inner;
    first.Update(salt.data(), salt.size());
    const std::uint8_t one[4] = {0, 0, 0, 1};
    first.Update(one, 4);
    const auto in1 = first.Digest();
    std::array<std::uint8_t, 32> u = outer.FinishWith32(in1.data());
    std::array<std::uint8_t, 32> t = u;
    for (std::uint32_t i = 1; i < iterations; ++i) {
        const auto inD = inner.FinishWith32(u.data());
        u = outer.FinishWith32(inD.data());
        for (std::size_t k = 0; k < 32; ++k) t[k] ^= u[k];
    }
    return t;
}


} // namespace account
namespace voice {
inline std::array<std::uint8_t, 32> VoiceSha256(const std::string& msg) {
    static const std::uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    std::uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    std::vector<std::uint8_t> m(msg.begin(), msg.end());
    const std::uint64_t bits = (std::uint64_t)m.size() * 8;
    m.push_back(0x80);
    while (m.size() % 64 != 56) m.push_back(0);
    for (int i = 7; i >= 0; --i) m.push_back((std::uint8_t)(bits >> (i * 8)));
    auto rotr = [](std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); };
    for (std::size_t off = 0; off < m.size(); off += 64) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (std::uint32_t)m[off + i * 4] << 24 | (std::uint32_t)m[off + i * 4 + 1] << 16 |
                   (std::uint32_t)m[off + i * 4 + 2] << 8 | m[off + i * 4 + 3];
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
            const std::uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }
    std::array<std::uint8_t, 32> out{};
    for (int i = 0; i < 8; ++i) for (int j = 0; j < 4; ++j) out[(std::size_t)(i * 4 + j)] = (std::uint8_t)(h[i] >> (24 - j * 8));
    return out;
}

} // namespace voice
namespace anticheat {
inline std::array<std::uint8_t, 32> HmacSha256(const std::string& key, const std::string& msg) {
    using okay_old::account::Sha256;
    std::array<std::uint8_t, 64> k{};
    if (key.size() > 64) {
        Sha256 h; h.Update(key);
        const auto d = h.Digest();
        std::copy(d.begin(), d.end(), k.begin());
    } else {
        std::copy(key.begin(), key.end(), k.begin());
    }
    std::array<std::uint8_t, 64> ipad{}, opad{};
    for (int i = 0; i < 64; ++i) { ipad[i] = k[i] ^ 0x36; opad[i] = k[i] ^ 0x5c; }
    Sha256 in; in.Update(ipad.data(), ipad.size()); in.Update(msg);
    const auto inner = in.Digest();
    Sha256 out; out.Update(opad.data(), opad.size()); out.Update(inner.data(), inner.size());
    
    return out.Digest();
}

} // namespace anticheat
} // namespace okay_old
