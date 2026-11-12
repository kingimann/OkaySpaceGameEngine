// X25519 (RFC 7748 section 5) and the Kx session-key derivation built on it.
// UNREVIEWED (see Crypto.hpp). A fixed 255-step Montgomery ladder: the secret
// scalar only ever drives masked conditional swaps.
#include <okay/Crypto/Crypto.hpp>
#include "Fe25519.hpp"

namespace okay::crypto {

bool X25519(std::uint8_t out[32], const std::uint8_t scalar[32], const std::uint8_t point[32]) {
    using namespace fe;
    std::uint8_t k[32];
    for (int i = 0; i < 32; ++i) k[i] = scalar[i];
    k[0] &= 248; k[31] &= 127; k[31] |= 64;           // decodeScalar25519

    std::uint8_t up[32];
    for (int i = 0; i < 32; ++i) up[i] = point[i];
    up[31] &= 127;                                    // decodeUCoordinate: mask the top bit
    Fe x1, x2, z2, x3, z3, a24;
    FromBytes(x1, up);
    One(x2); Zero(z2); Copy(x3, x1); One(z3);
    Zero(a24); a24.v[0] = 121665 & 0xffff; a24.v[1] = 121665 >> 16;   // (A - 2) / 4

    std::int64_t swap = 0;
    for (int t = 254; t >= 0; --t) {
        const std::int64_t kt = (k[t >> 3] >> (t & 7)) & 1;
        swap ^= kt;
        CSwap(x2, x3, swap);
        CSwap(z2, z3, swap);
        swap = kt;

        Fe A, AA, B, BB, E, C, D, DA, CB, T;
        Add(A, x2, z2);  Square(AA, A);
        Sub(B, x2, z2);  Square(BB, B);
        Sub(E, AA, BB);
        Add(C, x3, z3);
        Sub(D, x3, z3);
        Mul(DA, D, A);
        Mul(CB, C, B);
        Add(T, DA, CB);  Square(x3, T);
        Sub(T, DA, CB);  Square(T, T);  Mul(z3, x1, T);
        Mul(x2, AA, BB);
        Mul(T, a24, E);  Add(T, AA, T);  Mul(z2, E, T);
    }
    CSwap(x2, x3, swap);
    CSwap(z2, z3, swap);

    Fe zi, r;
    Invert(zi, z2);
    Mul(r, x2, zi);
    ToBytes(out, r);
    MemZero(k, sizeof k);
    MemZero(&x1, sizeof x1); MemZero(&x2, sizeof x2); MemZero(&z2, sizeof z2);
    MemZero(&x3, sizeof x3); MemZero(&z3, sizeof z3);
    std::uint32_t acc = 0;
    for (int i = 0; i < 32; ++i) acc |= out[i];
    return acc != 0;
}

void X25519Base(std::uint8_t out[32], const std::uint8_t scalar[32]) {
    std::uint8_t nine[32] = {9};
    X25519(out, scalar, nine);
}

void KxKeypair(std::uint8_t pk[32], std::uint8_t sk[32]) {
    RandomBytes(sk, 32);
    X25519Base(pk, sk);
}

namespace {
// BLAKE2b-512(q || client_pk || server_pk) -> first half, second half.
bool KxDerive(std::uint8_t h[64], const std::uint8_t mySk[32], const std::uint8_t theirPk[32],
              const std::uint8_t clientPk[32], const std::uint8_t serverPk[32]) {
    std::uint8_t q[32];
    const bool ok = X25519(q, mySk, theirPk);
    if (!ok) { MemZero(q, sizeof q); MemZero(h, 64); return false; }
    Blake2b b(64);
    b.Update(q, 32); b.Update(clientPk, 32); b.Update(serverPk, 32);
    b.Final(h);
    MemZero(q, sizeof q);
    return true;
}
} // namespace

bool KxClientSessionKeys(std::uint8_t rx[32], std::uint8_t tx[32], const std::uint8_t clientPk[32],
                         const std::uint8_t clientSk[32], const std::uint8_t serverPk[32]) {
    std::uint8_t h[64];
    if (!KxDerive(h, clientSk, serverPk, clientPk, serverPk)) { MemZero(rx, 32); MemZero(tx, 32); return false; }
    for (int i = 0; i < 32; ++i) { rx[i] = h[i]; tx[i] = h[32 + i]; }
    MemZero(h, sizeof h);
    return true;
}

bool KxServerSessionKeys(std::uint8_t rx[32], std::uint8_t tx[32], const std::uint8_t serverPk[32],
                         const std::uint8_t serverSk[32], const std::uint8_t clientPk[32]) {
    std::uint8_t h[64];
    if (!KxDerive(h, serverSk, clientPk, clientPk, serverPk)) { MemZero(rx, 32); MemZero(tx, 32); return false; }
    for (int i = 0; i < 32; ++i) { tx[i] = h[i]; rx[i] = h[32 + i]; }
    MemZero(h, sizeof h);
    return true;
}

} // namespace okay::crypto
