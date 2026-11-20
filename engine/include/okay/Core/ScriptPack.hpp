#pragma once
// Stage 4 of docs/script_protection.md: an OPTIONAL encrypted pak mode for shipped script
// (and other) data, alongside the existing plain and DataPack-XOR pak modes. Off by
// default; gated by one Build Settings toggle (Options::enabled below), never by a new
// CMake option, because it uses only crypto the engine already builds with by default.
//
// This calls into okay::net::netcrypto (engine/include/okay/Net/NetCrypto.hpp) and nothing
// else for the actual cryptography: AeadSeal/AeadOpen (XChaCha20-Poly1305 via libsodium, or
// via okay::crypto when the engine is built with OKAY_OWN_CRYPTO=ON — see rule 18, that
// switch is the owner's call, not this feature's) and Random for key/nonce generation.
// NO NEW CIPHER, MAC OR KEY-DERIVATION CODE IS WRITTEN HERE. If this is ever reviewed
// against rule 18, the reviewer should find exactly two calls out to already-reviewed-path
// functions and nothing home-grown.
//
// Format of a packed blob: "OKE1" (4) + nonce (24, netcrypto::kNonceBytes) +
// ciphertext+tag (plaintext size + netcrypto::kTagBytes). The 32-byte symmetric key is
// NEVER stored in the pack; it is generated once per build (ScriptPack::MakeBuildKey) and
// baked into the shipped binary by whatever calls this (e.g. as a compiled-in byte array
// the build step writes out) — same "the key ships with the lock" limit DataPack already
// documents, stated again here because it is still true for AEAD.
//
// Availability: Available() mirrors okay::net::SecureChannel::Available() — true only when
// the engine was actually built with a crypto backend (OKAY_NET_CRYPTO defined, which is
// the default whenever libsodium was found, or always true under OKAY_OWN_CRYPTO). This is
// the one "runtime" check the task asks for in place of a new CMake option: callers check
// ScriptPack::Available() before offering the toggle, rather than the build needing a new
// flag to know whether encryption exists.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <okay/Net/NetCrypto.hpp>

namespace okay::scriptpack {

struct Options {
    bool enabled = false;   ///< Build Settings: "Encrypt packed script/data" toggle, default OFF
};

inline const char* Magic() { return "OKE1"; }
constexpr std::size_t kKeyBytes = 32;

/// True when the engine was built with a crypto backend at all (see the file comment).
/// A build with no backend simply cannot offer this toggle; ScriptPack::Pack then returns
/// false rather than silently shipping plaintext under a lock icon.
inline bool Available() {
#ifdef OKAY_NET_CRYPTO
    return true;
#else
    return false;
#endif
}

/// A fresh random 32-byte key for one build. Call once per Build Game run when the toggle
/// is on, and have the build step embed the bytes into the shipped binary (e.g. as a
/// generated .cpp array) — never write them to a file that ships as data, and never reuse
/// a key across builds (a leaked key from one game must not expose another).
inline std::vector<std::uint8_t> MakeBuildKey() {
    std::vector<std::uint8_t> key(kKeyBytes, 0);
#ifdef OKAY_NET_CRYPTO
    okay::net::netcrypto::Init();
    okay::net::netcrypto::Random(key.data(), key.size());
#endif
    return key;
}

/// Seal `plain` under `key` (must be kKeyBytes long) with a fresh random nonce. False (and
/// `out` untouched) when the engine has no crypto backend or the key is the wrong size —
/// callers must not fall back to shipping plaintext silently; they should refuse the build
/// or fall back to a non-encrypted pak mode and say so.
inline bool Pack(const std::string& plain, const std::vector<std::uint8_t>& key, std::string& out) {
#ifdef OKAY_NET_CRYPTO
    if (key.size() != kKeyBytes) return false;
    okay::net::netcrypto::Init();
    std::uint8_t nonce[okay::net::netcrypto::kNonceBytes];
    okay::net::netcrypto::Random(nonce, sizeof(nonce));
    std::vector<std::uint8_t> cipher(plain.size() + okay::net::netcrypto::kTagBytes);
    okay::net::netcrypto::AeadSeal(cipher.data(), reinterpret_cast<const std::uint8_t*>(plain.data()),
                                   plain.size(), nullptr, 0, nonce, key.data());
    out.clear();
    out.reserve(4 + sizeof(nonce) + cipher.size());
    out.append(Magic(), 4);
    out.append(reinterpret_cast<const char*>(nonce), sizeof(nonce));
    out.append(reinterpret_cast<const char*>(cipher.data()), cipher.size());
    return true;
#else
    (void)plain; (void)key; (void)out;
    return false;
#endif
}

/// True for data that starts with the encrypted-pak magic (so callers can tell it apart
/// from a plain file or a DataPack-XOR pack, which use different magics).
inline bool IsPacked(const std::string& d) { return d.size() >= 4 && d.compare(0, 4, Magic()) == 0; }

/// Open a pack made by Pack(). False on a bad magic, a truncated blob, the wrong key, or a
/// tampered/corrupted blob (the AEAD tag fails to verify) — `out` is left empty in every
/// failure case and the caller never gets partially-decrypted bytes. This never crashes on
/// a bit-flipped or truncated input; it just reports failure, same as any other load error.
inline bool Unpack(const std::string& packed, const std::vector<std::uint8_t>& key, std::string& out,
                   std::string* error = nullptr) {
    out.clear();
#ifdef OKAY_NET_CRYPTO
    if (!IsPacked(packed)) { if (error) *error = "not an encrypted pack"; return false; }
    if (key.size() != kKeyBytes) { if (error) *error = "wrong key size"; return false; }
    const std::size_t head = 4 + okay::net::netcrypto::kNonceBytes;
    if (packed.size() < head + okay::net::netcrypto::kTagBytes) {
        if (error) *error = "truncated pack";
        return false;
    }
    const std::uint8_t* nonce = reinterpret_cast<const std::uint8_t*>(packed.data() + 4);
    const std::size_t cipherLen = packed.size() - head;
    std::vector<std::uint8_t> plain(cipherLen - okay::net::netcrypto::kTagBytes);
    const bool ok = okay::net::netcrypto::AeadOpen(
        plain.data(), reinterpret_cast<const std::uint8_t*>(packed.data() + head), cipherLen, nullptr, 0, nonce,
        key.data());
    if (!ok) { if (error) *error = "tampered, corrupt, or wrong key"; return false; }
    out.assign(reinterpret_cast<const char*>(plain.data()), plain.size());
    return true;
#else
    (void)packed; (void)key; (void)error;
    if (error) *error = "engine built with no crypto backend";
    return false;
#endif
}

} // namespace okay::scriptpack
