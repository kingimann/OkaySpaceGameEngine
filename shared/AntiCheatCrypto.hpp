#pragma once
// Anti-cheat crypto: ed25519 signatures (and the SHA-512 they are built on) for the build's integrity manifest
// (AntiCheatIntegrity.hpp), the signed versions.json (UpdateSignature.hpp) and the host identity (Net/NetworkManager).
//
// This header holds NO crypto code of its own any more. It is a thin wrapper:
//   * with libsodium in the build (OKAY_HAVE_SODIUM, the engine's OKAY_USE_SODIUM option) Ed25519 goes to libsodium
//     (the preferred path wherever it is built), unless -DOKAY_OWN_CRYPTO=ON asks for our own;
//   * everywhere else (no libsodium, or OKAY_OWN_CRYPTO), and for SHA-512 always, it goes to okay::crypto
//     (engine/include/okay/Crypto/Crypto.hpp, the static library okay_crypto), which is our own code written from RFC 8032
//     and FIPS 180-4. okay::crypto has NOT had its outside review yet (CLAUDE.md rule 18, docs/crypto_review_package.md).
// It used to hold a C++ port of public-domain TweetNaCl as the fallback; the owner approved replacing it. Before it was
// deleted, tests/test_anticheat_golden.cpp recorded what it answered on a fixed set of inputs, and that recorded answer is
// still checked on every build. The extra checks libsodium 1.0.18's crypto_sign_verify_detached makes (S < L, R not
// small-order, public key canonical and not small-order) are made by okay::crypto's Ed25519Verify and Ed25519IsWeakKey, and
// are pinned in tests/test_anticheat_integrity.cpp (the identity-key forgery). Not claimed to match libsodium on every
// malformed input beyond those checks (crypto_difftest compares the two on millions of inputs).
// Signing never needs randomness (ed25519 is deterministic), and no key is ever made here: the owner makes them
// (docs/anticheat.md).
//
// Anything that includes this header and is not linked to the engine must link okay_crypto (target okay::crypto).
//
// SHA-256 for file hashes is the engine's (okayofficial::FileSha256), so the manifest and the official game's updater hash
// files the same way.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#ifdef OKAY_HAVE_SODIUM
#include <sodium.h>
#endif
#include <okay/Crypto/Crypto.hpp>

namespace okayac {

// ---- Hex -------------------------------------------------------------------
inline std::string ToHex(const std::uint8_t* d, std::size_t n) {
    static const char* hx = "0123456789abcdef";
    std::string o;
    o.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) { o += hx[d[i] >> 4]; o += hx[d[i] & 15]; }
    return o;
}

// Strict: an even count of hex digits, with surrounding whitespace (a key file's
// trailing newline) ignored and none inside.
inline bool FromHex(const std::string& s, std::vector<std::uint8_t>& out) {
    const std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) { out.clear(); return false; }
    const std::size_t b = s.find_last_not_of(" \t\r\n");
    const std::string t = s.substr(a, b - a + 1);
    if (t.size() % 2) return false;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::vector<std::uint8_t> v(t.size() / 2);
    for (std::size_t i = 0; i < v.size(); ++i) {
        const int h = nib(t[2 * i]), l = nib(t[2 * i + 1]);
        if (h < 0 || l < 0) return false;
        v[i] = (std::uint8_t)(h << 4 | l);
    }
    out = std::move(v);
    return true;
}

// Zero key material (not optimised away).
inline void Wipe(void* p, std::size_t n) {
#ifdef OKAY_HAVE_SODIUM
    sodium_memzero(p, n);
#else
    volatile std::uint8_t* v = static_cast<volatile std::uint8_t*>(p);
    while (n--) *v++ = 0;
#endif
}

// ---- SHA-512 (FIPS 180-4), from okay::crypto -----------------------------------
namespace sha512 {
using Digest = std::array<std::uint8_t, 64>;

// One-shot over several pieces (ed25519 hashes R || A || M without copying M).
inline Digest Hash(std::initializer_list<std::pair<const std::uint8_t*, std::size_t>> parts) {
    okay::crypto::Sha512 h;
    for (const auto& part : parts) if (part.second) h.Update(part.first, part.second);
    Digest d{};
    h.Final(d.data());
    return d;
}
inline Digest Hash(const std::uint8_t* p, std::size_t n) { return Hash({{p, n}}); }
} // namespace sha512

// ---- ed25519 -----------------------------------------------------------------
namespace ed25519 {
using PublicKey = std::array<std::uint8_t, 32>;
using Seed = std::array<std::uint8_t, 32>;        // the private key (RFC 8032's 32-byte secret)
using Signature = std::array<std::uint8_t, 64>;

// True when Ed25519 goes to libsodium (it is built in, initialised, and OKAY_OWN_CRYPTO did not ask for our own).
#ifdef OKAY_HAVE_SODIUM
inline bool SodiumReady() {
    static const bool ok = sodium_init() >= 0;
    return ok;
}
#endif
inline bool UseSodium() {
#if defined(OKAY_HAVE_SODIUM) && !defined(OKAY_OWN_CRYPTO)
    return SodiumReady();
#else
    return false;
#endif
}

// The backend every caller uses.
inline const char* Backend() { return UseSodium() ? "libsodium" : "okay::crypto"; }

// A public key no signature should be accepted for (non-canonical encoding or a small-order point): the identity key
// "verifies" R = identity, S = 0 for any message. Verify refuses these too; the key-list parser uses this. The answer
// always comes from okay::crypto (libsodium has no such call; its verify makes the same refusals).
inline bool IsWeakKey(const PublicKey& pk) { return okay::crypto::Ed25519IsWeakKey(pk.data()); }

inline PublicKey PublicKeyFromSeed(const Seed& seed) {
    PublicKey pk{};
#ifdef OKAY_HAVE_SODIUM
    if (UseSodium()) {
        unsigned char sk[crypto_sign_SECRETKEYBYTES];
        crypto_sign_seed_keypair(pk.data(), sk, seed.data());
        Wipe(sk, sizeof(sk));
        return pk;
    }
#endif
    okay::crypto::Ed25519PublicFromSeed(pk.data(), seed.data());
    return pk;
}

inline Signature Sign(const std::uint8_t* m, std::size_t n, const Seed& seed) {
    Signature sig{};
#ifdef OKAY_HAVE_SODIUM
    if (UseSodium()) {
        PublicKey pk{};
        unsigned char sk[crypto_sign_SECRETKEYBYTES];
        crypto_sign_seed_keypair(pk.data(), sk, seed.data());
        crypto_sign_detached(sig.data(), nullptr, m, (unsigned long long)n, sk);
        Wipe(sk, sizeof(sk));
        return sig;
    }
#endif
    std::uint8_t pk[32], sk[64];
    okay::crypto::Ed25519SeedKeypair(pk, sk, seed.data());
    okay::crypto::Ed25519Sign(sig.data(), m, n, sk);
    okay::crypto::MemZero(sk, sizeof(sk));
    return sig;
}

inline bool Verify(const std::uint8_t* m, std::size_t n, const Signature& sig, const PublicKey& pk) {
#ifdef OKAY_HAVE_SODIUM
    if (UseSodium()) return crypto_sign_verify_detached(sig.data(), m, (unsigned long long)n, pk.data()) == 0;
#endif
    return okay::crypto::Ed25519Verify(sig.data(), m, n, pk.data());
}

inline Signature Sign(const std::string& m, const Seed& seed) {
    return Sign(reinterpret_cast<const std::uint8_t*>(m.data()), m.size(), seed);
}
inline bool Verify(const std::string& m, const Signature& sig, const PublicKey& pk) {
    return Verify(reinterpret_cast<const std::uint8_t*>(m.data()), m.size(), sig, pk);
}
} // namespace ed25519
} // namespace okayac
