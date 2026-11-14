// ChaCha20-Poly1305 (RFC 8439 section 2.8) and XChaCha20-Poly1305
// (draft-irtf-cfrg-xchacha, the "IETF" construction: HChaCha20 subkey, then the
// RFC 8439 AEAD with nonce 0000 || last 8 nonce bytes). UNREVIEWED (see Crypto.hpp).
#include <okay/Crypto/Crypto.hpp>

#include <cstdlib>

namespace okay::crypto {
namespace {

inline void Store64(std::uint8_t* p, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
}

void PadTo16(Poly1305& p, std::size_t n) {
    static const std::uint8_t zeros[16] = {0};
    if (n % 16) p.Update(zeros, 16 - n % 16);
}

// tag = Poly1305(otk, aad || pad || ct || pad || le64(aadLen) || le64(ctLen)),
// otk = first 32 bytes of the ChaCha20 block at counter 0.
void ComputeTag(std::uint8_t tag[16], const std::uint8_t* aad, std::size_t aadLen,
                const std::uint8_t* ct, std::size_t n, const std::uint8_t nonce[12], const std::uint8_t key[32]) {
    std::uint8_t otk[64] = {0};
    ChaCha20Xor(otk, otk, 64, key, nonce, 0);
    Poly1305 p(otk);
    MemZero(otk, sizeof otk);
    if (aadLen) p.Update(aad, aadLen);
    PadTo16(p, aadLen);
    if (n) p.Update(ct, n);
    PadTo16(p, n);
    std::uint8_t lens[16];
    Store64(lens, static_cast<std::uint64_t>(aadLen));
    Store64(lens + 8, static_cast<std::uint64_t>(n));
    p.Update(lens, 16);
    p.Final(tag);
}

} // namespace

void ChaCha20Poly1305Seal(std::uint8_t* out, const std::uint8_t* in, std::size_t n,
                          const std::uint8_t* aad, std::size_t aadLen,
                          const std::uint8_t nonce[12], const std::uint8_t key[32]) {
    // A message of more than (2^32 - 1) * 64 bytes (about 256 GiB) would wrap the block counter: that is a caller bug, and
    // like libsodium we stop instead of reusing keystream (Seal cannot report an error). Not reachable on 32-bit size_t.
    if (n && !ChaCha20Xor(out, in, n, key, nonce, 1)) std::abort();
    ComputeTag(out + n, aad, aadLen, out, n, nonce, key);
}

bool ChaCha20Poly1305Open(std::uint8_t* out, const std::uint8_t* in, std::size_t cn,
                          const std::uint8_t* aad, std::size_t aadLen,
                          const std::uint8_t nonce[12], const std::uint8_t key[32]) {
    if (cn < kAeadTagBytes) return false;
    const std::size_t n = cn - kAeadTagBytes;
    std::uint8_t tag[16];
    ComputeTag(tag, aad, aadLen, in, n, nonce, key);     // over the ciphertext, before any decryption
    const bool ok = ConstantTimeEqual(tag, in + n, 16);
    if (!ok) {
        if (n) MemZero(out, n);                          // never hand back plaintext for a bad tag
        return false;
    }
    if (n && !ChaCha20Xor(out, in, n, key, nonce, 1)) { MemZero(out, n); return false; }   // counter exhausted
    return true;
}

void XChaCha20Poly1305Seal(std::uint8_t* out, const std::uint8_t* in, std::size_t n,
                           const std::uint8_t* aad, std::size_t aadLen,
                           const std::uint8_t nonce[24], const std::uint8_t key[32]) {
    std::uint8_t sub[32], n12[12] = {0, 0, 0, 0};
    HChaCha20(sub, key, nonce);
    for (int i = 0; i < 8; ++i) n12[4 + i] = nonce[16 + i];
    ChaCha20Poly1305Seal(out, in, n, aad, aadLen, n12, sub);
    MemZero(sub, sizeof sub);
}

bool XChaCha20Poly1305Open(std::uint8_t* out, const std::uint8_t* in, std::size_t cn,
                           const std::uint8_t* aad, std::size_t aadLen,
                           const std::uint8_t nonce[24], const std::uint8_t key[32]) {
    std::uint8_t sub[32], n12[12] = {0, 0, 0, 0};
    HChaCha20(sub, key, nonce);
    for (int i = 0; i < 8; ++i) n12[4 + i] = nonce[16 + i];
    const bool ok = ChaCha20Poly1305Open(out, in, cn, aad, aadLen, n12, sub);
    MemZero(sub, sizeof sub);
    return ok;
}

} // namespace okay::crypto
